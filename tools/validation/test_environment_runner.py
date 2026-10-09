import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('environment_benchmark', ROOT/'tools/environment_benchmark.py')
mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)

class RunnerTests(unittest.TestCase):
    def base_rows(self):
        return [dict(native_1080p_valid='1', display_width='1920', display_height='1080', render_width='1920', render_height='1080', render_scale='1.000000', quality='1', scene_status='2', render_frame_serial=str(100+i), gpu_sample_origin_frame=str(100+i-3) if i>=3 else 'NA', gpu_render_scene_ms='10' if i>=3 else 'NA', gpu_sample_id=str(i+1) if i>=3 else 'NA') for i in range(10)]
    def inspect(self, rows, expected=10):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)/'trace.csv'
            with p.open('w', newline='') as f:
                w = csv.DictWriter(f, fieldnames=list(self.base_rows()[0])); w.writeheader(); w.writerows(rows)
            return mod.inspect_csv(p, expected, 1)
    def test_valid_capture_is_not_fps_pass(self):
        r=self.inspect(self.base_rows()); self.assertEqual(r['capture_issues'], [])
        self.assertTrue(r['performance_verdict'].startswith('NOT_EVALUATED'))
    def test_wrong_native_or_quality_rejected(self):
        rows=self.base_rows(); rows[3]['render_scale']='0.750000'; rows[4]['quality']='0'
        self.assertEqual(len(self.inspect(rows)['capture_issues']), 2)
    def test_missing_repeated_or_late_gpu_rejected(self):
        rows=self.base_rows(); rows[-1]['gpu_sample_id']='NA'
        self.assertTrue(self.inspect(rows)['capture_issues'])
        rows=self.base_rows(); rows[-1]['gpu_sample_id']=rows[-2]['gpu_sample_id']
        self.assertTrue(self.inspect(rows)['capture_issues'])
    def test_wrong_gpu_origin_rejected(self):
        rows=self.base_rows(); rows[6]['gpu_sample_origin_frame']='999'
        self.assertTrue(self.inspect(rows)['capture_issues'])

    def test_short_capture_and_crash_rejected(self):
        rows=self.base_rows()[:-1]; rows[-1]['scene_status']='-1'
        self.assertGreaterEqual(len(self.inspect(rows)['capture_issues']), 2)
    def test_material_disabling_environment_cleared(self):
        for k in ['CLOUDSPLITOFF', 'SHMAPOFF', 'TSHOFF', 'NOTRF', 'WAKEOFF', 'MESHOFF', 'NOMARCH', 'NOEARLY']:
            self.assertIn(k, mod.DEBUG_ENV)
    def test_weather_bundle_avoids_resetting_presets(self):
        for scene in mod.CASES.values():
            if '@' in scene: self.assertTrue(scene.startswith('look_'))

if __name__ == '__main__': unittest.main()
