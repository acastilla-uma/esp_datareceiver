import csv
import math
import tempfile
import unittest
from pathlib import Path

from server import compute_physics, sanitize_measurement_name, write_csv_atomic


class PhysicsTest(unittest.TestCase):
    def test_compute_physics_matches_excel_formulas(self):
        result = compute_physics(50.0, 0.47, 0.25, 0.0)
        d1 = math.sqrt(0.25**2 + (0.47 / 2.0) ** 2)
        ixx = 50.0 * d1**2
        fic = math.atan(0.47 / (2.0 * 0.25)) * 180.0 / math.pi
        self.assertAlmostEqual(result["d1_m"], d1)
        self.assertAlmostEqual(result["ixx_kg_m2"], ixx)
        self.assertAlmostEqual(result["fic_deg"], fic)
        self.assertAlmostEqual(result["coeff_si"], 2.0 * 50.0 * 9.81 / ixx)
        self.assertAlmostEqual(result["alfa_deg"], 90.0 - fic)

    def test_rejects_invalid_physics_values(self):
        with self.assertRaises(ValueError):
            compute_physics(0.0, 0.47, 0.25, 0.0)


class StorageTest(unittest.TestCase):
    def test_sanitize_measurement_name(self):
        self.assertEqual(sanitize_measurement_name("../Prueba 01:*"), "Prueba_01")
        self.assertTrue(sanitize_measurement_name("   "))

    def test_write_csv_atomic(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "sample.csv"
            write_csv_atomic(path, [{"a": 1, "b": 2}, {"b": 3, "c": 4}])
            with path.open(newline="", encoding="utf-8") as fh:
                rows = list(csv.DictReader(fh))
            self.assertEqual(rows[0]["a"], "1")
            self.assertEqual(rows[1]["b"], "3")
            self.assertIn("c", rows[1])


if __name__ == "__main__":
    unittest.main()
