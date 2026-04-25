const state = {
  samples: [],
  maxPoints: 720,
  ws: null,
  wsConnected: false,
  pollTimer: null,
  reconnectTimer: null,
  current: null,
};

const els = {
  wsDot: document.getElementById("ws-dot"),
  wsState: document.getElementById("ws-state"),
  desiredMode: document.getElementById("desired-mode"),
  apSsid: document.getElementById("ap-ssid"),
  apIp: document.getElementById("ap-ip"),
  uptime: document.getElementById("uptime-badge"),
  breath: document.getElementById("breath-value"),
  heart: document.getElementById("heart-value"),
  distance: document.getElementById("distance-value"),
  presence: document.getElementById("presence-value"),
  movementLabel: document.getElementById("movement-label"),
  confidence: document.getElementById("confidence-value"),
  sensorBaud: document.getElementById("sensor-baud"),
  historyPoints: document.getElementById("history-points"),
  binaryFrames: document.getElementById("binary-frames"),
  textFrames: document.getElementById("text-frames"),
  parseErrors: document.getElementById("parse-errors"),
  lastSample: document.getElementById("last-sample"),
  fallState: document.getElementById("fall-state"),
  fallBreakHeight: document.getElementById("fall-break-height"),
  staticResidency: document.getElementById("static-residency"),
  modeSleep: document.getElementById("mode-sleep"),
  modeFall: document.getElementById("mode-fall"),
  clearLog: document.getElementById("clear-log"),
  chart: document.getElementById("trend-chart"),
};

const ctx = els.chart.getContext("2d");

function fmt(value, digits = 1, suffix = "") {
  if (value === null || value === undefined || Number.isNaN(value)) return "--";
  return `${Number(value).toFixed(digits)}${suffix}`;
}

function uptimeLabel(ms) {
  if (!Number.isFinite(ms)) return "--";
  const total = Math.floor(ms / 1000);
  const hours = String(Math.floor(total / 3600)).padStart(2, "0");
  const minutes = String(Math.floor((total % 3600) / 60)).padStart(2, "0");
  const seconds = String(total % 60).padStart(2, "0");
  return `${hours}:${minutes}:${seconds}`;
}

function setWsState(online) {
  state.wsConnected = online;
  els.wsDot.classList.toggle("online", online);
  els.wsState.textContent = online ? "Live WebSocket" : "Reconnecting";
}

function setPollingState() {
  if (state.wsConnected) return;
  els.wsDot.classList.add("online");
  els.wsState.textContent = "Live Polling";
}

function renderLiveState(current, summary = {}) {
  if (!current) return;

  els.breath.textContent = fmt(current.breath_bpm, 1);
  els.heart.textContent = fmt(current.heart_bpm, 1);
  els.distance.textContent = fmt(current.motion_score ?? current.distance_cm, 0);
  els.presence.textContent = current.present ? "Target present" : "No target";
  els.movementLabel.textContent = current.movement_label || "none";
  els.confidence.textContent = current.confidence ?? 0;
  els.lastSample.textContent = current.millis ? uptimeLabel(current.millis) : "-";
  els.fallState.textContent = current.fall_label || "normal";
  els.fallBreakHeight.textContent = current.fall_break_height ?? 0;
  els.staticResidency.textContent = current.static_residency_state ?? 0;
  if (summary.desired_mode !== undefined) {
    els.modeSleep.classList.toggle("active", (summary.desired_mode || "sleep") === "sleep");
    els.modeFall.classList.toggle("active", (summary.desired_mode || "") === "fall");
  }
}

