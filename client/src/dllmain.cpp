// AC2AP.asi - Archipelago client for Assassin's Creed II (v0)
// - watches 1.save, parses the completion records (records.hpp)
// - maps check ID -> AP location via AC2AP_map.txt
// - sends the LocationChecks to the server via apclientpp
// Zero in-game hooks for v0: purely file-based detection (see docs/re/mission-records.md).
#include <windows.h>

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "records.hpp"
#include "game.hpp"
#include "overlay.hpp"

#ifdef AC2AP_WITH_AP
#include <apclient.hpp>
#include <apuuid.hpp>
#endif

// apworld item IDs (apworld/ac2/Items.py)
namespace item_ids {
constexpr int64_t PROGRESSIVE_SEQUENCE = 20240012000;
constexpr int64_t CODEX_PAGE = 20240012001;
constexpr int64_t ASSASSIN_SEAL = 20240012002;
constexpr int64_t PROGRESSIVE_HIDDEN_BLADE = 20240012003;
constexpr int64_t FLORINS_100 = 20240012004;
constexpr int64_t FLORINS_500 = 20240012005;
constexpr int64_t FLORINS_1000 = 20240012006;
constexpr int64_t PROGRESSIVE_TEMPLAR_GRIP = 20240012007;  // -25% notoriety floor each
// Traps (reserved in Items.py on the apworld side; provisional ids)
constexpr int64_t TRAP_TEMPLAR_TAX = 20240012100;   // -25% florins
constexpr int64_t TRAP_BAD_MEDICINE = 20240012101;  // health -> 1
constexpr int64_t TRAP_DEATH = 20240012102;         // kills Ezio
constexpr int64_t TRAP_WANTED = 20240012103;        // notoriety -> max (guards hunt you)
}

namespace {

std::string g_dir;          // folder of the .asi
bool g_hmon = false;        // dev: health/death monitor (toggled by the `hmon` cmd)
FILE* g_log = nullptr;
std::string g_save_path;
std::string g_server, g_slot, g_password;
bool g_ap_enabled = false;

void logf(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fprintf(g_log, "\n");
    fflush(g_log);
}

std::string module_dir(HMODULE mod) {
    char buf[MAX_PATH];
    GetModuleFileNameA(mod, buf, MAX_PATH);
    std::string p(buf);
    return p.substr(0, p.find_last_of("\\/"));
}

std::string ini_get(const std::string& ini, const char* key, const char* def) {
    char buf[512];
    GetPrivateProfileStringA("ac2ap", key, def, buf, sizeof(buf), ini.c_str());
    return buf;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    // read/write share: the game may be writing at the same time, we'll retry next tick
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr);
    out.resize(size);
    DWORD got = 0;
    BOOL ok = ReadFile(h, out.data(), size, &got, nullptr);
    CloseHandle(h);
    return ok && got == size;
}

// Finds a slot-1 save inside a Ubisoft "<account>\4" folder. The exact name varies between
// installs (1.save vs save1), so try the common names then glob for any save file (skip the
// 999 profile). Returns "" if none.
std::string find_save_file(const std::string& dir4) {
    const char* names[] = {"1.save", "save1", "2.save", "save2", "3.save", "save3"};
    for (auto n : names) {
        std::string c = dir4 + "\\" + n;
        if (GetFileAttributesA(c.c_str()) != INVALID_FILE_ATTRIBUTES) return c;
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir4 + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string nm = fd.cFileName;
            if (nm.find("999") != std::string::npos) continue;   // 999 = profile/options
            bool save = (nm.size() >= 5 && nm.compare(nm.size() - 5, 5, ".save") == 0)
                        || nm.compare(0, 4, "save") == 0;
            if (save) { std::string c = dir4 + "\\" + nm; FindClose(h); return c; }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return "";
}

// Auto-detect the Ubisoft Connect save. The launcher stores saves under:
//   <base>\savegames\<accountId>\4\<slot>.save          (4 = AC2 game id)
// with <base> = Program Files (x86)\Ubisoft\Ubisoft Game Launcher (typical) or %LOCALAPPDATA%.
// The <accountId> folder is per account, so we enumerate it. Returns "" if none is found.
std::string find_ubisoft_save() {
    std::vector<std::string> bases;
    if (const char* pf = std::getenv("ProgramFiles(x86)"))
        bases.push_back(std::string(pf) + "\\Ubisoft\\Ubisoft Game Launcher\\savegames");
    if (const char* pf = std::getenv("ProgramFiles"))
        bases.push_back(std::string(pf) + "\\Ubisoft\\Ubisoft Game Launcher\\savegames");
    if (const char* loc = std::getenv("LOCALAPPDATA"))
        bases.push_back(std::string(loc) + "\\Ubisoft Game Launcher\\savegames");
    for (const auto& base : bases) {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((base + "\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        std::string found;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
            found = find_save_file(base + "\\" + fd.cFileName + "\\4");
            if (!found.empty()) break;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        if (!found.empty()) return found;
    }
    return "";
}

// --- persistent state: set of (type,id) already reported (fix BUG-001) -------
std::string seen_path() { return g_dir + "\\AC2AP_seen.txt"; }

ac2ap::RecordSet load_seen() {
    ac2ap::RecordSet s;
    std::ifstream f(seen_path());
    uint64_t t;
    uint32_t id;
    while (f >> std::hex >> t >> id)
        s.insert({t, id});
    return s;
}

void save_seen(const ac2ap::RecordSet& s) {
    std::ofstream f(seen_path(), std::ios::trunc);
    for (const auto& k : s)
        f << std::hex << k.type << " " << k.id << "\n";
}

// --- persisted send queue: checks detected but not yet accepted by the server
//     (fix BUG-002: survives closing the game while the server is down) --------
std::string pending_path() { return g_dir + "\\AC2AP_pending.txt"; }

std::vector<int64_t> load_pending() {
    std::vector<int64_t> v;
    std::ifstream f(pending_path());
    int64_t x;
    while (f >> x) v.push_back(x);
    return v;
}

void save_pending(const std::vector<int64_t>& v) {
    std::ofstream f(pending_path(), std::ios::trunc);
    for (auto x : v) f << x << "\n";
}

// --- received items: index of the last applied one (persisted, anti-replay) -
std::string items_path() { return g_dir + "\\AC2AP_items.txt"; }

int load_applied_index() {
    std::ifstream f(items_path());
    int idx = -1;
    f >> idx;
    return idx;
}

void save_applied_index(int idx) {
    std::ofstream f(items_path(), std::ios::trunc);
    f << idx << "\n";
}

// Applies an item in-game. false = not possible right now (loading...), retry.
bool apply_item(int64_t item) {
    switch (item) {
    case item_ids::FLORINS_100:  return ac2ap::game::add_money(100);
    case item_ids::FLORINS_500:  return ac2ap::game::add_money(500);
    case item_ids::FLORINS_1000: return ac2ap::game::add_money(1000);
    case item_ids::TRAP_TEMPLAR_TAX:  return ac2ap::game::tax_money(25);
    case item_ids::TRAP_BAD_MEDICINE: return ac2ap::game::cripple_health();
    case item_ids::TRAP_DEATH:        return ac2ap::game::kill_player();
    case item_ids::TRAP_WANTED:       return ac2ap::game::set_notoriety(1.0f);
    case item_ids::PROGRESSIVE_TEMPLAR_GRIP:
        return true;  // passive: the notoriety floor is recomputed from grip_indexes each tick
    default:
        // logical progression (Sequence/Codex/Seal/Blade): no in-game effect in v1,
        // the gating happens on the AP logic side. Counted as applied.
        logf("item %lld: no in-game effect (v1), marked applied", (long long)item);
        return true;
    }
}

// --- game ID -> AP location mapping: AC2AP_map.txt "IDHEX AP_LOCATION_ID" ----
std::map<uint32_t, int64_t> load_map() {
    std::map<uint32_t, int64_t> m;
    std::ifstream f(g_dir + "\\AC2AP_map.txt");
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        uint32_t gid;
        int64_t loc;
        if (ss >> std::hex >> gid >> std::dec >> loc) m[gid] = loc;
    }
    return m;
}

// --- victory: "!GOAL <game_id_hex>" = mission record whose presence means the seed is won ----
uint32_t load_goal_id() {
    std::ifstream f(g_dir + "\\AC2AP_map.txt");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("!GOAL", 0) != 0) continue;
        std::istringstream ss(line.substr(5));
        uint32_t id = 0;
        if (ss >> std::hex >> id) return id;
    }
    return 0;
}

