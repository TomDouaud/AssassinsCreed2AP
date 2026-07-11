// AC2AP - in-process memory access to the game (applying items).
// Chain validated in the lab (tools/resolve_money.py + Paul44 cheat table):
//   AOB "8A 41 2C 84 C0" in the code -> mov reg,[static] instruction 6 bytes before
//   r1=[static]; r2=[r1+0x20]; r3=[r2+0x18]; BhvAss=[r3]
//   inv=[[[BhvAss+0x10]+0x58]+0xC]; money = [[inv+0x10]+0] + 0x10 (u32)
// All reads go through safe_read: pointers are invalid during
// loading/menus -> clean failure, we retry next tick.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <MinHook.h>

namespace ac2ap::game {

inline bool safe_read(uintptr_t addr, void* out, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, out, n, &got) && got == n;
}

inline bool safe_write(uintptr_t addr, const void* data, size_t n) {
    SIZE_T put = 0;
    return WriteProcessMemory(GetCurrentProcess(), (LPVOID)addr, data, n, &put) && put == n;
}

inline bool rd32(uintptr_t addr, uint32_t& v) { return safe_read(addr, &v, 4); }

// AOB scan in the executable sections of the main module. 0 if absent.
inline uintptr_t find_aob(const uint8_t* pat, size_t n) {
    auto base = (uintptr_t)GetModuleHandleA(nullptr);
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; s++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint8_t* p = (const uint8_t*)(base + sec->VirtualAddress);
        size_t len = sec->Misc.VirtualSize;
        for (size_t i = 0; i + n <= len; i++)
            if (memcmp(p + i, pat, n) == 0) return (uintptr_t)(p + i);
    }
    return 0;
}

// Resolves pBhvAssassin (base shared by money, health, inventory), 0 if unavailable.
inline uintptr_t resolve_bhv() {
    static uintptr_t s_static = 0;   // static address, constant for the session
    if (!s_static) {
        static const uint8_t AOB[] = {0x8A, 0x41, 0x2C, 0x84, 0xC0};
        uintptr_t m = find_aob(AOB, sizeof(AOB));
        if (!m) return 0;
        uint8_t instr[6];
        if (!safe_read(m - 6, instr, 6) || instr[0] != 0x8B) return 0;
        memcpy(&s_static, instr + 2, 4);
    }
    uint32_t r1, r2, r3, bhv;
    if (!rd32(s_static, r1) || !r1) return 0;
    if (!rd32(r1 + 0x20, r2) || !r2) return 0;
    if (!rd32(r2 + 0x18, r3) || !r3) return 0;
    if (!rd32(r3, bhv) || !bhv) return 0;
    return bhv;
}

// Address of the florin counter, 0 if unavailable (menu, loading...).
inline uintptr_t resolve_money_addr() {
    uint32_t bhv = resolve_bhv();
    if (!bhv) return 0;
    uint32_t a, b, inv, m1, m2;
    if (!rd32(bhv + 0x10, a) || !a) return 0;
    if (!rd32(a + 0x58, b) || !b) return 0;
    if (!rd32(b + 0xC, inv) || !inv) return 0;
    if (!rd32(inv + 0x10, m1) || !m1) return 0;
    if (!rd32(m1, m2) || !m2) return 0;
    return (uintptr_t)m2 + 0x10;
}

// Counter of a consumable (u32), 0 if unavailable. Same container as money
// ([[[pInventory]+10]+slot]+10), 'slot' selects the consumable (Paul44 cheat table):
//   0x04 = smoke bombs, 0x0C = medicine, 0x10 = poison vials, 0x14 = ammo.
inline uintptr_t resolve_consumable_addr(uint32_t slot) {
    uint32_t bhv = resolve_bhv();
    if (!bhv) return 0;
    uint32_t a, b, inv, m1, cont;
    if (!rd32(bhv + 0x10, a) || !a) return 0;
    if (!rd32(a + 0x58, b) || !b) return 0;
    if (!rd32(b + 0xC, inv) || !inv) return 0;       // pInventory
    if (!rd32(inv + 0x10, m1) || !m1) return 0;
    if (!rd32(m1 + slot, cont) || !cont) return 0;   // consumable container
    return (uintptr_t)cont + 0x10;                    // u32 counter
}

