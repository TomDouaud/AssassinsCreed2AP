// AC2AP - parser for the completion records in AC2 saves.
// Faithful port of tools/extract_records.py (semantics proven in the lab):
// flat scan of the whole buffer, node = [size:u32][size2:u32==size-8][magic:u32=0x11][type:u64][payload]
// check ID: u32 at +0x19 from the record start when byte +0x18 == 0x0B.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace ac2ap {

struct RecordKey {
    uint64_t type;
    uint32_t id;
    bool operator<(const RecordKey& o) const {
        return type != o.type ? type < o.type : id < o.id;
    }
};

// Multiset (type,id) -> occurrences, like the python Counter.
using RecordCounts = std::map<RecordKey, int>;

// "memory completed" record type (also used for the per-district collectible trackers).
constexpr uint64_t REC_MISSION_TYPE = 0x5FDACBA05FDACBA0ull;

// A record of that type is written as soon as the memory STARTS / becomes available, with its
// completion flag still clear, and the flag is only set once the memory is really finished.
// Reporting the record the moment its id appears therefore fired the check at mission START,
// one memory ahead of the player (issue #4: "the check for Ace Up My Sleeve was sent right
// after Sequence 1 was completed").
// The flag is a u32 storing 0 or 100 - it is a boolean, not a percentage: AC2 has no partial
// synchronisation, and across the 27 lab saves the field only ever holds those two values
// (272 records at 0, 2242 at 100, nothing in between).
// Verified across the per-chapter saves - the same id goes 0 -> 100:
//   Friend of the Family 0 (Seq1 M8) -> 100 (Seq2 M1); Fitting In 0 (Seq2 M1) -> 100 (Seq2 M4);
//   Arrivederci 0 (Seq2 M4) -> 100 (Seq3 M3). The per-district collectible trackers behave the
//   same way: their flag is only set once that district is fully looted.
// Layout: nested record marker 0x0B at +0x2D, completion flag as u32 at +0x2E.
inline bool record_completed(const uint8_t* rec, size_t avail) {
    if (avail < 0x32) return true;        // too short to tell -> keep it, never drop a check
    if (rec[0x2D] != 0x0B) return true;   // unexpected layout -> keep it
    uint32_t done;
    std::memcpy(&done, rec + 0x2E, 4);
    return done != 0;
}

inline RecordCounts parse_records(const uint8_t* buf, size_t len) {
    RecordCounts out;
    if (len < 20) return out;
    for (size_t i = 0; i + 20 <= len; i++) {
        uint32_t size, size2, magic;
        std::memcpy(&size, buf + i, 4);
        std::memcpy(&size2, buf + i + 4, 4);
        std::memcpy(&magic, buf + i + 8, 4);
        if (magic != 0x11 || size < 20 || size - 8 != size2) continue;
        if (i + 8 + (size_t)size > len) continue;
        RecordKey k{};
        std::memcpy(&k.type, buf + i + 12, 8);
        k.id = 0;
        if (i + 0x1D <= len && buf[i + 0x18] == 0x0B)
            std::memcpy(&k.id, buf + i + 0x19, 4);
        // Only completion is a check. Restricted to the mission type: that is the layout we
        // verified, and other record types (loot, item acquired) mean "done" on sight.
        if (k.type == REC_MISSION_TYPE && !record_completed(buf + i, len - i)) continue;
        out[k]++;
    }
    return out;
}

// Records that appeared (count increased) between before and after.
inline std::vector<RecordKey> new_records(const RecordCounts& before, const RecordCounts& after) {
    std::vector<RecordKey> out;
    for (const auto& [k, n] : after) {
        auto it = before.find(k);
        int prev = it == before.end() ? 0 : it->second;
        for (int j = prev; j < n; j++) out.push_back(k);
    }
    return out;
}

// BUG-001: counts fluctuate when the game rewrites its containers (the same
// (type,id) can appear 10x in one autosave). One check = one unique location,
// so dedup is done by SET of already-seen (type,id), never by count.
using RecordSet = std::set<RecordKey>;

inline std::vector<RecordKey> unseen_keys(const RecordSet& seen, const RecordCounts& current) {
    std::vector<RecordKey> out;
    for (const auto& [k, n] : current)
        if (!seen.count(k)) out.push_back(k);
    return out;
}

