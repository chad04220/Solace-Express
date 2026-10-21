"""The point tables: airports.bin (OurAirports) and places.bin (GeoNames), each sorted by 1-degree cell with a cell
index, read by the game as they are (src/earth_data.cpp).

airports.bin (little endian):
  header, 32 bytes: magic "SVA2", u32 version (1), u32 airports, u32 runways, u32 string bytes, 12 bytes reserved
  cell index: 64,800 x (u32 first airport, u32 count), cell (floor(lat) + 90) * 360 + floor(lon) + 180
  airports, 48 bytes each: i32 lat, i32 lon (1e-7 degree), i16 elevation (m), u8 type (0 small, 1 medium, 2 large),
    u8 runway count, u32 first runway, u32 ident, u32 name, u32 municipality, u32 country, u32 iata (string offsets),
    u32 OurAirports id, u32 flags (1 scheduled service), u32 reserved
  runways, 40 bytes each: i32 lat1, lon1, lat2, lon2 (the two ends, 1e-7 degree; 0x7fffffff unknown), u16 length (m),
    u16 width (m, 0 unknown), u8 surface (0 unknown, 1 asphalt, 2 concrete, 3 grass, 4 gravel, 5 dirt, 6 sand,
    7 water, 8 snow or ice), u8 flags (1 lighted, 4 ends known), i16 elevation at each end (m, -32768 unknown),
    u16 true heading of end 1 (0.1 degree, 0xffff unknown), char ident1[4], char ident2[4], u16 displaced
    threshold at each end (m)
  strings: UTF-8, each NUL-terminated (offset 0 is the empty string)

places.bin:
  header, 32 bytes: magic "SVP2", u32 version (1), u32 places, u32 string bytes, 16 bytes reserved
  cell index as above
  places, 24 bytes each: i32 lat, i32 lon (1e-7 degree), u32 population, u32 name, u32 GeoNames id, u8 kind
    (0 place, 1 seat of a second-order division or lower, 2 seat of a first-order division, 3 capital), u8 reserved,
    char country[2]
  strings as above

landmarks.bin (Wikidata, src/earth_data.cpp Pack::landmarks):
  header, 32 bytes: magic "SVL2", u32 version (1), u32 landmarks, u32 string bytes, 16 bytes reserved
  cell index as above
  landmarks, 16 bytes each: i32 lat, i32 lon (1e-7 degree), u8 kind (1 lighthouse), u8 reserved, u16 height (0.1 m,
    0 unknown), u32 name
  strings as above
"""
import csv
import io
import math
import os
import struct
import zipfile

AIRPORT_HEADER = struct.Struct("<4sIIII12x")
AIRPORT = struct.Struct("<iihBBIIIIIIII4x")
RUNWAY = struct.Struct("<iiiiHHBBhhH4s4sHH")
PLACE_HEADER = struct.Struct("<4sIII16x")
PLACE = struct.Struct("<iiIIIBx2s")
LANDMARK_HEADER = struct.Struct("<4sIII16x")
LANDMARK = struct.Struct("<iiBxHI")
CELLS = 360 * 180
CELL = struct.Struct("<II")
UNKNOWN_E7 = 0x7FFFFFFF
assert AIRPORT_HEADER.size == 32 and AIRPORT.size == 48 and RUNWAY.size == 40 and PLACE_HEADER.size == 32 and PLACE.size == 24
assert LANDMARK_HEADER.size == 32 and LANDMARK.size == 16

AIRPORT_TYPES = {"small_airport": 0, "medium_airport": 1, "large_airport": 2}
MIN_RUNWAY_M = 300
FT = 0.3048


def cell_of(lat, lon):
    la = min(max(int(math.floor(lat)), -90), 89)
    lo = int(math.floor(lon))
    lo = (lo + 180) % 360 - 180
    return (la + 90) * 360 + lo + 180


def e7(v):
    return int(round(float(v) * 1e7))


