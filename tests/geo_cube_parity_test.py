"""Live C++/offline-producer cube contract, including all integer apron owners.

Exact transcendental tile boundaries can round to either adjacent tile across
libm implementations. Test deterministic exact vector ties and integer sample
ownership; random interiors and sample centres avoid that meaningless ambiguity.
"""
from pathlib import Path
import math
import random
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "earth"))
import cube


def main():
    queries, expected = [], []

    def add(query, values, integers=0, tolerance=2e-12):
        queries.append(query)
        expected.append((values, integers, tolerance))

    for face_number, face in enumerate(cube.FACES):
        for u in (-1, -.8, -.2, 0, .1, .5, 1):
            for v in (-1, -.75, -.01, 0, .125, .9, 1):
                add(f"F {face_number} {u} {v}", cube.face_uv_to_direction(face, u, v))
        for level in (0, 1, 6, 16):
            n = 1 << level
            for x in sorted({0, n // 2, n - 1}):
                for y in sorted({0, n // 2, n - 1}):
                    tile = cube.Tile(face, level, x, y)
                    for col in (-1, 0, 128, 255, 256):
                        for row in (-1, 0, 128, 255, 256):
                            f, i, j = cube.sample_location(tile, col, row)
                            count = 256 * n
                            direction = cube.face_uv_to_direction(f, 2 * (i + .5) / count - 1,
                                                                  2 * (j + .5) / count - 1)
                            add(f"S {face_number} {level} {x} {y} {col} {row}",
                                (cube.FACES.index(f), i, j, *direction), 3)
    # Exact dominant-axis ties must obey the same fixed face priority.
    for x in (-1, 0, 1):
        for y in (-1, 0, 1):
            for z in (-1, 0, 1):
                if not (x or y or z):
                    continue
                for level in (0, 6, 16):
                    f, u, v = cube.direction_to_face_uv((x, y, z))
                    n = 1 << level
                    tx, ty = (u + 1) * n / 2, (v + 1) * n / 2
                    ix, iy = min(n - 1, int(tx)), min(n - 1, int(ty))
                    add(f"D {x} {y} {z} {level}",
                        (cube.FACES.index(f), ix, iy, tx - ix, ty - iy), 3, 2e-10)
    rng = random.Random(187)
    latlons = [(lat, lon) for lat in (-90, -89.999999, 0, 32.5, 89.999999, 90)
               for lon in (-180, -179.99999, -58, 0, 179.99999, 180)]
    latlons += [(rng.uniform(-90, 90), rng.uniform(-180, 180)) for _ in range(3000)]
    for lat, lon in latlons:
        level = rng.choice((0, 1, 6, 16))
        tile = cube.tile_for_latlon(lat, lon, level)
        add(f"L {lat:.17g} {lon:.17g} {level}",
            (cube.FACES.index(tile.face), tile.x, tile.y), 3)

    run = subprocess.run([sys.argv[1], "--cube-probe"], input="\n".join(queries) + "\n",
                         text=True, capture_output=True, check=True)
    lines = run.stdout.splitlines()
    assert len(lines) == len(expected), (len(lines), len(expected), run.stderr)
    maximum = 0
    for query, line, (want, integers, tolerance) in zip(queries, lines, expected):
        got = tuple(map(float, line.split()))
        assert len(got) == len(want), (query, got, want)
        for index, (a, b) in enumerate(zip(got, want)):
            assert math.isfinite(a), (query, got)
            if index < integers:
                assert a == b, (query, got, want)
            else:
                error = abs(a - b)
                maximum = max(maximum, error)
                assert error <= tolerance, (query, got, want, error)
    print(f"Earth cube parity: {len(queries)} live C++/Python queries passed; "
          f"integer apron/tile ownership exact, maximum numeric difference {maximum:.3g}")


if __name__ == "__main__":
    main()
