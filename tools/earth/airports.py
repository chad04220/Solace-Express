"""Parse bounded OurAirports CSVs, preserving unknown runway fields as null."""
import csv
import io
import math
from .common import read_bounded

MAX_CSV_BYTES = 64 * 1024 * 1024
TYPES = {"small_airport", "medium_airport", "large_airport"}


def _rows(path, required):
    text = read_bounded(path, MAX_CSV_BYTES).decode("utf-8-sig")
    reader = csv.DictReader(io.StringIO(text, newline=""))
    if not reader.fieldnames or len(reader.fieldnames) != len(set(reader.fieldnames)) or not required.issubset(reader.fieldnames):
        raise ValueError("invalid OurAirports CSV columns")
    result = []
    try:
        for row in reader:
            if None in row or any(value is None or len(value) > 8192 for value in row.values()):
                raise ValueError("invalid OurAirports CSV row")
            result.append(row)
            if len(result) > 500000:
                raise ValueError("OurAirports row limit exceeded")
    except csv.Error as exc:
        raise ValueError("invalid OurAirports CSV") from exc
    return result


def _number(value, label, minimum, maximum, optional=True):
    if value is None or value == "":
        if optional:
            return None
        raise ValueError("missing " + label)
    try:
        number = float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError("invalid " + label) from exc
    if not math.isfinite(number) or not minimum <= number <= maximum:
        raise ValueError("invalid " + label)
    return number


def _metres(value, label, minimum, maximum):
    n = _number(value, label, minimum, maximum)
    return round(n * .3048, 6) if n is not None else None


def _id(value):
    if not isinstance(value, str) or not value.isdigit() or len(value) > 20:
        raise ValueError("invalid OurAirports ID")
    return value


def _bool(value, label):
    if value not in ("0", "1"):
        raise ValueError("invalid " + label)
    return value == "1"


def read_airports(airports_path, runways_path):
    airports = _rows(airports_path, {"id", "ident", "type", "name", "latitude_deg", "longitude_deg", "elevation_ft"})
    runways = _rows(runways_path, {"id", "airport_ref", "closed", "length_ft", "width_ft", "surface"})
    by_airport, runway_ids = {}, set()
    for row in runways:
        rid, aid = _id(row["id"]), _id(row["airport_ref"])
        if rid in runway_ids:
            raise ValueError("duplicate runway ID")
        runway_ids.add(rid)
        if _bool(row["closed"], "runway closed flag"):
            continue
        runway = {"id": rid, "length_m": _metres(row["length_ft"], "runway length", 0, 100000),
                  "width_m": _metres(row["width_ft"], "runway width", 0, 10000), "surface": row["surface"],
                  "lighted": _bool(row.get("lighted", "0") or "0", "runway lighted flag")}
        if runway["length_m"] is not None and runway["length_m"] <= 0:
            raise ValueError("open runway length must be positive if known")
        for end in ("le", "he"):
            runway[end + "_ident"] = row.get(end + "_ident", "")
            runway[end + "_latitude_deg"] = _number(row.get(end + "_latitude_deg"), "runway latitude", -90, 90)
            runway[end + "_longitude_deg"] = _number(row.get(end + "_longitude_deg"), "runway longitude", -180, 180)
            runway[end + "_heading_degT"] = _number(row.get(end + "_heading_degT"), "runway heading", 0, 360)
            runway[end + "_elevation_m"] = _metres(row.get(end + "_elevation_ft"), "runway elevation", -2000, 30000)
            runway[end + "_displaced_threshold_m"] = _metres(row.get(end + "_displaced_threshold_ft"), "displaced threshold", 0, 100000)
            if (runway[end + "_latitude_deg"] is None) != (runway[end + "_longitude_deg"] is None):
                raise ValueError("incomplete runway endpoint")
        by_airport.setdefault(aid, []).append(runway)
    result, ids = [], set()
    for row in airports:
        aid = _id(row["id"])
        if aid in ids:
            raise ValueError("duplicate airport ID")
        ids.add(aid)
        if row["type"] not in TYPES or aid not in by_airport:
            continue
        airport = {"id": aid, "ident": row["ident"], "icao_code": row.get("icao_code", ""),
                   "gps_code": row.get("gps_code", ""), "iata_code": row.get("iata_code", ""),
                   "name": row["name"], "type": row["type"],
                   "latitude_deg": _number(row["latitude_deg"], "airport latitude", -90, 90, False),
                   "longitude_deg": _number(row["longitude_deg"], "airport longitude", -180, 180, False),
                   "elevation_m": _metres(row["elevation_ft"], "airport elevation", -2000, 30000),
                   "runways": sorted(by_airport[aid], key=lambda r: r["id"])}
        result.append(airport)
    result.sort(key=lambda a: a["id"])
    validate_airports(result)
    return result