// --- collectibles by PRESENCE: "~<forge_id_hex> <ap_id>" ---------------------
// A collectible ID appears in the save ONLY if it has been collected
// (proven: feathers-present == feathers record-based, identical). For "loot" collectibles
// (chests/statues) that have no individual 0x0B record (fragile packed list, magic 0x0116),
// we detect by simple PRESENCE of the ID in the save buffer. Robust, no list parsing.
// Dedup via the usual seen set with a sentinel type.
constexpr uint64_t REC_PRESENCE = 0x50524553454E4345ull;  // "PRESENCE"

inline bool buf_has_u32(const std::vector<uint8_t>& buf, uint32_t id) {
    if (buf.size() < 4) return false;
    const uint8_t le[4] = {(uint8_t)id, (uint8_t)(id >> 8), (uint8_t)(id >> 16), (uint8_t)(id >> 24)};
    for (size_t i = 0; i + 4 <= buf.size(); i++)
        if (buf[i] == le[0] && buf[i + 1] == le[1] && buf[i + 2] == le[2] && buf[i + 3] == le[3])
            return true;
    return false;
}

std::map<uint32_t, int64_t> load_presence() {
    std::map<uint32_t, int64_t> m;
    std::ifstream f(g_dir + "\\AC2AP_map.txt");
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] != '~') continue;
        std::istringstream ss(line.substr(1));  // strip '~'
        uint32_t id;
        int64_t loc;
        if (ss >> std::hex >> id >> std::dec >> loc) m[id] = loc;
    }
    return m;
}

// --- "by count" collectibles: @CAT idx apid in AC2AP_map.txt -----------------
std::map<std::string, std::vector<int64_t>> load_counted() {
    std::map<std::string, std::vector<int64_t>> c;
    std::ifstream f(g_dir + "\\AC2AP_map.txt");
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] != '@') continue;
        std::istringstream ss(line);
        std::string cat;
        int idx;
        int64_t apid;
        if (!(ss >> cat >> idx >> apid)) continue;
        cat = cat.substr(1);  // strip '@'
        auto& v = c[cat];
        if ((int)v.size() <= idx) v.resize(idx + 1, 0);
        v[idx] = apid;
    }
    return c;
}

// max index already sent per category (persistent, anti-replay)
std::string counted_state_path() { return g_dir + "\\AC2AP_counted.txt"; }

std::map<std::string, int> load_counted_state() {
    std::map<std::string, int> m;
    std::ifstream f(counted_state_path());
    std::string cat;
    int n;
    while (f >> cat >> n) m[cat] = n;
    return m;
}

void save_counted_state(const std::map<std::string, int>& m) {
    std::ofstream f(counted_state_path(), std::ios::trunc);
    for (const auto& [k, v] : m) f << k << " " << v << "\n";
}

const char* type_name(uint64_t t) {
    if (t == ac2ap::REC_MISSION) return "MISSION";
    if (t == ac2ap::REC_VIEWPOINT) return "VIEWPOINT";
    if (t == ac2ap::REC_LOOT) return "LOOT";
    return "?";
}

// --- F9 breakdown: AP location id -> category. Ranges from worlds/ac2/Locations.py -----------
constexpr int64_t AP_BASE = 20240002000LL;
enum { CAT_MISSION, CAT_VIEWPOINT, CAT_FEATHER, CAT_GLYPH, CAT_SECONDARY,
       CAT_TOMB, CAT_SHOP, CAT_VILLA, CAT_STATUE, CAT_CODEX, CAT_CHEST, CAT_N };
static const char* CAT_NAMES[CAT_N] = {
    "Missions", "Viewpoints", "Feathers", "Glyphs", "Secondary",
    "Tombs", "Shop", "Villa", "Statues", "Codex", "Chests" };
inline int cat_of(int64_t apid) {
    int64_t off = apid - AP_BASE;
    if (off < 0)     return -1;
    if (off < 1000)  return CAT_MISSION;
    if (off < 2000)  return CAT_VIEWPOINT;
    if (off < 3000)  return CAT_FEATHER;
    if (off < 4000)  return CAT_GLYPH;
    if (off < 5000)  return CAT_SECONDARY;
    if (off < 6000)  return CAT_TOMB;
    if (off < 7000)  return CAT_SHOP;
    if (off < 8000)  return CAT_VILLA;
    if (off < 8300)  return CAT_STATUE;    // 8200 statues (8000/8100 district bundles unused)
    if (off < 8400)  return CAT_CODEX;
    if (off < 9000)  return CAT_SECONDARY; // 8400 = DLC liberations, grouped with secondary
    return CAT_CHEST;
}