class Strings:
    def __init__(self):
        self.data = bytearray(b"\0")
        self.index = {"": 0}

    def add(self, s):
        s = (s or "").strip()
        if s not in self.index:
            self.index[s] = len(self.data)
            self.data += s.encode("utf-8") + b"\0"
        return self.index[s]


def surface_code(text):
    s = (text or "").strip().upper()
    for prefixes, code in ((("ASP", "BIT", "TAR", "MAC", "PEM", "PAV", "BLACKTOP"), 1), (("CON", "COP", "PSP"), 2),
                           (("GRS", "GRASS", "TURF", "GRE"), 3), (("GRV", "GVL", "GRAV", "GRAVEL", "CRUSH", "COR", "SHALE", "ROCK"), 4),
                           (("DIRT", "CLAY", "SOIL", "EARTH", "LAT", "MUD", "LOAM", "SILT", "UNPAVED", "UNK-U"), 5),
                           (("SAND",), 6), (("WATER", "WAT"), 7), (("SNOW", "ICE"), 8)):
        if s.startswith(prefixes):
            return code
    return 0


def _num(v, default=None):
    try:
        x = float(v)
    except (TypeError, ValueError):
        return default
    return x if x == x else default


def _ident(v):
    return (v or "").strip().encode("ascii", "replace")[:4].ljust(4, b"\0")


def read_airports(airports_csv, runways_csv, box=None):
    """The airports the game uses: open small, medium and large airports with at least one open runway of
    MIN_RUNWAY_M or more (or of unknown length with known ends at least that far apart). box: (south, west, north,
    east) or None for all. Returns a list of (airport dict, [runway dicts])."""
    runways = {}
    with open(runways_csv, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if (r.get("closed") or "0").strip() == "1":
                continue
            runways.setdefault(r["airport_ref"], []).append(r)
    out = []
    with open(airports_csv, newline="", encoding="utf-8") as f:
        for a in csv.DictReader(f):
            kind = AIRPORT_TYPES.get(a["type"])
            lat, lon = _num(a["latitude_deg"]), _num(a["longitude_deg"])
            if kind is None or lat is None or lon is None:
                continue
            if box and not (box[0] <= lat < box[2] and box[1] <= lon < box[3]):
                continue
            rws = []
            for r in runways.get(a["id"], []):
                length = _num(r["length_ft"])
                width = _num(r["width_ft"])
                ends = [_num(r[k]) for k in ("le_latitude_deg", "le_longitude_deg", "he_latitude_deg", "he_longitude_deg")]
                known = all(v is not None for v in ends)
                if known:
                    dlat = (ends[2] - ends[0]) * 111195.0
                    dlon = (ends[3] - ends[1]) * 111195.0 * math.cos(math.radians((ends[0] + ends[2]) / 2))
                    span = math.hypot(dlat, dlon)
                    if span < 50:   # (both ends at one point: not a runway's ends)
                        known = False
                m = length * FT if length and length > 0 else (span if known else 0)
                if m < MIN_RUNWAY_M or (surface_code(r["surface"]) == 7):
                    continue
                heading = _num(r["le_heading_degT"])
                if heading is None and known:
                    heading = math.degrees(math.atan2(dlon, dlat)) % 360
                rws.append({
                    "ends": ends if known else None,
                    "length": int(round(min(m, 65535))),
                    "width": int(round(min(width * FT, 65535))) if width and width > 0 else 0,
                    "surface": surface_code(r["surface"]),
                    "lighted": (r.get("lighted") or "0").strip() == "1",
                    "elev": [_num(r["le_elevation_ft"]), _num(r["he_elevation_ft"])],
                    "heading": heading,
                    "ident": [r["le_ident"], r["he_ident"]],
                    "displaced": [_num(r["le_displaced_threshold_ft"], 0), _num(r["he_displaced_threshold_ft"], 0)],
                })
            if not rws:
                continue
            # the longest hard runway first, then the longest
            rws.sort(key=lambda r: (r["surface"] not in (1, 2), -r["length"], r["ident"][0] or ""))
            out.append((a, rws, kind, lat, lon))
    return out


def write_airports(path, rows):
    strings = Strings()
    rows = sorted(rows, key=lambda t: (cell_of(t[3], t[4]), -t[2], t[0]["ident"]))
    airports, runways = [], []
    cells = [[0, 0] for _ in range(CELLS)]
    for i, (a, rws, kind, lat, lon) in enumerate(rows):
        c = cell_of(lat, lon)
        if cells[c][1] == 0:
            cells[c][0] = i
        cells[c][1] += 1
        elev = _num(a["elevation_ft"])
        airports.append(AIRPORT.pack(
            e7(lat), e7(lon), int(round(elev * FT)) if elev is not None else -32768, kind, min(len(rws), 255), len(runways),
            strings.add(a["ident"]), strings.add(a["name"]), strings.add(a["municipality"]), strings.add(a["iso_country"]),
            strings.add(a["iata_code"]), int(a["id"]), 1 if (a.get("scheduled_service") or "") == "yes" else 0))
        for r in rws[:255]:
            ends = r["ends"]
            el = [int(round(v * FT)) if v is not None else -32768 for v in r["elev"]]
            runways.append(RUNWAY.pack(
                *( [e7(v) for v in ends] if ends else [UNKNOWN_E7] * 4),
                r["length"], r["width"], r["surface"], (1 if r["lighted"] else 0) | (4 if ends else 0), el[0], el[1],
                int(round(r["heading"] * 10)) % 3600 if r["heading"] is not None else 0xFFFF,
                _ident(r["ident"][0]), _ident(r["ident"][1]),
                int(round(min(r["displaced"][0] * FT, 65535))), int(round(min(r["displaced"][1] * FT, 65535)))))
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path + ".tmp", "wb") as f:
        f.write(AIRPORT_HEADER.pack(b"SVA2", 1, len(airports), len(runways), len(strings.data)))
        for first, count in cells:
            f.write(CELL.pack(first, count))
        f.writelines(airports)
        f.writelines(runways)
        f.write(bytes(strings.data))
    os.replace(path + ".tmp", path)
    return os.path.getsize(path), len(airports), len(runways)