def _json_number(value, label, minimum, maximum, optional=True):
    if value is not None and (isinstance(value, bool) or not isinstance(value, (int, float))):
        raise ValueError("invalid stored numeric type: " + label)
    return _number(value, label, minimum, maximum, optional)


def validate_airports(airports):
    if not isinstance(airports, list) or len(airports) > 200000:
        raise ValueError("invalid airport table")
    seen, runways_seen = set(), set()
    for airport in airports:
        keys = {"id", "ident", "icao_code", "gps_code", "iata_code", "name", "type", "latitude_deg", "longitude_deg", "elevation_m", "runways"}
        if not isinstance(airport, dict) or set(airport) != keys or airport["type"] not in TYPES:
            raise ValueError("invalid airport record")
        aid = _id(airport["id"])
        if aid in seen:
            raise ValueError("duplicate airport ID")
        seen.add(aid)
        for key in ("ident", "icao_code", "gps_code", "iata_code", "name"):
            if not isinstance(airport[key], str) or len(airport[key]) > 8192:
                raise ValueError("invalid airport text")
        _json_number(airport["latitude_deg"], "airport latitude", -90, 90, False)
        _json_number(airport["longitude_deg"], "airport longitude", -180, 180, False)
        _json_number(airport["elevation_m"], "airport elevation", -1000, 10000)
        runways = airport["runways"]
        if not isinstance(runways, list) or not 1 <= len(runways) <= 1000:
            raise ValueError("invalid runway count")
        for runway in runways:
            runway_keys = {"id", "length_m", "width_m", "surface", "lighted"}
            runway_keys.update(end + "_" + k for end in ("le", "he") for k in
                                ("ident", "latitude_deg", "longitude_deg", "heading_degT", "elevation_m", "displaced_threshold_m"))
            if not isinstance(runway, dict) or set(runway) != runway_keys:
                raise ValueError("invalid runway schema")
            rid = _id(runway["id"])
            if rid in runways_seen:
                raise ValueError("duplicate runway ID")
            runways_seen.add(rid)
            for key, maximum in (("length_m", 30480), ("width_m", 3048)):
                number = _json_number(runway[key], key, 0, maximum)
                if key == "length_m" and number is not None and number <= 0:
                    raise ValueError("invalid runway length")
            if type(runway["lighted"]) is not bool or not isinstance(runway["surface"], str) or len(runway["surface"]) > 8192:
                raise ValueError("invalid runway surface/lighted")
            for end in ("le", "he"):
                if not isinstance(runway[end + "_ident"], str) or len(runway[end + "_ident"]) > 8192:
                    raise ValueError("invalid runway ident")
                for key, minimum, maximum in (("latitude_deg", -90, 90), ("longitude_deg", -180, 180),
                                               ("heading_degT", 0, 360), ("elevation_m", -1000, 10000),
                                               ("displaced_threshold_m", 0, 30480)):
                    _json_number(runway[end + "_" + key], key, minimum, maximum)
                if (runway[end + "_latitude_deg"] is None) != (runway[end + "_longitude_deg"] is None):
                    raise ValueError("incomplete runway endpoint")