DWORD WINAPI worker(LPVOID) {
    std::string ini = g_dir + "\\AC2AP.ini";
    // Save to watch. Priority: explicit save_path in the ini > Ubisoft Connect auto-detect >
    // Skidrow default (%LOCALAPPDATA%\storage\SKIDROW\4\1.save).
    bool save_from_ini = false;
    g_save_path = ini_get(ini, "save_path", "");
    if (!g_save_path.empty()) save_from_ini = true;
    if (g_save_path.empty()) g_save_path = find_ubisoft_save();
    if (g_save_path.empty()) {
        const char* local = std::getenv("LOCALAPPDATA");
        g_save_path = std::string(local ? local : "") + "\\storage\\SKIDROW\\4\\1.save";
    }
    // Save-found feedback: the file must exist for detection to work. Toast it (queued until the
    // overlay is up) so the player knows immediately whether their save was located.
    bool save_found = GetFileAttributesA(g_save_path.c_str()) != INVALID_FILE_ATTRIBUTES;
    logf("save path: %s  [%s, %s]", g_save_path.c_str(),
         save_from_ini ? "from ini" : "auto-detected", save_found ? "FOUND" : "NOT FOUND");
    if (save_found)
        ac2ap::overlay::toast("Save found", IM_COL32(140, 230, 140, 255), 5000);
    else
        ac2ap::overlay::toast("SAVE NOT FOUND - set save_path in AC2AP.ini",
                              IM_COL32(240, 120, 120, 255), 12000);
    g_server = ini_get(ini, "server", "");
    g_slot = ini_get(ini, "slot", "");
    g_password = ini_get(ini, "password", "");
    g_ap_enabled = !g_server.empty() && !g_slot.empty();
    ac2ap::game::g_health_hook_enabled = ini_get(ini, "enable_health_hook", "0") == "1";

    logf("AC2AP v0 started. save=%s server=%s slot=%s ap=%d",
         g_save_path.c_str(), g_server.c_str(), g_slot.c_str(), (int)g_ap_enabled);

    // In-game overlay (D3D9 + ImGui): opt-in, off by default. Installed lazily in the loop
    // (the game's D3D device / fullscreen state may not be ready at startup). Failure = off.
    bool overlay_on = ini_get(ini, "enable_overlay", "0") == "1";
    bool overlay_installed = false;
    int overlay_tries = 0;
    if (overlay_on) ac2ap::overlay::set_defaults(g_server, g_slot, g_password);  // prefill the menu
    // Pre-fill the in-game connection form with the ini values.
    strncpy(ac2ap::overlay::g_conn.server, g_server.c_str(), sizeof(ac2ap::overlay::g_conn.server) - 1);
    strncpy(ac2ap::overlay::g_conn.slot, g_slot.c_str(), sizeof(ac2ap::overlay::g_conn.slot) - 1);
    strncpy(ac2ap::overlay::g_conn.password, g_password.c_str(), sizeof(ac2ap::overlay::g_conn.password) - 1);

    auto seen = load_seen();
    auto id_map = load_map();
    auto presence = load_presence();
    auto counted = load_counted();
    auto counted_state = load_counted_state();
    uint32_t goal_id = load_goal_id();               // 0 = no goal line in the map
    bool goal_sent = false;                          // report ClientStatus::GOAL once per session
    logf("state loaded: %zu (type,id) known, map: %zu missions, %zu counted categories, goal=%08X",
         seen.size(), id_map.size(), counted.size(), goal_id);

    // Per-category totals/done for the F9 breakdown. Filled on connect from the server's location
    // set for our slot (missing + checked) so it reflects the YAML (only enabled categories show).
    int cat_total[CAT_N] = {0}, cat_done[CAT_N] = {0};

    int stat_checks = 0, stat_items = 0;             // overlay status-line counters (this session)
    std::vector<int64_t> pending = load_pending();   // checks to send to the server
    if (!pending.empty()) logf("%zu checks pending send (previous session)", pending.size());
    std::vector<std::pair<int, int64_t>> item_queue;  // received items to apply (index, item)
    int applied_index = load_applied_index();
    bool first_pass = true;
    bool resync_pending = false;   // set on connect: re-send every check already done in the save
    logf("items already applied: index <= %d", applied_index);

#ifdef AC2AP_WITH_AP
    std::unique_ptr<APClient> ap;
    bool ap_authenticated = false;
    bool death_link = false;         // enabled via slot_data
    bool death_pending = false;      // a remote death to apply in-game
    bool prev_desync = false;        // "armed": Ezio is at 0 HP (one hit from a real desync/death)
    ULONGLONG dl_applied_at = 0;     // when we last applied a received death (echo suppression)
    // Templar Grip (reverse notoriety): floor = start - 25% per grip item received.
    // Grip items are counted by INDEX in the items_received handler (index-set = robust
    // against resync replays), not in apply_item (skipped indexes would be lost on restart).
    bool grip_enabled = false;
    float grip_start = 0.0f;         // starting floor 0..1 (slot_data, percent/100)
    std::set<int> grip_indexes;      // indexes of received Progressive Templar Grip items
    // (Re)connects the AP client with new params. Callable at startup AND from the in-game
    // connection menu (runtime reconnect). Updates the globals the handlers read.
    auto connect_ap = [&](const std::string& server, const std::string& slot, const std::string& pw) {
        g_server = server; g_slot = slot; g_password = pw;
        ap_authenticated = false;
        std::string uuid = ap_get_uuid(g_dir + "\\AC2AP_uuid.txt");
        ap.reset(new APClient(uuid, "Assassin's Creed II", g_server));
        logf("AP: connecting to %s slot '%s'", g_server.c_str(), g_slot.c_str());
        ac2ap::overlay::toast("Connecting to " + g_server + " ...", IM_COL32(230, 220, 130, 255), 3000);
        ap->set_room_info_handler([&]() {
            logf("AP: room info, connecting slot '%s'", g_slot.c_str());
            ap->ConnectSlot(g_slot, g_password, 0b111, {}, {0, 6, 7});
        });
        ap->set_slot_connected_handler([&](const nlohmann::json& slot_data) {
            logf("AP: slot connected!");
            ap_authenticated = true;
            resync_pending = true;   // re-send all past checks + make the counter retroactive
            ac2ap::overlay::toast("Connected as " + g_slot + "!",
                                  IM_COL32(120, 230, 120, 255), 6000);
            ac2ap::overlay::g_menu_open = false;   // close the connection menu on success
            if (slot_data.contains("death_link") && slot_data["death_link"].get<bool>()) {
                death_link = true;
                ap->ConnectUpdate(false, 0, true, {"DeathLink"});
                logf("AP: DeathLink ACTIVE");
            }
            if (slot_data.contains("templar_grip") && slot_data["templar_grip"].get<int>() != 0) {
                grip_enabled = true;
                int pct = slot_data.contains("templar_grip_start")
                          ? slot_data["templar_grip_start"].get<int>() : 100;
                grip_start = (float)pct / 100.0f;
                logf("AP: Templar Grip ACTIVE, starting floor %d%%", pct);
            }
        });
        ap->set_bounced_handler([&](const nlohmann::json& cmd) {
            if (!death_link) return;
            bool is_dl = cmd.contains("tags") && std::find(cmd["tags"].begin(),
                         cmd["tags"].end(), "DeathLink") != cmd["tags"].end();
            if (!is_dl) return;
            std::string src = cmd.contains("data") && cmd["data"].contains("source")
                              ? cmd["data"]["source"].get<std::string>() : "?";
            if (src == g_slot) return;   // not our own death
            death_pending = true;
            logf("AP: DeathLink received from '%s'", src.c_str());
        });
        ap->set_slot_refused_handler([&](const std::list<std::string>& errs) {
            std::string why;
            for (auto& e : errs) { logf("AP: connection refused: %s", e.c_str()); if (why.empty()) why = e; }
            ac2ap::overlay::toast("Connection refused: " + why, IM_COL32(240, 120, 120, 255), 6000);
        });
        ap->set_items_received_handler([&](const std::list<APClient::NetworkItem>& items) {
            for (auto& it : items) {
                logf("AP: item received index=%d item=%lld", it.index, (long long)it.item);
                item_queue.push_back({it.index, it.item});
                if (it.item == item_ids::PROGRESSIVE_TEMPLAR_GRIP)
                    grip_indexes.insert(it.index);
                if (it.index > applied_index) {          // don't toast resync replays
                    stat_items++;
                    std::string name = ap->get_item_name(it.item, ap->get_game());
                    std::string from = it.player > 0 ? ap->get_player_alias(it.player) : "";
                    // Category tag + color (ASCII text stands in for a per-item icon; the AP data
                    // package ships no item art, so classification is the meaningful marker).
                    const char* tag; ImU32 col;
                    if (it.flags & 4)      { tag = "[Trap] ";        col = IM_COL32(240, 120, 120, 255); }
                    else if (it.flags & 1) { tag = "[Progression] "; col = IM_COL32(255, 215, 90, 255); }
                    else if (it.flags & 2) { tag = "[Useful] ";      col = IM_COL32(150, 200, 255, 255); }
                    else                   { tag = "";               col = IM_COL32(160, 220, 160, 255); }
                    std::string msg = std::string(tag) + "Received: " + name;
                    if (!from.empty() && it.player != ap->get_player_number())
                        msg += "  (from " + from + ")";
                    ac2ap::overlay::toast(msg, col, 7000);
                }
            }
        });
        // "Sent X -> Player": the server broadcasts an ItemSend for every check. Toast the ones
        // we found that go to someone else (our own items are already toasted on receipt).
        ap->set_print_json_handler([&](const APClient::PrintJSONArgs& args) {
            if (args.type != "ItemSend" || !args.item || !args.receiving) return;
            int me = ap->get_player_number();
            if (args.item->player != me) return;          // only items WE found
            int recv = *args.receiving;
            if (recv == me) return;                        // our own -> handled by items_received
            std::string name = ap->get_item_name(args.item->item, ap->get_player_game(recv));
            std::string who = ap->get_player_alias(recv);
            ac2ap::overlay::toast("Sent " + name + " -> " + who, IM_COL32(180, 200, 255, 255), 5000);
        });
        ap->set_socket_disconnected_handler([&]() {
            logf("AP: socket disconnected");
            if (ap_authenticated)
                ac2ap::overlay::toast("Disconnected from server", IM_COL32(240, 160, 120, 255), 5000);
            ap_authenticated = false;
        });
        ap->set_socket_error_handler([&](const std::string& msg) {
            static ULONGLONG last = 0;   // debounce: the socket retries every second
            logf("AP: socket error: %s", msg.c_str());
            ULONGLONG now = GetTickCount64();
            if (now - last > 6000) {
                last = now;
                ac2ap::overlay::toast("Can't reach server: " + msg, IM_COL32(240, 120, 120, 255), 5000);
            }
        });
    };
    if (g_ap_enabled) connect_ap(g_server, g_slot, g_password);
#endif

    FILETIME last_mtime{};
    for (;;) {
        Sleep(250);

        // 0-) lazy overlay install: retry until the game's graphics are up (~30 tries = ~30s)
        if (overlay_on && !overlay_installed && overlay_tries < 30) {
            overlay_tries++;
            if (ac2ap::overlay::install(true)) {
                overlay_installed = true;
                logf("overlay: installed (D3D9 hook active, try %d)", overlay_tries);
                ac2ap::overlay::toast("AC2AP overlay ready", IM_COL32(120, 200, 255, 255), 4000);
            } else if (overlay_tries == 30) {
                logf("overlay: gave up after %d tries (last fail: %s)",
                     overlay_tries, ac2ap::overlay::g_fail);
            }
            Sleep(1000);   // space out the retries
        }

        // 0-bis) connection request from the in-game menu (INSERT -> form -> Connect)
#ifdef AC2AP_WITH_AP
        if (ac2ap::overlay::g_conn.requested) {
            ac2ap::overlay::g_conn.requested = false;
            connect_ap(ac2ap::overlay::g_conn.server, ac2ap::overlay::g_conn.slot,
                       ac2ap::overlay::g_conn.password);
            g_ap_enabled = true;
            WritePrivateProfileStringA("ac2ap", "server", g_server.c_str(), ini.c_str());
            WritePrivateProfileStringA("ac2ap", "slot", g_slot.c_str(), ini.c_str());
            WritePrivateProfileStringA("ac2ap", "password", g_password.c_str(), ini.c_str());
        }
        ac2ap::overlay::g_conn.connected = ap_authenticated;
        ac2ap::overlay::set_stats(stat_checks, stat_items);
        {   // push only the non-empty categories to the F9 breakdown
            const char* names[CAT_N]; int done[CAT_N], total[CAT_N]; int n = 0;
            for (int i = 0; i < CAT_N; i++)
                if (cat_total[i] > 0) { names[n] = CAT_NAMES[i]; done[n] = cat_done[i]; total[n] = cat_total[i]; n++; }
            ac2ap::overlay::set_cats(n, names, done, total);
        }
#endif

        // 0) health diagnostic: logs once the hook has captured the object
        {
            static bool logged = false;
            if (!logged) {
                uint32_t cur, mx;
                if (ac2ap::game::get_health(cur, mx)) {
                    logf("HEALTH captured by hook: cur=%u max=%u", cur, mx);
                    logged = true;
                }
            }
        }

        // 0a2) death-signal monitor (dev): with `hmon on`, log health cur/max/desync + a few
        //      bytes each time they change, to find the true natural-death signal (RE for #2).
        if (g_hmon) {
            uint32_t cur = 0, mx = 0;
            bool ok = ac2ap::game::get_health(cur, mx);
            uint8_t dz = ac2ap::game::is_desynced() ? 1 : 0;
            static uint32_t p_cur = 0xFFFFFFFF, p_mx = 0xFFFFFFFF; static uint8_t p_dz = 0xFF;
            if (ok && (cur != p_cur || mx != p_mx || dz != p_dz)) {
                logf("HMON cur=%u max=%u desync=%u", cur, mx, dz);
                p_cur = cur; p_mx = mx; p_dz = dz;
            }
        }

        // 0b) file-based test trigger: scripts/AC2AP_cmd.txt holds a command
        //     ("sethp N", "tax N", "money N"), executed then the file is deleted.
        //     Dev tool to test traps/DeathLink without rebuilding.
        {
            std::string cmdfile = g_dir + "\\AC2AP_cmd.txt";
            std::ifstream cf(cmdfile);
            if (cf) {
                std::string op, s1, s2, s3;
                cf >> op >> s1 >> s2 >> s3;   // args via strtoul base 0: decimal or 0xHEX
                cf.close();
                unsigned long arg  = s1.empty() ? 0 : strtoul(s1.c_str(), nullptr, 0);
                unsigned long arg2 = s2.empty() ? 0 : strtoul(s2.c_str(), nullptr, 0);
                unsigned long arg3 = s3.empty() ? 0 : strtoul(s3.c_str(), nullptr, 0);
                auto dump40 = [&](uintptr_t addr) {
                    uint8_t buf[0x40] = {0};
                    if (!addr || !ac2ap::game::safe_read(addr, buf, sizeof(buf))) return false;
                    char hex[0x40 * 3 + 1]; int p = 0;
                    for (int i = 0; i < 0x40; i++) p += sprintf(hex + p, "%02X ", buf[i]);
                    logf("DUMPA %p : %s", (void*)addr, hex);
                    return true;
                };
                bool done = true;
                if (op == "sethp") done = ac2ap::game::set_health((uint32_t)arg);
                else if (op == "tax") done = ac2ap::game::tax_money((int)arg);
                else if (op == "money") done = ac2ap::game::add_money((int)arg);
                else if (op == "kill") done = ac2ap::game::kill_player();
                else if (op == "haddr") {   // logs the health object -> HWBP target for the death-writer hunt
                    uintptr_t hp = ac2ap::game::resolve_health_addr();
                    logf("HADDR curHP@%08X (hwbp this addr)  obj@%08X",
                         (unsigned)hp, (unsigned)(hp ? hp - 0x58 : 0));
                    done = (hp != 0);
                }
                else if (op == "hmon") { g_hmon = (arg != 0) || s1.empty(); done = true;
                    logf("HMON %s", g_hmon ? "on" : "off"); }
                else if (op == "dlrecv") {   // simulate receiving a DeathLink (test buffering + focus-gating)
#ifdef AC2AP_WITH_AP
                    death_pending = true;
#endif
                    done = true;
                }
                else if (op == "noto") done = ac2ap::game::set_notoriety((float)arg / 100.0f);
                else if (op == "nnoto") done = ac2ap::game::call_set_notoriety((float)arg / 100.0f);  // native SetNotoriety
                else if (op == "smoke")  done = ac2ap::game::set_consumable(ac2ap::game::consumable::SMOKE, (uint32_t)arg);
                else if (op == "med")    done = ac2ap::game::set_consumable(ac2ap::game::consumable::MEDICINE, (uint32_t)arg);
                else if (op == "poison") done = ac2ap::game::set_consumable(ac2ap::game::consumable::POISON, (uint32_t)arg);
                else if (op == "bullet") done = ac2ap::game::set_consumable(ac2ap::game::consumable::BULLETS, (uint32_t)arg);
                else if (op == "poke")   // poke <slot> <offset> <val>: probes the unlock/capacity field
                    done = ac2ap::game::poke_consumable((uint32_t)arg, (uint32_t)arg2, (uint32_t)arg3);
                else if (op == "dump")   // read-only: dump 0x40 of the consumable container for slot 'arg'
                    done = dump40(ac2ap::game::resolve_consumable_container((uint32_t)arg));
                else if (op == "dumpa")  // read-only: dump 0x40 at an absolute address (0xHEX)
                    done = dump40((uintptr_t)arg);
                else if (op == "pokea")  // pokea <addr_hex> <val_hex>: absolute u32 write (weapon grant)
                    done = ac2ap::game::poke_abs((uintptr_t)arg, (uint32_t)arg2);
                else if (op == "sinit") {   // sinit <Mcand> [lo_hex] [hi_hex]: snapshot (bounded if lo/hi)
                    size_t cap = (size_t)(arg ? arg : 20) * 1000000u;
                    uintptr_t lo = arg2 ? (uintptr_t)arg2 : 0x10000;
                    uintptr_t hi = arg3 ? (uintptr_t)arg3 : 0xFFFF0000;
                    size_t n = ac2ap::game::scan_init(cap, lo, hi);
                    logf("SCAN init: %zu candidates (cap %zu, [%p,%p))", n, cap, (void*)lo, (void*)hi);
                }
                else if (op == "sfilter") { // sfilter <mode> [val]: 0=chg 1=unchg 2=inc 3=dec 4=eq(val)
                    size_t n = ac2ap::game::scan_filter((int)arg, (uint32_t)arg2);
                    const char* nm[] = {"changed","unchanged","increased","decreased","equals"};
                    logf("SCAN filter %s%s: %zu remaining",
                         (arg <= 4 ? nm[arg] : "?"), (arg == 4 ? "" : ""), n);
                }
                else if (op == "sdump") {   // logs the first 'arg' candidates (default 20)
                    size_t lim = arg ? arg : 20;
                    if (lim > ac2ap::game::g_scan_count) lim = ac2ap::game::g_scan_count;
                    char buf[64 * 22]; int p = 0;
                    for (size_t i = 0; i < lim && p < (int)sizeof(buf) - 24; i++)
                        p += sprintf(buf + p, "%08X=%u ", (unsigned)ac2ap::game::g_scan_addrs[i],
                                     ac2ap::game::g_scan_vals[i]);
                    logf("SCAN dump (%zu/%zu): %s", lim, ac2ap::game::g_scan_count, buf);
                }
                else if (op == "sfree") { ac2ap::game::scan_free(); logf("SCAN freed"); }
                else if (op == "modbase") {  // Animus Lock: module base + RVA of the active-mission slot
                    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
                    uint32_t mid = 0; ac2ap::game::rd32(0x02214B64, mid);
                    logf("MODBASE base=%p slot@0x02214B64 rva=0x%llX mission_active=0x%08X",
                         (void*)base, (unsigned long long)(0x02214B64 - base), mid);
                }
                else if (op == "find") {  // find <value_hex>: scan heap, log the addresses
                    uintptr_t hits[24];
                    int nh = ac2ap::game::scan_u32((uint32_t)arg, hits, 24);
                    char buf[24 * 11 + 1]; int p = 0;
                    for (int i = 0; i < nh; i++) p += sprintf(buf + p, "%08X ", (unsigned)hits[i]);
                    logf("FIND 0x%08lX : %d hits : %s", arg, nh, buf);
                }
                else if (op == "villa") {   // lists the villa buildings (building-ID + level)
                    uintptr_t v = ac2ap::game::resolve_villa_array();
                    if (!v) { logf("VILLA: array not found"); }
                    else {
                        char buf[64 * 20]; int p = 0;
                        for (int k = 0; k < 20 && ac2ap::game::villa_is_building(v + (uintptr_t)k * 0x18); k++) {
                            uint32_t lvl = 0, id = 0;
                            ac2ap::game::rd32(v + k * 0x18, lvl);
                            ac2ap::game::rd32(v + k * 0x18 + 0x14, id);
                            p += sprintf(buf + p, "%08X=L%u ", id, lvl);
                        }
                        logf("VILLA @%p : %s", (void*)v, buf);
                    }
                }
                else if (op == "hwbp") {   // hwbp <addr_hex>: hardware watchpoint on write (find writer EIPs)
                    done = ac2ap::game::hwbp_arm((uintptr_t)arg);
                    logf("HWBP arm @%08lX -> %s", arg, done ? "armed" : "failed");
                }
                else if (op == "hwbpdump") {  // logs distinct writer EIPs collected so far (+ RVA)
                    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
                    LONG n = ac2ap::game::g_hwbp_neips;
                    char buf[64 * 24]; int p = 0; buf[0] = 0;
                    for (LONG i = 0; i < n && p < (int)sizeof(buf) - 32; i++)
                        p += sprintf(buf + p, "%08X(rva %llX) ", (unsigned)ac2ap::game::g_hwbp_eips[i],
                                     (unsigned long long)(ac2ap::game::g_hwbp_eips[i] - base));
                    logf("HWBP writers (%ld): %s", n, buf);
                }
                else if (op == "hwbpoff") { ac2ap::game::hwbp_disarm(); logf("HWBP disarmed"); }
                else if (op == "notoobj") { // dumps the captured NotorietyManager (renegade-flag hunt)
                    uintptr_t o = (uintptr_t)ac2ap::game::g_noto_obj;
                    logf("NOTOOBJ = %p", (void*)o);
                    done = o ? dump40(o) : false;
                }
                else if (op == "villaall") {  // lists ALL villa-array copies (find the live one = the one that changes on renovate)
                    int nc = ac2ap::game::villa_list_all();
                    logf("VILLAALL: %d copies", nc);
                    for (int c = 0; c < nc; c++) {
                        auto& v = ac2ap::game::g_villa_copies[c];
                        char b[200]; int p = 0;
                        p += sprintf(b, "@%08X id0=%08X n=%d lvls=", (unsigned)v.base, v.first_id, v.n);
                        for (int k = 0; k < v.n && k < 32 && p < 180; k++) p += sprintf(b + p, "%u", v.lvl[k]);
                        logf("  %s", b);
                    }
                }
                else if (op == "bases") { // logs the inventory bases (weapon probe)
                    uintptr_t inv = 0, pdi = 0, m1 = 0;
                    uintptr_t bhv = ac2ap::game::resolve_inv_bases(&inv, &pdi, &m1);
                    if (!bhv) done = false;
                    else logf("BASES bhv=%p pInventory=%p PlayerDataItem=%p m1=%p",
                              (void*)bhv, (void*)inv, (void*)pdi, (void*)m1);
                }
                else logf("unknown cmd: %s", op.c_str());
                logf("cmd '%s %lu' -> %s", op.c_str(), arg, done ? "OK" : "out-of-game, retry");
                if (done) DeleteFileA(cmdfile.c_str());
            }
        }

        // 1) AP network
#ifdef AC2AP_WITH_AP
        if (ap) {
            ap->poll();
            if (ap_authenticated && !pending.empty()) {
                std::list<int64_t> l(pending.begin(), pending.end());
                ap->LocationChecks(l);
                logf("AP: %zu LocationChecks sent", pending.size());
                pending.clear();
                save_pending(pending);
            }
        }
#endif

        // 1a) DeathLink: death detection (send) + applying a received death
#ifdef AC2AP_WITH_AP
        if (ap && ap_authenticated && death_link) {
            static ULONGLONG last_emit = 0;      // shared debounce, hook + fallback (BUG-006)
            auto emit_death = [&](const char* how) {
                ULONGLONG now = GetTickCount64();
                if (now - last_emit <= 15000) return;
                if (dl_applied_at && now - dl_applied_at <= 10000) return;  // echo of an applied death
                last_emit = now;
                ap->Bounce({{"time", ap->get_server_time()},
                            {"cause", "Ezio has desynchronized"},
                            {"source", g_slot}}, {}, {}, {"DeathLink"});
                logf("AP: DeathLink emitted (Ezio died, %s)", how);
            };
            // PRIMARY: SetHealth hook (HWBP + Ghidra, 19/07). A real death calls
            // SetHealth(hp < 0) - catches every death, including instant falls the
            // 250 ms poll misses. Filtered to Ezio's health object in the detour.
            static bool sh_logged = false;
            if (ac2ap::game::install_sethealth_hook() && !sh_logged) {
                logf("SetHealth hook installed (death detection)");
                sh_logged = true;
            }
            if (InterlockedExchange(&ac2ap::game::g_death_hook_flag, 0)) {
                if (!death_pending) emit_death("SetHealth hook");
            }
            {   // DIAG: does SetHealth(hp<0) fire for OTHER entities (guard kills)?
                static LONG prev_any = 0;
                LONG a = ac2ap::game::g_death_hook_any;
                if (a != prev_any) { logf("DIAG SetHealth hp<0 call #%ld (any entity)", a); prev_any = a; }
            }
            // FALLBACK (kept in case the hook could not install - wrong build/prologue):
            // Ezio at low HP, then the health object gets INVALIDATED (max leaves the
            // plausible range) = real death, not the survivable 0-HP warning (issue #2).
            uint32_t cur = 0, max = 0;
            bool valid = ac2ap::game::get_health(cur, max) && max >= 1 && max <= 200;
            if (valid) {
                prev_desync = (cur <= 4);
            } else if (prev_desync && !death_pending) {
                prev_desync = false;
                emit_death("object invalidated");
            }
        } else {
            // death_link off/disconnected: drop any stale hook flag so it can't fire later
            InterlockedExchange(&ac2ap::game::g_death_hook_flag, 0);
        }
        // Apply a received/pending death only while the game is FOCUSED (outside the death_link
        // gate so the `dlrecv` debug cmd can drive it): unfocused = paused, and the desync byte
        // written then is wiped on resume, so the death was lost (friend's unfocused report).
        // Buffered in death_pending until the player refocuses.
        if (death_pending && ac2ap::game::is_game_focused()) {
            if (ac2ap::game::kill_player()) {
                logf("DeathLink applied: Ezio killed (focused)");
                death_pending = false;
                dl_applied_at = GetTickCount64(); // suppress the echo (the engine may route the
                                                  // applied death through SetHealth(hp<0) too)
                prev_desync = false;                // not "armed" -> don't emit our own applied death
            }
        }
#endif

        // 1a-bis) Templar Grip: clamp notoriety to the current floor. Vanilla lowering
        // (posters/heralds/officials) still works above the floor; below it, the value
        // snaps back next tick. No-op while out of game (resolve fails cleanly).
#ifdef AC2AP_WITH_AP
        if (ap && ap_authenticated && grip_enabled) {
            float floor = grip_start - 0.25f * (float)grip_indexes.size();
            // Never clamp up to the renegade-trigger level (meter 1.0). Capping below it means
            // the clamp stays INERT while the player is actually renegade (cur == 1.0 > cap),
            // instead of re-pinning 1.0 every tick and re-firing the renegade animation
            // (the buggy behavior at floor 1.0). The forced-renegade tier isn't achievable via
            // the meter anyway - see docs/design-notoriety.md (state is guard-AI driven).
            const float GRIP_CAP = 0.90f;
            if (floor > GRIP_CAP) floor = GRIP_CAP;
            if (floor > 0.001f) {
                float cur;
                if (ac2ap::game::get_notoriety(cur) && cur < floor) {
                    ac2ap::game::set_notoriety(floor);
                    static bool logged_grip = false;
                    if (!logged_grip) {
                        logf("Templar Grip: notoriety clamped to floor %.2f (%zu grip items)",
                             floor, grip_indexes.size());
                        logged_grip = true;
                    }
                }
            }
        }
#endif

        // 1b) apply received items, in order, with resume
        while (!item_queue.empty()) {
            auto [idx, item] = item_queue.front();
            if (idx <= applied_index) {           // already applied (resync/reconnect)
                item_queue.erase(item_queue.begin());
                continue;
            }
            if (!apply_item(item)) break;         // player not in-game -> retry next tick
#ifdef AC2AP_WITH_AP
            // Receiving a grip item is felt immediately: notoriety drops to the new floor.
            // (Otherwise the player would stay at the old level until tearing posters.)
            if (grip_enabled && item == item_ids::PROGRESSIVE_TEMPLAR_GRIP) {
                float nf = grip_start - 0.25f * (float)grip_indexes.size();
                if (nf < 0.0f) nf = 0.0f;
                ac2ap::game::set_notoriety(nf);
                logf("Templar Grip: floor lowered to %.2f, notoriety set to it", nf);
            }
#endif
            logf("item %lld applied (index %d)", (long long)item, idx);
            applied_index = idx;
            save_applied_index(applied_index);
            item_queue.erase(item_queue.begin());
        }

        // 2) has the save changed?
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExA(g_save_path.c_str(), GetFileExInfoStandard, &fad))
            continue;
        if (CompareFileTime(&fad.ftLastWriteTime, &last_mtime) == 0 && !first_pass && !resync_pending)
            continue;
        Sleep(300);  // let the game finish writing

        std::vector<uint8_t> buf;
        if (!read_file(g_save_path, buf)) continue;
        last_mtime = fad.ftLastWriteTime;

        auto counts = ac2ap::parse_records(buf.data(), buf.size());
        auto fresh = ac2ap::unseen_keys(seen, counts);

        // Victory: if the goal mission record is present in the save, tell the server the seed
        // is won. Checked on every scan (incl. baseline) so it fires even if the goal was
        // completed before connecting. Sent once per session; harmless if the server already knows.
#ifdef AC2AP_WITH_AP
        if (ap && ap_authenticated && goal_id && !goal_sent) {
            for (const auto& [k, n] : counts) {
                if (k.id == goal_id) {
                    ap->StatusUpdate(APClient::ClientStatus::GOAL);
                    logf("AP: GOAL reached (goal mission %08X) -> StatusUpdate sent", goal_id);
                    ac2ap::overlay::toast("Goal complete! In Bocca al Lupo",
                                          IM_COL32(255, 215, 90, 255), 10000);
                    goal_sent = true;
                    break;
                }
            }
        }
#endif

        // current counts of the "by count" collectibles
        int cur_vp = ac2ap::count_distinct_of_type(counts, ac2ap::REC_VIEWPOINT);
        int cur_codex = ac2ap::count_codex(counts);
        int cur_feather = (int)ac2ap::read_type_counter(
            buf.data(), buf.size(), ac2ap::REC_FEATHER_COUNTER, 0x52);

        if (first_pass) {
            // baseline: existing state considered already processed (records AND counters)
            logf("baseline: %zu (type,id), viewpoints=%d feathers=%d",
                 counts.size(), cur_vp, cur_feather);
            for (const auto& k : fresh) seen.insert(k);
            // presence baseline: already-collected collectibles = already processed (no flood)
            for (const auto& [id, apid] : presence)
                if (buf_has_u32(buf, id)) seen.insert({REC_PRESENCE, id});
            save_seen(seen);
            if (counted_state.find("VIEWPOINT") == counted_state.end())
                counted_state["VIEWPOINT"] = cur_vp;
            if (counted_state.find("CODEX") == counted_state.end())
                counted_state["CODEX"] = cur_codex;
            if (counted_state.find("FEATHER") == counted_state.end())
                counted_state["FEATHER"] = cur_feather;
            save_counted_state(counted_state);
            first_pass = false;
            continue;
        }

#ifdef AC2AP_WITH_AP
        // Resync on connect: (re)send every check already done in this save, so the counter is
        // retroactive and the server receives all past checks (needed on a fresh seed / after a
        // server state loss). Idempotent: the server ignores locations it already has, and seen.txt
        // still suppresses re-toasting. Runs once per connect.
        if (resync_pending && ap && ap_authenticated) {
            resync_pending = false;
            // Locations this save proves done (all categories, before the seed filter).
            std::set<int64_t> locs;
            for (const auto& [k, cnt] : counts) {
                auto it = id_map.find(k.id);
                if (it != id_map.end()) locs.insert(it->second);
            }
            for (const auto& [id, apid] : presence)
                if (buf_has_u32(buf, id)) locs.insert(apid);
            auto resync_counted = [&](const char* cat, int count) {
                auto it = counted.find(cat);
                if (it == counted.end()) return;
                for (int i = 0; i < count && i < (int)it->second.size(); i++)
                    if (it->second[i]) locs.insert(it->second[i]);
            };
            resync_counted("VIEWPOINT", cur_vp);
            resync_counted("CODEX", cur_codex);
            resync_counted("FEATHER", cur_feather);

            // The server tells us every location for our slot: missing + checked = the seed's
            // locations (respects the YAML). Totals/done are computed against THAT, so disabled
            // categories don't appear. done = server-checked + our save-detected ones in the seed
            // (covers a fresh seed the server hasn't recorded yet).
            std::set<int64_t> checked = ap->get_checked_locations();
            std::set<int64_t> seed = ap->get_missing_locations();
            seed.insert(checked.begin(), checked.end());
            std::set<int64_t> done = checked;
            for (auto id : locs) if (seed.count(id)) done.insert(id);

            for (int i = 0; i < CAT_N; i++) { cat_total[i] = 0; cat_done[i] = 0; }
            for (auto id : seed) { int c = cat_of(id); if (c >= 0) cat_total[c]++; }
            for (auto id : done) { int c = cat_of(id); if (c >= 0) cat_done[c]++; }
            stat_checks = (int)done.size();

            for (auto id : locs) pending.push_back(id);   // re-send (idempotent) to sync the server
            logf("AP: resync -> seed=%zu locs, done=%zu, %zu (re)sent",
                 seed.size(), done.size(), locs.size());
        }
#endif

        bool queued = false;
        for (const auto& k : fresh) {
            logf("CHECK %s type=%016llX id=%08X", type_name(k.type),
                 (unsigned long long)k.type, k.id);
            seen.insert(k);
            auto it = id_map.find(k.id);
            if (it != id_map.end()) {
                pending.push_back(it->second);
                queued = true;
                { int c = cat_of(it->second); if (c >= 0 && cat_total[c] > 0) { cat_done[c]++; stat_checks++; } }
                logf("  -> location AP %lld queued", (long long)it->second);
#ifdef AC2AP_WITH_AP
                if (ap && ap_authenticated) {
                    std::string loc = ap->get_location_name(it->second, ap->get_game());
                    ac2ap::overlay::toast("Checked: " + loc, IM_COL32(210, 210, 210, 255), 4000);
                }
#endif
            }
        }
        // PRESENCE detection (chests/statues): ID present in the save = collected
        for (const auto& [id, apid] : presence) {
            ac2ap::RecordKey pk{REC_PRESENCE, id};
            if (seen.count(pk)) continue;
            if (!buf_has_u32(buf, id)) continue;
            seen.insert(pk);
            pending.push_back(apid);
            queued = true;
            { int c = cat_of(apid); if (c >= 0 && cat_total[c] > 0) { cat_done[c]++; stat_checks++; } }
            logf("CHECK PRESENCE id=%08X -> location AP %lld", id, (long long)apid);
        }
        save_seen(seen);

        // "by count" detection: send the locations from the 1st to the Nth collected
        auto send_counted = [&](const char* cat, int count) {
            auto it = counted.find(cat);
            if (it == counted.end()) return;
            int& sent = counted_state[cat];
            for (int i = sent; i < count && i < (int)it->second.size(); i++) {
                if (it->second[i]) {
                    pending.push_back(it->second[i]);
                    queued = true;
                    { int c = cat_of(it->second[i]); if (c >= 0 && cat_total[c] > 0) { cat_done[c]++; stat_checks++; } }
                    logf("CHECK %s #%d -> location AP %lld", cat, i + 1,
                         (long long)it->second[i]);
                }
            }
            if (count > sent) sent = count;
        };
        // VIEWPOINT STILL DISABLED (BUG-005 still open): type 0xB3195056E38B5102
        // is NOT viewpoint-exclusive. Analysis: 40 ids over 18 prefixes, incl. 0x4549D89E
        // (TOMB range) -> generic "discovery/completion" type (tombs/missions/events).
        // The counter explodes (2 real syncs -> +13 false). Over-counting. Re-identify the real
        // viewpoint discriminant (DataBlocks ADB_Viewpoint extraction?) before re-enabling.
        (void)cur_vp;
        // CODEX by count: ranges 0x4658D3xx/0x45B9E6xx = codex-ONLY (verified against catalog),
        // bounded and reliable (7 collected ids, no contamination). Nth page = "Codex Page #N".
        send_counted("CODEX", cur_codex);
        // FEATHER disabled: the feather counter is not reliably identified
        // (record 5B6A6F41/25620DBE unchanged between 4 and 5 feathers; in-place change
        // buried in the noise). Re-enable after a clean isolated lab (LIM-002). cur_feather
        // kept for diagnostics but NOT sent (avoids false positives).
        (void)cur_feather;
        save_counted_state(counted_state);

        if (queued) save_pending(pending);
    }
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        g_dir = module_dir(inst);
        g_log = fopen((g_dir + "\\AC2AP.log").c_str(), "a");
        HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
