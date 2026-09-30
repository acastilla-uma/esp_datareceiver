(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.DobackCore = api;
  }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  function normalizedKey(value) {
    return String(value)
      .trim()
      .toLowerCase()
      .normalize("NFD")
      .replace(/[\u0300-\u036f]/g, "")
      .replace(/[^a-z0-9]/g, "");
  }

  function numericValue(value) {
    if (typeof value === "number") return Number.isFinite(value) ? value : null;
    if (typeof value !== "string" || value.trim() === "") return null;
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : null;
  }

  function truthyValue(value) {
    if (typeof value === "boolean") return value;
    if (typeof value === "number") return value !== 0;
    const normalized = String(value || "").trim().toLowerCase();
    return ["1", "true", "yes", "si", "sí", "available"].includes(normalized);
  }

  function parseCsv(text) {
    const firstLine = text.split(/\r?\n/, 1)[0];
    const delimiter = (firstLine.match(/;/g) || []).length >
      (firstLine.match(/,/g) || []).length ? ";" : ",";
    const rows = [];
    let row = [];
    let field = "";
    let quoted = false;

    for (let index = 0; index < text.length; index += 1) {
      const character = text[index];
      const next = text[index + 1];
      if (character === '"') {
        if (quoted && next === '"') {
          field += '"';
          index += 1;
        } else {
          quoted = !quoted;
        }
      } else if (character === delimiter && !quoted) {
        row.push(field);
        field = "";
      } else if ((character === "\n" || character === "\r") && !quoted) {
        if (character === "\r" && next === "\n") index += 1;
        row.push(field);
        if (row.some((value) => value.trim() !== "")) rows.push(row);
        row = [];
        field = "";
      } else {
        field += character;
      }
    }
    if (field !== "" || row.length) {
      row.push(field);
      if (row.some((value) => value.trim() !== "")) rows.push(row);
    }
    if (rows.length < 2) {
      throw new Error("El CSV no contiene una cabecera y muestras válidas.");
    }

    const headers = rows[0].map((header, index) =>
      header.replace(/^\uFEFF/, "").trim() || `columna_${index + 1}`,
    );
    return rows.slice(1).map((values) => Object.fromEntries(
      headers.map((header, index) => [header, (values[index] || "").trim()])
    )).filter((record) => Object.values(record).some((value) => value !== ""));
  }

  function csvRowToSnapshot(row, index, total) {
    const telemetry = {
      measurement: {},
      orientation: {},
      gps: {},
      gnss_status: {},
      physics: {},
    };
    Object.entries(row).forEach(([key, value]) => {
      const separator = key.indexOf(".");
      if (separator < 0) return;
      const section = key.slice(0, separator);
      const field = key.slice(separator + 1);
      if (Object.prototype.hasOwnProperty.call(telemetry, section)) {
        telemetry[section][field] = value;
      }
    });
    const timestamp = row.doback_timestamp_utc || row.received_utc || row.timestamp || `Muestra ${index + 1}`;
    return {
      telemetry,
      age_s: null,
      recording: false,
      recorded_rows: total,
      last_command_status: "CSV histórico",
      historyTimestamp: timestamp,
    };
  }

  function displayValue(value, suffix, digits) {
    const numeric = numericValue(value);
    if (numeric === null) return "--";
    const precision = Number.isInteger(digits) ? digits : 3;
    return `${numeric.toFixed(precision)}${suffix || ""}`;
  }

  function textValue(value) {
    return value === null || value === undefined || value === "" ? "--" : String(value);
  }

  function classifyGnss(gps, status, sampleAgeS) {
    const available = truthyValue(gps.available);
    const deviceConnected = truthyValue(status.device_connected);
    const solutionAgeMs = numericValue(status.solution_age_ms);
    const staleBySolution = solutionAgeMs !== null && solutionAgeMs > 2000;
    const staleBySample = typeof sampleAgeS === "number" && sampleAgeS > 3;
    const fix = String(gps.fix || "").toUpperCase();
    const rtk = String(gps.rtk || "").toUpperCase();

    if (!deviceConnected && !available) {
      return {state: "disconnected", label: "Sin GNSS", detail: "Receptor no conectado"};
    }
    if (!available || staleBySolution || staleBySample) {
      return {state: "stale", label: "GNSS sin muestra válida", detail: "Fuera de ventana RTK/estabilidad"};
    }
    if (rtk === "FIXED" || fix === "RTK_FIXED") {
      return {state: "fixed", label: "RTK fijo", detail: "Solución centimétrica"};
    }
    if (rtk === "FLOAT" || fix === "RTK_FLOAT") {
      return {state: "float", label: "RTK flotante", detail: "Correcciones recibidas"};
    }
    return {state: "autonomous", label: "GNSS autónomo", detail: fix || "Sin RTK"};
  }

  function gnssSnapshot(snapshot) {
    const telemetry = snapshot && snapshot.telemetry ? snapshot.telemetry : {};
    const gps = telemetry.gps || {};
    const status = telemetry.gnss_status || {};
    const classification = classifyGnss(gps, status, snapshot ? snapshot.age_s : null);
    return {
      ...classification,
      badgeClass: `gnss-badge gnss-${classification.state}`,
      overview: {
        "Fix": textValue(gps.fix),
        "RTK": textValue(gps.rtk),
        "Satélites": textValue(gps.num_sats),
        "Δ estabilidad-GNSS": displayValue(gps.delta_ms, " ms", 0),
      },
      position: {
        "Latitud": displayValue(gps.latitude_deg, "°", 8),
        "Longitud": displayValue(gps.longitude_deg, "°", 8),
        "Altura elipsoidal": displayValue(gps.height_ellipsoid_m, " m", 3),
        "Altura MSL": displayValue(gps.height_msl_m, " m", 3),
      },
      precision: {
        "Horizontal": displayValue(gps.h_acc_m, " m", 3),
        "Vertical": displayValue(gps.v_acc_m, " m", 3),
        "PDOP": displayValue(gps.pdop, "", 2),
        "HDOP": displayValue(gps.hdop, "", 2),
        "VDOP": displayValue(gps.vdop, "", 2),
      },
      motion: {
        "Velocidad": displayValue(gps.speed_m_s, " m/s", 3),
        "Rumbo": displayValue(gps.heading_deg, "°", 2),
        "Edad corrección": displayValue(gps.correction_age_s, " s", 1),
        "Base RTCM": textValue(gps.base_station_id),
      },
      connectivity: {
        "USB": truthyValue(status.device_connected) ? "Conectado" : "Desconectado",
        "Puerto": textValue(status.port),
        "NTRIP": textValue(status.ntrip_state),
        "Caster": [status.ntrip_host, status.ntrip_port, status.ntrip_mountpoint]
          .filter((value) => value !== null && value !== undefined && value !== "")
          .join(":") || "--",
        "RTCM": displayValue(status.rtcm_age_ms, " ms", 0),
        "Bytes RTCM": textValue(status.rtcm_bytes),
        "Mensajes RTCM": textValue(status.rtcm_messages),
        "Tipo RTCM": textValue(status.rtcm_message_type),
        "RTCM utilizado": status.rtcm_used === true || status.rtcm_used === "true" ? "Sí" :
          status.rtcm_used === false || status.rtcm_used === "false" ? "No" : "--",
        "CRC RTCM": status.rtcm_crc_failed === true || status.rtcm_crc_failed === "true" ? "Error" :
          status.rtcm_crc_failed === false || status.rtcm_crc_failed === "false" ? "Válido" : "--",
        "Reconexiones": textValue(status.reconnects),
        "RF": [status.interference_state, status.jam_ind]
          .filter((value) => value !== null && value !== undefined && value !== "")
          .join(" / ") || "--",
        "Último error": textValue(status.last_error),
      },
    };
  }

  return {
    normalizedKey,
    numericValue,
    parseCsv,
    csvRowToSnapshot,
    classifyGnss,
    gnssSnapshot,
  };
});
