"""Offline great-circle road clipping, stable nodes and topology-safe reduction."""
from collections import Counter
import hashlib
import math
from .common import canonical_json, read_bounded, strict_json
from .cube import (BASES, EARTH_RADIUS_M, Tile, direction_to_latlon, dot,
                   latlon_to_direction, normalise)

CLASSES = ("highway", "primary", "secondary", "tertiary", "local")
MAX_ROAD_FILE = 64 * 1024 * 1024
MAX_ROAD_VERTICES = 1_000_000
MAX_TILE_ROAD_VERTICES = 200_000


def read_geojson(path):
    document = strict_json(read_bounded(path, MAX_ROAD_FILE))
    if not isinstance(document, dict) or document.get("type") != "FeatureCollection":
        raise ValueError("roads must be a GeoJSON FeatureCollection")
    features = document.get("features")
    _normalise_features(features)
    return features


def _text(value, label):
    if not isinstance(value, (str, int)) or isinstance(value, bool) or not str(value) or len(str(value)) > 256:
        raise ValueError("invalid " + label)
    return str(value)



def _provenance(value):
    if not isinstance(value, dict) or len(value) > 12:
        raise ValueError("invalid road provenance")
    allowed = {"gp_rsi", "gp_rsy", "gp_rcy", "gp_gripreg", "gp_rex", "gp_rav", "source_feature_id", "source_sha256"}
    if not set(value).issubset(allowed):
        raise ValueError("unknown road provenance field")
    for key, item in value.items():
        if key.startswith("gp_"):
            if type(item) is not int or not 0 <= item <= 1000000:
                raise ValueError("invalid GRIP provenance number")
        else:
            _text(item, "road provenance text")
    return value


def _coordinate_node(lon, lat):
    # +180/-180 and pole longitudes are geometrically the same point.
    x, y, z = latlon_to_direction(lat, lon)
    return "coord:" + hashlib.sha256(canonical_json([round(v, 12) or 0.0 for v in (x, y, z)])).hexdigest()[:32]


def _normalise_features(features):
    if isinstance(features, dict):
        if features.get("type") != "FeatureCollection":
            raise ValueError("invalid road collection")
        features = features.get("features")
    if not isinstance(features, list) or len(features) > 200_000:
        raise ValueError("invalid or oversized road feature list")
    result, road_ids, node_positions = [], set(), {}
    count = 0
    for feature in features:
        if not isinstance(feature, dict) or feature.get("type") != "Feature":
            raise ValueError("invalid road feature")
        props, geometry = feature.get("properties", {}), feature.get("geometry", {})
        if not isinstance(props, dict) or not isinstance(geometry, dict) or geometry.get("type") != "LineString":
            raise ValueError("roads must contain LineStrings")
        road_id = _text(props.get("id", feature.get("id")), "road ID")
        if road_id in road_ids:
            raise ValueError("duplicate road ID")
        road_ids.add(road_id)
        if "provenance" in props:
            _provenance(props["provenance"])
        road_class = props.get("class")
        if road_class not in CLASSES:
            raise ValueError("unknown road class")
        points = geometry.get("coordinates")
        if not isinstance(points, list) or len(points) < 2:
            raise ValueError("road needs at least two points")
        count += len(points)
        if count > MAX_ROAD_VERTICES:
            raise ValueError("road input vertex limit exceeded")
        ids = props.get("node_ids")
        if ids is not None and (not isinstance(ids, list) or len(ids) != len(points)):
            raise ValueError("road node_ids length mismatch")
        vertices = []
        for index, point in enumerate(points):
            if not isinstance(point, (list, tuple)) or len(point) != 2 or any(isinstance(v, bool) or not isinstance(v, (int, float)) for v in point):
                raise ValueError("invalid road coordinate")
            lon, lat = point
            direction = latlon_to_direction(lat, lon)
            node = "node:" + _text(ids[index], "node ID") if ids is not None else _coordinate_node(lon, lat)
            old = node_positions.setdefault(node, direction)
            if any(abs(a - b) > 1e-10 for a, b in zip(old, direction)):
                raise ValueError("shared road node has inconsistent coordinates")
            vertices.append((node, direction))
        result.append((road_id, road_class, vertices))
    return sorted(result)