// Base of a consumable container (before +0x10). Used for dump/struct analysis.
inline uintptr_t resolve_consumable_container(uint32_t slot) {
    uint32_t bhv = resolve_bhv();
    if (!bhv) return 0;
    uint32_t a, b, inv, m1, cont;
    if (!rd32(bhv + 0x10, a) || !a) return 0;
    if (!rd32(a + 0x58, b) || !b) return 0;
    if (!rd32(b + 0xC, inv) || !inv) return 0;
    if (!rd32(inv + 0x10, m1) || !m1) return 0;
    if (!rd32(m1 + slot, cont) || !cont) return 0;
    return (uintptr_t)cont;
}

// Writes a u32 at container+off (debug probe: looks for the unlock/capacity field). false if out-of-game.
inline bool poke_consumable(uint32_t slot, uint32_t off, uint32_t val) {
    uintptr_t base = resolve_consumable_container(slot);
    if (!base) return false;
    return safe_write(base + off, &val, 4);
}

// Writes a u32 at an absolute address (weapon-grant probe). false if the address is low.
inline bool poke_abs(uintptr_t addr, uint32_t val) {
    if (addr < 0x10000) return false;
    return safe_write(addr, &val, 4);
}

// --- Differential "unknown value" scanner (Cheat Engine style) ----------------
// THE TANK for value RE (renovations, states, etc.): snapshot all u32s in the heap,
// then filter by changed/unchanged/increased/decreased/equals across in-game actions
// until the target address is isolated. In-process -> direct access, no UAC.
inline uintptr_t* g_scan_addrs = nullptr;
inline uint32_t*  g_scan_vals  = nullptr;
inline size_t g_scan_count = 0;
inline size_t g_scan_cap = 0;

inline void scan_free() {
    free(g_scan_addrs); free(g_scan_vals);
    g_scan_addrs = nullptr; g_scan_vals = nullptr; g_scan_count = 0; g_scan_cap = 0;
}

