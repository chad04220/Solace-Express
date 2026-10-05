#!/usr/bin/env python3
"""Reproduce the separate fleet-gear and XR-9 canopy changes; fail atomically on a different parent."""
import argparse
import hashlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
EXPECTED = {
    "src/shaders/plane_sdf.glsl": "4afbd54086d6379231d659602bda10842ce0ee9a",
    "src/shaders/plane_material.glsl": "77ba4fc0b59d0fc383c67f0c37371f71e9ffde56",
    "src/shaders/wraith_sdf.glsl": "44952d973431647906b8fa42bdc96e14bfd28f6b",
}


def once(s, old, new):
    if s.count(old) != 1:
        raise ValueError(f"Source anchor must occur once: {old[:100]}")
    return s.replace(old, new, 1)


def finish_research(s):
    # XR-9 and XR-11 keep their distinct deployment paths and paired nose tyres.
    old = """    res = opU(res, vec2(legs, 8.0));
    res = opU(res, vec2(tyres, 6.0));
  }
  return res;"""
    new = """    res = opU(res, vec2(legs, 8.0));
    res = opU(res, vec2(tyres, 6.0));
    res = gearWheelDetails(ap - wc, res, wr, 0.13, true);
    res = gearLegDetails(ap, res, vec3(G0.x*0.8, -0.3, G0.z), wc + vec3(-0.1, 0.05, 0.0), 0.07, true);
    vec3 nq = vec3(abs(p.x) - 0.1, p.y, p.z) - nc;
    res = gearWheelDetails(nq, res, 0.33, 0.07, false);
    res = gearLegDetails(p, res, vec3(0.0, -0.35, G0.w), nc + vec3(0.0, 0.1, 0.0), 0.06, true);
  }
  return res;"""
    s = once(s, old, new)
    start = s.index("  {   // gear bays: mains outboard, nose bay with twin doors")
    stop = s.index("  return res;", start)
    part = s[start:stop]
    assert part.count("sdRoundCylX") == 2
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", type=Path, required=True)
    root = ap.parse_args().source.resolve()
    files = {}
    for name, expected in EXPECTED.items():
        data = (root / name).read_bytes()
        actual = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if actual != expected:
            raise SystemExit(f"Parent source differs: {name}; rebase deliberately.")
        files[name] = data.decode()
    name = "src/shaders/plane_sdf.glsl"
    s = files[name]
    anchor = "// ---------------- XR-9 Specter research jet (engine code 5):"
    assert s.count(anchor) == 1
    s = s.replace(anchor, (HERE / "gear_helpers.glsl.inc").read_text() + "\n" + anchor, 1)
    s = finish_research(s)

    s = once(s, "      legs = min(legs, nl); tyres = min(tyres, nt);", """      legs = min(legs, nl); tyres = min(tyres, nt);
      if (!retract || gear >= 0.06) {
        float halfWidth = gtype == 3 ? 0.07 : 0.055;
        vec3 wheelQ = gtype == 3 ? vec3(abs(q.x) - 0.15, q.yz) - nc : q - nc;
        res = gearWheelDetails(wheelQ, res, nwr, halfWidth, false);
        res = gearLegDetails(q, res, vec3(0.0, secN.z - secN.y*0.7, -0.05), nc + vec3(0.0, nwr*0.9, 0.0), gtype >= 3 ? 0.07 : 0.035, true);
        if (gtype == 0) spats = max(spats, -sdCylX(q - nc, nwr*0.54, 0.12));
      }""")
    s = once(s, "      tyres = min(tyres, sdRoundCylX(q - tc, 0.1, 0.035, 0.02));", """      tyres = min(tyres, sdRoundCylX(q - tc, 0.1, 0.035, 0.02));
      res = gearWheelDetails(q - tc, res, 0.1, 0.035, false);
      res = gearLegDetails(q, res, vec3(0.0, tsec.z - tsec.y*0.6, -0.3), tc + vec3(0.0, 0.03, -0.05), 0.02, false);""")
    s = once(s, "    res = opU(res, vec2(spats, 1.0));", """    if (!retract || gear >= 0.06) {
      float halfWidth = gtype == 3 ? 0.11 : gtype == 0 ? 0.065 : gtype == 1 ? 0.09 : gtype == 2 ? 0.14 : 0.1;
      vec3 wheelQ = ap - wc;
      if (gtype == 3) wheelQ.x = abs(wheelQ.x) - 0.22;
      res = gearWheelDetails(wheelQ, res, wr, halfWidth, true);
      if (gtype == 0) spats = max(spats, -sdCylX(ap - wc, wr*0.54, 0.12));
      vec3 mount = gtype == 3 ? vec3(track, nsec.x, mz) : gtype >= 4 ? vec3(track, gM[10].x + track*gM[10].z - 0.1 + lift*0.2, mz) :
                   vec3(secM.x*(gtype == 2 ? 0.8 : gtype == 0 ? 0.75 : 0.7), secM.z - secM.y*(gtype == 1 ? 0.85 : 0.8), mz);
      vec3 ankle = gtype >= 3 ? wc + vec3(0.0, 0.05, 0.0) : gtype == 2 ? wc : wc + vec3(gtype == 0 ? -0.06 : -0.08, gtype == 1 ? 0.06 : 0.04, 0.0);
      float shaft = gtype == 3 ? 0.09 : gtype >= 4 ? 0.06 : gtype == 1 ? 0.045 : 0.03;
      res = gearLegDetails(ap, res, mount, ankle, shaft, retract);
    }
    res = opU(res, vec2(spats, 1.0));""")
    start = s.index("  // ---------------- landing gear (fixed types")
    stop = s.index("  // ---------------- small details:", start)
    part = s[start:stop]
    assert part.count("sdRoundCylX") == 11
    # Keep tyre silhouettes, radii and the existing clearance envelopes unchanged.
    s = once(s, "  res = opU(res, vec2(sdEllipsoid(p - vec3(0.0, 0.5, -4.6), vec3(0.6, 0.42, 1.9)), 32.0));", """  // Lower the crown by 11 cm and blend the shoulders, retaining the opaque sensor film.
  float canopy = sdEllipsoid(p - vec3(0.0, 0.44, -4.6), vec3(0.62, 0.37, 2.0));
  res = vec2(smin(res.x, canopy, 0.16), canopy < res.x ? 32.0 : res.y);""")
    files[name] = s
    name = "src/shaders/plane_material.glsl"
    s = files[name]
    anchor = "// The airframe material at a hit."
    s = once(s, anchor, (HERE / "gear_material.glsl.inc").read_text() + "\n" + anchor)
    s = once(s, "  n = applyTS(n, m.nrm, interior ? 0.35 : 0.12);",
             "  if (mid == 6 || mid == 8) m = gearFinish(lp, mid, m, t*2.0*uTanHalf/uRes.y);\n  n = applyTS(n, m.nrm, interior ? 0.35 : 0.12);")
    files[name] = s
    name = "src/shaders/wraith_sdf.glsl"
    files[name] = finish_research(files[name])
    for name, data in files.items():
        (root / name).write_text(data)
        print(name, hashlib.sha256(data.encode()).hexdigest())


if __name__ == "__main__":
    main()
