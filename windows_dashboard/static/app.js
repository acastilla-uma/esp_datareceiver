const state = {
  latest: null,
};

const el = (id) => document.getElementById(id);

function fmt(value, digits = 3) {
  if (typeof value !== "number" || !Number.isFinite(value)) return "--";
  return value.toFixed(digits);
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
    const cell = document.createElement("div");
    const label = document.createElement("span");
    const strong = document.createElement("strong");
    cell.className = "cell";
    label.textContent = key;
    strong.textContent = value === null || value === undefined ? "--" : String(value);
    cell.append(label, strong);
    container.append(cell);
  });
}

function render(snapshot) {
  state.latest = snapshot;
  const telemetry = snapshot.telemetry || {};
  const orientation = telemetry.orientation || {};
  const age = snapshot.age_s;
  const online = typeof age === "number" && age < 3;

  el("connection").textContent = online
    ? `Recibiendo datos, última muestra hace ${age.toFixed(1)} s`
    : "Esperando telemetría UDP...";
  el("connection").className = online ? "" : "stale";

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
  setCells(el("measurement"), telemetry.measurement || {});
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
  const payload = Object.fromEntries(Array.from(form.entries()).map(([key, value]) => [key, Number(value)]));
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

fetch("/api/state")
  .then((response) => response.json())
  .then(render)
  .catch(() => {});

const events = new EventSource("/events");
events.onmessage = (event) => render(JSON.parse(event.data));
