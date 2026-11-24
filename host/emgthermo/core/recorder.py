"""Recepción UDP de tramas y escritura de emg_raw.csv y frames.csv.

emg_raw.csv : t_us, ch1, ch2, ..., flags, seq   (una fila por muestra; valores = sumas de os conversiones)
frames.csv  : seq, t_us, flags, n, t_recv_s     (una fila por trama válida recibida)

Las tramas se escriben en el orden de llegada; la pérdida se detecta por huecos en `seq`.
"""
from __future__ import annotations

import csv
import select
import socket
import threading
import time
from pathlib import Path

import numpy as np

from .protocol import FrameError, decode_frame


class Recorder:
    def __init__(self, raw_path: Path, frames_path: Path, fs_hz: float, n_ch: int,
                 udp_port: int = 5005, mirror_port: int | None = None, mirror_host: str = "127.0.0.1"):
        self.raw_path, self.frames_path = Path(raw_path), Path(frames_path)
        self.fs_hz, self.n_ch = fs_hz, n_ch
        self.udp_port = udp_port
        self.mirror = (mirror_host, mirror_port) if mirror_port else None

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        try:
            self.sock.bind(("", udp_port))
        except OSError as e:
            raise OSError(f"no se pudo abrir el puerto UDP {udp_port} ({e}); ¿otro programa lo usa?") from e
        self.sock.setblocking(False)
        self._mirror_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) if self.mirror else None

        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._lock = threading.Lock()
        self.seqs: list[int] = []
        self.t_us: list[int] = []
        self.bad = {"header": 0, "crc": 0}
        self.flags_or = 0
        self.flag_frames = 0
        self.samples = 0
        self.t_first_recv: float | None = None
        self.t_last_recv: float | None = None

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, name="recorder", daemon=True)
        self._thread.start()

    def stop(self, drain_s: float = 0.3) -> None:
        time.sleep(drain_s)  # Recoger lo que siga en vuelo tras STOP
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=5)
        self.sock.close()
        if self._mirror_sock:
            self._mirror_sock.close()

    def _run(self) -> None:
        cols = [f"ch{c + 1}" for c in range(self.n_ch)]
        with open(self.raw_path, "w", newline="") as fr, open(self.frames_path, "w", newline="") as ff:
            raw_w, frm_w = csv.writer(fr), csv.writer(ff)
            raw_w.writerow(["t_us", *cols, "flags", "seq"])
            frm_w.writerow(["seq", "t_us", "flags", "n", "t_recv_s"])
            last_flush = time.monotonic()
            while not self._stop.is_set():
                r, _, _ = select.select([self.sock], [], [], 0.1)
                if r:
                    self._drain(raw_w, frm_w)
                if time.monotonic() - last_flush > 1.0:
                    fr.flush()
                    ff.flush()
                    last_flush = time.monotonic()
            self._drain(raw_w, frm_w)

    def _drain(self, raw_w, frm_w) -> None:
        while True:
            try:
                pkt = self.sock.recv(2048)
            except (BlockingIOError, OSError):
                return
            t_recv = time.time()
            if self._mirror_sock:
                try:
                    self._mirror_sock.sendto(pkt, self.mirror)
                except OSError:
                    pass
            try:
                fr = decode_frame(pkt)
            except FrameError as e:
                with self._lock:
                    self.bad[e.kind] += 1
                continue
            ts = fr.sample_times_us(self.fs_hz)
            for i in range(fr.n_samples):
                raw_w.writerow([int(ts[i]), *fr.samples[i].tolist(), fr.flags, fr.seq])
            frm_w.writerow([fr.seq, fr.t_us, fr.flags, fr.n_samples, f"{t_recv:.6f}"])
            with self._lock:
                self.seqs.append(fr.seq)
                self.t_us.append(fr.t_us)
                self.samples += fr.n_samples
                self.flags_or |= fr.flags
                self.flag_frames += fr.flags != 0
                if self.t_first_recv is None:
                    self.t_first_recv = t_recv
                self.t_last_recv = t_recv

    def stats(self, spf: int = 20) -> dict:
        """Bloque `recording` de session.json."""
        with self._lock:
            seq = np.array(self.seqs, dtype=np.int64)
            tus = np.array(self.t_us, dtype=np.int64)
            bad = sum(self.bad.values())
            out = dict(frames=len(seq), expected_frames=0, lost_frames=0, duplicates=0, out_of_order=0,
                       bad_frames=bad, bad_header=self.bad["header"], bad_crc=self.bad["crc"],
                       samples=self.samples, loss_pct=0.0, max_gap_ms=0.0, fs_eff_hz=None,
                       frames_with_flags=self.flag_frames, flags_or=self.flags_or)
        if len(seq) == 0:
            return out
        uniq = np.unique(seq)
        expected = int(uniq[-1] + 1)  # seq empieza en 0 con START: cuenta también la pérdida inicial
        gaps_head = np.r_[uniq[0], np.diff(uniq) - 1]
        lost = expected - len(uniq)
        gaps = gaps_head
        out.update(expected_frames=expected, lost_frames=lost, duplicates=int(len(seq) - len(uniq)),
                   out_of_order=int(np.sum(np.diff(seq) < 0)), loss_pct=100.0 * lost / expected,
                   max_gap_ms=float(gaps.max() * spf / self.fs_hz * 1e3) if len(gaps) and gaps.max() > 0 else 0.0)
        if len(uniq) > 1:
            order = np.argsort(seq, kind="stable")
            s, t = seq[order], tus[order]
            dt_frame = (t[-1] - t[0]) / (s[-1] - s[0])
            out["fs_eff_hz"] = float(spf * 1e6 / dt_frame)
        return out
