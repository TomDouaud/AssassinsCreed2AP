// AC2AP - the player's inventory, through the engine's own structures and routines.
//
// How this was reversed is in docs/re/inventory-system.md. The short version:
//
//   InventoryDataItem            (game::resolve_inv_bases -> pInventory)
//     +0x0C LogicalInventory     (inline)
//             +0x04 InventoryItemContainer*[]   array-ref: {ptr, u16 capacity, u16 count|flags}
//   InventoryItemContainer / InventoryRechargeableContainer  (0x30, one per category)
//     +0x08 Category             +0x0C CurrentContainerLevel
//     +0x10 CurrentItemCount     +0x14 MaximumItemCount
//     +0x18 ItemList             PhysicalInventoryItem*[]
//   PhysicalInventoryItem (0xC)
//     +0x04 Item                 -> handle slot {object*, refcount, flags, object id}
//   InventoryItemSettings (0x38) = the object behind the handle; its id IS the forge id
//     +0x0C Category  +0x10 SubCategory  +0x14 InitialDefaultPrice  +0x30 Locked
//
// An item is OWNED when a PhysicalInventoryItem referencing its settings sits in the ItemList
// of the container of its category. `Locked` is an availability flag, not ownership.
//
// Nothing here is a per-build address. Classes are recognised by the crc32 of their name,
// read from the class-id getter every reflected class has at vtable slot 5
// (`mov eax, crc32(name); ret`), and the engine routines are reached by vtable slot index,
// which was checked to be identical on the retail and Steam builds:
//   slot 15  bool AddItem   (uint count, Handle<InventoryItemSettings>* item)
//   slot 17  bool RemoveItem(uint count, Handle<InventoryItemSettings>* item)
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <MinHook.h>

#include "game.hpp"

namespace ac2ap::inv {

// InventoryItemCategory, dumped from the engine's reflection tables (tools/enumdump.py).
enum : uint32_t {
    CAT_UNSET = 0, CAT_MONEY = 1, CAT_WEAPON = 2, CAT_ARMOR = 3, CAT_TREASURE_KEYS = 4,
    CAT_SMOKE_BOMB = 5, CAT_UPGRADES = 6, CAT_HEALTH_OBJECT = 7, CAT_KNIVES = 8,
    CAT_QUEST_ITEM = 9, CAT_GUN_BULLET = 10, CAT_POISON = 11, CAT_COLORS = 12, CAT_CAPE = 13,
    CAT_METAL_FIST = 14, CAT_HIDDEN_BLADE = 15, CAT_CAPE_COLOR = 16, CAT_ULTIMATE_ARMOR = 17,
    CAT_ABILITY = 18, CAT_UNLOCKABLE = 19,
};

inline const char* category_name(uint32_t c) {
    static const char* const N[] = {
        "Unset", "Money", "Weapon", "Armor", "TreasureKeys", "SmokeBomb", "Upgrades",
        "HealthObject", "Knives", "QuestItem", "GunBullet", "Poison", "Colors", "Cape",
        "MetalFist", "HiddenBlade", "CapeColor", "UltimateArmor", "Ability", "Unlockable"};
    return c < sizeof(N) / sizeof(N[0]) ? N[c] : "?";
}

// crc32 of the class names (the engine's class ids).
constexpr uint32_t CRC_ITEM_SETTINGS  = 0xC69075AB;  // InventoryItemSettings
constexpr uint32_t CRC_LOGICAL        = 0xC3BFD283;  // LogicalInventory
constexpr uint32_t CRC_CONTAINER      = 0x6D8B7591;  // InventoryItemContainer
constexpr uint32_t CRC_RECHARGEABLE   = 0x08CE3886;  // InventoryRechargeableContainer

constexpr int VT_CLASS_ID    = 5;    // every reflected class
constexpr int VT_ADD_ITEM    = 15;   // containers
constexpr int VT_REMOVE_ITEM = 17;

// Armour SubCategory = body part * 5 + tier (chest 0-4, greaves 5-9, vambraces 10-14,
// spaulders 15-19). The shop keeps one piece per body part, so do we.
constexpr uint32_t ARMOR_TIERS = 5;

// Categories the player holds several of (a count or one list entry each). Everything else is
// a unique piece of equipment: holding it twice means nothing, so a second grant is a no-op.
inline bool is_stackable(uint32_t cat) {
    return cat == CAT_MONEY || cat == CAT_SMOKE_BOMB || cat == CAT_HEALTH_OBJECT ||
           cat == CAT_KNIVES || cat == CAT_GUN_BULLET || cat == CAT_POISON ||
           cat == CAT_TREASURE_KEYS;
}

// A pouch is an Upgrades item; what it does is the CurrentContainerLevel (+0x0C) of the container
// it extends - read live: owning tier I of the medicine / knife / smoke pouch = level 1 there.
// The capacity follows from (category, level): the container carries no capacity of its own.
// ponytail: hard-coded for the 10 pouches the apworld ships; the engine links them through
// InventoryItemContainerLevelSettings {Level, ?, Item, Locked} if more are ever needed.
struct PouchLevel { uint32_t forge; uint32_t category; uint32_t level; };
constexpr PouchLevel POUCHES[] = {
    {0x55308E7F, CAT_KNIVES, 1},        {0x55308E80, CAT_KNIVES, 2},
    {0x55308E81, CAT_KNIVES, 3},        {0x55308E83, CAT_HEALTH_OBJECT, 1},
    {0x55308EA5, CAT_HEALTH_OBJECT, 2}, {0x55308EA6, CAT_HEALTH_OBJECT, 3},
    {0x61D961DF, CAT_POISON, 2},        {0x61D961DE, CAT_POISON, 3},
    {0x55308EA7, CAT_SMOKE_BOMB, 1},    {0x55308EA9, CAT_SMOKE_BOMB, 3},
};
constexpr uint32_t CONTAINER_LEVEL = 0x0C;

struct Item {
    uintptr_t phys, slot, settings;
    uint32_t forge, category, sub, price;
};

enum Result {
    GRANTED = 0,         // added to the inventory
    ALREADY_OWNED,       // the very same item is already there
    HAVE_BETTER,         // armour: that body part already carries an equal or better tier
    NOT_IN_GAME,         // no inventory right now (menu, loading) - retry later
    UNKNOWN_ITEM,        // no InventoryItemSettings with that forge id is loaded
    NO_CONTAINER,        // the player has no container for the item's category
    REFUSED,             // the engine said no (wrong category or no room)
    CALL_FAILED,         // the native call could not be made or faulted
    REMOVED,             // drop(): taken out of the inventory
    NOT_OWNED,           // drop(): was not there
};

inline const char* result_name(int r) {
    static const char* const N[] = {"granted", "already owned", "better piece equipped",
        "not in game", "unknown item", "no container", "refused by the game", "call failed",
        "removed", "not owned"};
    return (r >= 0 && r < (int)(sizeof(N) / sizeof(N[0]))) ? N[r] : "?";
}

// --- reading -------------------------------------------------------------------

inline bool in_module(uintptr_t a) {
    static uintptr_t lo = 0, hi = 0;
    if (!lo) {
        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), GetModuleHandleA(nullptr), &mi, sizeof(mi)))
            return false;
        lo = (uintptr_t)mi.lpBaseOfDll;
        hi = lo + mi.SizeOfImage;
    }
    return a >= lo && a < hi;
}

