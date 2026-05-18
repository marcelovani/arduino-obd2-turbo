#!/usr/bin/env python3
"""Recording viewer — run with: make viewer"""

import csv
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse

PORT = 8080
VIEWER_DIR     = os.path.dirname(__file__)
RECORDINGS_DIR = os.path.join(VIEWER_DIR, '..', 'recordings')

sys.path.insert(0, os.path.join(VIEWER_DIR, '..', 'lib'))
from turbo_logic import TurboTrigger, load_config_h_as_settings  # noqa: E402

STATIC_FILES = {
    '/':           ('index.html', 'text/html'),
    '/index.html': ('index.html', 'text/html'),
    '/style.css':  ('style.css',  'text/css'),
    '/app.js':     ('app.js',     'application/javascript'),
}

DEFAULT_SETTINGS = load_config_h_as_settings()

# Measured durations of the spray MP3 files (ffprobe)
SPRAY_DURATION_S = {1: 1.185, 2: 1.185, 3: 1.185}  # all gears use 0012.mp3


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


def detect_triggers(rows, settings):
    """Replay rows through TurboTrigger from lib/turbo_logic.py."""
    trigger = TurboTrigger(
        throttle_high=settings['throttle_high'],
        throttle_low= settings['throttle_low'],
        rpm_min=      settings['rpm_min'],
        min_gear=     settings['min_gear'],
        max_gear=     settings['max_gear'],
        cooldown_ms=  settings['cooldown_ms'],
        speed12=      settings['speed12'],
        speed23=      settings['speed23'],
    )
    results = []
    for row in rows:
        if trigger.update(row['tps'], row['rpm'], row['speed'], row['ms_abs']):
            gear     = trigger.events[-1]['gear']
            duration = SPRAY_DURATION_S.get(gear, 0.75)
            results.append({
                'start_s': round(row['time_s'], 3),
                'end_s':   round(row['time_s'] + duration, 3),
                'gear':    gear,
            })
    return results


class Server(HTTPServer):
    allow_reuse_address = True


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        path = urlparse(self.path).path

        if path in STATIC_FILES:
            filename, content_type = STATIC_FILES[path]
            filepath = os.path.join(VIEWER_DIR, filename)
            with open(filepath, 'rb') as f:
                body = f.read()
            self._send(200, content_type, body)

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
                first = f.readline()
                parsed = parse_settings_header(first)
                if parsed is not None:
                    settings = parsed
                else:
                    f.seek(0)
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
