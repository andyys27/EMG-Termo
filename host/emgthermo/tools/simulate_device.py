"""Nodo EMG simulado: mismo protocolo TCP/UDP que el firmware, para probar sin hardware.

  python -m emgthermo.tools.simulate_device --port 5006 [--loss 0.01] [--drift-ppm 40]

Señal: offset de 1.65 V (~2000 cuentas × os) + ruido + ráfagas de EMG en un ciclo de 6 s
(3 s activo, 3 s reposo) parecido a la sentadilla con metrónomo a 60 bpm. ch2 (BF) es más débil.
"""
from __future__ import annotations

import argparse
import json
import random
import socket
import threading
import time

import numpy as np

from ..core.protocol import PROTOCOL_VERSION, encode_frame


class SimNode:
    def __init__(self, host: str = "127.0.0.1", port: int = 5006, fs_hz: int = 2000, n_ch: int = 2,
                 os_factor: int = 8, spf: int = 20, loss: float = 0.0, drift_ppm: float = 40.0, seed: int = 1):
        self.fs, self.n_ch, self.os, self.spf = fs_hz, n_ch, os_factor, spf
        self.loss, self.drift = loss, drift_ppm
        self.rng = np.random.default_rng(seed)
        self._dev_offset_us = 123_456_789  # El nodo arranca su reloj en otro instante que la PC
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind((host, port))
        self.srv.listen(1)
        self.port = self.srv.getsockname()[1]
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._stream: threading.Thread | None = None
        self._streaming = threading.Event()
        self.frames_sent = 0
        self._closed = False

    # Reloj del nodo: monotónico de la PC con desfase y deriva
    def dev_us(self) -> int:
        return int(time.monotonic_ns() / 1000 * (1 + self.drift * 1e-6)) + self._dev_offset_us

    def serve_forever(self) -> None:
        while not self._closed:
            try:
                conn, addr = self.srv.accept()
            except OSError:
                return
            try:
                self._handle(conn, addr[0])
            finally:
                self._stop_stream()
                conn.close()

    def start_background(self) -> "SimNode":
        threading.Thread(target=self.serve_forever, name="simnode", daemon=True).start()
        return self

    def close(self) -> None:
        self._closed = True
        self._stop_stream()
        self.srv.close()

    def _handle(self, conn: socket.socket, client_ip: str) -> None:
        buf = b""
        while True:
            data = conn.recv(1024)
            if not data:
                return
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                reply = self._command(line.decode().strip(), client_ip)
                conn.sendall((reply + "\n").encode())

    def _command(self, line: str, client_ip: str) -> str:
        cmd, _, arg = line.partition(" ")
        if cmd == "PING":
            return f"PONG {self.dev_us()}"
        if cmd == "HELLO":
            return "OK HELLO " + json.dumps(dict(fw="emg_node_sim", ver="sim", proto=PROTOCOL_VERSION,
                                                 fs_hz=self.fs, n_ch=self.n_ch, os=self.os, spf=self.spf,
                                                 mac="00:00:00:00:00:00"))
        if cmd == "ARM":
            if self._streaming.is_set():
                return "ERR ARM no permitido durante START"
            time.sleep(1.0)
            x = self._signal(0, self.fs, active=False)
            return "OK ARM " + json.dumps(dict(n=self.fs, mean=x.mean(0).round(1).tolist(),
                                               std=x.std(0).round(2).tolist(), min=x.min(0).tolist(),
                                               max=x.max(0).tolist(), flags=0))
        if cmd == "START":
            if self._streaming.is_set():
                return "ERR ya transmitiendo"
            port = int(arg) if arg else 5005
            t0 = self.dev_us()
            self._streaming.set()
            self._stream = threading.Thread(target=self._run_stream, args=(client_ip, port, t0), daemon=True)
            self._stream.start()
            return "OK START " + json.dumps(dict(port=port, t_us=t0))
        if cmd == "STOP":
            self._stop_stream()
            return "OK STOP " + json.dumps(dict(frames=self.frames_sent, send_errors=0, overruns=0, lost_conv=0,
                                                bad_conv=0))
        if cmd == "STATUS":
            return "OK STATUS " + json.dumps(dict(streaming=self._streaming.is_set(), frames=self.frames_sent,
                                                  seq_next=self.frames_sent, send_errors=0, overruns=0,
                                                  lost_conv=0, bad_conv=0, rssi=-55, tx_power_qdbm=80,
                                                  heap=200000, uptime_us=self.dev_us()))
        if cmd == "TXPOWER":
            return "OK TXPOWER " + json.dumps(dict(qdbm=int(float(arg) * 4)))
        return f"ERR comando desconocido: {cmd}"

    def _stop_stream(self) -> None:
        self._streaming.clear()
        if self._stream and self._stream is not threading.current_thread():
            self._stream.join(timeout=2)
        self._stream = None

    def _signal(self, k0: int, n: int, active: bool = True) -> np.ndarray:
        """n muestras (sumas) a partir del índice k0."""
        t = (k0 + np.arange(n)) / self.fs
        base = 2000 * self.os
        x = base + self.rng.normal(0, 1.2 * np.sqrt(self.os), (n, self.n_ch))
        if active:
            env = ((t % 6.0) < 3.0) * np.sin(np.pi * (t % 6.0) / 3.0)  # Ráfaga de 3 s cada 6 s
            gains = np.array([600, 200, 150, 150][: self.n_ch]) * self.os / 8
            x += env[:, None] * self.rng.normal(0, 1, (n, self.n_ch)) * gains
        return np.clip(np.round(x), 0, 4095 * self.os).astype(np.uint16)

    def _run_stream(self, ip: str, port: int, t0_us: int) -> None:
        self.frames_sent = 0
        seq = 0
        frame_s = self.spf / self.fs
        t_next = time.monotonic()
        while self._streaming.is_set():
            t_next += frame_s
            delay = t_next - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            samples = self._signal(seq * self.spf, self.spf)
            t_us = t0_us + int(round(seq * frame_s * 1e6 * (1 + self.drift * 1e-6)))
            pkt = encode_frame(seq, t_us, samples, self.os)
            if random.random() >= self.loss:
                try:
                    self.udp.sendto(pkt, (ip, port))
                except OSError:
                    pass
            seq += 1
            self.frames_sent = seq


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=5006)
    ap.add_argument("--loss", type=float, default=0.0, help="fracción de tramas que no se envían")
    ap.add_argument("--drift-ppm", type=float, default=40.0)
    a = ap.parse_args()
    node = SimNode(a.host, a.port, loss=a.loss, drift_ppm=a.drift_ppm)
    print(f"Nodo simulado escuchando en {a.host}:{node.port} (Ctrl+C para salir)")
    try:
        node.serve_forever()
    except KeyboardInterrupt:
        node.close()


if __name__ == "__main__":
    main()
