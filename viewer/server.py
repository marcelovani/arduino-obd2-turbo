#!/usr/bin/env python3
"""Recording viewer — run with: python3 viewer/server.py"""

import csv
import json
import os
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse

PORT = 8080
RECORDINGS_DIR = os.path.join(os.path.dirname(__file__), '..', 'recordings')

# Fallback defaults — mirror Config.h. Used only for CSV files recorded before
# the settings header was added (old recordings without a # line).
DEFAULT_SETTINGS = {
    'throttle_high': 60.0,
    'throttle_low':  10.0,
    'rpm_min':       3000.0,
    'min_gear':      1,
    'max_gear':      2,
    'cooldown_ms':   2000,
    'speed12':       50.0,
    'speed23':       65.0,
}

# Measured durations of the spray MP3 files (ffprobe)
SPRAY_DURATION_S = {1: 0.759, 2: 0.741}


def parse_settings_header(line):
    """Parse the # settings line written by Recorder.h into a dict.
    Returns None if the line is not a valid settings header."""
    if not line.startswith('#'):
        return None
    settings = dict(DEFAULT_SETTINGS)
    for part in line[1:].split(','):
        part = part.strip()
        if '=' not in part:
            continue
        key, _, val = part.partition('=')
        key = key.strip()
        if key in ('min_gear', 'max_gear', 'cooldown_ms'):
            settings[key] = int(float(val))
        elif key in settings:
            settings[key] = float(val)
    return settings


def estimate_gear(rpm, speed, s12, s23):
    """Mirror GearEstimator.h — speed-band approach."""
    if speed < 3.0 or rpm < 200.0:
        return 0
    if speed < s12:
        return 1
    if speed < s23:
        return 2
    if speed < 145.0:
        return 3
    if speed < 165.0:
        return 4
    if speed < 200.0:
        return 5
    return 6


def detect_triggers(rows, settings):
    """Mirror TurboTrigger.h — returns list of {start_s, end_s, gear}."""
    triggers = []
    prev_tps = 0.0
    last_trigger_ms = -(settings['cooldown_ms'] + 1)

    for row in rows:
        ms    = row['ms_abs']
        tps   = row['tps']
        rpm   = row['rpm']
        speed = row['speed']
        gear  = estimate_gear(rpm, speed, settings['speed12'], settings['speed23'])

        if (prev_tps >= settings['throttle_high'] and
                tps  <  settings['throttle_low']  and
                rpm  >  settings['rpm_min']        and
                settings['min_gear'] <= gear <= settings['max_gear'] and
                ms - last_trigger_ms > settings['cooldown_ms']):
            duration = SPRAY_DURATION_S.get(gear, 0.75)
            triggers.append({
                'start_s': round(row['time_s'], 3),
                'end_s':   round(row['time_s'] + duration, 3),
                'gear':    gear,
            })
            last_trigger_ms = ms

        prev_tps = tps

    return triggers


HTML = r"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Turbo Recording Viewer</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
  <script src="https://cdn.jsdelivr.net/npm/chartjs-plugin-annotation@3"></script>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Courier New', monospace;
      background: #0d0d0d;
      color: #ddd;
      display: flex;
      height: 100vh;
      overflow: hidden;
    }
    #sidebar {
      width: 200px;
      min-width: 200px;
      background: #141414;
      border-right: 1px solid #2a2a2a;
      display: flex;
      flex-direction: column;
      overflow: hidden;
    }
    #sidebar h2 {
      padding: 16px;
      font-size: 13px;
      color: #888;
      text-transform: uppercase;
      letter-spacing: 1px;
      border-bottom: 1px solid #2a2a2a;
    }
    #file-list {
      overflow-y: auto;
      flex: 1;
      padding: 8px;
    }
    .file-item {
      cursor: pointer;
      padding: 8px 10px;
      border-radius: 4px;
      font-size: 13px;
      color: #aaa;
      margin-bottom: 2px;
      transition: background 0.1s;
    }
    .file-item:hover  { background: #222; color: #eee; }
    .file-item.active { background: #1e3a5f; color: #6af; }
    #main {
      flex: 1;
      display: flex;
      flex-direction: column;
      overflow: hidden;
      padding: 20px;
      gap: 12px;
    }
    #title { font-size: 15px; color: #aaa; font-weight: normal; }
    #stats         { font-size: 12px; color: #555; }
    #settings-info { font-size: 11px; color: #3a3a3a; }
    #chart-wrap {
      flex: 1;
      position: relative;
      min-height: 0;
    }
    #chart-wrap canvas {
      position: absolute;
      top: 0; left: 0;
      width: 100% !important;
      height: 100% !important;
    }
    #empty {
      flex: 1;
      display: flex;
      align-items: center;
      justify-content: center;
      color: #333;
      font-size: 14px;
    }
  </style>
