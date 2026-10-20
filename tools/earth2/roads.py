"""The roads layer from GRIP4 (GLOBIO): the polylines noded into edges between junctions, simplified, and filed in
every 1-degree tile their bounds touch (so a chart reads each edge whole).

A road tile is a blob (svb.TYPE_BLOB), little endian:
  u32 edge count, then per edge:
    u64 id (FNV-1a of the class and the points: the same edge has the same id in every tile and every pack),
    u8 class (GRIP's GP_RTP: 1 highway, 2 primary, 3 secondary, 4 tertiary, 5 local, 0 unspecified),
    u8 surface (GP_RSE: 1 paved, 2 unpaved, 0 unknown), u16 point count,
    the first point as i32 lat, i32 lon (1e-7 degree), then each next one as zigzag varint deltas (lat, lon).
An edge's ends are its junctions: two edges meet where an end of one is exactly a point of the other (the same
1e-7 degree position), and an edge never passes through a junction.
"""
import io
import itertools
import math
import os
import struct
import zipfile
import zlib

from . import svb

MAX_EDGE_M = 5000.0   # (longer edges are cut at vertices: a chart reads a few km past its edge, not a whole highway)
SIMPLIFY_M = 6.0      # (Douglas-Peucker tolerance: the game rounds and re-spaces the line itself, layRoad)
CLASSES = (0, 1, 2, 3, 4, 5)


def _dbf_fields(f):
    head = f.read(32)
    count, header_len, record_len = struct.unpack("<4xIHH20x", head)
    rest = f.read(header_len - 32)
    fields, at = [], 1
    for i in range(0, len(rest) - 1, 32):
        d = rest[i:i + 32]
        if d[0] == 0x0D:
            break
        name = d[:11].split(b"\0")[0].decode()
        fields.append((name, at, d[16]))
        at += d[16]
    return count, record_len, {n: (a, l) for n, a, l in fields}


def _records(zip_path, box=None):
    """GRIP's polyline records: (record bounds (xmin, ymin, xmax, ymax), class, surface, [parts as [(lat, lon)]]), only
    those whose bounds reach box (south, west, north, east) when one is given - none when the file's own don't."""
    z = zipfile.ZipFile(zip_path)
    shp_name = [n for n in z.namelist() if n.lower().endswith(".shp")][0]
    dbf_name = shp_name[:-4] + ".dbf"
    shp = io.BufferedReader(z.open(shp_name), 1 << 20)
    dbf = io.BufferedReader(z.open(dbf_name), 1 << 20)
    count, record_len, fields = _dbf_fields(dbf)
    rtp, rse = fields["GP_RTP"], fields["GP_RSE"]
    head = shp.read(100)
    if box:
        s, w, n, e = box
        xmin, ymin, xmax, ymax = struct.unpack_from("<4d", head, 36)
        if xmax < w or xmin > e or ymax < s or ymin > n:   # (a region elsewhere: not read through)
            return
    for _ in range(count):
        rec_head = shp.read(8)
        if len(rec_head) < 8:
            break
        length = struct.unpack(">ii", rec_head)[1] * 2
        content = shp.read(length)
        attrs = dbf.read(record_len)
        if struct.unpack_from("<i", content, 0)[0] != 3:
            continue
        bounds = struct.unpack_from("<4d", content, 4)
        if box and (bounds[2] < w or bounds[0] > e or bounds[3] < s or bounds[1] > n):
            continue
        nparts, npoints = struct.unpack_from("<ii", content, 36)
        parts = list(struct.unpack_from("<%di" % nparts, content, 44)) + [npoints]
        pts = struct.unpack_from("<%dd" % (2 * npoints), content, 44 + 4 * nparts)
        cls = int(attrs[rtp[0]:rtp[0] + rtp[1]].strip() or 0)
        surface = int(attrs[rse[0]:rse[0] + rse[1]].strip() or 0)
        lines = []
        for p in range(nparts):
            line = [(pts[2 * k + 1], pts[2 * k]) for k in range(parts[p], parts[p + 1])]
            if len(line) >= 2:
                lines.append(line)
        yield bounds, cls if cls in CLASSES else 0, surface if surface in (1, 2) else 0, lines


