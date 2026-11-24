"""Carpeta de sesión, events.csv y session.json.

data/<sujeto>_<AAAAMMDD_HHMMSS>/
    emg_raw.csv, frames.csv   (los escribe Recorder)
    events.csv                t_pc_s, event, detail   (t_pc_s = hora de la PC, segundos Unix)
    session.json
"""
from __future__ import annotations

import csv
import json
import re
import time
from datetime import datetime
from pathlib import Path


class Session:
    def __init__(self, folder: Path, subject: str):
        self.folder = Path(folder)
        self.subject = subject
        self.t0_pc_us = time.time_ns() // 1000
        self.meta: dict = {"subject": subject, "t0_pc_us": self.t0_pc_us,
                           "created": datetime.now().astimezone().isoformat(timespec="seconds")}
        self._events_f = open(self.folder / "events.csv", "w", newline="")
        self._events = csv.writer(self._events_f)
        self._events.writerow(["t_pc_s", "event", "detail"])
        self.events: list[tuple[float, str, str]] = []

    @classmethod
    def create(cls, root: str | Path, subject: str) -> "Session":
        if not re.fullmatch(r"[A-Za-z0-9_-]+", subject):
            raise ValueError("el sujeto solo puede tener letras, números, '_' y '-'")
        folder = Path(root) / f"{subject}_{datetime.now():%Y%m%d_%H%M%S}"
        folder.mkdir(parents=True, exist_ok=False)
        return cls(folder, subject)

    @property
    def raw_path(self) -> Path:
        return self.folder / "emg_raw.csv"

    @property
    def frames_path(self) -> Path:
        return self.folder / "frames.csv"

    def event(self, name: str, detail: str = "", quiet: bool = False) -> float:
        t = time.time()
        self._events.writerow([f"{t:.6f}", name, detail])
        self._events_f.flush()
        self.events.append((t, name, detail))
        if not quiet:
            elapsed = (t * 1e6 - self.t0_pc_us) / 1e6
            print(f"  [{elapsed:8.1f} s] {name}" + (f"  ({detail})" if detail else ""))
        return t

    def save(self) -> Path:
        path = self.folder / "session.json"
        tmp = path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(self.meta, indent=2, ensure_ascii=False, default=str))
        tmp.replace(path)
        return path

    def close(self) -> None:
        self.save()
        self._events_f.close()
