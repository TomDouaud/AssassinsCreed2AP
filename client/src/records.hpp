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
// FIX BUG-005 (live capture): viewpoints have their OWN type, never shared.
// The old 0xC69075ABBF298A20 is actually the generic "item ACQUIRED" record (feathers,
// codex, equipment...) - counting on it mixed feathers and viewpoints.
constexpr uint64_t REC_VIEWPOINT = 0xB3195056E38B5102ull; // synchronized viewpoint
constexpr uint64_t REC_ITEM_ACQ  = 0xC69075ABBF298A20ull; // acquired item (feather/codex/equip)
constexpr uint64_t REC_LOOT      = 0x00000000DA47DC47ull; // loot/chest
constexpr uint64_t REC_FEATHER_COUNTER = 0x000000005B6A6F41ull; // global feather counter

// Number of codex pages picked up: distinct ids from the "acquired item" record whose ID
// falls in the codex ranges (0x4658D3xx / 0x45B9E6xx - verified codex-only).
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