def read_grip(zip_path, box, margin=0.05):
    """GRIP polylines with any part inside box (south, west, north, east), widened by margin degrees. Yields
    (class, surface, [(lat, lon), ...]) per part."""
    for _, cls, surface, lines in _records(zip_path, (box[0] - margin, box[1] - margin, box[2] + margin, box[3] + margin)):
        for line in lines:
            yield cls, surface, line


BLOCK_DEGREES = 10


def split_grip(zip_paths, out_dir, margin=0.5, flush_bytes=1 << 27, log=None):
    """GRIP's lines filed by 10-degree block, for a pack built in parts (build --grip-blocks): each line in every block
    whose box, widened by margin, its record's bounds reach - what read_grip(block, margin) yields, so a block reads
    its own file alone. Quantised as the edges are (1e-7 degree), each line its first point and then its steps, in
    zlib chunks; returns the files written."""
    os.makedirs(out_dir, exist_ok=True)
    written = set()
    for z in zip_paths:
        buffers, size = {}, 0

        def flush():
            for (south, west), chunks in buffers.items():
                path = os.path.join(out_dir, svb.corner_name(south, west) + ".lines")
                raw = zlib.compress(b"".join(chunks), 6)
                with open(path, "ab") as f:
                    f.write(struct.pack("<I", len(raw)) + raw)
                written.add(path)
            buffers.clear()
        for k, ((xmin, ymin, xmax, ymax), cls, surface, lines) in enumerate(_records(z)):
            records = []
            for line in lines:
                q = [_q(*p) for p in line]
                flat = [q[0][0], q[0][1]]   # (the first point, then each one's step from the last: small, so zlib packs them)
                for a, b in zip(q, q[1:]):
                    flat += [b[0] - a[0], b[1] - a[1]]
                records.append(struct.pack("<BBI", cls, surface, len(q)) + struct.pack("<%di" % len(flat), *flat))
            s0 = max(-90, int(math.floor((ymin - margin) / BLOCK_DEGREES)) * BLOCK_DEGREES)
            w0 = max(-180, int(math.floor((xmin - margin) / BLOCK_DEGREES)) * BLOCK_DEGREES)
            for south in range(s0, min(80, int(math.floor((ymax + margin) / BLOCK_DEGREES)) * BLOCK_DEGREES) + 1, BLOCK_DEGREES):
                for west in range(w0, min(170, int(math.floor((xmax + margin) / BLOCK_DEGREES)) * BLOCK_DEGREES) + 1, BLOCK_DEGREES):
                    s, w, n, e = south - margin, west - margin, south + BLOCK_DEGREES + margin, west + BLOCK_DEGREES + margin
                    if xmax < w or xmin > e or ymax < s or ymin > n:
                        continue
                    buffers.setdefault((south, west), []).extend(records)
                    size += sum(len(r) for r in records)
            if size > flush_bytes:
                flush()
                size = 0
            if log and k % 500000 == 0:
                log("%s: %d records" % (os.path.basename(z), k))
        flush()
    return sorted(written)


def read_block_lines(lines_dir, south, west):
    """A block's lines (split_grip), quantise_lines' form: (class, surface, [packed points]), repeated points dropped"""
    path = os.path.join(lines_dir, svb.corner_name(south, west) + ".lines")
    if not os.path.exists(path):
        return
    with open(path, "rb") as f:
        data = f.read()
    at = 0
    while at < len(data):
        (size,) = struct.unpack_from("<I", data, at)
        raw = zlib.decompress(data[at + 4:at + 4 + size])
        at += 4 + size
        k = 0
        while k < len(raw):
            cls, surface, n = struct.unpack_from("<BBI", raw, k)
            k += 6
            vals = struct.unpack_from("<%di" % (2 * n), raw, k)
            k += 8 * n
            q = [_pack(p) for p in zip(itertools.accumulate(vals[0::2]), itertools.accumulate(vals[1::2]))]
            dedup = [q[0]] + [b for a, b in zip(q, q[1:]) if b != a]
            if len(dedup) >= 2:
                yield cls, surface, dedup


def _q(lat, lon):
    return (int(round(lat * 1e7)), int(round(lon * 1e7)))


def _pack(p):
    """A quantised point as one int, ordered as the (lat, lon) pair is: a third of a pair's memory"""
    return ((p[0] + 0x80000000) << 32) | (p[1] + 0x80000000)


def _unpack(k):
    return ((k >> 32) - 0x80000000, (k & 0xFFFFFFFF) - 0x80000000)