</head>
<body>
  <div id="sidebar">
    <h2>Recordings</h2>
    <div id="file-list"></div>
  </div>
  <div id="main">
    <div id="title">Select a recording</div>
    <div id="stats"></div>
    <div id="settings-info"></div>
    <div id="empty">← pick a file</div>
    <div id="chart-wrap" style="display:none">
      <canvas id="chart"></canvas>
    </div>
  </div>

  <script>
    let chart = null;

    async function loadFiles() {
      const files = await fetch('/api/files').then(r => r.json());
      const list = document.getElementById('file-list');
      if (files.length === 0) {
        list.innerHTML = '<div style="color:#555;padding:8px;font-size:12px">No recordings found</div>';
        return;
      }
      files.forEach(f => {
        const el = document.createElement('div');
        el.className = 'file-item';
        el.textContent = f;
        el.onclick = () => loadFile(f, el);
        list.appendChild(el);
      });
    }

    async function loadFile(filename, el) {
      document.querySelectorAll('.file-item').forEach(e => e.classList.remove('active'));
      el.classList.add('active');
      document.getElementById('title').textContent = filename;
      document.getElementById('stats').textContent = 'Loading...';

      const data = await fetch('/api/data/' + filename).then(r => r.json());

      const dur      = data.time.length ? data.time[data.time.length - 1].toFixed(1) : 0;
      const maxTPS   = Math.max(...data.tps).toFixed(1);
      const maxRPM   = Math.max(...data.rpm).toFixed(0);
      const maxSpeed = Math.max(...data.speed).toFixed(0);
      const sprays   = data.triggers.length;
      const s        = data.settings;
      const src      = s._source === 'csv' ? 'settings from recording' : 'default settings (old recording)';
      document.getElementById('stats').textContent =
        `${data.time.length} samples · ${dur}s · peak TPS ${maxTPS}% · peak RPM ${maxRPM} · peak speed ${maxSpeed} km/h · ${sprays} spray${sprays !== 1 ? 's' : ''}`;
      document.getElementById('settings-info').textContent =
        `Trigger thresholds (${src}): TPS ${s.throttle_high}%→${s.throttle_low}%  RPM>${s.rpm_min}  gear ${s.min_gear}–${s.max_gear}  spd12=${s.speed12}  spd23=${s.speed23}`;

      document.getElementById('empty').style.display     = 'none';
      document.getElementById('chart-wrap').style.display = 'block';
      renderChart(data);
    }

    function buildAnnotations(triggers) {
      const annotations = {};
      triggers.forEach((t, i) => {
        // Solid yellow line at trigger moment (spray starts)
        annotations[`spray${i}_start`] = {
          type: 'line',
          scaleID: 'x',
          value: t.start_s,
          borderColor: '#ffe000',
          borderWidth: 1.5,
          label: {
            display: true,
            content: `G${t.gear} ▶`,
            color: '#ffe000',
            backgroundColor: 'transparent',
            font: { family: 'Courier New', size: 10 },
            position: 'start',
            yAdjust: -4,
          },
        };
        // Dashed yellow line where the spray MP3 ends
        annotations[`spray${i}_end`] = {
          type: 'line',
          scaleID: 'x',
          value: t.end_s,
          borderColor: 'rgba(255,224,0,0.35)',
          borderWidth: 1,
          borderDash: [4, 4],
        };
        // Shaded region between start and end
        annotations[`spray${i}_band`] = {
          type: 'box',
          xMin: t.start_s,
          xMax: t.end_s,
          backgroundColor: 'rgba(255,224,0,0.06)',
          borderWidth: 0,
        };
      });
      return annotations;
    }

    function renderChart(data) {
      if (chart) { chart.destroy(); chart = null; }

      // Use {x, y} point objects so the linear x-axis works with annotations
      const toPoints = (values) => values.map((v, i) => ({ x: data.time[i], y: v }));

      const ctx = document.getElementById('chart').getContext('2d');
      chart = new Chart(ctx, {
        type: 'line',
        data: {
          datasets: [
            {
              label: 'TPS (%)',
              data: toPoints(data.tps),
              borderColor: '#4dff91',
              backgroundColor: 'transparent',
              yAxisID: 'tps',
              pointRadius: 0,
              borderWidth: 1.5,
              tension: 0.15,
            },
            {
              label: 'RPM',
              data: toPoints(data.rpm),
              borderColor: '#ff5c5c',
              backgroundColor: 'transparent',
              yAxisID: 'rpm',
              pointRadius: 0,
              borderWidth: 1.5,
              tension: 0.15,
            },
            {
              label: 'Speed (km/h)',
              data: toPoints(data.speed),
              borderColor: '#5c9eff',
              backgroundColor: 'transparent',
              yAxisID: 'speed',
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
          interaction: { mode: 'index', intersect: false },
          plugins: {
            legend: {
              labels: { color: '#aaa', font: { family: 'Courier New', size: 12 } },
            },
            tooltip: {
              backgroundColor: '#1a1a1a',
              borderColor: '#333',
              borderWidth: 1,
              titleColor: '#888',
              bodyColor: '#ddd',
              bodyFont: { family: 'Courier New' },
              callbacks: {
                title: items => `t = ${items[0].parsed.x.toFixed(2)}s`,
              },
            },
            annotation: {
              annotations: buildAnnotations(data.triggers),
            },
          },
          scales: {
            x: {
              type: 'linear',
              title: { display: true, text: 'Time (s)', color: '#666' },
              ticks: { color: '#555', maxTicksLimit: 20, font: { size: 11 } },
              grid:  { color: '#1e1e1e' },
            },
            tps: {
              type: 'linear',
              position: 'left',
              title: { display: true, text: 'TPS (%)', color: '#4dff91' },
              ticks: { color: '#4dff91', font: { size: 11 } },
              grid:  { color: '#1e1e1e' },
              min: 0, max: 100,
            },
            rpm: {
              type: 'linear',
              position: 'right',
              title: { display: true, text: 'RPM', color: '#ff5c5c' },
              ticks: { color: '#ff5c5c', font: { size: 11 } },
              grid:  { drawOnChartArea: false },
              min: 0,
            },
            speed: {
              type: 'linear',
              position: 'right',
              title: { display: true, text: 'Speed (km/h)', color: '#5c9eff' },
              ticks: { color: '#5c9eff', font: { size: 11 } },
              grid:  { drawOnChartArea: false },
              min: 0, max: 200,
            },
          },
        },
      });
    }

    loadFiles();
  </script>
</body>
</html>
"""


class Server(HTTPServer):
    allow_reuse_address = True


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        path = urlparse(self.path).path

        if path in ('/', '/index.html'):
            self._send(200, 'text/html', HTML.encode())

        elif path == '/api/files':
            try:
                files = sorted(
                    f for f in os.listdir(RECORDINGS_DIR) if f.endswith('.csv')
                )
            except FileNotFoundError:
                files = []
            self._send_json(files)

        elif path.startswith('/api/data/'):
            filename = path[len('/api/data/'):]
            if '/' in filename or '..' in filename:
                self._send(400, 'text/plain', b'Bad filename')
                return
            filepath = os.path.join(RECORDINGS_DIR, filename)
            if not os.path.isfile(filepath):
                self._send(404, 'text/plain', b'Not found')
                return

            rows = []
            settings = None
            data = {'time': [], 'tps': [], 'rpm': [], 'speed': [],
                    'triggers': [], 'settings': None}
            with open(filepath, newline='') as f:
                # Check for settings header before handing off to csv.DictReader
                first = f.readline()
                parsed = parse_settings_header(first)
                if parsed is not None:
                    settings = parsed
                else:
                    f.seek(0)  # no header — rewind so DictReader sees the column row
                settings = settings or dict(DEFAULT_SETTINGS)

                reader = csv.DictReader(f)
                t0 = None
                for row in reader:
                    ms = int(row['ms'])
                    if t0 is None:
                        t0 = ms
                    time_s = round((ms - t0) / 1000, 2)
                    tps    = float(row['tps'])
                    rpm    = float(row['rpm'])
                    speed  = float(row['speed'])
                    data['time'].append(time_s)
                    data['tps'].append(tps)
                    data['rpm'].append(rpm)
                    data['speed'].append(speed)
                    rows.append({'ms_abs': ms, 'time_s': time_s,
                                 'tps': tps, 'rpm': rpm, 'speed': speed})

            settings['_source'] = 'csv' if parsed is not None else 'defaults'
            data['settings'] = settings
            data['triggers'] = detect_triggers(rows, settings)
            self._send_json(data)

        else:
            self._send(404, 'text/plain', b'Not found')

    def _send(self, code, content_type, body):
        self.send_response(code)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', len(body))
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, obj):
        body = json.dumps(obj).encode()
        self._send(200, 'application/json', body)

    def log_message(self, *_):
        pass  # silence request logs


if __name__ == '__main__':
    try:
        server = Server(('', PORT), Handler)
    except OSError:
        print(f'ERROR: port {PORT} is already in use. Kill the existing process and retry.')
        raise SystemExit(1)
    print(f'Open http://localhost:{PORT}')
    server.serve_forever()
