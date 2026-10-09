import math
from pathlib import Path
import tempfile
import unittest
from analyze_frames import analyze, summarize

class FrameTests(unittest.TestCase):
    def write_csv(self, text):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        path = Path(folder.name) / "frames.csv"
        path.write_text(text, encoding="utf-8-sig")
        return path

    def test_constant(self):
        result = summarize([10] * 100)
        self.assertEqual(result["aggregate_fps_from_intervals"], 100)
        self.assertEqual(result["p99_ms"], 10)
        self.assertEqual(result["one_percent_low_fps_slowest_mean"], 100)
        self.assertEqual(result["over_budget_count"], 0)

    def test_tail_is_not_p99_reciprocal(self):
        result = summarize([10] * 199 + [100])
        self.assertEqual(result["p99_ms"], 10)
        self.assertAlmostEqual(result["one_percent_low_fps_slowest_mean"], 1000 / 55)
        self.assertEqual(result["over_budget_count"], 1)
        self.assertEqual(result["at_least_100ms_count"], 1)

    def test_no_average_instantaneous_fps(self):
        result = summarize([10, 30])
        self.assertEqual(result["aggregate_fps_from_intervals"], 50)
        self.assertEqual(result["p95_ms"], 30)

    def test_invalid_samples(self):
        for values in ([], [0], [-1], [math.nan], [math.inf]):
            with self.assertRaises(ValueError):
                summarize(values)

    def test_missing_displayed_is_not_zero(self):
        path = self.write_csv("ProcessID,SwapChainAddress,DisplayedTime,MsBetweenPresents\n12,0,16,8\n12,0,NA,8\n12,0,0,8\n")
        r = analyze(path, "DisplayedTime")
        self.assertEqual(r["statistics"]["valid_intervals"], 1)
        self.assertEqual(r["unavailable_metric_rows"], 1)
        self.assertEqual(r["zero_metric_rows"], 1)
        self.assertTrue(r["native_1080p_60fps_verdict"].startswith("NOT_EVALUATED"))
        self.assertEqual(analyze(path, "msbetweenpresents")["statistics"]["valid_intervals"], 3)

    def test_multiple_streams_rejected(self):
        path = self.write_csv("ProcessID,SwapChainAddress,MsBetweenPresents\n12,0,10\n13,0,30\n")
        with self.assertRaises(ValueError): analyze(path, "MsBetweenPresents")
        self.assertEqual(analyze(path, "MsBetweenPresents", "12")["statistics"]["mean_ms"], 10)

    def test_invalid_csv_refuses_partial_result(self):
        path = self.write_csv("MsBetweenPresents\n10\nNaN\n")
        with self.assertRaises(ValueError): analyze(path, "MsBetweenPresents")

    def test_metric_required_and_missing_filter_column_rejected(self):
        path = self.write_csv("MsBetweenPresents\n10\n")
        with self.assertRaises(ValueError): analyze(path, "DisplayedTime")
        with self.assertRaises(ValueError): analyze(path, "MsBetweenPresents", "12")

    def test_duplicate_headers_rejected(self):
        path = self.write_csv("MsBetweenPresents,msbetweenpresents\n10,20\n")
        with self.assertRaises(ValueError): analyze(path, "MsBetweenPresents")

    def test_multiple_scene_streams_rejected(self):
        path = self.write_csv("scene,present_interval_ms\nair,10\nnight,20\n")
        with self.assertRaises(ValueError): analyze(path, "present_interval_ms")
        self.assertEqual(analyze(path, "present_interval_ms", scene="air")["statistics"]["mean_ms"], 10)

    def test_explicit_dropped_count_preserved(self):
        path = self.write_csv("ProcessID,SwapChainAddress,MsBetweenPresents,Dropped\n12,0,10,0\n12,0,15,1\n")
        r = analyze(path, "MsBetweenPresents")
        self.assertEqual(r["dropped_rows_if_explicit_column"], 1)
        self.assertEqual(r["statistics"]["valid_intervals"], 2)

if __name__ == "__main__": unittest.main()