// Known record types (docs/re/mission-records.md)
constexpr uint64_t REC_MISSION   = 0x5FDACBA05FDACBA0ull; // memory completed
// A record type is a PAIR of CRC32s of the engine's own class names, which is how these
// constants can be read back: crc32("Mission") == 0x5FDACBA0 (hence the doubled halves
// above), crc32("MissionStep") == 0xB3195056, crc32("Step") == 0xE38B5102,
// crc32("InventoryItemSettings") == 0xC69075AB, crc32("ObjectID") == 0x5B6A6F41.
// So this type is "MissionStep / Step" - a mission STEP, not a viewpoint. That is why a
// finished save holds 96 of them when the game only has 73 viewpoints, and why some
// disappear between saves. Counting them cannot yield viewpoints (BUG-005 stays open);
// the name is kept only because the viewpoints option, which is off and documented as
// not working, still counts on it.
constexpr uint64_t REC_VIEWPOINT = 0xB3195056E38B5102ull; // really: MissionStep / Step
constexpr uint64_t REC_ITEM_ACQ  = 0xC69075ABBF298A20ull; // acquired item (feather/codex/equip)
constexpr uint64_t REC_LOOT      = 0x00000000DA47DC47ull; // loot/chest
constexpr uint64_t REC_FEATHER_COUNTER = 0x000000005B6A6F41ull; // global feather counter

// Number of codex pages picked up: distinct ids from the "acquired item" record whose ID
// falls in the codex ranges (0x4658D3xx / 0x45B9E6xx - verified codex-only).
// --- Codex pages -----------------------------------------------------------------------------
// Codex state lives in the save as a serialised list of boolean arrays, each written as
// <count:u32><count bytes of 0/1>. The codex one holds 30 entries - one per page - and is the
// array immediately followed by a 36-entry array, which is what tells it apart from the other
// 30-entry array in the file (verified on six saves: it reads 0 at Sequence 1, 30 on a completed
// game, and matched the player's in-game "23 decoded" exactly).
//
// This replaces counting codex records in the acquired-items list. That count was NOT the pages
// you own: those records are transient (a page sits there until it is decoded, then goes away),
// so the count went up AND down - a completed save, which requires all 30 pages, contains none of
// them. Since checks were only sent when the count passed its previous maximum, codex checks went
// silent for good after the first few pages (reported by a player: "the ones in Forli, Toscana
// and Venezia don't send anything").
constexpr int CODEX_PAGES = 30;

// Fills flags[30] with the per-page state. false if the array could not be located.
inline bool read_codex_flags(const uint8_t* buf, size_t len, uint8_t flags[CODEX_PAGES]) {
    if (len < 4 + CODEX_PAGES + 4) return false;
    for (size_t i = 0; i + 4 + CODEX_PAGES + 4 <= len; i++) {
        uint32_t n;
        std::memcpy(&n, buf + i, 4);
        if (n != CODEX_PAGES) continue;
        const uint8_t* a = buf + i + 4;
        bool boolish = true;
        for (int k = 0; k < CODEX_PAGES; k++)
            if (a[k] > 1) { boolish = false; break; }
        if (!boolish) continue;
        uint32_t next;
        std::memcpy(&next, a + CODEX_PAGES, 4);
        if (next != 36) continue;              // the array that follows identifies the codex one
        std::memcpy(flags, a, CODEX_PAGES);
        return true;
    }
    return false;
}

inline int count_codex(const RecordCounts& counts) {
    int n = 0;
    for (const auto& [k, cnt] : counts)
        if (k.type == REC_ITEM_ACQ &&
            ((k.id >> 8) == 0x4658D3u || (k.id >> 8) == 0x45B9E6u))
            n++;
    return n;
}

// Number of DISTINCT ids of a given type in the save. For "by count" collectibles
// (viewpoints): each entity has a unique id, so the number of distinct ids =
// number collected. Robust against BUG-001 (occurrences fluctuate, the set of ids
// does not).
inline int count_distinct_of_type(const RecordCounts& counts, uint64_t type) {
    int n = 0;
    for (const auto& [k, cnt] : counts)
        if (k.type == type) n++;
    return n;
}

// Reads a u32 counter stored in the payload of a record of a given type, at
// offset payload+off. For feathers (record REC_FEATHER_COUNTER, +0x52).
// Direct scan of the buffer (the record is unique). 0 if absent.
inline uint32_t read_type_counter(const uint8_t* buf, size_t len, uint64_t type, size_t off) {
    for (size_t i = 0; i + 20 <= len; i++) {
        uint32_t size, size2, magic;
        std::memcpy(&size, buf + i, 4);
        std::memcpy(&size2, buf + i + 4, 4);
        std::memcpy(&magic, buf + i + 8, 4);
        if (magic != 0x11 || size < 20 || size - 8 != size2) continue;
        uint64_t t;
        std::memcpy(&t, buf + i + 12, 8);
        if (t != type) continue;
        if (i + off + 4 > len) return 0;
        uint32_t v;
        std::memcpy(&v, buf + i + off, 4);
        return v;
    }
    return 0;
}

} // namespace ac2ap