function applySummary(summary) {
  if (!summary) return;

  els.apSsid.textContent = summary.ap_ssid || "-";
  els.apIp.textContent = summary.ap_ip || "-";
  els.desiredMode.textContent = summary.desired_mode || "-";
  els.uptime.textContent = `Uptime ${summary.uptime || "--:--:--"}`;
  els.sensorBaud.textContent = summary.sensor_baud || "-";
  els.historyPoints.textContent = summary.history_points || 0;
  els.binaryFrames.textContent = summary.sensor_reads || 0;
  els.textFrames.textContent = summary.work_mode || "-";
  els.parseErrors.textContent = summary.sensor_errors || 0;

  const current = summary.current || {};
  state.current = current;
  renderLiveState(current, summary);

  if (current.millis) {
    pushSample(current);
  }
}

function pushSample(sample) {
  const point = {
    millis: sample.millis,
    breath_bpm: sample.breath_bpm ?? null,
    heart_bpm: sample.heart_bpm ?? null,
    distance_cm: sample.motion_score ?? null,
  };

  const last = state.samples[state.samples.length - 1];
  if (last && last.millis === point.millis) {
    Object.assign(last, point);
  } else {
    state.samples.push(point);
  }

  while (state.samples.length > state.maxPoints) {
    state.samples.shift();
  }

  drawChart();
}

function loadHistory(summary) {
  state.samples = (summary.samples || []).map((sample) => ({
    millis: sample.millis,
    breath_bpm: sample.breath_bpm ?? null,
    heart_bpm: sample.heart_bpm ?? null,
    distance_cm: sample.motion_score ?? null,
  }));
  if ((!state.current || !state.current.millis) && state.samples.length > 0) {
    const last = state.samples[state.samples.length - 1];
    renderLiveState(last, summary);
  }
  drawChart();
}

function drawSeries(color, accessor, xFor, yFor) {
  ctx.beginPath();
  ctx.strokeStyle = color;
  ctx.lineWidth = 3;

  let started = false;
  state.samples.forEach((sample) => {
    const value = accessor(sample);
    if (value === null || value === undefined || Number.isNaN(value)) return;
    const x = xFor(sample.millis);
    const y = yFor(value);
    if (!started) {
      ctx.moveTo(x, y);
      started = true;
    } else {
      ctx.lineTo(x, y);
    }
  });

  ctx.stroke();
}

function drawChart() {
  const { width, height } = els.chart;
  ctx.clearRect(0, 0, width, height);

  ctx.fillStyle = "rgba(7, 16, 22, 0.95)";
  ctx.fillRect(0, 0, width, height);

  const padding = { top: 30, right: 24, bottom: 42, left: 50 };
  const innerWidth = width - padding.left - padding.right;
  const innerHeight = height - padding.top - padding.bottom;

  ctx.strokeStyle = "rgba(151, 182, 193, 0.12)";
  ctx.lineWidth = 1;
  for (let i = 0; i <= 5; i += 1) {
    const y = padding.top + (innerHeight / 5) * i;
    ctx.beginPath();
    ctx.moveTo(padding.left, y);
    ctx.lineTo(width - padding.right, y);
    ctx.stroke();
  }

  ctx.font = "12px Segoe UI";
  ctx.fillStyle = "rgba(151, 182, 193, 0.9)";
  ctx.fillText("0", 20, height - padding.bottom + 4);
  ctx.fillText("240", 14, padding.top + 4);
  ctx.fillText("Time", width - padding.right - 30, height - 14);

  if (state.samples.length < 2) {
    ctx.fillStyle = "rgba(151, 182, 193, 0.75)";
    ctx.font = "16px Segoe UI";
    ctx.fillText("Waiting for sensor history...", padding.left + 18, padding.top + 26);
    return;
  }

  const firstTime = state.samples[0].millis;
  const lastTime = state.samples[state.samples.length - 1].millis;
  const timeSpan = Math.max(1, lastTime - firstTime);
  const maxDistance = state.samples.reduce((max, sample) => {
    if (sample.distance_cm === null || sample.distance_cm === undefined || Number.isNaN(sample.distance_cm)) {
      return max;
    }
    return Math.max(max, sample.distance_cm);
  }, 0);
  const distanceScaleMax = Math.max(240, Math.ceil(maxDistance / 25) * 25);

  const xFor = (millis) => padding.left + ((millis - firstTime) / timeSpan) * innerWidth;
  const yForVitals = (value) => padding.top + innerHeight - (Math.max(0, Math.min(240, value)) / 240) * innerHeight;
  const yForDistance = (value) =>
    padding.top + innerHeight - (Math.max(0, Math.min(distanceScaleMax, value)) / distanceScaleMax) * innerHeight;

  drawSeries("rgba(126, 240, 199, 0.95)", (s) => s.breath_bpm, xFor, yForVitals);
  drawSeries("rgba(255, 142, 114, 0.95)", (s) => s.heart_bpm, xFor, yForVitals);
  drawSeries("rgba(108, 198, 255, 0.95)", (s) => s.distance_cm, xFor, yForDistance);

  const labels = 4;
  for (let i = 0; i <= labels; i += 1) {
    const x = padding.left + (innerWidth / labels) * i;
    const millis = firstTime + (timeSpan / labels) * i;
    ctx.fillStyle = "rgba(151, 182, 193, 0.85)";
    ctx.fillText(uptimeLabel(millis), x - 24, height - 18);
  }

  ctx.fillStyle = "rgba(126, 240, 199, 0.85)";
  ctx.fillText("Vitals max: 240", padding.left, 18);
  ctx.fillStyle = "rgba(108, 198, 255, 0.85)";
  ctx.fillText(`Motion max: ${distanceScaleMax}`, padding.left + 150, 18);
}

