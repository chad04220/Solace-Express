"""Real landmarks for the Earth pack's landmarks.bin (tables.write_landmarks): Wikidata's (CC0), read through its
query service. So far the lighthouses - instances of lighthouse (Q39715) or of any of its subclasses, with a
coordinate, not demolished - with their height where Wikidata has it, in metres (its normalised value), and their
English label (else any). Two within 60 m are one (an item and a duplicate of it, a range's front and rear lights):
the one with a height kept, else the older item.

  python3 -m tools.earth2.landmarks OUT.csv [--box SOUTH WEST NORTH EAST]
"""
import argparse
import csv
import math
import re
import sys
import time
import urllib.parse
import urllib.request

ENDPOINT = "https://query.wikidata.org/sparql"
AGENT = "SomniumVolandi-EarthPack/1 (https://github.com/chad04220/Solace-Express)"
KIND_LIGHTHOUSE = 1
MERGE_M = 60.0

QUERY = """SELECT ?item ?itemLabel ?coord ?height WHERE {
  %s
  ?item wdt:P31/wdt:P279* wd:Q39715 .
  FILTER NOT EXISTS { ?item wdt:P576 ?gone . }
  OPTIONAL { ?item p:P2048/psn:P2048/wikibase:quantityAmount ?height . }
  SERVICE wikibase:label { bd:serviceParam wikibase:language "en,mul,fr,de,es,it,pt,nl,sv,no,da,ja,zh". }
}"""
BOX = """SERVICE wikibase:box { ?item wdt:P625 ?coord .
    bd:serviceParam wikibase:cornerSouthWest "Point(%f %f)"^^geo:wktLiteral .
    bd:serviceParam wikibase:cornerNorthEast "Point(%f %f)"^^geo:wktLiteral . }"""


def fetch(out_csv, box=None, tries=4):
    """The query's answer (CSV) into out_csv: the world's lighthouses, or those in box (south, west, north, east).
    The service is busy at times: asked again after 15, 30 and 60 s before the last failure is raised."""
    where = BOX % (box[1], box[0], box[3], box[2]) if box else "?item wdt:P625 ?coord ."
    url = ENDPOINT + "?" + urllib.parse.urlencode({"query": QUERY % where})
    req = urllib.request.Request(url, headers={"Accept": "text/csv", "User-Agent": AGENT})
    for attempt in range(tries):
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                body = r.read()
            if not body.startswith(b"item,"):
                raise IOError("not the query's CSV: %r" % body[:80])
            with open(out_csv, "wb") as f:
                f.write(body)
            return
        except (OSError, IOError) as e:
            if attempt + 1 == tries:
                raise
            print("the query service: %s (again in %d s)" % (e, 15 << attempt), file=sys.stderr)
            time.sleep(15 << attempt)


def _distance(a, b):
    la1, la2 = math.radians(a["lat"]), math.radians(b["lat"])
    d = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin(math.radians(b["lon"] - a["lon"]) / 2) ** 2
    return 2 * 6371000.0 * math.asin(min(1.0, math.sqrt(d)))


def read(csv_path, box=None):
    """The landmarks in the query's CSV (those in box when given), one per item, near duplicates merged."""
    items = {}
    with open(csv_path, encoding="utf-8") as f:
        for row in csv.DictReader(f):
            m = re.match(r"Point\(([-0-9.eE]+) ([-0-9.eE]+)\)", row.get("coord", ""))
            ident = re.search(r"Q(\d+)$", row.get("item", ""))
            if not m or not ident:
                continue
            lon, lat = float(m.group(1)), float(m.group(2))
            if not (-90 <= lat <= 90 and -180 <= lon <= 180) or (box and not (box[0] <= lat < box[2] and box[1] <= lon < box[3])):
                continue
            qid = int(ident.group(1))
            name = row.get("itemLabel", "") or ""
            if re.fullmatch(r"Q\d+", name):
                name = ""   # (no label in any language asked for)
            try:
                height = float(row.get("height") or 0) or None
            except ValueError:
                height = None
            if height is not None and not (2.0 <= height <= 200.0):
                height = None   # (an outlier: a mistaken unit, or the height above the sea)
            if qid not in items:   # (an item's first coordinate)
                items[qid] = {"id": qid, "name": name, "lat": lat, "lon": lon, "kind": KIND_LIGHTHOUSE, "height": height}
    kept = []
    for it in sorted(items.values(), key=lambda m: (m["height"] is None, m["id"])):
        if any(_distance(it, k) < MERGE_M for k in kept if abs(k["lat"] - it["lat"]) < 0.01):
            continue
        kept.append(it)
    return kept


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 -m tools.earth2.landmarks")
    ap.add_argument("out")
    ap.add_argument("--box", type=float, nargs=4, metavar=("SOUTH", "WEST", "NORTH", "EAST"))
    a = ap.parse_args(argv)
    fetch(a.out, a.box)
    print("%s: %d lighthouses" % (a.out, len(read(a.out, a.box))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
