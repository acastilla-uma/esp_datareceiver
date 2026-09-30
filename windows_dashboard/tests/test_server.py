import csv
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from server import (
    AppState,
    compute_physics,
    flatten_telemetry,
    sanitize_measurement_name,
    write_csv_atomic,
)


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

    def test_includes_live_stability_parameters(self):
        result = compute_physics(50.0, 0.47, 0.25, 0.0, 3.0, 1.15, 2.05)
        self.assertAlmostEqual(result["alphav_deg"], result["alfa_deg"] + 3.0)
        self.assertEqual(result["k1"], 1.15)
        self.assertEqual(result["k2"], 2.05)


class LiveUpdatesTest(unittest.TestCase):
    def test_slow_client_stays_registered_and_receives_latest_snapshot(self):
        with tempfile.TemporaryDirectory() as folder:
            state = AppState(Path(folder), "auto", 50101)
            client = state.register_client()

            for sequence in range(20):
                state.update_telemetry(
                    {"type": "telemetry", "sequence": sequence}, "192.168.8.174"
                )

            self.assertIn(client, state.clients)
            latest = json.loads(client.get_nowait())
            self.assertEqual(latest["telemetry"]["sequence"], 19)
            self.assertTrue(client.empty())

            state.unregister_client(client)
            self.assertNotIn(client, state.clients)


class StorageTest(unittest.TestCase):
    def test_flatten_telemetry_includes_js_calculated_values_and_parameters(self):
        telemetry = {
            "sequence": 166,
            "measurement": {"ax": -457.5, "az": 841.07, "gy": -16493.05},
        }
        physics = {
            "mass_kg": 30.0,
            "track_width_m": 0.47,
            "cg_height_m": 0.25,
            "roll_inertia_kg_m2": 0.0,
            **compute_physics(30.0, 0.47, 0.25, 0.0),
        }
        row = flatten_telemetry(telemetry, physics)
        self.assertAlmostEqual(row["calculated.ax_g"], -0.4575)
        self.assertAlmostEqual(row["calculated.gy_deg_s"], -16.49305)
        self.assertAlmostEqual(row["js.k1"], 1.15)
        self.assertIsNotNone(row["calculated.si_js"])

    def test_flatten_telemetry_includes_gnss_status_columns(self):
        telemetry = {
            "sequence": 3,
            "gps": {
                "available": True,
                "delta_ms": 200,
                "fix": "RTK_FIXED",
                "latitude_deg": 40.4168,
                "longitude_deg": -3.7038,
            },
            "gnss_status": {
                "device_connected": True,
                "port": "/dev/serial/by-id/usb-u-blox",
                "ntrip_state": "STREAMING",
                "ntrip_host": "ergnss-tr.ign.es",
                "ntrip_mountpoint": "VRS3M",
                "rtcm_age_ms": 120,
            },
        }
        row = flatten_telemetry(telemetry, compute_physics(50.0, 0.47, 0.25, 0.0))
        self.assertTrue(row["gps.available"])
        self.assertEqual(row["gps.delta_ms"], 200)
        self.assertEqual(row["gnss_status.ntrip_state"], "STREAMING")
        self.assertEqual(row["gnss_status.ntrip_mountpoint"], "VRS3M")

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
