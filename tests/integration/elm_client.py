"""
Simple TCP client for talking to an ELM327-compatible server.

Works with both:
  - MockElm327Server (tests/integration/mock_elm327.py)
  - Real ELM327-emulator started with: python -m elm --net 35000
"""

import socket
import time


class ElmClient:
    """TCP client that speaks ELM327 protocol."""

    def __init__(self, host: str = "127.0.0.1", port: int = 35000, timeout: float = 3.0):
        self.host    = host
        self.port    = port
        self.timeout = timeout
        self._sock: socket.socket | None = None

    def connect(self, retries: int = 5, retry_delay: float = 0.5):
        """Connect to the server; retry a few times to allow server startup."""
        for attempt in range(retries):
            try:
                self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self._sock.settimeout(self.timeout)
                self._sock.connect((self.host, self.port))
                return
            except (ConnectionRefusedError, OSError):
                if attempt < retries - 1:
                    time.sleep(retry_delay)
        raise ConnectionRefusedError(
            f"Could not connect to ELM327 server at {self.host}:{self.port} "
            f"after {retries} attempts"
        )

    def query(self, cmd: str, timeout: float | None = None) -> str:
        """Send a command and wait for the '>' prompt. Returns the response text."""
        assert self._sock, "Not connected — call connect() first"
        t = timeout or self.timeout

        self._sock.sendall((cmd + '\r').encode())

        data = b""
        deadline = time.time() + t
        while time.time() < deadline:
            try:
                chunk = self._sock.recv(256)
                if not chunk:
                    break
                data += chunk
                if b'>' in data:
                    text = data.decode(errors="replace")
                    text = text.replace('\r', '').replace('\n', '').replace('>', '').strip()
                    return text
            except socket.timeout:
                break
        return data.decode(errors="replace").strip()

    def close(self):
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *_):
        self.close()
