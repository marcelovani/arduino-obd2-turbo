let chart = null;

const toMph = (kmh) => (kmh * 0.621371).toFixed(0);
const fmtSpeed = (kmh) => `${(+kmh).toFixed(0)} km/h (${toMph(kmh)} mph)`;

async function loadFiles() {
  const files = await fetch("/api/files").then((r) => r.json());
  const list = document.getElementById("file-list");
  if (files.length === 0) {
    list.innerHTML =
      '<div style="color:#555;padding:8px;font-size:12px">No recordings found</div>';
    return;
  }
  files.forEach((f) => {
    const el = document.createElement("div");
    el.className = "file-item";
    el.textContent = f;
    el.onclick = () => loadFile(f, el);
    list.appendChild(el);
  });
}

async function loadFile(filename, el) {
  document
    .querySelectorAll(".file-item")
    .forEach((e) => e.classList.remove("active"));
  el.classList.add("active");
  document.getElementById("title").textContent = filename;
  document.getElementById("stats").textContent = "Loading...";

  const data = await fetch("/api/data/" + filename).then((r) => r.json());

  const dur = data.time.length ? data.time[data.time.length - 1].toFixed(1) : 0;
  const maxTPS = Math.max(...data.tps).toFixed(1);
  const maxRPM = Math.max(...data.rpm).toFixed(0);
  const maxSpeed = Math.max(...data.speed);
  const sprays = data.triggers.length;
  const s = data.settings;
  const src =
    s._source === "csv"
      ? "settings from recording"
      : "default settings (old recording)";

  document.getElementById("stats").textContent =
    `${data.time.length} samples · ${dur}s · peak TPS ${maxTPS}% · peak RPM ${maxRPM} · peak speed ${fmtSpeed(maxSpeed)} · ${sprays} spray${sprays !== 1 ? "s" : ""}`;
  document.getElementById("settings-info").textContent =
    `Trigger thresholds (${src}): TPS ${s.throttle_high}%→${s.throttle_low}%  RPM>${s.rpm_min}  gear ${s.min_gear}–${s.max_gear}  spd12=${fmtSpeed(s.speed12)}  spd23=${fmtSpeed(s.speed23)}`;

  document.getElementById("empty").style.display = "none";
  document.getElementById("chart-wrap").style.display = "block";
  renderChart(data);
}

function buildAnnotations(triggers) {
  const annotations = {};
  triggers.forEach((t, i) => {
    annotations[`spray${i}_start`] = {
      type: "line",
      scaleID: "x",
      value: t.start_s,
      borderColor: "#ffe000",
      borderWidth: 1.5,
      label: {
        display: true,
        content: `G${t.gear} ▶`,
        color: "#ffe000",
        backgroundColor: "transparent",
        font: { family: "Courier New", size: 10 },
        position: "start",
        yAdjust: -4,
      },
    };
    annotations[`spray${i}_end`] = {
      type: "line",
      scaleID: "x",
      value: t.end_s,
      borderColor: "rgba(255,224,0,0.35)",
      borderWidth: 1,
      borderDash: [4, 4],
    };
    annotations[`spray${i}_band`] = {
      type: "box",
      xMin: t.start_s,
      xMax: t.end_s,
      backgroundColor: "rgba(255,224,0,0.06)",
      borderWidth: 0,
    };
  });
  return annotations;
}

function renderChart(data) {
  if (chart) {
    chart.destroy();
    chart = null;
  }

  const toPoints = (values) =>
    values.map((v, i) => ({ x: data.time[i], y: v }));

  const ctx = document.getElementById("chart").getContext("2d");
  chart = new Chart(ctx, {
    type: "line",
    data: {
      datasets: [
        {
          label: "TPS (%)",
          data: toPoints(data.tps),
          borderColor: "#4dff91",
          backgroundColor: "transparent",
          yAxisID: "tps",
          pointRadius: 0,
          borderWidth: 1.5,
          tension: 0.15,
        },
        {
          label: "RPM",
          data: toPoints(data.rpm),
          borderColor: "#ff5c5c",
          backgroundColor: "transparent",
          yAxisID: "rpm",
          pointRadius: 0,
          borderWidth: 1.5,
          tension: 0.15,
        },
        {
          label: "Speed (km/h · mph)",
          data: toPoints(data.speed),
          borderColor: "#5c9eff",
          backgroundColor: "transparent",
          yAxisID: "speed",
          pointRadius: 0,
          borderWidth: 1.5,
          tension: 0.15,
        },
      ],
    },
    options: {
      animation: false,
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: "index", intersect: false },
      plugins: {
        legend: {
          labels: { color: "#aaa", font: { family: "Courier New", size: 12 } },
        },
        tooltip: {
          backgroundColor: "#1a1a1a",
          borderColor: "#333",
          borderWidth: 1,
          titleColor: "#888",
          bodyColor: "#ddd",
          bodyFont: { family: "Courier New" },
          callbacks: {
            title: (items) => `t = ${items[0].parsed.x.toFixed(2)}s`,
            label: (item) => {
              if (item.dataset.yAxisID === "speed") {
                const kmh = item.parsed.y;
                return ` Speed: ${kmh.toFixed(0)} km/h (${toMph(kmh)} mph)`;
              }
              return null;
            },
          },
        },
        annotation: {
          annotations: buildAnnotations(data.triggers),
        },
        zoom: {
          zoom: {
            drag: {
              enabled: true,
              backgroundColor: "rgba(100,160,255,0.1)",
              borderColor: "rgba(100,160,255,0.4)",
              borderWidth: 1,
            },
            mode: "x",
            onZoomComplete: () =>
              (document.getElementById("reset-zoom").style.display =
                "inline-block"),
          },
          pan: { enabled: true, mode: "x" },
        },
      },
      scales: {
        x: {
          type: "linear",
          title: { display: true, text: "Time (s)", color: "#666" },
          ticks: { color: "#555", maxTicksLimit: 20, font: { size: 11 } },
          grid: { color: "#1e1e1e" },
        },
        tps: {
          type: "linear",
          position: "left",
          title: { display: true, text: "TPS (%)", color: "#4dff91" },
          ticks: { color: "#4dff91", font: { size: 11 } },
          grid: { color: "#1e1e1e" },
          min: 0,
          max: 100,
        },
        rpm: {
          type: "linear",
          position: "right",
          title: { display: true, text: "RPM", color: "#ff5c5c" },
          ticks: { color: "#ff5c5c", font: { size: 11 } },
          grid: { drawOnChartArea: false },
          min: 0,
        },
        speed: {
          type: "linear",
          position: "right",
          title: {
            display: true,
            text: "Speed (km/h · mph)",
            color: "#5c9eff",
          },
          ticks: { color: "#5c9eff", font: { size: 11 } },
          grid: { drawOnChartArea: false },
          min: 0,
          max: 200,
        },
      },
    },
  });
}

function resetZoom() {
  if (chart) chart.resetZoom();
  document.getElementById("reset-zoom").style.display = "none";
}

loadFiles();