def quantise_lines(lines):
    """(class, surface, [(lat, lon)]) -> (class, surface, [packed points]), repeated points dropped, lines of fewer
    than two points left out: what build_edges works on (a caller with millions of lines packs them as it reads)."""
    for cls, surf, line in lines:
        q = [_pack(_q(*p)) for p in line]
        dedup = [q[0]] + [b for a, b in zip(q, q[1:]) if b != a]
        if len(dedup) >= 2:
            yield cls, surf, dedup


def _metres(a, b):
    dlat = (b[0] - a[0]) * 1.11195e-2
    dlon = (b[1] - a[1]) * 1.11195e-2 * math.cos(math.radians(a[0] * 1e-7))
    return math.hypot(dlat, dlon)


def _simplify(pts, tol):
    """Douglas-Peucker on quantised points (metres from a local equirectangular frame)."""
    if len(pts) < 3:
        return pts
    lat0 = pts[0][0] * 1e-7
    k = math.cos(math.radians(lat0))
    xy = [((p[1] - pts[0][1]) * 1.11195e-2 * k, (p[0] - pts[0][0]) * 1.11195e-2) for p in pts]
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        i, j = stack.pop()
        ax, ay = xy[i]
        bx, by = xy[j]
        dx, dy = bx - ax, by - ay
        L2 = dx * dx + dy * dy
        best, at = -1.0, -1
        for m in range(i + 1, j):
            px, py = xy[m][0] - ax, xy[m][1] - ay
            if L2 > 0:
                t = max(0.0, min(1.0, (px * dx + py * dy) / L2))
                ex, ey = px - t * dx, py - t * dy
            else:
                ex, ey = px, py
            d = ex * ex + ey * ey
            if d > best:
                best, at = d, m
        if at >= 0 and best > tol * tol:
            keep[at] = True
            stack.append((i, at))
            stack.append((at, j))
    return [p for p, kp in zip(pts, keep) if kp]


def build_edges(lines, quantised=False):
    """lines: (class, surface, [(lat, lon)]) -> edges [(class, surface, [quantised points])]: split at every
    junction (an end of any line met by a point of another), chained through the points where just two lines of the
    same class and surface meet end to end, simplified, and cut into pieces of at most MAX_EDGE_M. quantised: the
    lines are quantise_lines' already."""
    qlines = lines if quantised else list(quantise_lines(lines))
    ends = {}
    for cls, surf, q in qlines:
        for p in (q[0], q[-1]):
            ends[p] = ends.get(p, 0) + 1
    # split each line at its interior points that are other lines' ends (T junctions)
    pieces = []
    for cls, surf, q in qlines:
        start = 0
        for k in range(1, len(q) - 1):
            if q[k] in ends:
                pieces.append((cls, surf, q[start:k + 1]))
                start = k
        pieces.append((cls, surf, q[start:]))
    degree = {}
    for cls, surf, q in pieces:
        for p in (q[0], q[-1]):
            degree[p] = degree.get(p, 0) + 1
    # chain through degree-2 points joining two pieces of the same class and surface
    at_point = {}
    for i, (cls, surf, q) in enumerate(pieces):
        at_point.setdefault(q[0], []).append(i)
        at_point.setdefault(q[-1], []).append(i)
    used = [False] * len(pieces)

    def joinable(p, cls, surf):
        ids = at_point.get(p, [])
        return degree.get(p) == 2 and len(ids) == 2 and all(pieces[j][0] == cls and pieces[j][1] == surf for j in ids)

    edges = []
    order = sorted(range(len(pieces)), key=lambda i: (pieces[i][2][0], pieces[i][2][-1], pieces[i][0]))
    for i in order:
        if used[i]:
            continue
        cls, surf, q = pieces[i]
        used[i] = True
        chain = list(q)
        # walk forward from the end, then backward from the start
        for forward in (True, False):
            while True:
                p = chain[-1] if forward else chain[0]
                if not joinable(p, cls, surf):
                    break
                nxt = [j for j in at_point[p] if not used[j]]
                if not nxt:
                    break
                j = nxt[0]
                used[j] = True
                qj = pieces[j][2]
                if forward:
                    seg = qj if qj[0] == p else qj[::-1]
                    chain.extend(seg[1:])
                else:
                    seg = qj if qj[-1] == p else qj[::-1]
                    chain[:0] = seg[:-1]
                if chain[0] == chain[-1]:
                    break
        # a canonical direction (the smaller end first), so the same chain is the same edge whichever piece it began at
        if chain[-1] < chain[0]:
            chain.reverse()
        simple = _simplify([_unpack(k) for k in chain], SIMPLIFY_M)
        for part in _cut(simple):
            edges.append((cls, surf, part))
    return edges


