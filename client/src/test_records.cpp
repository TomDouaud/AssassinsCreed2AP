// Console test for the parser (out of game): compares two saves and prints the new records.
// Expected ground truth (lab):
//   T0->T1: +1 MISSION id=0E553CCF (Benvenuto)
//   T1->T2: +1 MISSION id=1594A6F1 (Ca va laisser des traces)
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

#include "records.hpp"

static std::vector<uint8_t> slurp(const char* p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    if (argc == 3) {
        auto a = slurp(argv[1]), b = slurp(argv[2]);
        auto ca = ac2ap::parse_records(a.data(), a.size());
        auto cb = ac2ap::parse_records(b.data(), b.size());
        for (const auto& k : ac2ap::new_records(ca, cb))
            printf("+ type=%016llX id=%08X\n", (unsigned long long)k.type, k.id);
        return 0;
    }
    // auto-test on the lab saves (set AC2AP_LAB to the labo-diff dir, or pass two saves as args)
    const char* lab = std::getenv("AC2AP_LAB");
    if (!lab) { printf("usage: %s <save_a> <save_b>   (or set AC2AP_LAB)\n", argv[0]); return 2; }
    auto t0 = slurp((std::string(lab) + "T0.save").c_str());
    auto t1 = slurp((std::string(lab) + "T1-benvenuto-fini.save").c_str());
    auto t2 = slurp((std::string(lab) + "T2-traces-fini.save").c_str());
    assert(!t0.empty() && !t1.empty() && !t2.empty());
    auto c0 = ac2ap::parse_records(t0.data(), t0.size());
    auto c1 = ac2ap::parse_records(t1.data(), t1.size());
    auto c2 = ac2ap::parse_records(t2.data(), t2.size());

    bool benvenuto = false;
    for (const auto& k : ac2ap::new_records(c0, c1))
        if (k.type == ac2ap::REC_MISSION && k.id == 0x0E553CCF) benvenuto = true;
    assert(benvenuto && "Benvenuto record expected in T0->T1");

    auto d12 = ac2ap::new_records(c1, c2);
    bool traces = false;
    for (const auto& k : d12)
        if (k.type == ac2ap::REC_MISSION && k.id == 0x1594A6F1) traces = true;
    assert(traces && "Traces record expected in T1->T2");

    // fix BUG-001: dedup by set - an already-seen (type,id) never comes back,
    // even if its count fluctuates in the save
    ac2ap::RecordSet seen;
    for (const auto& [k, n] : c1) seen.insert(k);
    auto fresh = ac2ap::unseen_keys(seen, c2);
    bool traces2 = false;
    for (const auto& k : fresh) {
        assert(!seen.count(k) && "dedup violated");
        if (k.type == ac2ap::REC_MISSION && k.id == 0x1594A6F1) traces2 = true;
    }
    assert(traces2 && "Traces must show up in unseen_keys");
    for (const auto& k : fresh) seen.insert(k);
    assert(ac2ap::unseen_keys(seen, c2).empty() && "re-scan = zero new");

    printf("OK: parser + dedup match the lab ground truth\n");
    return 0;
}
