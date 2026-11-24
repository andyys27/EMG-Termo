"""Flujo común de grabación: sincronía de reloj → START → recepción → STOP → sincronía final.

Lo usan el lanzador de sesiones y capture_raw.
"""
from __future__ import annotations

from pathlib import Path

from .clock_sync import SyncPoint, clock_dict, measure
from .node import NodeControl, NodeError
from .recorder import Recorder


class Acquisition:
    def __init__(self, node: NodeControl, hello: dict, raw_path: Path, frames_path: Path,
                 udp_port: int = 5005, mirror_port: int | None = None, sync_pings: int = 30):
        self.node, self.hello = node, hello
        self.fs = float(hello["fs_hz"])
        self.spf = int(hello.get("spf", 20))
        self.udp_port, self.sync_pings = udp_port, sync_pings
        self.recorder = Recorder(raw_path, frames_path, self.fs, int(hello["n_ch"]), udp_port, mirror_port)
        self.sync_start: SyncPoint | None = None
        self.sync_end: SyncPoint | None = None
        self.start_reply: dict | None = None
        self.stop_stats: dict | None = None
        self.running = False

    def start(self) -> None:
        self.sync_start = measure(self.node, self.sync_pings)
        self.recorder.start()
        try:
            self.start_reply = self.node.start(self.udp_port)
        except Exception:
            self.recorder.stop(drain_s=0)
            raise
        self.running = True

    def stop(self) -> None:
        """Siempre deja el recorder cerrado, aunque el nodo no responda."""
        if not self.running:
            return
        self.running = False
        try:
            self.stop_stats = self.node.stop()
        except NodeError as e:
            print(f"⚠ STOP falló: {e}")
        finally:
            self.recorder.stop()
        try:
            self.sync_end = measure(self.node, self.sync_pings)
        except NodeError as e:
            print(f"⚠ sincronía final falló: {e}")

    def clock(self) -> dict | None:
        return clock_dict(self.sync_start, self.sync_end) if self.sync_start else None

    def recording(self) -> dict:
        return self.recorder.stats(self.spf)


def print_summary(rec: dict, fs_nominal: float, stop_stats: dict | None = None) -> None:
    from .protocol import flag_names

    print("\n=== Resumen de la grabación ===")
    print(f"Tramas: {rec['frames']} recibidas / {rec['expected_frames']} esperadas"
          + (f" / {stop_stats.get('frames')} enviadas" if stop_stats else ""))
    print(f"Pérdida: {rec['lost_frames']} ({rec['loss_pct']:.3f} %)   hueco máx: {rec['max_gap_ms']:.0f} ms   "
          f"tramas inválidas: {rec['bad_frames']}   duplicadas: {rec['duplicates']}")
    if rec["fs_eff_hz"]:
        err = (rec["fs_eff_hz"] / fs_nominal - 1) * 100
        print(f"fs efectiva: {rec['fs_eff_hz']:.2f} Hz ({err:+.3f} %)")
    if rec["flags_or"]:
        print(f"Banderas vistas: {', '.join(flag_names(rec['flags_or']))} "
              f"(en {rec['frames_with_flags']} tramas)")
    if stop_stats:
        print(f"Nodo: send_errors={stop_stats.get('send_errors')}  overruns={stop_stats.get('overruns')}  "
              f"lost_conv={stop_stats.get('lost_conv')}")
    ok = rec["frames"] > 0 and rec["loss_pct"] <= 1.0 and rec["max_gap_ms"] <= 20
    print("Veredicto rápido:", "OK" if ok else "REVISAR (pérdida > 1 % o huecos > 20 ms)")
