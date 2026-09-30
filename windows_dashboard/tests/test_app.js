const assert = require("assert");
const core = require("../static/app_core.js");

const csv = [
  "received_utc,gps.available,gps.delta_ms,gps.fix,gps.rtk,gps.latitude_deg,gps.longitude_deg,gnss_status.device_connected,gnss_status.ntrip_host,gnss_status.ntrip_state,gnss_status.ntrip_mountpoint,gnss_status.rtcm_message_type,gnss_status.rtcm_used,gnss_status.rtcm_crc_failed,measurement.ax",
  "2026-09-30T10:00:00Z,true,200,RTK_FIXED,FIXED,40.4168,-3.7038,true,ergnss-tr.ign.es,STREAMING,VRS3M,1077,true,false,10",
].join("\n");

const rows = core.parseCsv(csv);
assert.strictEqual(rows.length, 1);
assert.strictEqual(rows[0]["gnss_status.ntrip_mountpoint"], "VRS3M");

const snapshot = core.csvRowToSnapshot(rows[0], 0, rows.length);
assert.strictEqual(snapshot.telemetry.gps.delta_ms, "200");
assert.strictEqual(snapshot.telemetry.gnss_status.ntrip_state, "STREAMING");

const gnss = core.gnssSnapshot(snapshot);
assert.strictEqual(gnss.state, "fixed");
assert.strictEqual(gnss.label, "RTK fijo");
assert.strictEqual(gnss.overview["Δ estabilidad-GNSS"], "200 ms");
assert.strictEqual(gnss.connectivity["Caster"], "ergnss-tr.ign.es:VRS3M");
assert.strictEqual(gnss.connectivity["Tipo RTCM"], "1077");
assert.strictEqual(gnss.connectivity["RTCM utilizado"], "Sí");
assert.strictEqual(gnss.connectivity["CRC RTCM"], "Válido");

const stale = core.gnssSnapshot({
  age_s: 0,
  telemetry: {
    gps: {available: false},
    gnss_status: {device_connected: true, solution_age_ms: 2501},
  },
});
assert.strictEqual(stale.state, "stale");

const semicolon = core.parseCsv("gps.fix;gnss_status.ntrip_state\nRTK_FLOAT;STREAMING\n");
assert.strictEqual(semicolon[0]["gps.fix"], "RTK_FLOAT");

console.log("app_core tests ok");