def _edge_id(a_id, b_id, direction):
    # Same source segment and geometric cut across faces/tiles gets one ID.
    key = sorted((a_id, b_id)) + [round(v, 11) or 0.0 for v in normalise(direction)]
    return "edge:" + hashlib.sha256(canonical_json(key)).hexdigest()[:32]


def _clip_segment(p, q, planes):
    lo, hi = 0.0, 1.0
    delta = tuple(b - a for a, b in zip(p, q))
    for plane in planes:
        a, change = dot(p, plane), dot(delta, plane)
        if abs(change) < 1e-15:
            if a < -1e-14:
                return None
            continue
        t = -a / change
        if change > 0:
            lo = max(lo, t)
        else:
            hi = min(hi, t)
        if lo > hi + 1e-13:
            return None
    if hi - lo < 1e-13:
        return None  # Point-only contact does not create a road fragment.
    return lo, hi


def _distance_to_chord(point, a, b):
    ab = tuple(y - x for x, y in zip(a, b))
    ap = tuple(y - x for x, y in zip(a, point))
    length = dot(ab, ab)
    t = max(0.0, min(1.0, dot(ap, ab) / length)) if length else 0.0
    nearest = tuple(x + t * y for x, y in zip(a, ab))
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(point, nearest))) * EARTH_RADIUS_M


def _simplify(vertices, pinned, tolerance):
    if len(vertices) <= 2 or tolerance == 0:
        return vertices
    keep = {0, len(vertices) - 1}
    keep.update(i for i, v in enumerate(vertices) if v["id"] in pinned or v["id"].startswith("edge:"))
    anchors = sorted(keep)
    stack = list(zip(anchors, anchors[1:]))
    while stack:
        first, last = stack.pop()
        best, distance = None, tolerance
        for i in range(first + 1, last):
            d = _distance_to_chord(vertices[i]["_direction"], vertices[first]["_direction"], vertices[last]["_direction"])
            if d > distance:
                best, distance = i, d
        if best is not None:
            keep.add(best)
            stack.extend(((first, best), (best, last)))
    return [vertices[i] for i in sorted(keep)]


