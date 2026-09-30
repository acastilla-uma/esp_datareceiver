const state = {
  latest: null,
  lastEventAt: 0,
  pollInFlight: false,
  viewMode: "live",
  historyRows: [],
  historyIndex: 0,
  playbackTimer: null,
};

const el = (id) => document.getElementById(id);
const core = window.DobackCore;

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
  return core.normalizedKey(value);
}

function numericValue(value) {
  return core.numericValue(value);
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

function scaledMeasurementValue(measurement, modernAliases, legacyAliases, legacyScale) {
  const modern = numericValue(measurementValue(measurement, modernAliases));
  if (modern !== null) return modern;
  const legacy = numericValue(measurementValue(measurement, legacyAliases));
  return legacy === null ? null : legacy * legacyScale;
}

function sensorValues(measurement) {
  return {
    axG: scaledMeasurementValue(measurement, ["ax_g", "accel_x_g", "acceleration_x_g"], ["ax", "accel_x", "acceleration_x"], 0.001),
    ayG: scaledMeasurementValue(measurement, ["ay_g", "accel_y_g", "acceleration_y_g"], ["ay", "accel_y", "acceleration_y"], 0.001),
    azG: scaledMeasurementValue(measurement, ["az_g", "accel_z_g", "acceleration_z_g"], ["az", "accel_z", "acceleration_z"], 0.001),
    gxDegS: scaledMeasurementValue(measurement, ["gx_deg_s", "gyro_x_deg_s", "gyro_x"], ["gx"], 0.001),
    gyDegS: scaledMeasurementValue(measurement, ["gy_deg_s", "gy_avg_deg_s", "gyavg_deg_s", "gyro_y_deg_s", "gyro_y"], ["gyavg", "gy"], 0.001),
    gzDegS: scaledMeasurementValue(measurement, ["gz_deg_s", "gyro_z_deg_s", "gyro_z"], ["gz"], 0.001),
  };
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

function renderGnss(snapshot) {
  const gnss = core.gnssSnapshot(snapshot);
  el("gnssBadge").textContent = gnss.label;
  el("gnssBadge").className = gnss.badgeClass;
  el("gnssDetail").textContent = gnss.detail;
  setDl(el("gnssOverview"), gnss.overview);
  setDl(el("gnssPosition"), gnss.position);
  setDl(el("gnssPrecision"), gnss.precision);
  setDl(el("gnssMotion"), gnss.motion);
  setDl(el("gnssConnectivity"), gnss.connectivity);
}

function setPrimarySensors(measurement) {
  const sensors = sensorValues(measurement);
  el("accelX").textContent = fmt(sensors.axG, 3);
  el("accelY").textContent = fmt(sensors.ayG, 3);
  el("accelZ").textContent = fmt(sensors.azG, 3);
  el("gyroX").textContent = fmt(sensors.gxDegS, 3);
  el("gyroY").textContent = fmt(sensors.gyDegS, 3);
  el("gyroZ").textContent = fmt(sensors.gzDegS, 3);
  el("rawStabilityIndex").textContent = fmt(
    measurementValue(measurement, ["si", "stability_index", "indice_estabilidad"]),
    3
  );
  return sensors;
}

function configValues() {
  const form = el("configForm");
  const values = Object.fromEntries(new FormData(form).entries());
  return Object.fromEntries(Object.entries(values).map(([key, value]) => [key, Number(value)]));
}

function resolvedPhysics(rawPhysics = {}) {
  const form = configValues();
  const number = (...values) => values.map(numericValue).find((value) => value !== null);
  const positive = (...values) => values.map(numericValue).find((value) => value !== null && value > 0);
  const trackWidthM = positive(rawPhysics.track_width_m, numericValue(rawPhysics.s) === null ? null : numericValue(rawPhysics.s) / 1000, form.track_width_m);
  const cgHeightM = positive(rawPhysics.cg_height_m, form.cg_height_m);
  const d1M = positive(rawPhysics.d1_m, rawPhysics.d1, Math.sqrt(cgHeightM ** 2 + (trackWidthM / 2) ** 2));
  const ficDeg = positive(rawPhysics.fic_deg, Math.atan(trackWidthM / (2 * cgHeightM)) * 180 / Math.PI);
  const alphaDeg = positive(rawPhysics.alfa_deg, rawPhysics.alpha, 90 - ficDeg);
  const alphaMarginDeg = number(rawPhysics.alpha_margin_deg, form.alpha_margin_deg);
  const alphaVDeg = positive(rawPhysics.alphav_deg, rawPhysics.alphav, alphaDeg + alphaMarginDeg);
  const massKg = positive(rawPhysics.mass_kg, form.mass_kg);
  const inertia = number(rawPhysics.roll_inertia_kg_m2, form.roll_inertia_kg_m2);
  const ixx = positive(rawPhysics.ixx_kg_m2, rawPhysics.ixx, massKg * d1M ** 2 + inertia);
  return {
    trackWidthM, cgHeightM, d1M, ficDeg, alphaDeg, alphaVDeg, alphaMarginDeg,
    massKg, inertia, ixx,
    coeff: positive(rawPhysics.coeff_si, rawPhysics.coeff, 2 * massKg * 9.81 / ixx),
    k1: positive(rawPhysics.k1, form.k1),
    k2: positive(rawPhysics.k2, form.k2),
  };
}

function calculateStability(sensors, physics) {
  const heightM = Math.sqrt(physics.d1M ** 2 - (physics.trackWidthM / 2) ** 2);
  const hasStaticInputs = Number.isFinite(sensors.axG) && Number.isFinite(sensors.azG) && sensors.azG !== 0 && Number.isFinite(heightM) && heightM > 0;
  const phiRad = hasStaticInputs ? Math.abs(Math.atan(sensors.axG / sensors.azG)) : null;
  const phiCritRad = hasStaticInputs ? Math.atan((physics.trackWidthM / 2) / heightM) : null;
  const staticTerm = phiRad !== null && phiCritRad > 0 ? physics.k1 * phiRad / phiCritRad : null;
  const omegaCritDegS = physics.coeff >= 0 && physics.trackWidthM > 0 && physics.alphaVDeg >= 0
    ? Math.sqrt(physics.coeff * physics.trackWidthM * physics.alphaVDeg / 4) * 360 / 6.28
    : null;
  const omegaDegS = Number.isFinite(sensors.gyDegS) ? Math.abs(sensors.gyDegS) : null;
  const dynamicTerm = omegaDegS !== null && omegaCritDegS > 0 ? physics.k2 * (omegaDegS / omegaCritDegS) ** 2 : null;
  return {heightM, phiRad, phiCritRad, staticTerm, omegaDegS, omegaCritDegS, dynamicTerm,
    si: staticTerm !== null && dynamicTerm !== null ? 1 - staticTerm - dynamicTerm : null};
}

function renderStabilityEquation(sensors, rawPhysics) {
  const physics = resolvedPhysics(rawPhysics);
  const result = calculateStability(sensors, physics);
  el("stabilityIndex").textContent = fmt(result.si, 3);
  el("calculatedStabilityIndex").textContent = fmt(result.si, 3);
  el("staticPenalty").textContent = fmt(result.staticTerm, 4);
  el("dynamicPenalty").textContent = fmt(result.dynamicTerm, 4);
  el("staticEquation").textContent = `${fmt(physics.k1, 3)} · ${fmt(result.phiRad === null ? null : result.phiRad * 180 / Math.PI, 2)}° / ${fmt(result.phiCritRad === null ? null : result.phiCritRad * 180 / Math.PI, 2)}°`;
  el("dynamicEquation").textContent = `${fmt(physics.k2, 3)} · (${fmt(result.omegaDegS, 3)} °/s / ${fmt(result.omegaCritDegS, 3)} °/s)²`;
  setDl(el("staticTerms"), {
    "φ = |atan(ax / az)|": `${fmt(result.phiRad === null ? null : result.phiRad * 180 / Math.PI, 3)} °`,
    "φcrit": `${fmt(result.phiCritRad === null ? null : result.phiCritRad * 180 / Math.PI, 3)} °`,
    "k₁": fmt(physics.k1, 3),
    "Penalización estática": fmt(result.staticTerm, 5),
  });
  setDl(el("dynamicTerms"), {
    "ω = |Gy|": `${fmt(result.omegaDegS, 4)} °/s`,
    "ωcrit": `${fmt(result.omegaCritDegS, 4)} °/s`,
    "k₂": fmt(physics.k2, 3),
    "Penalización dinámica": fmt(result.dynamicTerm, 7),
  });
  el("stabilitySource").textContent = Number.isFinite(sensors.gyDegS)
    ? "Modelo del firmware: Gy se expresa en °/s; ωcrit se calcula con los parámetros activos."
    : "Faltan Gy o parámetros válidos para calcular la parte dinámica.";
  return physics;
}

function render(snapshot) {
  state.latest = snapshot;
  const telemetry = snapshot.telemetry || {};
  const orientation = telemetry.orientation || {};
  const measurement = telemetry.measurement || {};
  const age = snapshot.age_s;
  const online = typeof age === "number" && age < 3;

  if (state.viewMode === "history") {
    el("connection").textContent = "Reproduciendo una muestra histórica del CSV";
    el("connection").className = "online";
  } else {
    el("connection").textContent = online
      ? `Recibiendo datos, última muestra hace ${age.toFixed(1)} s`
      : "Esperando telemetría UDP...";
    el("connection").className = online ? "online" : "stale";
  }

  const sensors = setPrimarySensors(measurement);
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

  renderGnss(snapshot);
  const physics = snapshot.physics || telemetry.physics || {};
  const resolved = renderStabilityEquation(sensors, physics);
  setDl(el("physicsList"), {
    "D1 (m)": fmt(resolved.d1M, 6),
    "Ixx (kg·m²)": fmt(resolved.ixx, 6),
    "FIc (°)": fmt(resolved.ficDeg, 4),
    "Coeff (s⁻²)": fmt(resolved.coeff, 6),
    "α (°)": fmt(resolved.alphaDeg, 4),
    "αv (°)": fmt(resolved.alphaVDeg, 4),
  });
  setCells(el("measurement"), measurement);
}

function updateHistoryControls() {
  const total = state.historyRows.length;
  const hasRows = total > 0;
  const atStart = state.historyIndex <= 0;
  const atEnd = state.historyIndex >= total - 1;
  el("sampleSlider").disabled = !hasRows;
  el("sampleSlider").max = String(Math.max(total - 1, 0));
  el("sampleSlider").value = String(hasRows ? state.historyIndex : 0);
  el("samplePosition").textContent = hasRows ? `${state.historyIndex + 1} / ${total}` : "0 / 0";
  el("previousSample").disabled = !hasRows || atStart;
  el("nextSample").disabled = !hasRows || atEnd;
  el("playSamples").disabled = !hasRows;
  el("playSamples").textContent = state.playbackTimer ? "Ⅱ Pausar" : "▶ Reproducir";
}

function renderHistoryFrame() {
  if (!state.historyRows.length) return;
  const frame = state.historyRows[state.historyIndex];
  render(frame);
  el("historyTimestamp").textContent = frame.historyTimestamp;
  updateHistoryControls();
}

function stopPlayback() {
  if (state.playbackTimer) window.clearInterval(state.playbackTimer);
  state.playbackTimer = null;
  updateHistoryControls();
}

function setViewMode(mode) {
  state.viewMode = mode;
  if (mode === "live") {
    stopPlayback();
    el("liveTab").classList.add("is-active");
    el("historyTab").classList.remove("is-active");
    el("liveTab").setAttribute("aria-selected", "true");
    el("historyTab").setAttribute("aria-selected", "false");
    el("historyPanel").classList.add("is-hidden");
    fetchState();
  } else {
    el("liveTab").classList.remove("is-active");
    el("historyTab").classList.add("is-active");
    el("liveTab").setAttribute("aria-selected", "false");
    el("historyTab").setAttribute("aria-selected", "true");
    el("historyPanel").classList.remove("is-hidden");
    if (state.historyRows.length) renderHistoryFrame();
  }
}

async function loadCsv(file) {
  stopPlayback();
  try {
    const rows = core.parseCsv(await file.text());
    state.historyRows = rows.map((row, index) => core.csvRowToSnapshot(row, index, rows.length));
    state.historyIndex = 0;
    el("historyStatus").textContent = `${file.name}: ${rows.length} muestras cargadas localmente.`;
    setViewMode("history");
    renderHistoryFrame();
  } catch (error) {
    state.historyRows = [];
    state.historyIndex = 0;
    updateHistoryControls();
    el("historyStatus").textContent = error.message || "No se pudo leer el CSV.";
    el("historyTimestamp").textContent = "Sin muestra seleccionada";
  }
}

async function fetchState() {
  if (state.viewMode === "history") return;
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

el("liveTab").addEventListener("click", () => setViewMode("live"));
el("historyTab").addEventListener("click", () => setViewMode("history"));
el("csvFile").addEventListener("change", (event) => {
  const [file] = event.currentTarget.files;
  if (file) loadCsv(file);
});
el("previousSample").addEventListener("click", () => {
  state.historyIndex = Math.max(0, state.historyIndex - 1);
  renderHistoryFrame();
});
el("nextSample").addEventListener("click", () => {
  state.historyIndex = Math.min(state.historyRows.length - 1, state.historyIndex + 1);
  renderHistoryFrame();
});
el("sampleSlider").addEventListener("input", (event) => {
  state.historyIndex = Number(event.currentTarget.value);
  renderHistoryFrame();
});
el("playSamples").addEventListener("click", () => {
  if (state.playbackTimer) {
    stopPlayback();
    return;
  }
  if (!state.historyRows.length) return;
  if (state.historyIndex >= state.historyRows.length - 1) state.historyIndex = 0;
  state.playbackTimer = window.setInterval(() => {
    if (state.historyIndex >= state.historyRows.length - 1) {
      stopPlayback();
      return;
    }
    state.historyIndex += 1;
    renderHistoryFrame();
  }, 250);
  updateHistoryControls();
});

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
    await postJson("/api/start", {name: measurementName(), physics: configValues()});
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
  const payload = configValues();
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
  if (state.viewMode === "history") return;
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
