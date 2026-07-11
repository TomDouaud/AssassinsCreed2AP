"""Generates AC2AP_map.txt: game_IDHEX -> AP_location_id.

Mapping source: docs/re/mission-id-map.json (extracted from the AnvilToolkit XML,
runtime_id_hex -> "Sequence N - Name"). Cross-referenced with the apworld location
table (Locations.py) to get the AP location id.

Dev tool: needs the docs/re/ RE data (not shipped). The generated AC2AP_map.txt
is what ships in client/dist/.
"""
import importlib.util
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APWORLD_LOC = os.path.join(ROOT, "apworld", "ac2", "Locations.py")
ID_MAP = os.path.join(ROOT, "docs", "re", "mission-id-map.json")
ITEM_CATALOG = os.path.join(ROOT, "docs", "re", "item-id-catalog.txt")
CHEST_DISTRICTS = os.path.join(ROOT, "docs", "re", "chest-districts.json")
OUT = os.path.join(ROOT, "client", "dist", "AC2AP_map.txt")

spec = importlib.util.spec_from_file_location("ac2loc", APWORLD_LOC)
loc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(loc)
name_to_id = loc.LOCATION_NAME_TO_ID

# runtime_id_hex -> "Sequence N - Nom"
id_map = json.load(open(ID_MAP, encoding="utf-8"))

# "By count" locations: ordered lists of ap_location_id (the client sends the
# Nth one when the type's counter reaches N). Lines "@CAT idx apid".
def counted(prefix, n):
    out = []
    for i in range(1, n + 1):
        apid = name_to_id.get(f"{prefix} #{i}")
        if apid is not None:
            out.append(apid)
    return out

# FEATHER + CHEST removed from "counted": INDIVIDUAL detection by forge ID.
# Feathers: record 0x0B (id_map). Chests/statues: by PRESENCE of the ID in the save (~ lines).
COUNTED = {
    "VIEWPOINT": counted("Viewpoint", 73),
    "CODEX": counted("Codex Page", 30),
    "GLYPH": counted("Glyph", 20),
}


# Collectibles by PRESENCE (chests, statues): the catalog names them <prefix>_NN (NN not
# contiguous -> mapping by SORTED ORDER to ap_fmt #1..#K, stable bijection). Returns forge_id -> ap_id.
def load_presence_collectibles(prefix, ap_fmt):
    import re
    items = []  # (NN, forge_id)
    for line in open(ITEM_CATALOG, encoding="utf-8"):
        m = re.match(r'\s*(\d+)\s+0x[0-9A-F]+\s+' + prefix + r'_(\d+)\s*$', line)
        if m:
            items.append((int(m.group(2)), int(m.group(1))))
    items.sort()
    out = {}
    for i, (_nn, fid) in enumerate(items):
        apid = name_to_id.get(ap_fmt.format(i + 1))
        if apid is not None:
            out[fid] = apid
    return out


# Chests by PRESENCE, named BY DISTRICT (docs/re/chest-districts.json). Within each district,
# chests are sorted by forge NN -> rank #1..#K. Must stay consistent with Locations.py
# (CHEST_DISTRICT_ORDER, same labels/counts). Returns forge_id -> ap_id.
def load_chest_presence():
    import json
    from collections import defaultdict
    dj = json.load(open(CHEST_DISTRICTS, encoding="utf-8"))
    groups = defaultdict(list)  # label -> [(nn, forge_id)]
    for hexid, (label, _region, nn) in dj.items():
        groups[label].append((nn, int(hexid, 16)))
    out = {}
    for label, lst in groups.items():
        lst.sort()
        for k, (_nn, fid) in enumerate(lst, start=1):
            apid = name_to_id.get(f"Treasure Chest - {label} #{k}")
            if apid is not None:
                out[fid] = apid
    return out


# Feathers: forge ID (docs/re/item-id-catalog.txt, Feather_NN) -> location "Feather #(NN+1)".
# 100 clean feathers Feather_00..99 (no gap), 1:1 with Feather #1..#100.
def load_feather_map():
    import re
    forge = {}  # NN -> forge_id
    for line in open(ITEM_CATALOG, encoding="utf-8"):
        m = re.match(r'\s*(\d+)\s+0x[0-9A-F]+\s+Feather_(\d+)\s*$', line)
        if m:
            forge[int(m.group(2))] = int(m.group(1))
    out = {}  # forge_id -> ap_id
    for nn, fid in forge.items():
        apid = name_to_id.get(f"Feather #{nn + 1}")
        if apid is not None:
            out[fid] = apid
    return out

os.makedirs(os.path.dirname(OUT), exist_ok=True)
mapped = missing_loc = 0
with open(OUT, "w", encoding="ascii") as f:
    f.write("# AC2AP mapping. Missions: <game_id_hex> <ap_location_id>\n")
    f.write("# By-count collectibles: @CAT <index0> <ap_location_id>\n")
    for gid_hex, apname in sorted(id_map.items()):
        apid = name_to_id.get(apname)
        if apid is None:
            f.write(f"# MISMATCH name absent from Locations.py: {apname} (gid {gid_hex})\n")
            missing_loc += 1
        else:
            f.write(f"{gid_hex} {apid}\n")
            mapped += 1
    # Feathers: individual detection by forge ID, record 0x0B (id_map).
    feather_map = load_feather_map()
    f.write("# Individual feathers: <forge_id_hex> <ap_location_id>\n")
    for fid, apid in sorted(feather_map.items()):
        f.write(f"{fid:08X} {apid}\n")
    # Chests + statues: detection by PRESENCE (~ lines).
    presence = {}
    presence.update(load_chest_presence())
    presence.update(load_presence_collectibles("Statue_Codex", "Statue #{}"))
    # Shop: purchase = forge item acquired (record) -> presence. SHOP_EQUIPMENT (forge_id, name).
    for fid, name in loc.SHOP_EQUIPMENT:
        apid = name_to_id.get(f"Shop - {name}")
        if apid is not None:
            presence[fid] = apid
    f.write("# Collectibles by presence: ~<forge_id_hex> <ap_location_id>\n")
    for fid, apid in sorted(presence.items()):
        f.write(f"~{fid:08X} {apid}\n")
    for cat, ids in COUNTED.items():
        for idx, apid in enumerate(ids):
            f.write(f"@{cat} {idx} {apid}\n")
print(f"{mapped} missions, {missing_loc} missing; feathers={len(feather_map)}; "
      f"presence(chests+statues)={len(presence)}; "
      f"counted: " + ", ".join(f"{c}={len(v)}" for c, v in COUNTED.items()) + f" -> {OUT}")