PLACE_KINDS = {"PPLC": 3, "PPLA": 2, "PPLA2": 1, "PPLA3": 1, "PPLA4": 1, "PPLA5": 1, "PPLG": 1}


def read_places(cities_zip_or_txt, box=None, min_population=0):
    """GeoNames' populated places (feature class P). box as read_airports'."""
    if cities_zip_or_txt.endswith(".zip"):
        z = zipfile.ZipFile(cities_zip_or_txt)
        name = [n for n in z.namelist() if n.endswith(".txt")][0]
        f = io.TextIOWrapper(z.open(name), encoding="utf-8")
    else:
        f = open(cities_zip_or_txt, encoding="utf-8")
    out = []
    for line in f:
        p = line.rstrip("\n").split("\t")
        if len(p) < 15 or p[6] != "P":
            continue
        lat, lon = float(p[4]), float(p[5])
        if box and not (box[0] <= lat < box[2] and box[1] <= lon < box[3]):
            continue
        pop = int(p[14] or 0)
        if pop < min_population:
            continue
        out.append({"id": int(p[0]), "name": p[1], "lat": lat, "lon": lon, "pop": pop,
                    "kind": PLACE_KINDS.get(p[7], 0), "cc": p[8][:2]})
    return out


def write_places(path, places):
    strings = Strings()
    places = sorted(places, key=lambda p: (cell_of(p["lat"], p["lon"]), -p["pop"], p["id"]))
    cells = [[0, 0] for _ in range(CELLS)]
    recs = []
    for i, p in enumerate(places):
        c = cell_of(p["lat"], p["lon"])
        if cells[c][1] == 0:
            cells[c][0] = i
        cells[c][1] += 1
        recs.append(PLACE.pack(e7(p["lat"]), e7(p["lon"]), min(p["pop"], 0xFFFFFFFF), strings.add(p["name"]), p["id"],
                               p["kind"], p["cc"].encode("ascii", "replace").ljust(2, b"\0")[:2]))
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path + ".tmp", "wb") as f:
        f.write(PLACE_HEADER.pack(b"SVP2", 1, len(recs), len(strings.data)))
        for first, count in cells:
            f.write(CELL.pack(first, count))
        f.writelines(recs)
        f.write(bytes(strings.data))
    os.replace(path + ".tmp", path)
    return os.path.getsize(path), len(recs)


