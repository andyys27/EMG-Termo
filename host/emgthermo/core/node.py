"""Canal de control TCP del nodo (comandos de texto, una línea por comando y por respuesta).

Comandos y respuestas: ver firmware/emg_node/include/protocol.h.
"""
from __future__ import annotations

import json
import socket
import time


class NodeError(RuntimeError):
    pass


class NodeControl:
    def __init__(self, host: str, port: int = 5006, timeout: float = 5.0):
        self.host, self.port, self.timeout = host, port, timeout
        try:
            self.sock = socket.create_connection((host, port), timeout=timeout)
        except OSError as e:
            raise NodeError(f"no se pudo conectar a {host}:{port} ({e}). ¿IP correcta y misma red 2.4 GHz?") from e
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._buf = b""

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _readline(self, deadline: float) -> str:
        while b"\n" not in self._buf:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError
            self.sock.settimeout(remaining)
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout as e:
                raise TimeoutError from e
            if not chunk:
                raise NodeError("el nodo cerró la conexión TCP")
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        return line.decode(errors="replace").strip()

    def cmd(self, line: str, expect: str, timeout: float | None = None):
        """Envía `line` y espera la respuesta que empieza por `expect`.

        Devuelve el JSON si la respuesta trae un objeto, o el resto del texto.
        Ignora líneas que no correspondan (p. ej. registros del nodo).
        """
        self.sock.sendall((line + "\n").encode())
        deadline = time.monotonic() + (timeout or self.timeout)
        while True:
            try:
                txt = self._readline(deadline)
            except TimeoutError:
                raise NodeError(f"sin respuesta a {line!r}") from None
            if txt.startswith("ERR"):
                raise NodeError(f"{line}: {txt}")
            if txt.startswith(expect):
                rest = txt[len(expect):].strip()
                return json.loads(rest) if rest.startswith("{") else rest

    def hello(self) -> dict:
        return self.cmd("HELLO", "OK HELLO")

    def arm(self) -> dict:
        return self.cmd("ARM", "OK ARM", timeout=8)

    def start(self, udp_port: int) -> dict:
        return self.cmd(f"START {udp_port}", "OK START")

    def stop(self) -> dict:
        return self.cmd("STOP", "OK STOP")

    def status(self) -> dict:
        return self.cmd("STATUS", "OK STATUS")

    def txpower(self, dbm: float) -> dict:
        return self.cmd(f"TXPOWER {dbm:g}", "OK TXPOWER")

    def ping(self) -> tuple[int, int, int]:
        """Un PING: (t_envío_pc_us, t_nodo_us, t_recepción_pc_us), reloj de pared de la PC."""
        t_send = time.time_ns() // 1000
        rest = self.cmd("PING", "PONG")
        t_recv = time.time_ns() // 1000
        return t_send, int(rest), t_recv
