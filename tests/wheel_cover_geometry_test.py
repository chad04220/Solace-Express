#!/usr/bin/env python3
"""Source-bound wheel-pants geometry contract for every actual gear-0 aircraft.

Checks closed side covers, exposed contact tread, nose-part extraction bounds,
and left/right steering alignment. This is CPU geometry evidence, not a raster
or hardware-performance test. Formula guards fail closed if the shader changes.
"""
import hashlib
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NUMBER = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
paths = [ROOT / name for name in (
    "src/models.cpp", "src/shaders/plane_sdf.glsl",
    "src/shaders/plane_parts.glsl", "src/aircraft_mesh_build_fleet.h")]
model_source, sdf_source, parts_source, mesh_source = [p.read_text() for p in paths]
compact = lambda s: re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", s))
sdf, parts, mesh = map(compact, (sdf_source, parts_source, mesh_source))

def numbers(s):
    return [float(x.rstrip("f")) for x in re.findall(NUMBER + r"f?", s)]

def capture(pattern, source):
    match = re.search(pattern, source)
    assert match, "Shader geometry changed: reconcile wheel-cover mirror: " + pattern
    return tuple(map(float, match.groups()))

# Read all production records, rather than assuming only the two current users.
covered = []
for index, block in enumerate(model_source.split("// ----------------------------------------------------------------")[1:]):
    body = re.sub(r"//[^\n]*", "", block.split("\n", 1)[1])
    values = numbers(body.split("vec3", 1)[0])
    assert len(values) == 78, "ModelDef layout changed; reconcile parser"
    if int(values[69]) == 0:
        covered.append((index, block.split("\n", 1)[0].strip(), values[70]))
assert covered, "No actual gear-0 records were found"

n = "(" + NUMBER + ")"
main = capture(r"sdEllipsoid\(ap-wc-vec3\(0\.0," + n + "," + n +
               r"\),vec3\(" + n + r",wr\*" + n + r",wr\*" + n + r"\)\)", sdf)
nose = capture(r"sdEllipsoid\(l-nc-vec3\(0\.0," + n + "," + n +
               r"\),vec3\(" + n + r",nwr\*" + n + r",nwr\*" + n + r"\)\)", sdf)
main_cut, = capture(r"spats=max\(sp,-\(ap\.y-\(wc\.y-wr\*" + n + r"\)\)\)", sdf)
nose_cut, = capture(r"sp=max\(sp,-\(l\.y-\(nc\.y-nwr\*" + n + r"\)\)\)", sdf)
main_half, = capture(r"sdRoundCylX\(ap-wc,wr," + n + r",0\.04\)", sdf)
nose_half, = capture(r"sdRoundCylX\(l-nc,nwr," + n + r",0\.035\)", sdf)
nose_scale, = capture(r"gM\[18\]\.y\*" + n + r";\}", parts)
assert "if(gtype!=0)res=gearWheelDetails(isAtlas()?" in sdf
assert "if(gtype!=0)res=gearWheelDetails(ap-wc," in sdf
assert "X.R=partRxz(gPS.z);X.T=vec3(0.0,0.0,gM[18].w);" in parts
assert "gearNoseShape(transpose(X.R)*(p-X.T)/ns)" in sdf
assert "if(k==PT_GEAR_NOSE)returngearNoseShape(l);" in sdf
assert "casePT_GEAR_NOSE:lo=vec3(-0.45f,-gh-0.1f,-1.0f);hi=vec3(0.45f,1.5f,1.0f);" in mesh

def ellipsoid(point, radii):
    k0 = math.sqrt(sum((a / b) ** 2 for a, b in zip(point, radii)))
    k1 = math.sqrt(sum((a / (b * b)) ** 2 for a, b in zip(point, radii)))
    return k0 * (k0 - 1) / k1 if k1 > 1e-5 else -min(radii)

checks = 0
results = []
for index, label, wheel_radius in covered:
    for is_nose in (False, True):
        radius = wheel_radius * nose_scale if is_nose else wheel_radius
        dy, dz, width, height_scale, length_scale = nose if is_nose else main
        half = nose_half if is_nose else main_half
        cut = nose_cut if is_nose else main_cut
        def cover(p):
            return max(ellipsoid((p[0], p[1] - dy, p[2] - dz),
                                (width, height_scale * radius, length_scale * radius)),
                       -(p[1] + cut * radius))
        assert 0 < cut < 1, "Cover must expose tread while covering the wheel centre"
        assert cover((0, -radius, 0)) >= (1 - cut) * radius - 1e-9
        checks += 2
        margin = float("inf")
        for side in (-1, 1):
            for radial in (0, .15 * radius, .35 * radius, .48 * radius):
                for degrees in range(0, 360, 5):
                    angle = math.radians(degrees)
                    p = (side * half, math.cos(angle) * radial, math.sin(angle) * radial)
                    distance = cover(p)
                    assert distance < 0, (label, is_nose, "exposed central sidewall", p)
                    margin = min(margin, -distance)
                    checks += 1
                    if is_nose:
                        # Production steering is +/- .45 rad; include additional margin.
                        for steer in (-.6, -.45, 0, .45, .6):
                            c, s = math.cos(steer), math.sin(steer)
                            world = (c * p[0] - s * p[2], p[1], s * p[0] + c * p[2])
                            local = (c * world[0] + s * world[2], world[1], -s * world[0] + c * world[2])
                            assert abs(cover(local) - distance) < 1e-12
                            checks += 1
        if is_nose:
            # Extraction happens in the unsteered part's frame; steering is a later pose.
            assert width < .45 and abs(dz) + length_scale * radius < 1
            checks += 1
        results.append(dict(model=index, label=label, wheel="nose" if is_nose else "main",
                            radius_m=radius, cover_half_width_m=width,
                            minimum_sidewall_cover_m=margin,
                            exposed_vertical_tread_m=(1 - cut) * radius))

print(json.dumps(dict(checks=checks, failures=0, covered_wheels=results,
                     source_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
                     limits="CPU/source geometry and steering contract; native raster inspection remains separate."), indent=2))