async function fetchJson(url, options) {
  const response = await fetch(url, options);
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  return response.json();
}

async function loadBootstrap() {
  const summary = await fetchJson("/api/history");
  applySummary(summary);
  loadHistory(summary);
}

async function pollStatus() {
  try {
    const summary = await fetchJson("/api/status");
    applySummary(summary);
    setPollingState();
  } catch (_) {
    if (!state.wsConnected) {
      els.wsDot.classList.remove("online");
      els.wsState.textContent = "Disconnected";
    }
  }
}

function startPolling() {
  if (state.pollTimer) return;
  state.pollTimer = window.setInterval(() => {
    pollStatus();
  }, 1500);
}

function stopReconnectTimer() {
  if (state.reconnectTimer) {
    window.clearTimeout(state.reconnectTimer);
    state.reconnectTimer = null;
  }
}

function connectWebSocket() {
  stopReconnectTimer();
  const url = `ws://${location.hostname}:81/`;
  const ws = new WebSocket(url);
  state.ws = ws;

  ws.addEventListener("open", () => {
    setWsState(true);
    pollStatus();
  });
  ws.addEventListener("close", () => {
    setWsState(false);
    pollStatus();
    state.reconnectTimer = window.setTimeout(connectWebSocket, 1500);
  });
  ws.addEventListener("error", () => {
    if (!state.wsConnected) {
      pollStatus();
    }
    ws.close();
  });
  ws.addEventListener("message", (event) => {
    try {
      const message = JSON.parse(event.data);
      if (message.type === "hello" || message.type === "snapshot") {
        applySummary(message);
      }
    } catch (_) {
    }
  });
}

els.clearLog.addEventListener("click", async () => {
  try {
    await fetchJson("/api/log/clear", { method: "POST" });
  } catch (_) {
  }
});

async function setMode(mode) {
  try {
    await fetchJson(`/api/mode?mode=${encodeURIComponent(mode)}`, { method: "POST" });
    await pollStatus();
  } catch (_) {
  }
}

els.modeSleep.addEventListener("click", () => setMode("sleep"));
els.modeFall.addEventListener("click", () => setMode("fall"));

window.addEventListener("resize", drawChart);

loadBootstrap()
  .catch(() => {})
  .finally(() => {
    startPolling();
    connectWebSocket();
  });
