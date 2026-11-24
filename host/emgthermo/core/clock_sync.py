"""Desfase y deriva entre el reloj del nodo (esp_timer, µs) y el de la PC (reloj maestro).

Cada PING da (t_envío, t_nodo, t_recepción). Con trayecto simétrico,
    offset = t_nodo − (t_envío + t_recepción) / 2      (nodo − PC, µs)
con error ≤ RTT/2. Se usan solo los PING de menor RTT.

Una medición al inicio y otra al final de la grabación dan la deriva lineal:
    offset(t_nodo) = offset_us + drift_ppm·1e-6·(t_nodo − t_ref_dev_us)
    t_pc_us        = t_nodo − offset(t_nodo)
"""
from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np


@dataclass
class SyncPoint:
    offset_us: float   # nodo − PC
    t_dev_us: int      # tiempo del nodo de la medición
    rtt_us: float      # RTT medio de los PING usados
    rtt_min_us: float
    n_pings: int
    n_used: int


def measure(node, n: int = 30, keep_frac: float = 0.25) -> SyncPoint:
    """Hace `n` PING y estima el desfase con la fracción `keep_frac` de menor RTT."""
    rows = np.array([node.ping() for _ in range(n)], dtype=np.float64)
    t_send, t_dev, t_recv = rows.T
    rtt = t_recv - t_send
    offset = t_dev - (t_send + t_recv) / 2
    k = max(1, int(round(n * keep_frac)))
    best = np.argsort(rtt)[:k]
    return SyncPoint(
        offset_us=float(np.median(offset[best])),
        t_dev_us=int(np.median(t_dev[best])),
        rtt_us=float(rtt[best].mean()),
        rtt_min_us=float(rtt.min()),
        n_pings=n,
        n_used=k,
    )


@dataclass
class ClockModel:
    offset_us: float
    t_ref_dev_us: int
    drift_ppm: float
    rtt_us: float

    @classmethod
    def from_points(cls, start: SyncPoint, end: SyncPoint | None = None) -> "ClockModel":
        drift = 0.0
        if end is not None and end.t_dev_us != start.t_dev_us:
            drift = (end.offset_us - start.offset_us) / (end.t_dev_us - start.t_dev_us) * 1e6
        rtt = start.rtt_us if end is None else max(start.rtt_us, end.rtt_us)
        return cls(start.offset_us, start.t_dev_us, drift, rtt)

    def dev_to_pc_us(self, t_dev_us):
        t = np.asarray(t_dev_us, dtype=np.float64)
        return t - (self.offset_us + self.drift_ppm * 1e-6 * (t - self.t_ref_dev_us))


def clock_dict(start: SyncPoint, end: SyncPoint | None) -> dict:
    """Bloque `clock` de session.json."""
    d = asdict(ClockModel.from_points(start, end))
    d["start"] = asdict(start)
    d["end"] = asdict(end) if end else None
    return d