// True if obj is an instance of the class whose name hashes to crc.
inline bool class_is(uintptr_t obj, uint32_t crc) {
    uint32_t vt = 0, getter = 0;
    uint8_t code[6];
    if (obj < 0x10000 || !game::rd32(obj, vt) || !in_module(vt)) return false;
    if (!game::rd32(vt + VT_CLASS_ID * 4, getter) || !in_module(getter)) return false;
    if (!game::safe_read(getter, code, sizeof(code))) return false;
    uint32_t imm;
    memcpy(&imm, code + 1, 4);
    return code[0] == 0xB8 && code[5] == 0xC3 && imm == crc;   // mov eax, crc ; ret
}

// Count of an engine array-ref whose pointer field is at `field`.
inline uint32_t array_count(uintptr_t field) {
    uint32_t hdr = 0;
    if (!game::rd32(field + 4, hdr)) return 0;
    return (hdr >> 16) & 0x3FFF;
}

// The object behind a handle slot, 0 if the handle is dead.
inline uintptr_t slot_object(uintptr_t slot) {
    uint32_t obj = 0, flags = 0;
    if (!slot || !game::rd32(slot, obj) || !game::rd32(slot + 8, flags)) return 0;
    return ((int32_t)flags < 0) ? obj : 0;
}

// The player's LogicalInventory, 0 when there is none (menu, loading screen).
inline uintptr_t logical() {
    uintptr_t inv = 0;
    if (!game::resolve_inv_bases(&inv, nullptr, nullptr) || !inv) return 0;
    uintptr_t l = inv + 0x0C;
    return class_is(l, CRC_LOGICAL) ? l : 0;
}

inline bool is_container(uintptr_t c) {
    return class_is(c, CRC_CONTAINER) || class_is(c, CRC_RECHARGEABLE);
}