// Initial snapshot: all 4-aligned u32s of the MEM_PRIVATE writable committed regions.
// cap = max number of candidates (memory = cap*12 bytes). Returns the number captured.
inline size_t scan_init(size_t cap, uintptr_t lo = 0x10000, uintptr_t hi = 0xFFFF0000) {
    scan_free();
    g_scan_addrs = (uintptr_t*)malloc(cap * sizeof(uintptr_t));
    g_scan_vals  = (uint32_t*)malloc(cap * sizeof(uint32_t));
    if (!g_scan_addrs || !g_scan_vals) { scan_free(); return 0; }
    g_scan_cap = cap;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = lo;
    while (addr < hi && g_scan_count < cap) {
        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) break;
        uintptr_t base = (uintptr_t)mbi.BaseAddress; size_t sz = mbi.RegionSize;
        DWORD prot = mbi.Protect & 0xFF;
        bool ok = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE
                  && (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READWRITE)
                  && !(mbi.Protect & PAGE_GUARD);
        if (ok && sz >= 4) {
            uintptr_t s = base > lo ? base : lo;                 // clamp region to [lo,hi)
            uintptr_t e = (base + sz) < hi ? (base + sz) : hi;
            __try {
                for (uintptr_t a = (s + 3) & ~(uintptr_t)3; a + 4 <= e && g_scan_count < cap; a += 4) {
                    g_scan_addrs[g_scan_count] = a;
                    g_scan_vals[g_scan_count] = *(uint32_t*)a;
                    g_scan_count++;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        addr = base + sz;
    }
    return g_scan_count;
}

// Filters the current set. mode: 0=changed 1=unchanged 2=increased 3=decreased 4=equals(arg).
// Compacts in place + updates the stored values. Returns the number remaining.
inline size_t scan_filter(int mode, uint32_t arg = 0) {
    if (!g_scan_addrs) return 0;
    size_t k = 0;
    for (size_t i = 0; i < g_scan_count; i++) {
        uint32_t cur;
        if (!safe_read(g_scan_addrs[i], &cur, 4)) continue;
        uint32_t old = g_scan_vals[i];
        bool keep = mode == 0 ? cur != old : mode == 1 ? cur == old
                  : mode == 2 ? cur > old  : mode == 3 ? cur < old
                  : cur == arg;
        if (keep) { g_scan_addrs[k] = g_scan_addrs[i]; g_scan_vals[k] = cur; k++; }
    }
    g_scan_count = k;
    return k;
}

// In-process scan: looks for a u32 across all writable committed memory.
// The asi has access (no UAC, unlike external scanners). Returns the number of hits
// (addresses written into hits[], capped at maxhits). 4-aligned (item IDs are).
inline int scan_u32(uint32_t val, uintptr_t* hits, int maxhits) {
    int n = 0;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0x10000;
    while (addr < 0xFFFF0000 && n < maxhits) {
        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) break;
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        size_t sz = mbi.RegionSize;
        DWORD prot = mbi.Protect & 0xFF;
        bool ok = (mbi.State == MEM_COMMIT)
                  && (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY
                      || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY)
                  && !(mbi.Protect & PAGE_GUARD);
        if (ok && sz >= 4) {
            __try {
                uint8_t* p = (uint8_t*)base;
                for (size_t i = 0; i + 4 <= sz && n < maxhits; i += 4)
                    if (*(uint32_t*)(p + i) == val) hits[n++] = base + i;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        addr = base + sz;
    }
    return n;
}

// --- Villa renovations: array of building structs ----------------------------
// Each building = 0x18 struct: level u32 at +0x00, SELF-POINTER at +0x04 (struct+4 == struct),
// value float at +0x10, building-ID at +0x14 (0x88000D00 + index*0x100). The array is found by
// SIGNATURE (self-ptr + building-ID range) -> robust, no fragile pointer chain.
inline bool villa_is_building(uintptr_t a) {
    uint32_t self, id;
    if (!rd32(a + 4, self) || self != (uint32_t)a) return false;
    if (!rd32(a + 0x14, id)) return false;
    if ((id & 0xFF) != 0) return false;          // building-ID = 0x88000X00 (low byte 00)
    uint32_t hi = id >> 8;
    if (hi < 0x88000Du || hi > 0x880017u) return false;  // building-ID range
    uint32_t lvl;                                 // plausible level (small integer)
    return rd32(a, lvl) && lvl <= 10;
}

// Array length (number of structs) + max-min level SPREAD. The LIVE copy has MIXED
// levels (spread > 0); the templates are uniform (all-0 or all-max, spread == 0).
inline int villa_scan_array(uintptr_t first, uint32_t* spread_out) {
    int n = 0; uint32_t mn = 0xFFFFFFFF, mx = 0;
    while (n < 32 && villa_is_building(first + (uintptr_t)n * 0x18)) {
        uint32_t l; rd32(first + (uintptr_t)n * 0x18, l);
        if (l < mn) mn = l; if (l > mx) mx = l; n++;
    }
    if (spread_out) *spread_out = (n && mx >= mn) ? (mx - mn) : 0;
    return n;
}

// Address of the 1st building struct of the LIVE villa array (the one with the real levels =
// max level sum; the template/default copy has all levels 0). 0 if absent.
inline uintptr_t resolve_villa_array() {
    uintptr_t best = 0; uint32_t best_sum = 0;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0x10000;
    while (addr < 0xFFFF0000) {
        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) break;
        uintptr_t base = (uintptr_t)mbi.BaseAddress; size_t sz = mbi.RegionSize;
        DWORD prot = mbi.Protect & 0xFF;
        bool ok = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE
                  && (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY) && !(mbi.Protect & PAGE_GUARD);
        if (ok && sz >= 0x30) {
            __try {
                uint8_t* p = (uint8_t*)base;
                // DIRECT read (not ReadProcessMemory): fast scan, protected by the __try.
                auto sig = [](uint8_t* q) -> bool {
                    if (*(uint32_t*)(q + 4) != (uint32_t)(uintptr_t)q) return false;   // self-ptr
                    uint32_t id = *(uint32_t*)(q + 0x14);
                    if ((id & 0xFF) || (id >> 8) < 0x88000Du || (id >> 8) > 0x880017u) return false;
                    return *(uint32_t*)q <= 10;                                        // plausible level
                };
                for (size_t i = 0; i + 0x48 <= sz; i += 4) {
                    uint8_t* q = p + i;
                    if (!sig(q) || !sig(q + 0x18) || !sig(q + 0x30)) continue;
                    uint32_t id0 = *(uint32_t*)(q + 0x14), id1 = *(uint32_t*)(q + 0x18 + 0x14),
                             id2 = *(uint32_t*)(q + 0x30 + 0x14);
                    if (id1 != id0 + 0x100 || id2 != id1 + 0x100) continue;
                    uintptr_t first = base + i;                      // walk back to the start (id -0x100)
                    while (first >= base + 0x18 && sig((uint8_t*)(first - 0x18))
                           && *(uint32_t*)(first - 0x18 + 0x14) + 0x100 == *(uint32_t*)(first + 0x14))
                        first -= 0x18;
                    uint32_t spread = 0;
                    int n = villa_scan_array(first, &spread);
                    // LIVE copy = mixed levels (max spread); fallback = first found if all uniform
                    if (spread > best_sum || best == 0) { best_sum = spread; best = first; }
                    i = (first - base) + (size_t)n * 0x18;            // skip past this array
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        addr = base + sz;
    }
    return best;
}

// Inventory bases for exploration (weapon probe). 0 if out-of-game.
//   pInventory = [b+0xC] (InventoryDataItem); PlayerDataItem = [b+0x0]; m1 = [pInventory+0x10]
//   with b = [[bhv+0x10]+0x58]. (see enable-cheats notes: +58+0 = PlayerDataItem)
inline uintptr_t resolve_inv_bases(uintptr_t* pInv, uintptr_t* pPdi, uintptr_t* pM1) {
    uint32_t bhv = resolve_bhv();
    if (!bhv) return 0;
    uint32_t a, b, inv = 0, pdi = 0, m1 = 0;
    if (!rd32(bhv + 0x10, a) || !a) return 0;
    if (!rd32(a + 0x58, b) || !b) return 0;
    rd32(b + 0xC, inv);
    rd32(b + 0x0, pdi);
    if (inv) rd32(inv + 0x10, m1);
    if (pInv) *pInv = inv;
    if (pPdi) *pPdi = pdi;
    if (pM1)  *pM1 = m1;
    return bhv;
}

// Consumable slots (offsets in the inventory container).
namespace consumable {
    constexpr uint32_t SMOKE   = 0x04;
    constexpr uint32_t MEDICINE= 0x0C;
    constexpr uint32_t POISON  = 0x10;
    constexpr uint32_t BULLETS = 0x14;
}

// Adds delta to a consumable (clamp >= 0). false if out-of-game.
inline bool add_consumable(uint32_t slot, int32_t delta) {
    uintptr_t addr = resolve_consumable_addr(slot);
    if (!addr) return false;
    uint32_t cur;
    if (!rd32(addr, cur)) return false;
    int64_t next = (int64_t)cur + delta;
    if (next < 0) next = 0;
    uint32_t val = (uint32_t)next;
    return safe_write(addr, &val, 4);
}

// Sets a consumable to an absolute value (debug). false if out-of-game.
inline bool set_consumable(uint32_t slot, uint32_t val) {
    uintptr_t addr = resolve_consumable_addr(slot);
    if (!addr) return false;
    return safe_write(addr, &val, 4);
}

// --- Health hook (real health, captured in flight) ---------------------------
// The static pointer chain only gives a mirror (armor). The REAL health is
// only reachable during the game function that manipulates it: the pointer
// to the health object passes through eax then vanishes. We capture it via an inline hook,
// exactly like the Paul44 cheat table:
//   AOB "55 8B EC 8B 41 0C 8B 48 58 8B 55 08"; at +0x0A: "mov ecx,[eax+58]; mov edx,[ebp+08]"
//   -> we save eax; current health = [eax+0x58].
// Defensive: we verify the 6 bytes before patching; otherwise we install nothing.

inline volatile uint32_t* g_health_obj = nullptr;  // captured eax (health object)
inline bool g_health_hook_enabled = false;         // opt-in via ini (default OFF, see BUG-004)
inline void* g_health_tramp = nullptr;             // MinHook trampoline (rest of the function)

// FILTER (fix BUG-004): the function is generic (called for every entity).
// We keep eax as the health object ONLY if it looks like player health:
// [eax+0x58]=cur and [eax+0x5C]=max, small integers, 1<=cur<=max<=100. SEH to never
// crash if eax points to an invalid area (happens with transient entities).
inline void __fastcall health_filter(uint32_t obj) {
    if (obj < 0x10000) return;
    __try {
        // Ezio lock: if we already have a valid object, we KEEP it as long as it stays
        // consistent (Ezio persists frame after frame). We only change it if it becomes
        // invalid (dead/unloaded). => transient guards no longer overwrite Ezio.
        // Assumption: on the 1st pass (loading, out of combat), Ezio is the only one active.
        if (g_health_obj) {
            uint32_t c = *(volatile uint32_t*)((uintptr_t)g_health_obj + 0x58);
            uint32_t m = *(volatile uint32_t*)((uintptr_t)g_health_obj + 0x5C);
            // c==0 allowed: a DEAD Ezio stays Ezio (otherwise the lock releases on death, captures
            // a guard, and re-triggers a DeathLink emission -> double emit, BUG-006).
            if (c <= 100 && m >= 1 && m <= 100 && c <= m) return;  // Ezio ok, keep it
        }
        uint32_t cur = *(volatile uint32_t*)(obj + 0x58);
        uint32_t mx  = *(volatile uint32_t*)(obj + 0x5C);
        if (cur >= 1 && cur <= 100 && mx >= 1 && mx <= 100 && cur <= mx)
            g_health_obj = (volatile uint32_t*)obj;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Naked detour: filters eax (candidate health object) then resumes via the trampoline.
// pushad/popad -> the original state passes through intact. See BUG-004: MinHook freezes the
// threads and relocates cleanly, unlike a hand-rolled inline patch.
__declspec(naked) inline void health_detour() {
    __asm {
        pushad
        mov ecx, eax          // __fastcall: candidate health object in ecx
        call health_filter
        popad
        jmp [g_health_tramp]
    }
}

inline bool install_health_hook() {
    static bool tried = false;
    if (tried) return g_health_obj != nullptr || g_health_tramp != nullptr;
    if (!g_health_hook_enabled) return false;      // stability: no code patch by default
    tried = true;

    // Full AOB with 00 CC x9 padding prefix -> UNIQUE match (the table says "Health+10")
    static const uint8_t FUNC_AOB[] = {
        0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
        0x55, 0x8B, 0xEC, 0x8B, 0x41, 0x0C, 0x8B, 0x48, 0x58, 0x8B, 0x55, 0x08};
    uintptr_t func = find_aob(FUNC_AOB, sizeof(FUNC_AOB));
    if (!func) return false;
    void* target = (void*)(func + 0x10);          // "8B 48 58 8B 55 08"
    static const uint8_t ORIG[6] = {0x8B, 0x48, 0x58, 0x8B, 0x55, 0x08};
    uint8_t cur[6];
    if (!safe_read((uintptr_t)target, cur, 6) || memcmp(cur, ORIG, 6) != 0) return false;

    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED)
        return false;
    if (MH_CreateHook(target, (void*)&health_detour, &g_health_tramp) != MH_OK) return false;
    if (MH_EnableHook(target) != MH_OK) { g_health_tramp = nullptr; return false; }
    return true;
}

// Address of current health (u32) via the object captured by the hook. 0 if not yet seen.
inline uintptr_t resolve_health_addr(uintptr_t* max_out = nullptr) {
    install_health_hook();
    // the trampoline writes eax INTO the g_health_obj variable -> its value = the health object (eax)
    uintptr_t obj = (uintptr_t)g_health_obj;
    if (!obj || obj < 0x10000) return 0;
    if (max_out) *max_out = obj + 0x5C;   // Maximum Health = [pHealth]+5C (cheat table)
    return obj + 0x58;                     // Current Health = [pHealth]+58
}

// --- Notoriety hook (Wanted trap) --------------------------------------------
// Same principle as health: notoriety (float 0=None..1=max) lives in the
// NotorietyManager, reached via ecx in the getter function:
//   AOB "F3 0F 10 41 0C F3 0F 11 45 FC"; at offset 0: "movss xmm0,[ecx+0C]"
//   -> ecx = NotorietyManager; notoriety = [ecx+0C]. We capture ecx via hook.
// NotorietyManager = singleton -> we lock the 1st valid object (float 0..1).
inline volatile uint32_t* g_noto_obj = nullptr;   // captured ecx (NotorietyManager)
inline void* g_noto_tramp = nullptr;

inline void __fastcall noto_filter(uint32_t obj) {
    if (obj < 0x10000 || g_noto_obj) return;      // singleton: lock once
    __try {
        float v = *(volatile float*)(obj + 0x0C);
        if (v >= 0.0f && v <= 1.0f) g_noto_obj = (volatile uint32_t*)obj;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ecx = NotorietyManager on entry -> already the right __fastcall arg. pushad/popad intact.
__declspec(naked) inline void noto_detour() {
    __asm {
        pushad
        call noto_filter          // ecx (__fastcall arg) = current ecx = NotorietyManager
        popad
        jmp [g_noto_tramp]
    }
}

inline bool install_noto_hook() {
    static bool tried = false;
    if (tried) return g_noto_obj != nullptr || g_noto_tramp != nullptr;
    if (!g_health_hook_enabled) return false;      // same opt-in for RAM hooks
    tried = true;
    static const uint8_t AOB[] = {0xF3,0x0F,0x10,0x41,0x0C,0xF3,0x0F,0x11,0x45,0xFC};
    uintptr_t target = find_aob(AOB, sizeof(AOB));  // movss = 5 bytes = exactly the jmp's size
    if (!target) return false;
    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (MH_CreateHook((void*)target, (void*)&noto_detour, &g_noto_tramp) != MH_OK) return false;
    if (MH_EnableHook((void*)target) != MH_OK) { g_noto_tramp = nullptr; return false; }
    return true;
}

// Address of notoriety (float), 0 if unavailable.
inline uintptr_t resolve_noto_addr() {
    install_noto_hook();
    uintptr_t obj = (uintptr_t)g_noto_obj;
    if (!obj || obj < 0x10000) return 0;
    return obj + 0x0C;
}

inline bool get_notoriety(float& v) {
    uintptr_t a = resolve_noto_addr();
    return a && safe_read(a, &v, 4);
}

// Sets notoriety. The "meter" value is 0..1, but the hostile/pursuit state may
// need an overflow (> 1.0) to cross the internal threshold. Wide cap for testing.
// Wanted trap = value to tune based on testing.
inline bool set_notoriety(float v) {
    uintptr_t a = resolve_noto_addr();
    if (!a) return false;
    if (v < 0.0f) v = 0.0f; else if (v > 100.0f) v = 100.0f;
    return safe_write(a, &v, 4);
}

// Health read/write. Returns false if out-of-game.
inline bool get_health(uint32_t& cur, uint32_t& max) {
    uintptr_t maxa = 0;
    uintptr_t a = resolve_health_addr(&maxa);
    if (!a) return false;
    return rd32(a, cur) && rd32(maxa, max);
}

inline bool set_health(uint32_t val) {
    uintptr_t a = resolve_health_addr();
    if (!a) return false;
    return safe_write(a, &val, 4);
}

// Credits (or debits) florins. false if the player is not in-game -> retry.
inline bool add_money(int32_t delta) {
    uintptr_t addr = resolve_money_addr();
    if (!addr) return false;
    uint32_t cur;
    if (!rd32(addr, cur)) return false;
    int64_t next = (int64_t)cur + delta;
    if (next < 0) next = 0;
    uint32_t val = (uint32_t)next;
    return safe_write(addr, &val, 4);
}

// Debits a percentage of florins (Templar Tax trap). false if out-of-game.
inline bool tax_money(int percent) {
    uintptr_t addr = resolve_money_addr();
    if (!addr) return false;
    uint32_t cur;
    if (!rd32(addr, cur)) return false;
    uint32_t val = (uint32_t)((int64_t)cur * (100 - percent) / 100);
    return safe_write(addr, &val, 4);
}

// Kills Ezio (DeathLink received / Death trap).
// health=0 does NOT kill (LIM-004: death is event-gated). Real death = Animus DESYNC:
// byte [pHealth]+0xBC = 1 (Paul44 cheat table). Flag read continuously by the game (like God
// Mode [pSharedData]+0x20) -> INSTANT desync/death. g_health_obj = [pHealth] (captured by
// the hook). A pure single-byte write = safe (no native code call, respects the zero-bug rule).
inline bool kill_player() {
    resolve_health_addr();                    // force hook install / Ezio capture
    uintptr_t obj = (uintptr_t)g_health_obj;
    if (!obj || obj < 0x10000) return false;  // not in-game yet
    uint8_t one = 1;
    return safe_write(obj + 0xBC, &one, 1);   // Desync = death
}

// Sets health to 1 (Bad Medicine trap). false if out-of-game.
inline bool cripple_health() { return set_health(1); }

} // namespace ac2ap::game