def tile_roads(features, tile, tolerance_m=20):
    if isinstance(tolerance_m, bool) or not isinstance(tolerance_m, (int, float)) or not math.isfinite(tolerance_m) or not 0 <= tolerance_m <= 1000:
        raise ValueError("invalid road simplification tolerance")
    lines = _normalise_features(features)
    raw_features = features["features"] if isinstance(features, dict) else features
    provenance = {str(f["properties"].get("id", f.get("id"))): f["properties"]["provenance"]
                  for f in raw_features if "provenance" in f["properties"]}
    counts = Counter(node for _, _, vertices in lines for node, _ in vertices)
    pinned = {node for node, count in counts.items() if count > 1}
    n, a, b = BASES[tile.face]
    size = 1 << tile.level
    u0, u1 = 2 * tile.x / size - 1, 2 * (tile.x + 1) / size - 1
    v0, v1 = 2 * tile.y / size - 1, 2 * (tile.y + 1) / size - 1
    ta0, ta1 = math.tan(u0 * math.pi / 4), math.tan(u1 * math.pi / 4)
    tb0, tb1 = math.tan(v0 * math.pi / 4), math.tan(v1 * math.pi / 4)
    planes = [tuple(a[k] - ta0 * n[k] for k in range(3)),
              tuple(ta1 * n[k] - a[k] for k in range(3)),
              tuple(b[k] - tb0 * n[k] for k in range(3)),
              tuple(tb1 * n[k] - b[k] for k in range(3)), n]
    output, total = [], 0
    for road_id, road_class, vertices in lines:
        fragments, current = [], []
        for (aid, p), (bid, q) in zip(vertices, vertices[1:]):
            if dot(p, q) < -.999999:
                raise ValueError("ambiguous antipodal road segment")
            clipped = _clip_segment(p, q, planes)
            if clipped is None:
                if current:
                    fragments.append(current)
                    current = []
                continue
            lo, hi = clipped
            piece = []
            for t in (lo, hi):
                d = normalise(tuple(p[k] + t * (q[k] - p[k]) for k in range(3)))
                node = aid if abs(t) < 1e-12 else bid if abs(t - 1) < 1e-12 else _edge_id(aid, bid, d)
                denominator = dot(d, n)
                if denominator <= 0:
                    raise ValueError("invalid clipped road direction")
                u = math.atan2(dot(d, a), denominator) * 4 / math.pi
                v = math.atan2(dot(d, b), denominator) * 4 / math.pi
                uq = max(0, min(65535, math.floor((u - u0) / (u1 - u0) * 65535 + .5)))
                vq = max(0, min(65535, math.floor((v - v0) / (v1 - v0) * 65535 + .5)))
                piece.append({"id": node, "u": uq, "v": vq, "_direction": d})
            if current and current[-1]["id"] == piece[0]["id"]:
                current.append(piece[1])
            else:
                if current:
                    fragments.append(current)
                current = piece
            if hi < 1 - 1e-12:
                fragments.append(current)
                current = []
        if current:
            fragments.append(current)
        for fragment_index, fragment in enumerate(fragments):
            simplified = _simplify(fragment, pinned, tolerance_m)
            total += len(simplified)
            if total > MAX_TILE_ROAD_VERTICES:
                raise ValueError("road tile vertex limit exceeded")
            for vertex in simplified:
                del vertex["_direction"]
            record = {"id": road_id, "fragment": fragment_index, "class": road_class, "vertices": simplified}
            if road_id in provenance:
                record["provenance"] = provenance[road_id]
            output.append(record)
    result = {"version": 1, "tile": tile.as_dict(), "roads": output}
    validate_road_tile(result)
    return result


def validate_road_tile(value):
    if not isinstance(value, dict) or set(value) != {"version", "tile", "roads"} or (type(value["version"]) is not int or value["version"] != 1):
        raise ValueError("invalid road tile schema")
    try:
        tile = Tile(**value["tile"])
    except (TypeError, KeyError) as exc:
        raise ValueError("invalid road tile identity") from exc
    roads = value["roads"]
    if not isinstance(roads, list) or len(roads) > MAX_TILE_ROAD_VERTICES // 2:
        raise ValueError("road tile count limit")
    count, ids, node_positions = 0, set(), {}
    for road in roads:
        if not isinstance(road, dict) or set(road) not in ({"id", "fragment", "class", "vertices"}, {"id", "fragment", "class", "vertices", "provenance"}):
            raise ValueError("invalid road record")
        rid = _text(road["id"], "road ID")
        if "provenance" in road:
            _provenance(road["provenance"])
        index = road["fragment"]
        if type(index) is not int or index < 0 or (rid, index) in ids or road["class"] not in CLASSES:
            raise ValueError("invalid road class or fragment ID")
        ids.add((rid, index))
        vertices = road["vertices"]
        if not isinstance(vertices, list) or len(vertices) < 2:
            raise ValueError("invalid road vertex list")
        count += len(vertices)
        if count > MAX_TILE_ROAD_VERTICES:
            raise ValueError("road tile vertex limit")
        for vertex in vertices:
            if not isinstance(vertex, dict) or set(vertex) != {"id", "u", "v"}:
                raise ValueError("invalid road vertex")
            _text(vertex["id"], "global node ID")
            if any(type(vertex[k]) is not int or not 0 <= vertex[k] <= 65535 for k in ("u", "v")):
                raise ValueError("invalid road vertex quantization")
            position = (vertex["u"], vertex["v"])
            if node_positions.setdefault(vertex["id"], position) != position:
                raise ValueError("shared road node position mismatch")
    return tile
