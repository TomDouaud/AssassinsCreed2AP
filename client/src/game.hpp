// AC2AP - in-process memory access to the game (applying items).
// Chain validated in the lab (tools/resolve_money.py + Paul44 cheat table):
//   AOB "8A 41 2C 84 C0" in the code -> mov reg,[static] instruction 6 bytes before
//   r1=[static]; r2=[r1+0x20]; r3=[r2+0x18]; BhvAss=[r3]
//   inv=[[[BhvAss+0x10]+0x58]+0xC]; money = [[inv+0x10]+0] + 0x10 (u32)
// All reads go through safe_read: pointers are invalid during
// loading/menus -> clean failure, we retry next tick.
#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstring>
#include <MinHook.h>

namespace ac2ap::game {

// --- Hardware data breakpoint (non-invasive RE: find who writes an address) ----
// Uses the CPU debug registers (DR0/DR7) via SetThreadContext + a VEH. No code is
// patched, so it cannot corrupt the game; it only observes. Collects the distinct
// EIPs that write the watched address, so a differential (idle baseline vs. an action)
// reveals the event routine. One watchpoint (DR0), 4 bytes, on write.
inline uintptr_t g_hwbp_addr = 0;
inline uintptr_t g_hwbp_eips[64];
inline volatile LONG g_hwbp_neips = 0;
inline void* g_hwbp_veh = nullptr;

inline LONG CALLBACK hwbp_handler(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    if (!(c->Dr6 & 0x1)) return EXCEPTION_CONTINUE_SEARCH;   // not our DR0 hit
    uintptr_t eip = c->Eip;
    LONG n = g_hwbp_neips;
    bool found = false;
    for (LONG i = 0; i < n && i < 64; i++) if (g_hwbp_eips[i] == eip) { found = true; break; }
    if (!found && n < 64) { g_hwbp_eips[n] = eip; InterlockedIncrement(&g_hwbp_neips); }
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

inline void hwbp_apply(uintptr_t addr, bool enable) {
    DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{}; te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                                  FALSE, te.th32ThreadID);
            if (!h) continue;
            SuspendThread(h);
            CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(h, &ctx)) {
                if (enable) {
                    ctx.Dr0 = addr;
                    ctx.Dr7 &= ~((1UL << 0) | (0xFUL << 16));      // clear L0 + RW0/LEN0
                    ctx.Dr7 |= (1UL << 0) | (1UL << 16) | (3UL << 18);  // L0, write, 4 bytes
                } else {
                    ctx.Dr7 &= ~(1UL << 0);
                    ctx.Dr0 = 0;
                }
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                SetThreadContext(h, &ctx);
            }
            ResumeThread(h);
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

inline bool hwbp_arm(uintptr_t addr) {
    if (addr < 0x10000 || (addr & 3)) return false;   // must be 4-aligned
    if (!g_hwbp_veh) g_hwbp_veh = AddVectoredExceptionHandler(1, hwbp_handler);
    if (!g_hwbp_veh) return false;
    g_hwbp_neips = 0;
    g_hwbp_addr = addr;
    hwbp_apply(addr, true);
    return true;
}

inline void hwbp_disarm() {
    if (g_hwbp_addr) hwbp_apply(g_hwbp_addr, false);
    g_hwbp_addr = 0;
}

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

// Same as find_aob, but bytes whose mask entry is 0 are wildcards. Needed to match code that
// contains relative call/jump offsets, which differ between builds even when the code is
// identical - matching the fixed opcodes and reading the offsets afterwards is what makes a
// signature survive across the Skidrow / Steam / Ubisoft executables.
inline uintptr_t find_aob_masked(const uint8_t* pat, const uint8_t* mask, size_t n) {
    auto base = (uintptr_t)GetModuleHandleA(nullptr);
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; s++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint8_t* p = (const uint8_t*)(base + sec->VirtualAddress);
        size_t len = sec->Misc.VirtualSize;
        for (size_t i = 0; i + n <= len; i++) {
            size_t k = 0;
            for (; k < n; k++)
                if (mask[k] && p[i + k] != pat[k]) break;
            if (k == n) return (uintptr_t)(p + i);
        }
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

// Enumerate ALL villa-array copies (templates + live). The live one is identified at
// runtime as the copy whose levels CHANGE across a renovation (templates are frozen).
// Fills g_villa_copies; returns the count.
struct VillaCopy { uintptr_t base; int n; uint32_t first_id; uint8_t lvl[32]; };
inline VillaCopy g_villa_copies[48];
inline int g_villa_ncopies = 0;

inline int villa_list_all() {
    g_villa_ncopies = 0;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0x10000;
    while (addr < 0xFFFF0000 && g_villa_ncopies < 48) {
        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) break;
        uintptr_t base = (uintptr_t)mbi.BaseAddress; size_t sz = mbi.RegionSize;
        DWORD prot = mbi.Protect & 0xFF;
        bool ok = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE
                  && (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY) && !(mbi.Protect & PAGE_GUARD);
        if (ok && sz >= 0x30) {
            __try {
                uint8_t* p = (uint8_t*)base;
                auto sig = [](uint8_t* q) -> bool {
                    if (*(uint32_t*)(q + 4) != (uint32_t)(uintptr_t)q) return false;
                    uint32_t id = *(uint32_t*)(q + 0x14);
                    if ((id & 0xFF) || (id >> 8) < 0x88000Du || (id >> 8) > 0x880017u) return false;
                    return *(uint32_t*)q <= 10;
                };
                for (size_t i = 0; i + 0x48 <= sz && g_villa_ncopies < 48; i += 4) {
                    uint8_t* q = p + i;
                    if (!sig(q) || !sig(q + 0x18) || !sig(q + 0x30)) continue;
                    uint32_t id0 = *(uint32_t*)(q + 0x14), id1 = *(uint32_t*)(q + 0x18 + 0x14),
                             id2 = *(uint32_t*)(q + 0x30 + 0x14);
                    if (id1 != id0 + 0x100 || id2 != id1 + 0x100) continue;
                    uintptr_t first = base + i;
                    while (first >= base + 0x18 && sig((uint8_t*)(first - 0x18))
                           && *(uint32_t*)(first - 0x18 + 0x14) + 0x100 == *(uint32_t*)(first + 0x14))
                        first -= 0x18;
                    uint32_t spread = 0;
                    int n = villa_scan_array(first, &spread);
                    VillaCopy& c = g_villa_copies[g_villa_ncopies++];
                    c.base = first; c.n = n;
                    c.first_id = *(uint32_t*)(first + 0x14);
                    for (int k = 0; k < n && k < 32; k++)
                        c.lvl[k] = (uint8_t)*(uint32_t*)(first + (uintptr_t)k * 0x18);
                    i = (first - base) + (size_t)n * 0x18;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        addr = base + sz;
    }
    return g_villa_ncopies;
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
            // m>=10: loading-screen garbage (1/1) must not hold the lock (seen live 12/07).
            if (c <= 100 && m >= 10 && m <= 100 && c <= m) return;  // Ezio ok, keep it
        }
        uint32_t cur = *(volatile uint32_t*)(obj + 0x58);
        uint32_t mx  = *(volatile uint32_t*)(obj + 0x5C);
        if (cur >= 1 && cur <= 100 && mx >= 10 && mx <= 100 && cur <= mx)
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

// --- SetHealth hook (reliable death detection; found 19/07 via HWBP + Ghidra) ----------------
// __thiscall SetHealth(this, int newHP) at base+RVA 0x11FE0B0 (1.01 build). Ghidra decompile
// shows a REAL death calls it with newHP < 0, which is what sets the desync flag [this+0xBC]=1
// (the same flag kill_player writes). Hooking the call itself catches EVERY death - including
// instant falls that the 250 ms health poll misses. Guards go through the same function, so
// the detour filters this == g_health_obj (Ezio's captured health object).
// Located by pattern, not by address: a fixed RVA only matches the build it was mapped on, and
// the Steam/Ubisoft executable differs from the Skidrow one - so the hook silently never
// installed there, leaving those players without fall-death detection. This 16-byte signature is
// unique in the executable and covers the function's own prologue:
//   55 8B EC        push ebp; mov ebp,esp
//   56 8B F1        push esi; mov esi,ecx        (this)
//   8B 46 58        mov eax,[esi+58]             (current HP)
//   57              push edi
//   89 86 C0000000  mov [esi+C0],eax             (stash the old HP - see the decompile)
inline void* g_sethealth_tramp = nullptr;
inline volatile LONG g_death_hook_flag = 0;    // set on Ezio SetHealth(hp<0); worker consumes
inline volatile LONG g_death_hook_any = 0;     // DIAG: every hp<0 call, any entity. If guard
                                               // kills never bump it, the function is player-only
                                               // and the g_health_obj filter can be dropped.
inline bool g_sethealth_installed = false;

// Is the captured "Ezio health" object still believable? The capture is known to latch onto the
// wrong object sometimes (measured live: cur=4279906932 max=4291501413), and when it does, an
// equality test against it silently rejects every real death.
inline bool health_obj_plausible() {
    uintptr_t obj = (uintptr_t)g_health_obj;
    if (!obj || obj < 0x10000) return false;
    uint32_t cur = 0, mx = 0;
    if (!safe_read(obj + 0x58, &cur, 4) || !safe_read(obj + 0x5C, &mx, 4)) return false;
    return mx >= 1 && mx <= 200 && cur <= mx;
}

inline void __fastcall sethealth_detour(void* thisp, void* /*edx*/, int hp) {
    if (hp < 0) {
        InterlockedIncrement(&g_death_hook_any);
        // Prefer Ezio's captured health object when we can trust it. When we cannot, accept the
        // call anyway rather than lose the death: this path measured player-only in-game (killing
        // guards never reaches it - see the g_death_hook_any diagnostic), so the filter is a
        // refinement, not the thing keeping other entities out.
        void* ezio = (void*)g_health_obj;
        if (thisp == ezio || !health_obj_plausible())
            InterlockedExchange(&g_death_hook_flag, 1);
    }
    ((void(__fastcall*)(void*, void*, int))g_sethealth_tramp)(thisp, nullptr, hp);
}

inline bool install_sethealth_hook() {
    if (g_sethealth_installed) return true;
    if (!g_health_hook_enabled) return false;  // same opt-in gate as the health hook
    static bool tried = false;
    if (tried) return false;                   // one attempt: prologue mismatch = wrong build
    tried = true;
    static const uint8_t AOB[] = {0x55, 0x8B, 0xEC, 0x56, 0x8B, 0xF1, 0x8B, 0x46,
                                  0x58, 0x57, 0x89, 0x86, 0xC0, 0x00, 0x00, 0x00};
    uint8_t* p = (uint8_t*)find_aob(AOB, sizeof(AOB));
    if (!p) return false;                      // signature absent: leave the game alone
    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (MH_CreateHook((void*)p, (void*)&sethealth_detour, &g_sethealth_tramp) != MH_OK) return false;
    if (MH_EnableHook((void*)p) != MH_OK) return false;
    g_sethealth_installed = true;
    return true;
}

// --- Notoriety hook (Wanted trap / Templar Grip) -------------------------------
// Same principle as health: notoriety (float 0=None..1=max) lives in the
// NotorietyManager, reached via ecx in the getter function:
//   AOB "F3 0F 10 41 0C F3 0F 11 45 FC"; at offset 0: "movss xmm0,[ecx+0C]"
//   -> ecx = NotorietyManager; notoriety = [ecx+0C]. We capture ecx via hook.
// CONTINUOUS capture (BUG-007): the manager is RE-CREATED on city change, so locking
// the first object goes stale (writes land in a dead copy — clamp silently inert).
// The HUD calls this getter every frame for the ACTIVE manager, so last-seen = current;
// a transient wrong capture self-heals within a frame.
inline volatile uint32_t* g_noto_obj = nullptr;   // captured ecx (NotorietyManager)
inline void* g_noto_tramp = nullptr;

inline void __fastcall noto_filter(uint32_t obj) {
    if (obj < 0x10000) return;
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

// Calls the game's native SetNotoriety(float) instead of raw-writing [+0x0C].
// thiscall: ecx = manager, one float stack arg, ret 4 (callee-cleaned). RVA 0xCADDB0
// (module base 0x400000; resolved via GetModuleHandle). Unlike the raw write, this runs
// the store + the state-notify sub, so the renegade state updates. Found via HW breakpoint
// (docs/design-notoriety.md). EXPERIMENTAL: runs game logic from the worker thread; SEH-guarded
// and gated behind the caller. Returns false if the manager isn't captured or on fault.
inline uintptr_t g_set_noto_fn = 0;
inline bool call_set_notoriety(float v) {
    uintptr_t mgr = resolve_noto_addr();       // installs hook + validates g_noto_obj
    if (!mgr) return false;
    mgr -= 0x0C;                                // resolve_noto_addr returns obj+0x0C
    if (!g_set_noto_fn)
        g_set_noto_fn = (uintptr_t)GetModuleHandleA(nullptr) + 0xCADDB0;
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    uintptr_t fn = g_set_noto_fn;
    bool ok = true;
    __try {
        __asm {
            mov  ecx, mgr
            push bits
            call fn            // ret 4: callee pops the arg, stack stays balanced
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
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

// True when the game window is in the foreground (our process owns the focus). AC2 pauses
// when unfocused, so a desync byte written then gets wiped on resume - gate DeathLink
// application on this so a death received while unfocused is buffered and applied on refocus.
inline bool is_game_focused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

// Is Ezio Animus-DESYNCED (actually dead)? Reads the same [pHealth]+0xBC flag kill_player sets.
// This is the correct death signal for DeathLink emission - unlike health==0, which is only the
// low-health warning (Ezio can still take hits / act). Returns false if not in-game / not captured.
inline bool is_desynced() {
    resolve_health_addr();                    // ensure hook installed / Ezio captured
    uintptr_t obj = (uintptr_t)g_health_obj;
    if (!obj || obj < 0x10000) return false;
    uint8_t b = 0;
    return safe_read(obj + 0xBC, &b, 1) && b != 0;
}

// Sets health to 1 (Bad Medicine trap). false if out-of-game.
inline bool cripple_health() { return set_health(1); }

// --- Glyph puzzles (Animus Database) -------------------------------------------------------
// Glyphs leave NO record in the save (checked in the lab), which is why detection was parked.
// But the Animus Database UI can answer "is this minigame solved", and decompiling that bridge
// (the handler for GET_ANIMUS_DB_IS_MINIGAME_SOLVED) shows exactly where it looks:
//   mgr    = *(u32*)0x022553B0                        - glyph-puzzle manager
//   count  = *(u16*)(mgr + 0x2E) & 0x3FFF
//   slot   = *(u32**)(*(u32*)(mgr + 0x28) + i*4)      - array of handle slots
//   obj    = slot[0], valid only while (int)slot[2] < 0   - the engine's handle resolve
//   solved = (*(u8*)(obj + 0x44) >> 1) & 1
// Strictly READ-ONLY: we read what the pause menu already reads, so this cannot break anything.
// The manager pointer is NOT hardcoded: a fixed RVA only matches the build it was mapped on (the
// Steam/Ubisoft executable differs from the Skidrow one and silently misses every fixed address).
// Instead we locate the very instruction that loads it, which is unique in the executable:
//     8A 49 61        mov  cl,[ecx+61]      <- glyph index from the Animus DB entry
//     80 E1 1F        and  cl,1F
//     76 xx           jbe  short
//     A1 <abs32>      mov  eax,[manager]    <- the address we want, already relocated by the loader
//     0F B7 50 2E     movzx edx,[eax+2E]    <- the count read
inline uintptr_t glyph_mgr() {
    static uintptr_t s_ptr_addr = 0;
    if (!s_ptr_addr) {
        static const uint8_t AOB[] = {0x8A, 0x49, 0x61, 0x80, 0xE1, 0x1F};
        uintptr_t m = find_aob(AOB, sizeof(AOB));
        if (!m) return 0;
        uint8_t op = 0;
        if (!safe_read(m + 8, &op, 1) || op != 0xA1) return 0;   // expect mov eax,[abs32]
        uint32_t a = 0;
        if (!safe_read(m + 9, &a, 4) || a < 0x10000) return 0;
        s_ptr_addr = a;
    }
    uint32_t mgr = 0;
    if (!rd32(s_ptr_addr, mgr) || mgr < 0x10000) return 0;
    return mgr;
}

// Number of glyph puzzles the engine knows about (0 if unavailable, e.g. out of game).
inline int glyph_count() {
    uintptr_t mgr = glyph_mgr();
    if (!mgr) return 0;
    uint16_t n = 0;
    if (!safe_read(mgr + 0x2E, &n, 2)) return 0;
    n &= 0x3FFF;
    return n > 256 ? 0 : n;            // sanity: AC2 has 20
}

// Resolves puzzle i, 0 if the slot is empty/stale. See the handle-resolve note above.
inline uintptr_t glyph_obj(int i) {
    uintptr_t mgr = glyph_mgr();
    if (!mgr || i < 0) return 0;
    uint32_t arr = 0, slot = 0;
    if (!rd32(mgr + 0x28, arr) || arr < 0x10000) return 0;
    if (!rd32(arr + (uint32_t)i * 4, slot) || slot < 0x10000) return 0;
    uint32_t obj = 0, gen = 0;
    if (!rd32(slot, obj) || !rd32(slot + 8, gen)) return 0;
    if ((int32_t)gen >= 0) return 0;   // slot not live
    return obj < 0x10000 ? 0 : obj;
}

inline bool glyph_solved(int i) {
    uintptr_t o = glyph_obj(i);
    if (!o) return false;
    uint8_t b = 0;
    return safe_read(o + 0x44, &b, 1) && ((b >> 1) & 1) != 0;
}

// --- Cinematic state (investigation) --------------------------------------------------------
// AC2 has no cutscene skip - it arrived in Brotherhood - so there is no native "skip" to call for
// in-engine cinematics; what we found earlier only drives the pre-rendered video player, which
// the game already lets you skip. The game does however KNOW when a cinematic is playing: the
// pause menu answers "PauseIsInCinematic" with
//     mgr = *(u32*)<global> ; playing = *(u32*)(mgr + 0x928) != 0
// and that global is reachable without hardcoding an address:
//     E8 <rel>            call <getter>          <- unique when paired with the compare below
//     39 98 28 09 00 00   cmp [eax+0x928], ebx
//     <getter>: A1 <abs32> C3                    -> mov eax,[global]; ret
// Read-only: this reads exactly what the pause menu reads.
inline uintptr_t cine_mgr() {
    static uintptr_t s_global = 0;
    if (!s_global) {
        static const uint8_t PAT[] = {0xE8, 0, 0, 0, 0, 0x39, 0x98, 0x28, 0x09, 0x00, 0x00};
        static const uint8_t MSK[] = {1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1};
        uintptr_t m = find_aob_masked(PAT, MSK, sizeof(PAT));
        if (!m) return 0;
        int32_t rel = 0;
        std::memcpy(&rel, (const void*)(m + 1), 4);
        uintptr_t getter = m + 5 + rel;
        uint8_t op = 0;
        if (!safe_read(getter, &op, 1) || op != 0xA1) return 0;   // expect mov eax,[abs32]
        uint32_t g = 0;
        if (!safe_read(getter + 1, &g, 4) || g < 0x10000) return 0;
        s_global = g;
    }
    uint32_t mgr = 0;
    if (!rd32(s_global, mgr) || mgr < 0x10000) return 0;
    return mgr;
}

// Pointer to the cinematic currently playing, 0 when none. Same field the pause menu tests.
inline uintptr_t cine_current() {
    uintptr_t mgr = cine_mgr();
    if (!mgr) return 0;
    uint32_t c = 0;
    if (!rd32(mgr + 0x928, c) || c < 0x10000) return 0;
    return c;
}

// --- Skip the current cinematic ------------------------------------------------------------
// The engine routes named UI actions ("Quit", "ReloadLastCheckpoint", "RestartMemory",
// "AbortMemory", "SkipCinematic", "ExitReplayMode") through one handler; the SkipCinematic
// branch is just three cdecl calls taking no arguments:
//     prepare(); ev = skip_event(); dispatch(ev);
// where dispatch is the same generic event broadcast SetNotoriety goes through, and skip_event
// hands back the engine's MissionSkipCinematicEvent singleton. Found by decompiling the handler
// that references the "SkipCinematic" string (Ghidra).
// Rather than hardcoding the three addresses (build-specific, and wrong on Steam/Ubisoft), we
// find the call site itself. This masked signature is unique in the executable - the wildcards
// are the relative call offsets, which we then decode to get the real function addresses:
//   83 C4 08     add esp,8          } tail of the "SkipCinematic" name comparison
//   85 C0  75 ?? test eax,eax; jnz  }
//   E8 <rel>     call prepare        -> +7
//   8B F0        mov esi,eax         (kept: it becomes `this` below)
//   E8 <rel>     call get_event      -> +14
//   50           push eax            (the event, as the argument)
//   8B CE        mov ecx,esi         <- dispatch is a __thiscall, NOT cdecl: Ghidra typed it
//   E8 <rel>     call dispatch       -> +22    as one argument and missed the ecx.
inline bool skip_cinematic() {
    static const uint8_t PAT[] = {0x83, 0xC4, 0x08, 0x85, 0xC0, 0x75, 0x00,
                                  0xE8, 0, 0, 0, 0, 0x8B, 0xF0,
                                  0xE8, 0, 0, 0, 0, 0x50, 0x8B, 0xCE,
                                  0xE8, 0, 0, 0, 0};
    static const uint8_t MSK[] = {1, 1, 1, 1, 1, 1, 0,
                                  1, 0, 0, 0, 0, 1, 1,
                                  1, 0, 0, 0, 0, 1, 1, 1,
                                  1, 0, 0, 0, 0};
    uintptr_t m = find_aob_masked(PAT, MSK, sizeof(PAT));
    if (!m) return false;                  // signature absent: do nothing at all
    auto call_target = [m](size_t at) -> uintptr_t {
        int32_t rel = 0;
        std::memcpy(&rel, (const void*)(m + at + 1), 4);
        return m + at + 5 + rel;           // E8 is relative to the next instruction
    };
    auto prepare   = (void*(__cdecl*)())call_target(7);
    auto get_event = (void*(__cdecl*)())call_target(14);
    auto dispatch  = (void(__fastcall*)(void*, void*, void*))call_target(22);
    __try {
        void* self = prepare();
        void* ev = get_event();
        if (!ev) return false;             // no event object -> not in a cinematic
        dispatch(self, nullptr, ev);       // __fastcall(this, dummy edx, arg) == __thiscall
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace ac2ap::game
