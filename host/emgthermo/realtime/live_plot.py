"""Monitor en vivo (solo verificación visual; no graba nada).

Escucha las tramas UDP y dibuja los últimos segundos de cada canal en cuentas de 12 bits,
con RMS, pérdida y banderas.

  # Con el lanzador (copia de las tramas, sin afectar la grabación):
  python -m emgthermo.launcher.run_session --host <IP> --subject S01 --mirror-port 5105
  python -m emgthermo.realtime.live_plot --port 5105

  # Solo monitor, sin grabar (el propio monitor envía START/STOP al nodo):
  python -m emgthermo.realtime.live_plot --host <IP>
"""
from __future__ import annotations

import argparse
import socket
import sys

import numpy as np

from ..core.protocol import FrameError, decode_frame, flag_names


class Stream:
    def __init__(self, port: int, fs: float, window_s: float, n_ch: int = 2):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        self.sock.bind(("", port))
        self.sock.setblocking(False)
        self.fs, self.n = fs, int(window_s * fs)
        self.buf = np.full((self.n, n_ch), np.nan)
        self.last_seq = None
        self.frames = self.lost = self.bad = 0
        self.flags_recent = 0
        self.os = 8

    def poll(self) -> int:
        new = []
        while True:
            try:
                pkt = self.sock.recv(2048)
            except (BlockingIOError, OSError):
                break
            try:
                fr = decode_frame(pkt)
            except FrameError:
                self.bad += 1
                continue
            if self.last_seq is not None:
                if fr.seq == 0 and self.last_seq > 0:  # Nuevo START
                    self.last_seq = None
                elif fr.seq > self.last_seq + 1:
                    self.lost += fr.seq - self.last_seq - 1
            self.last_seq = fr.seq if self.last_seq is None else max(self.last_seq, fr.seq)
            self.frames += 1
            self.os = fr.os_factor
            self.flags_recent = fr.flags
            new.append(fr.samples.astype(np.float64) / fr.os_factor)
        if new:
            x = np.vstack(new)[-self.n:]
            self.buf = np.roll(self.buf, -len(x), axis=0)
            self.buf[-len(x):] = x
        return len(new)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=5105, help="puerto UDP a escuchar (5105 = espejo del lanzador)")
    ap.add_argument("--host", help="IP del nodo: el monitor hace START/STOP él mismo (sin grabar)")
    ap.add_argument("--tcp-port", type=int, default=5006)
    ap.add_argument("--fs", type=float, default=2000)
    ap.add_argument("--window", type=float, default=5.0, help="segundos visibles")
    ap.add_argument("--ac", action="store_true", help="restar la media (ver solo la parte alterna)")
    a = ap.parse_args(argv)

    try:
        import matplotlib.pyplot as plt
        from matplotlib.animation import FuncAnimation
    except ImportError:
        print("Falta matplotlib: pip install matplotlib")
        return 1

    node = None
    if a.host:
        from ..core.node import NodeControl, NodeError

        try:
            node = NodeControl(a.host, a.tcp_port)
            hello = node.hello()
        except NodeError as e:
            print(f"Error: {e}")
            return 1
        a.fs = float(hello["fs_hz"])
        if a.port == 5105:
            a.port = 5005
    try:
        st = Stream(a.port, a.fs, a.window)
    except OSError as e:
        print(f"No se pudo abrir el puerto UDP {a.port}: {e}")
        return 1
    if node:
        node.start(a.port)
    print(f"Escuchando UDP {a.port}. Cierra la ventana para salir.")

    n_ch = st.buf.shape[1]
    t = np.arange(st.n) / a.fs - a.window
    fig, axes = plt.subplots(n_ch, 1, sharex=True, figsize=(11, 3 * n_ch), squeeze=False)
    names = ["ch1 (VL)", "ch2 (BF)"] + [f"ch{i + 1}" for i in range(2, n_ch)]
    lines, texts = [], []
    for c in range(n_ch):
        ax = axes[c, 0]
        (ln,) = ax.plot(t, st.buf[:, c], lw=0.6)
        lines.append(ln)
        texts.append(ax.text(0.01, 0.95, "", transform=ax.transAxes, va="top", family="monospace", fontsize=9))
        ax.set_ylabel(f"{names[c]}\ncuentas")
        ax.grid(alpha=0.3)
    axes[-1, 0].set_xlabel("tiempo (s)")
    status = fig.suptitle("esperando tramas…")

    def update(_):
        st.poll()
        for c in range(n_ch):
            y = st.buf[:, c]
            valid = y[~np.isnan(y)]
            if a.ac and len(valid):
                y = y - valid.mean()
            lines[c].set_ydata(y)
            if len(valid):
                last = valid[-int(a.fs * 0.5):]
                texts[c].set_text(f"media {valid.mean():7.1f}   RMS(0.5 s) {last.std():6.2f}   "
                                  f"min {valid.min():.0f}  max {valid.max():.0f}")
                lo, hi = np.nanmin(y), np.nanmax(y)
                pad = max(5.0, 0.1 * (hi - lo))
                axes[c, 0].set_ylim(lo - pad, hi + pad)
        fl = ", ".join(flag_names(st.flags_recent)) or "ninguna"
        status.set_text(f"tramas {st.frames}   perdidas {st.lost}   inválidas {st.bad}   banderas: {fl}")
        return [*lines, *texts, status]

    _anim = FuncAnimation(fig, update, interval=50, blit=False, cache_frame_data=False)  # noqa: F841
    try:
        plt.show()
    finally:
        if node:
            try:
                node.stop()
            finally:
                node.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