def write_landmarks(path, landmarks):
    """landmarks: [{"id", "name", "lat", "lon", "kind", "height" (m or None)}], by cell, then by id."""
    strings = Strings()
    landmarks = sorted(landmarks, key=lambda m: (cell_of(m["lat"], m["lon"]), m["id"]))
    cells = [[0, 0] for _ in range(CELLS)]
    recs = []
    for i, m in enumerate(landmarks):
        c = cell_of(m["lat"], m["lon"])
        if cells[c][1] == 0:
            cells[c][0] = i
        cells[c][1] += 1
        h = m.get("height")
        recs.append(LANDMARK.pack(e7(m["lat"]), e7(m["lon"]), m["kind"], min(int(round(h * 10)), 0xFFFF) if h and h > 0 else 0,
                                  strings.add(m["name"])))
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path + ".tmp", "wb") as f:
        f.write(LANDMARK_HEADER.pack(b"SVL2", 1, len(recs), len(strings.data)))
        for first, count in cells:
            f.write(CELL.pack(first, count))
        f.writelines(recs)
        f.write(bytes(strings.data))
    os.replace(path + ".tmp", path)
    return os.path.getsize(path), len(recs)


def read_table(path):
    """Any of the tables back, checked: (magic, header fields, cells, records, runways or None, strings)."""
    with open(path, "rb") as f:
        data = f.read()
    magic = data[:4]
    if magic == b"SVA2":
        _, version, na, nr, ns = AIRPORT_HEADER.unpack_from(data, 0)
        rec, extra = AIRPORT, RUNWAY
    elif magic == b"SVP2":
        _, version, na, ns = PLACE_HEADER.unpack_from(data, 0)
        nr, rec, extra = 0, PLACE, None
    elif magic == b"SVL2":
        _, version, na, ns = LANDMARK_HEADER.unpack_from(data, 0)
        nr, rec, extra = 0, LANDMARK, None
    else:
        raise ValueError("%s: not a point table" % path)
    if version != 1:
        raise ValueError("%s: version %d" % (path, version))
    off = 32
    cells = [CELL.unpack_from(data, off + 8 * i) for i in range(CELLS)]
    off += 8 * CELLS
    recs = [rec.unpack_from(data, off + rec.size * i) for i in range(na)]
    off += rec.size * na
    runs = [extra.unpack_from(data, off + extra.size * i) for i in range(nr)] if extra else None
    off += (extra.size * nr) if extra else 0
    strings = data[off:]
    if len(strings) != ns or (ns and strings[0] != 0) or (ns and strings[-1] != 0):
        raise ValueError("%s: bad string table" % path)
    total = 0
    for c, (first, count) in enumerate(cells):
        if count and first + count > na:
            raise ValueError("%s: cell %d outside the table" % (path, c))
        total += count
    if total != na:
        raise ValueError("%s: cells cover %d of %d records" % (path, total, na))
    return magic, cells, recs, runs, strings


def string_at(strings, offset):
    end = strings.index(b"\0", offset)
    return strings[offset:end].decode("utf-8")
