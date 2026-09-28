const state = {
  latest: null,
  lastEventAt: 0,
  pollInFlight: false,
};

const el = (id) => document.getElementById(id);

const FEATURED_KEYS = new Set([
  "ax", "ay", "az",
  "accelx", "accely", "accelz",
  "accelerationx", "accelerationy", "accelerationz",
  "gx", "gy", "gz",
  "gyrox", "gyroy", "gyroz",
  "roll", "rolldeg", "pitch", "pitchdeg", "yaw", "yawdeg",
  "si", "stabilityindex", "indicedeestabilidad", "indiceestabilidad",
]);

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

function fmt(value, digits = 3) {
  const numeric = numericValue(value);
  return numeric === null ? "--" : numeric.toFixed(digits);
}

function measurementValue(measurement, aliases) {
  const wanted = new Set(aliases.map(normalizedKey));
  const match = Object.entries(measurement || {}).find(([key]) =>
    wanted.has(normalizedKey(key))
  );
  return match ? match[1] : null;
}

function setDl(container, values) {
  container.innerHTML = "";
  Object.entries(values || {}).forEach(([key, value]) => {
    const dt = document.createElement("dt");
    const dd = document.createElement("dd");
    dt.textContent = key;
    dd.textContent = value === null || value === undefined ? "--" : String(value);
    container.append(dt, dd);
  });
}

function setCells(container, values) {
  container.innerHTML = "";
  Object.entries(values || {}).forEach(([key, value]) => {
    if (FEATURED_KEYS.has(normalizedKey(key))) return;
    const cell = document.createElement("div");
    const label = document.createElement("span");
    const strong = document.createElement("strong");
    cell.className = "detail-cell";
    label.textContent = key;
    strong.textContent = value === null || value === undefined ? "--" : String(value);
    cell.append(label, strong);
    container.append(cell);
  });
}

function setPrimarySensors(measurement) {
  el("accelX").textContent = fmt(measurementValue(measurement, ["ax", "accel_x", "acceleration_x"]), 3);
  el("accelY").textContent = fmt(measurementValue(measurement, ["ay", "accel_y", "acceleration_y"]), 3);
  el("accelZ").textContent = fmt(measurementValue(measurement, ["az", "accel_z", "acceleration_z"]), 3);
  el("gyroX").textContent = fmt(measurementValue(measurement, ["gx", "gyro_x"]), 3);
  el("gyroY").textContent = fmt(measurementValue(measurement, ["gy", "gyro_y"]), 3);
  el("gyroZ").textContent = fmt(measurementValue(measurement, ["gz", "gyro_z"]), 3);
  el("stabilityIndex").textContent = fmt(
    measurementValue(measurement, ["si", "stability_index", "indice_estabilidad"]),
    3
  );
}

function render(snapshot) {
  state.latest = snapshot;
  const telemetry = snapshot.telemetry || {};
  const orientation = telemetry.orientation || {};
  const measurement = telemetry.measurement || {};
  const age = snapshot.age_s;
  const online = typeof age === "number" && age < 3;

  el("connection").textContent = online
    ? `Recibiendo datos, última muestra hace ${age.toFixed(1)} s`
    : "Esperando telemetría UDP...";
  el("connection").className = online ? "online" : "stale";

  setPrimarySensors(measurement);
  el("roll").textContent = fmt(orientation.roll_deg, 2);
  el("pitch").textContent = fmt(orientation.pitch_deg, 2);
  el("yaw").textContent = fmt(orientation.yaw_deg, 2);

  el("recording").textContent = snapshot.recording
    ? `Grabando: ${snapshot.recording_name}`
    : "Parado";
  el("recordedRows").textContent = String(snapshot.recorded_rows || 0);
  el("commandStatus").textContent = snapshot.last_command_status || "--";
  el("startBtn").disabled = !!snapshot.recording;
  el("stopBtn").disabled = !snapshot.recording;

  setDl(el("gpsList"), telemetry.gps || {});
  const physics = snapshot.physics || telemetry.physics || {};
  setDl(el("physicsList"), {
    "D1 (m)": fmt(physics.d1_m ?? physics.d1, 6),
    "Ixx (kg·m²)": fmt(physics.ixx_kg_m2 ?? physics.ixx, 6),
    "FIc (deg)": fmt(physics.fic_deg, 4),
    "Coeff_SI": fmt(physics.coeff_si, 6),
    "Alfa (deg)": fmt(physics.alfa_deg, 4),
  });
  setCells(el("measurement"), measurement);
}

async function fetchState() {
  if (state.pollInFlight) return;
  state.pollInFlight = true;
  try {
    const response = await fetch("/api/state", {cache: "no-store"});
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    render(await response.json());
  } catch (_) {
    // EventSource will reconnect automatically; the status remains stale meanwhile.
  } finally {
    state.pollInFlight = false;
  }
}

async function postJson(url, payload = {}) {
  const response = await fetch(url, {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify(payload),
  });
  const data = await response.json();
  if (!response.ok || data.ok === false) {
    throw new Error(data.error || `HTTP ${response.status}`);
  }
  return data;
}

function measurementName() {
  return el("measurementName").value || "medida";
}

el("startBtn").addEventListener("click", async () => {
  try {
    await postJson("/api/start", {name: measurementName()});
  } catch (error) {
    alert(error.message);
  }
});

el("stopBtn").addEventListener("click", async () => {
  try {
    const data = await postJson("/api/stop", {name: measurementName()});
    alert(`Guardado: ${data.path}`);
  } catch (error) {
    alert(error.message);
  }
});

el("calibrateBtn").addEventListener("click", async () => {
  try {
    await postJson("/api/calibrate");
  } catch (error) {
    alert(error.message);
  }
});

el("configForm").addEventListener("submit", async (event) => {
  event.preventDefault();
  const form = new FormData(event.currentTarget);
  const payload = Object.fromEntries(
    Array.from(form.entries()).map(([key, value]) => [key, Number(value)])
  );
  try {
    const data = await postJson("/api/config", payload);
    render({
      ...(state.latest || {}),
      physics: {...payload, ...data.physics},
      telemetry: state.latest?.telemetry || null,
    });
  } catch (error) {
    alert(error.message);
  }
});

fetchState();

const events = new EventSource("/events");
events.onmessage = (event) => {
  try {
    render(JSON.parse(event.data));
    state.lastEventAt = Date.now();
  } catch (_) {
    fetchState();
  }
};
events.onerror = () => fetchState();

// If an SSE stream stays open but stops producing samples, polling recovers the UI.
setInterval(() => {
  if (Date.now() - state.lastEventAt > 2000) fetchState();
}, 1000);