inline int containers(uintptr_t* out, int cap) {
    uintptr_t l = logical();
    uint32_t arr = 0;
    if (!l || !game::rd32(l + 4, arr) || !arr) return 0;
    uint32_t n = array_count(l + 4);
    int k = 0;
    for (uint32_t i = 0; i < n && k < cap; i++) {
        uint32_t c = 0;
        if (game::rd32(arr + i * 4, c) && is_container(c)) out[k++] = c;
    }
    return k;
}

inline uintptr_t container_for(uint32_t category) {
    uintptr_t cs[32];
    int n = containers(cs, 32);
    for (int i = 0; i < n; i++) {
        uint32_t cat = 0;
        if (game::rd32(cs[i] + 8, cat) && cat == category) return cs[i];
    }
    return 0;
}

inline bool read_item(uintptr_t slot, Item& it) {
    uintptr_t st = slot_object(slot);
    if (!st || !class_is(st, CRC_ITEM_SETTINGS)) return false;
    it.slot = slot;
    it.settings = st;
    it.forge = it.category = it.sub = it.price = 0;
    game::rd32(slot + 0x0C, it.forge);
    game::rd32(st + 0x0C, it.category);
    game::rd32(st + 0x10, it.sub);
    game::rd32(st + 0x14, it.price);
    return true;
}

// The items of one container (both of its lists).
inline int items(uintptr_t container, Item* out, int cap) {
    int k = 0;
    const uintptr_t lists[2] = {0x18, 0x20};
    for (uintptr_t off : lists) {
        uint32_t arr = 0;
        if (!game::rd32(container + off, arr) || !arr) continue;
        uint32_t n = array_count(container + off);
        for (uint32_t j = 0; j < n && k < cap; j++) {
            uint32_t phys = 0, slot = 0;
            if (!game::rd32(arr + j * 4, phys) || !phys) continue;
            if (!game::rd32(phys + 4, slot)) continue;
            if (read_item(slot, out[k])) { out[k].phys = phys; k++; }
        }
    }
    return k;
}

// --- forge id -> handle slot ---------------------------------------------------
// Every InventoryItemSettings, owned or not, has a handle slot carrying its forge id, so the
// slot is found by looking for the id and checking that what precedes it really is a live
// handle to an InventoryItemSettings.

inline bool valid_slot(uintptr_t slot, uint32_t forge) {
    uint32_t id = 0;
    if (!game::rd32(slot + 0x0C, id) || id != forge) return false;
    uintptr_t obj = slot_object(slot);
    return obj && class_is(obj, CRC_ITEM_SETTINGS);
}

