#!/usr/bin/env python3
"""Dependency-free release-wiring guards; payload/ZIP correctness is tested separately.

These source contracts deliberately fail if CI stops baking every release input,
validating against the consumer build, or waiting for existing release gates.
They do not claim a real driver bake or Windows runtime acceptance.
"""
from pathlib import Path
import re
import unittest

WORKFLOW = (Path(__file__).resolve().parents[1] / '.github/workflows/build.yml').read_text(encoding='utf-8')


def job(name):
    match = re.search(r'^  ' + re.escape(name) + r':[^\n]*\n(.*?)(?=^  [\w-]+:|\Z)', WORKFLOW, re.M | re.S)
    if not match:
        raise AssertionError(f'Missing CI job: {name}')
    return match.group(1)


class AircraftMeshWorkflowTests(unittest.TestCase):
    def test_fresh_strict_software_producer(self):
        producer = job('aircraft-meshes')
        self.assertIn('runs-on: ubuntu-24.04', producer)
        self.assertIn('libegl1 libgl1-mesa-dri libegl-mesa0', producer)
        self.assertIn('-DCMAKE_BUILD_TYPE=Release', producer)
        self.assertIn('--target aircraft_mesh_export --parallel 1', producer)
        self.assertIn("LIBGL_ALWAYS_SOFTWARE: '1'", producer)
        self.assertIn('GALLIUM_DRIVER: llvmpipe', producer)
        self.assertIn('run: build-mesh/aircraft_mesh_export out/aircraft-raw', producer)
        for forbidden in ('actions/cache@', 'actions/download-artifact@', 'NV_SAFE_GEAR:', 'AF_ALL:', 'CLIPDBG:', 'continue-on-error:', '|| true'):
            self.assertNotIn(forbidden, producer)

    def test_producer_validation_precedes_narrow_artifact(self):
        producer = job('aircraft-meshes')
        bundle = 'mesh_assets.py bundle --input out/aircraft-raw --source-manifest build-mesh/gen/aircraft_geometry_source.json --output out/SolaceExpress-aircraft.zip'
        validate = 'mesh_assets.py validate --input out/aircraft-raw --source-manifest build-mesh/gen/aircraft_geometry_source.json'
        self.assertLess(producer.index('aircraft_mesh_export out/aircraft-raw'), producer.index(bundle))
        self.assertLess(producer.index(bundle), producer.index(validate))
        self.assertLess(producer.index(validate), producer.index('actions/upload-artifact@'))
        self.assertIn('name: SolaceExpress-aircraft', producer)
        self.assertIn('path: out/SolaceExpress-aircraft.zip', producer)
        self.assertIn('if-no-files-found: error', producer)
        self.assertIn('python tools/mesh_assets/test_mesh_assets.py', producer)

    def test_consumer_checks_its_own_source_before_packaging(self):
        consumer = job('windows')
        self.assertIn('needs: [aircraft-meshes]', consumer)
        self.assertIn('name: SolaceExpress-aircraft\n          path: mesh-bundle', consumer)
        extract = 'mesh_assets.py extract --input mesh-bundle/SolaceExpress-aircraft.zip --source-manifest build/gen/aircraft_geometry_source.json --output dist/SolaceExpress/aircraft'
        validate = 'mesh_assets.py validate --input dist/SolaceExpress/aircraft --source-manifest build/gen/aircraft_geometry_source.json'
        self.assertLess(consumer.index('cmake --build build --config Release'), consumer.index(extract))
        self.assertLess(consumer.index('actions/download-artifact@'), consumer.index(extract))
        self.assertLess(consumer.index(extract), consumer.index(validate))
        self.assertLess(consumer.index(validate), consumer.index('Compress-Archive'))
        self.assertIn('Compress-Archive -Path dist/SolaceExpress/*', consumer)
        self.assertNotIn('Expand-Archive', consumer)

    def test_consumer_and_producer_have_identical_event_scope(self):
        event = lambda text: re.search(r'^    if: (.*)$', text, re.M).group(1)
        self.assertEqual(event(job('aircraft-meshes')), event(job('windows')))
        self.assertIn("github.event_name != 'schedule'", event(job('windows')))

    def test_existing_selection_and_sanitizer_shards_remain(self):
        sanitizers = job('linux-sanitizers')
        self.assertIn('shard: [1, 2, 3]', sanitizers)
        self.assertIn('--shards 3 --shard ${{ matrix.shard }}', sanitizers)
        self.assertIn('-DSANITIZE=ON', sanitizers)
        self.assertIn('python tools/ci/select_tests.py --build-dir build --config Release', job('windows'))
        self.assertIn("if: steps.pick.outputs.mode != 'none'", job('windows'))

    def test_release_waits_for_bake_and_existing_tests(self):
        release = job('release')
        self.assertIn('needs: [linux-sanitizers, aircraft-meshes, windows]', release)
        self.assertIn('files: dist/SolaceExpress-windows-x64.zip', release)


if __name__ == '__main__':
    unittest.main()