def _cut(pts):
    """Pieces of at most MAX_EDGE_M along the line, cut at its vertices (a segment longer than that alone first gets
    evenly spaced vertices of its own)."""
    dense = [pts[0]]
    for a, b in zip(pts, pts[1:]):
        d = _metres(a, b)
        k = int(math.ceil(d / MAX_EDGE_M)) if d > MAX_EDGE_M else 1
        for i in range(1, k):
            t = i / k
            dense.append((int(round(a[0] + (b[0] - a[0]) * t)), int(round(a[1] + (b[1] - a[1]) * t))))
        dense.append(b)
    out, cur, run = [], [dense[0]], 0.0
    for a, b in zip(dense, dense[1:]):
        d = _metres(a, b)
        if run + d > MAX_EDGE_M and len(cur) > 1:
            out.append(cur)
            cur, run = [a], 0.0
        cur.append(b)
        run += d
    out.append(cur)
    return out


def _varint(v, out):
    v = (v << 1) ^ (v >> 63)   # zigzag
    v &= (1 << 64) - 1
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return


def edge_id(cls, pts):
    h = 1469598103934665603
    for b in struct.pack("<B", cls) + b"".join(struct.pack("<ii", *p) for p in pts):
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def encode_tile(edges):
    out = bytearray(struct.pack("<I", len(edges)))
    for cls, surf, pts in edges:
        out += struct.pack("<QBBH", edge_id(cls, pts), cls, surf, len(pts))
        out += struct.pack("<ii", *pts[0])
        for a, b in zip(pts, pts[1:]):
            _varint(b[0] - a[0], out)
            _varint(b[1] - a[1], out)
    return bytes(out)


def decode_tile(raw):
    count = struct.unpack_from("<I", raw, 0)[0]
    at, edges = 4, []
    for _ in range(count):
        eid, cls, surf, n = struct.unpack_from("<QBBH", raw, at)
        at += 12
        lat, lon = struct.unpack_from("<ii", raw, at)
        at += 8
        pts = [(lat, lon)]
        for _ in range(n - 1):
            vals = []
            for _ in range(2):
                v, shift = 0, 0
                while True:
                    b = raw[at]
                    at += 1
                    v |= (b & 0x7F) << shift
                    shift += 7
                    if not b & 0x80:
                        break
                vals.append((v >> 1) ^ -(v & 1))
            lat, lon = lat + vals[0], lon + vals[1]
            pts.append((lat, lon))
        edges.append((eid, cls, surf, pts))
    if at != len(raw):
        raise ValueError("road tile: %d trailing bytes" % (len(raw) - at))
    return edges


def write_roads(root, edges, box, layer=svb.LAYER_ROADS):
    """File each edge in every tile of box its bounds touch; returns {relative path: bytes}."""
    import os
    tiles = {}
    for cls, surf, pts in edges:
        la0 = math.floor(min(p[0] for p in pts) * 1e-7)
        la1 = math.floor(max(p[0] for p in pts) * 1e-7)
        lo0 = math.floor(min(p[1] for p in pts) * 1e-7)
        lo1 = math.floor(max(p[1] for p in pts) * 1e-7)
        for la in range(la0, la1 + 1):
            for lo in range(lo0, lo1 + 1):
                if box[0] <= la < box[2] and box[1] <= lo < box[3]:
                    tiles.setdefault((la, lo), []).append((cls, surf, pts))
    blocks = {}
    for (la, lo), es in tiles.items():
        es.sort(key=lambda e: (e[2][0], e[2][-1], e[0], len(e[2])))
        raw = encode_tile(es)
        blocks.setdefault(svb.block_of(la, lo), {})[(la, lo)] = (svb.KIND_ZLIB, raw, 0)
    written = {}
    for (south, west), entries in sorted(blocks.items()):
        path = svb.bundle_path(root, layer, south, west)
        written[os.path.relpath(path, root)] = svb.write_bundle(path, layer, svb.TYPE_BLOB, south, west, 0, 0, entries)
    return written