inline uintptr_t scan_slot(uint32_t forge) {
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0x10000;
    while (addr < 0xFFFF0000) {
        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) break;
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        size_t sz = mbi.RegionSize;
        DWORD prot = mbi.Protect & 0xFF;
        bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && sz >= 0x10 &&
                  (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
                   prot == PAGE_EXECUTE_READWRITE);
        if (ok) {
            __try {
                for (uintptr_t a = base + 0x0C; a + 4 <= base + sz; a += 4)
                    if (*(uint32_t*)a == forge && valid_slot(a - 0x0C, forge)) return a - 0x0C;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (!sz) break;
        addr = base + sz;
    }
    return 0;
}

// Cached: the scan walks the whole heap. Entries are re-checked on every use, because the
// item pool does not sit at the same address from one session (or one load) to the next.
struct SlotEntry { uint32_t forge; uintptr_t slot; };
inline SlotEntry g_slots[256];
inline int g_nslots = 0;

inline void cache_slot(uint32_t forge, uintptr_t slot) {
    for (int i = 0; i < g_nslots; i++)
        if (g_slots[i].forge == forge) { g_slots[i].slot = slot; return; }
    if (g_nslots < 256) g_slots[g_nslots++] = {forge, slot};
}

inline uintptr_t find_slot(uint32_t forge) {
    for (int i = 0; i < g_nslots; i++)
        if (g_slots[i].forge == forge) {
            if (valid_slot(g_slots[i].slot, forge)) return g_slots[i].slot;
            g_slots[i].slot = scan_slot(forge);
            return g_slots[i].slot;
        }
    uintptr_t s = scan_slot(forge);
    if (s) cache_slot(forge, s);
    return s;
}

// One heap walk for many forge ids. Re-checking everything received on connect would
// otherwise cost one full walk per item and hold the client's worker for a minute or more.
inline void prefetch_slots(const uint32_t* want, int n) {
    uint32_t w[256];
    int m = 0;
    for (int i = 0; i < n && m < 256; i++) {          // sorted insert, no duplicates
        int j = m;
        while (j > 0 && w[j - 1] > want[i]) { w[j] = w[j - 1]; j--; }
        if (j > 0 && w[j - 1] == want[i]) { for (; j < m; j++) w[j] = w[j + 1]; continue; }
        w[j] = want[i];
        m++;
    }
    if (!m) return;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0x10000;
    while (addr < 0xFFFF0000) {
        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) break;
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        size_t sz = mbi.RegionSize;
        DWORD prot = mbi.Protect & 0xFF;
        bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && sz >= 0x10 &&
                  (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
                   prot == PAGE_EXECUTE_READWRITE);
        if (ok) {
            __try {
                for (uintptr_t a = base + 0x0C; a + 4 <= base + sz; a += 4) {
                    uint32_t v = *(uint32_t*)a;
                    int lo = 0, hi = m;
                    while (lo < hi) { int mid = (lo + hi) / 2; if (w[mid] < v) lo = mid + 1; else hi = mid; }
                    if (lo < m && w[lo] == v && valid_slot(a - 0x0C, v)) cache_slot(v, a - 0x0C);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (!sz) break;
        addr = base + sz;
    }
}

// --- the engine's own add / remove ---------------------------------------------

inline int give_now(uintptr_t slot, uint8_t flag, char* note, size_t cap, uint32_t mode = 0);   // below

// bool Container::AddItem / RemoveItem(uint count, Handle<InventoryItemSettings>* item).
// __thiscall, spelled as __fastcall with a dummy edx. Returns 1/0, or -1 if the call could
// not be made, -2 if it faulted.
inline int vcall(uintptr_t container, int index, uint32_t count, uintptr_t slot) {
    using Fn = bool(__fastcall*)(void* self, void* edx, uint32_t count, uintptr_t* handle);
    uint32_t vt = 0, fn = 0;
    if (!is_container(container)) return -1;
    if (!game::rd32(container, vt) || !game::rd32(vt + index * 4, fn) || !in_module(fn)) return -1;
    uintptr_t handle = slot;
    __try {
        return ((Fn)fn)((void*)container, nullptr, count, &handle) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -2;
    }
}

inline bool owns(uintptr_t container, uintptr_t settings) {
    Item its[96];
    int n = items(container, its, 96);
    for (int i = 0; i < n; i++)
        if (its[i].settings == settings) return true;
    return false;
}

// Adds the item to the player's inventory the way the shop does. Must run in the game's frame.
inline int grant_now(uintptr_t slot, char* note, size_t cap) {
    Item it{};
    note[0] = 0;
    if (!logical()) return NOT_IN_GAME;
    if (!read_item(slot, it)) return UNKNOWN_ITEM;
    uintptr_t c = container_for(it.category);
    if (!c) {
        snprintf(note, cap, "category %u %s", it.category, category_name(it.category));
        return NO_CONTAINER;
    }
    Item have[96];
    int n = items(c, have, 96);
    if (!is_stackable(it.category))
        for (int i = 0; i < n; i++)
            if (have[i].settings == it.settings) return ALREADY_OWNED;

    if (it.category == CAT_WEAPON)
        return give_now(slot, 1, note, cap);   // flag 1: what the game's own calls use

    int removed = 0;
    if (it.category == CAT_ARMOR) {
        // One piece per body part. Never trade a better piece for a worse one.
        for (int i = 0; i < n; i++) {
            if (have[i].sub / ARMOR_TIERS != it.sub / ARMOR_TIERS) continue;
            if (have[i].sub % ARMOR_TIERS >= it.sub % ARMOR_TIERS) {
                snprintf(note, cap, "wearing %08X (tier %u), offered tier %u", have[i].forge,
                         have[i].sub % ARMOR_TIERS + 1, it.sub % ARMOR_TIERS + 1);
                return HAVE_BETTER;
            }
        }
        for (int i = 0; i < n; i++) {
            if (have[i].sub / ARMOR_TIERS != it.sub / ARMOR_TIERS) continue;
            if (vcall(c, VT_REMOVE_ITEM, 1, have[i].slot) == 1) removed++;
        }
    }
    // Dyes: the give handler is what dyes Ezio (verified live: red at once); the Colors container
    // only records it, holds ONE dye and refuses a second, so swap it there afterwards and put
    // the old one back if the new one is refused anyway.
    if (it.category == CAT_COLORS) {
        int g = give_now(slot, 1, note, cap);
        if (g != GRANTED) return g;
    }
    Item old[8];
    int nold = 0;
    if (it.category == CAT_COLORS)
        for (int i = 0; i < n && nold < 8; i++)
            if (vcall(c, VT_REMOVE_ITEM, 1, have[i].slot) == 1) old[nold++] = have[i];
    removed += nold;

    int r = vcall(c, VT_ADD_ITEM, 1, slot);
    if (r != 1)
        for (int i = 0; i < nold; i++) vcall(c, VT_ADD_ITEM, 1, old[i].slot);
    if (r < 0) {
        snprintf(note, cap, "AddItem %s", r == -2 ? "faulted" : "unreachable");
        return CALL_FAILED;
    }
    // A pouch raises the level of the container it extends; never lowers it.
    char extra[64] = "";
    if (r == 1 && it.category == CAT_UPGRADES)
        for (const PouchLevel& p : POUCHES) {
            if (p.forge != it.forge) continue;
            uintptr_t t = container_for(p.category);
            uint32_t lv = 0;
            if (t && game::rd32(t + CONTAINER_LEVEL, lv) && lv < p.level &&
                game::poke_abs(t + CONTAINER_LEVEL, p.level))
                snprintf(extra, sizeof(extra), ", %s level %u -> %u", category_name(p.category),
                         lv, p.level);
        }
    bool there = owns(c, it.settings);
    snprintf(note, cap, "%s sub=%u price=%u%s%s%s", category_name(it.category), it.sub, it.price,
             removed ? ", replaced the one worn" : "", extra,
             (r == 1 && !there) ? ", counted (no list entry)" : "");
    return r == 1 ? GRANTED : REFUSED;
}

// Takes the item back out. Debug / test helper.
inline int drop_now(uintptr_t slot, char* note, size_t cap) {
    Item it{};
    note[0] = 0;
    if (!logical()) return NOT_IN_GAME;
    if (!read_item(slot, it)) return UNKNOWN_ITEM;
    uintptr_t c = container_for(it.category);
    if (!c) return NO_CONTAINER;
    if (!owns(c, it.settings)) return NOT_OWNED;
    int r = vcall(c, VT_REMOVE_ITEM, 1, slot);
    snprintf(note, cap, "%s sub=%u", category_name(it.category), it.sub);
    return r == 1 ? REMOVED : (r < 0 ? CALL_FAILED : REFUSED);
}

// --- the game's own "give an item to the player" handler ----------------------
// Analysed build 0187DAC0: __thiscall Give(this, GiveEvent* ev), reached only through an event
// delegate (no direct caller). It reads this+0x0C (the player's CharacterAI) and these fields:
//   +0x08 mode (0 give, 1 take)   +0x0C count   +0x10 extra (1 runs an extra reward step)
//   +0x14 Handle<InventoryItemSettings>         +0x18 flag (byte, forwarded to notifications)
// For category Weapon it does NOT use the inventory add: it builds and equips the weapon entity
// (0187D5D0). That is why AddItem put a sword in the Weapon list but not in Ezio's hands.
// The hook only records real calls, so a weapon grant can replay exactly what the game does.
constexpr uint8_t GIVE_SIG[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x8B, 0x41, 0x0C, 0x8B, 0x40, 0x58, 0x53, 0x56, 0x57,
    0x50, 0x89, 0x4D, 0xF8, 0xC6, 0x45, 0xF4, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x75,
    0x08, 0x8B, 0x4E, 0x14, 0x8B, 0x39, 0x8B, 0xD0, 0x8B, 0x41, 0x08, 0x8D, 0x5E, 0x14, 0xC1,
    0xF8, 0x1F, 0x23, 0xF8, 0x8B, 0x46, 0x08, 0x83, 0xC4, 0x04, 0x85, 0xC0, 0x89, 0x55, 0xFC,
    0x0F, 0x85};
constexpr size_t GIVE_SIG_CALL = 24;   // rel32 of the call at +23: differs per build

struct GiveRecord {
    uintptr_t self, character;
    uint32_t self_crc, character_crc, mode, count, extra, slot, forge, category, flag, ret;
};
inline GiveRecord g_give_rec[32];
inline volatile LONG g_give_n = 0;            // calls seen so far
inline volatile uintptr_t g_give_this = 0;    // `this` of the last real (game-made) call
inline volatile LONG g_give_ours = 0;         // >0 while we are the caller
using GiveFn = uint32_t(__fastcall*)(void* self, void* edx, void* ev);
inline GiveFn g_give_orig = nullptr;
inline uintptr_t g_give_fn = 0;

// crc32 of an object's class name, 0 if it is not a reflected object. Raw reads: only called
// inside a __try.
inline uint32_t class_crc_raw(uintptr_t obj) {
    if (obj < 0x10000) return 0;
    uint32_t vt = *(uint32_t*)obj;
    if (!in_module(vt)) return 0;
    uint32_t g = *(uint32_t*)(vt + VT_CLASS_ID * 4);
    if (!in_module(g)) return 0;
    const uint8_t* c = (const uint8_t*)g;
    return (c[0] == 0xB8 && c[5] == 0xC3) ? *(const uint32_t*)(c + 1) : 0;
}

inline void give_record(void* self, const uint8_t* e) {
    __try {
        LONG i = InterlockedIncrement(&g_give_n) - 1;
        GiveRecord& r = g_give_rec[i % 32];
        r.self = (uintptr_t)self;
        r.self_crc = class_crc_raw(r.self);
        r.character = *(uint32_t*)((uint8_t*)self + 0x0C);
        r.character_crc = class_crc_raw(r.character);
        r.mode = *(const uint32_t*)(e + 0x08);
        r.count = *(const uint32_t*)(e + 0x0C);
        r.extra = *(const uint32_t*)(e + 0x10);
        r.slot = *(const uint32_t*)(e + 0x14);
        r.flag = e[0x18];
        r.forge = 0;
        r.category = 0xFFFFFFFF;
        r.ret = 0xFFFFFFFF;
        if (r.slot) {
            r.forge = *(uint32_t*)(r.slot + 0x0C);
            uint32_t obj = *(uint32_t*)r.slot, fl = *(uint32_t*)(r.slot + 8);
            if ((int32_t)fl < 0 && obj) r.category = *(uint32_t*)(obj + 0x0C);
        }
        if (!g_give_ours) g_give_this = (uintptr_t)self;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

inline uint32_t __fastcall give_detour(void* self, void* edx, void* ev) {
    give_record(self, (const uint8_t*)ev);
    uint32_t ret = g_give_orig(self, edx, ev);
    g_give_rec[(g_give_n - 1) % 32].ret = ret & 0xFF;
    return ret;
}

// Retried, not one-shot: on the Steam/Uplay build the code section is still encrypted when the
// ASI loads (the DRM decrypts it later), so the signature only matches once the game has started.
inline bool install_give_hook() {
    if (g_give_orig) return true;
    static DWORD last = 0;
    DWORD now = GetTickCount();
    if (last && now - last < 5000) return false;    // the scan walks the whole code section
    last = now;
    uint8_t mask[sizeof(GIVE_SIG)];
    memset(mask, 1, sizeof(mask));
    for (size_t i = GIVE_SIG_CALL; i < GIVE_SIG_CALL + 4; i++) mask[i] = 0;
    g_give_fn = game::find_aob_masked(GIVE_SIG, mask, sizeof(GIVE_SIG));
    if (!g_give_fn) return false;
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (MH_CreateHook((void*)g_give_fn, (void*)&give_detour, (void**)&g_give_orig) != MH_OK)
        return false;
    if (MH_EnableHook((void*)g_give_fn) != MH_OK) { g_give_orig = nullptr; return false; }
    return true;
}

// --- the player entity the give handler works on ---------------------------------
// The handler only reads this+0x0C: the player's entity (class crc AA2DB0CE, a name the exe does
// not carry). Captured live, it is NOT the CharacterAI the client already resolves, but it OWNS
// it: the entity holds the CharacterAI pointer at +0x20 (and again further on). Nothing points
// back in clear, so the entity is found as "the AA2DB0CE object whose +0x20 is our CharacterAI".
// A second AA2DB0CE object also holds that CharacterAI at +0x20, but its +0x58 is not a pointer:
// the real one's +0x58 leads to the data block whose +0x6C the weapon routine needs (it gives
// up silently when that field is 0), so that is the deciding check.
constexpr uint32_t CRC_PLAYER_ENTITY = 0xAA2DB0CE;
constexpr uint32_t ENTITY_AI_FIELD = 0x20;

inline bool is_player_entity(uintptr_t e, uint32_t ai) {
    uint32_t v = 0, data = 0, weapons = 0;
    return class_is(e, CRC_PLAYER_ENTITY) && game::rd32(e + ENTITY_AI_FIELD, v) && v == ai &&
           game::rd32(e + 0x58, data) && data >= 0x10000 &&
           game::rd32(data + 0x6C, weapons) && weapons;
}

// scan_only: skip the cache and the mission's state, exercise the memory scan (debug)
inline uintptr_t player_entity(bool scan_only = false) {
    static uintptr_t cached = 0;
    uintptr_t bhv = game::resolve_bhv();
    uint32_t ai = 0;
    if (!bhv || !game::rd32(bhv + 0x10, ai) || !ai) return 0;
    if (!scan_only && cached && is_player_entity(cached, ai)) return cached;
    cached = 0;
    // a mission state that called the handler already names it
    if (uintptr_t st = scan_only ? 0 : g_give_this) {
        uint32_t e = 0;
        if (game::rd32(st + 0x0C, e) && is_player_entity(e, ai)) return cached = e;
    }
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0x10000;
    while (addr < 0xFFFF0000) {
        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) break;
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        size_t sz = mbi.RegionSize;
        DWORD prot = mbi.Protect & 0xFF;
        bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && sz >= 0x40 &&
                  (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
                   prot == PAGE_EXECUTE_READWRITE);
        if (ok) {
            __try {
                for (uintptr_t a = base + ENTITY_AI_FIELD; a + 4 <= base + sz; a += 4)
                    if (*(uint32_t*)a == ai && is_player_entity(a - ENTITY_AI_FIELD, ai))
                        return cached = a - ENTITY_AI_FIELD;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (!sz) break;
        addr = base + sz;
    }
    return 0;
}

// Gives an item through the game's own handler - for weapons, the only route that equips them.
// The handler reads nothing from `this` but +0x0C, so `this` is a stand-in carrying the player
// entity: a captured mission state could be freed by the time it is reused.
inline int give_now(uintptr_t slot, uint8_t flag, char* note, size_t cap, uint32_t mode) {
    if (!g_give_fn) {
        snprintf(note, cap, "give handler not found on this build");
        return CALL_FAILED;
    }
    uint32_t fake[16] = {};
    uintptr_t entity = player_entity();
    if (!entity) {
        snprintf(note, cap, "player entity not found");
        return NOT_IN_GAME;
    }
    fake[3] = (uint32_t)entity;
    uintptr_t self = (uintptr_t)fake;
    alignas(4) uint8_t ev[0x40] = {};
    *(uint32_t*)(ev + 0x08) = mode;     // 0 give, 1 take - the game disarms Ezio with mode 1
    *(uint32_t*)(ev + 0x0C) = 1;        // count
    *(uint32_t*)(ev + 0x10) = 0;        // no extra reward step
    *(uint32_t*)(ev + 0x14) = (uint32_t)slot;
    ev[0x18] = flag;
    InterlockedIncrement(&g_give_ours);
    int r = GRANTED;
    __try {
        ((GiveFn)g_give_fn)((void*)self, nullptr, ev);   // through the hook: our call is logged too
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r = CALL_FAILED;
    }
    InterlockedDecrement(&g_give_ours);
    snprintf(note, cap, "give handler, player entity %08X, flag=%u%s", (unsigned)entity, flag,
             r == CALL_FAILED ? ", FAULTED" : "");
    return r;
}

// --- running inside the game's frame -------------------------------------------
// The engine routines allocate and touch lists the game also walks, so they are run from the
// D3D9 EndScene hook (the overlay's), not from the client's worker thread. If no frame shows
// up in time (overlay off, game not rendering) the job is run where it was asked rather than
// dropped - less safe, but an item must never be lost.

struct Job {
    volatile LONG state;      // 0 idle, 1 pending, 2 running, 3 done
    int op;                   // 0 grant, 1 drop, 2 give handler
    uintptr_t slot;
    uint8_t flag;
    int result;
    bool in_frame;
    char note[200];
};
inline Job g_job{};
inline volatile LONG g_frames = 0;

inline void run_job() {
    g_job.result = g_job.op == 0 ? grant_now(g_job.slot, g_job.note, sizeof(g_job.note))
                 : g_job.op == 1 ? drop_now(g_job.slot, g_job.note, sizeof(g_job.note))
                 : give_now(g_job.slot, g_job.flag, g_job.note, sizeof(g_job.note),
                            g_job.op == 3 ? 1u : 0u);
}

// Called once per frame by the overlay's EndScene hook.
inline void frame_tick() {
    InterlockedIncrement(&g_frames);
    if (g_job.state != 1) return;
    if (InterlockedCompareExchange(&g_job.state, 2, 1) != 1) return;
    g_job.in_frame = true;
    run_job();
    InterlockedExchange(&g_job.state, 3);
}

inline int dispatch(int op, uintptr_t slot, char* note, size_t cap, bool* in_frame = nullptr,
                    uint8_t flag = 0) {
    g_job.op = op;
    g_job.slot = slot;
    g_job.flag = flag;
    g_job.result = CALL_FAILED;
    g_job.in_frame = false;
    g_job.note[0] = 0;
    LONG f0 = g_frames;
    InterlockedExchange(&g_job.state, 1);
    DWORD t0 = GetTickCount();
    while (g_job.state != 3) {
        DWORD dt = GetTickCount() - t0;
        // No frame in 400 ms (game minimised / alt-tabbed, or overlay off), or the job still
        // not picked up after 2 s.
        if ((dt > 400 && g_frames == f0) || dt > 2000) {
            // Once the frame hook has proven it works, NEVER run the job off the frame: the
            // give handler faulted there (seen live while the player was alt-tabbed), and the
            // client then counted the item as applied - a weapon received while the player
            // looked at the Archipelago client was lost for good. Hand it back instead; the
            // caller retries and the job runs in the next frame once the game renders again.
            // Without any frame ever (overlay off), running here is the only option left.
            if (g_frames > 0 && InterlockedCompareExchange(&g_job.state, 0, 1) == 1) {
                if (note && cap) snprintf(note, cap, "game not rendering, will retry");
                if (in_frame) *in_frame = false;
                return NOT_IN_GAME;
            }
            if (InterlockedCompareExchange(&g_job.state, 2, 1) == 1) {
                run_job();
                InterlockedExchange(&g_job.state, 3);
            }
            while (g_job.state != 3) Sleep(1);   // the frame hook took it at the last moment
            break;
        }
        Sleep(2);
    }
    if (note && cap) { strncpy(note, g_job.note, cap - 1); note[cap - 1] = 0; }
    if (in_frame) *in_frame = g_job.in_frame;
    int r = g_job.result;
    InterlockedExchange(&g_job.state, 0);
    return r;
}

// --- what the client calls (worker thread) -------------------------------------

inline int grant(uint32_t forge, char* note, size_t cap, bool* in_frame = nullptr) {
    if (note && cap) note[0] = 0;
    if (!logical()) return NOT_IN_GAME;
    uintptr_t slot = find_slot(forge);       // the heap walk stays off the game's frame
    if (!slot) return UNKNOWN_ITEM;
    return dispatch(0, slot, note, cap, in_frame);
}

// Raw call of the game's give handler for any item (test helper; flag is the event's +0x18).
inline int give(uint32_t forge, uint8_t flag, char* note, size_t cap, bool* in_frame = nullptr) {
    if (note && cap) note[0] = 0;
    if (!logical()) return NOT_IN_GAME;
    uintptr_t slot = find_slot(forge);
    if (!slot) return UNKNOWN_ITEM;
    return dispatch(2, slot, note, cap, in_frame, flag);
}

// The game's own way of taking an item from Ezio (give handler, mode 1): that is how a mission
// disarms him at load. Removing the item from its container alone does NOT do it - verified
// live: the chest piece left the list but stayed on the model and in the health bar.
inline int take(uint32_t forge, uint8_t flag, char* note, size_t cap, bool* in_frame = nullptr) {
    if (note && cap) note[0] = 0;
    if (!logical()) return NOT_IN_GAME;
    uintptr_t slot = find_slot(forge);
    if (!slot) return UNKNOWN_ITEM;
    return dispatch(3, slot, note, cap, in_frame, flag);
}

// Forge ids of what the player owns in one category. -1 when there is no inventory yet.
// Read-only, safe from the worker thread.
inline int owned_forges(uint32_t category, uint32_t* out, int cap) {
    if (!logical()) return -1;
    uintptr_t c = container_for(category);
    if (!c) return 0;
    Item its[96];
    int n = items(c, its, 96), k = 0;
    for (int i = 0; i < n && k < cap; i++) out[k++] = its[i].forge;
    return k;
}

// Gives an already-received item back if the inventory no longer has it - the game was
// closed before an autosave wrote it, so the save loaded without it (seen live: a sword
// given at 12:52, game closed at 12:56, gone at the next launch). grant() already refuses
// anything owned and any armour that would be a downgrade, so this only fills real holes.
// Two exceptions:
//  - dyes are never given back: the Colors container holds only the dye being worn, so
//    every dye received earlier reads as missing, and giving it back would undo the colour
//    the player picked at the tailor;
//  - weapons only while Ezio holds at least one. An empty weapon list is how a mission's
//    scripted disarm looks at load, and re-arming him there would break that section.
inline int reconcile(uint32_t forge, char* note, size_t cap) {
    if (note && cap) note[0] = 0;
    if (!logical()) return NOT_IN_GAME;
    uintptr_t slot = find_slot(forge);
    if (!slot) return UNKNOWN_ITEM;
    Item it{};
    if (!read_item(slot, it)) return UNKNOWN_ITEM;
    if (it.category == CAT_COLORS) return ALREADY_OWNED;
    if (it.category == CAT_WEAPON) {
        uint32_t w[4];
        if (owned_forges(CAT_WEAPON, w, 4) <= 0) return NOT_IN_GAME;   // disarmed: later
    }
    return dispatch(0, slot, note, cap);
}

inline int drop(uint32_t forge, char* note, size_t cap, bool* in_frame = nullptr) {
    if (note && cap) note[0] = 0;
    if (!logical()) return NOT_IN_GAME;
    uintptr_t slot = find_slot(forge);
    if (!slot) return UNKNOWN_ITEM;
    return dispatch(1, slot, note, cap, in_frame);
}

} // namespace ac2ap::inv
