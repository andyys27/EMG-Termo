"""Control de calidad de una grabación (sesión completa o captura de capture_raw).

  python -m emgthermo.validation.qc data/S01_20261124_101500
  python -m emgthermo.validation.qc hardware/measurements/prueba.csv

Calcula pérdida de tramas, huecos, fracción saturada, banderas, fs efectiva, RTT de la sincronía,
overruns, ruido en reposo (µV RMS referido a la entrada, 20–300 Hz) y fracción de potencia a 60 Hz.
Umbrales: config/quality_thresholds.yaml. Escala: config/devices.yaml (v_per_count, gain_total);
si aún no están medidos (Fase 1) se usan valores nominales y el ruido se marca como provisional.

Salida: <sesión>/processed/qc_report.json  (o <captura>_qc.json)
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import yaml
from scipy import signal

from ..core.protocol import FLAG_NAMES

REPO = Path(__file__).resolve().parents[3]
LEVEL = {"ok": 0, "warn": 1, "fail": 2, "info": 0}
VERDICT = {0: "aprobada", 1: "advertencias", 2: "rechazada"}


# --- carga --------------------------------------------------------------------------------------
def load_recording(path: Path) -> dict:
    """Devuelve raw, frames, meta, events y la ruta de salida del informe."""
    path = Path(path)
    if path.is_dir():
        raw_p, frames_p, meta_p = path / "emg_raw.csv", path / "frames.csv", path / "session.json"
        events_p = path / "events.csv"
        out = path / "processed" / "qc_report.json"
    else:
        raw_p, frames_p, meta_p = path, path.with_name(path.stem + "_frames.csv"), path.with_suffix(".json")
        events_p = None
        out = path.with_name(path.stem + "_qc.json")
    if not raw_p.exists():
        raise FileNotFoundError(f"no existe {raw_p}")
    meta = json.loads(meta_p.read_text()) if meta_p.exists() else {}
    events = pd.read_csv(events_p) if events_p and events_p.exists() else None
    frames = pd.read_csv(frames_p) if frames_p.exists() else None
    return dict(raw=pd.read_csv(raw_p), frames=frames, meta=meta, events=events, out=out, source=str(path))


def load_yaml(path: Path) -> dict:
    return yaml.safe_load(Path(path).read_text())


# --- métricas -----------------------------------------------------------------------------------
def scale_uv_per_count(devices: dict) -> tuple[float, bool]:
    """µV referidos a la entrada por cuenta de 12 bits. Devuelve (escala, es_medida)."""
    adc, analog = devices.get("adc", {}), devices.get("analog", {})
    vpc, gain = adc.get("v_per_count"), analog.get("gain_total")
    measured = vpc is not None and gain is not None
    vpc = vpc or adc.get("full_scale_v", 3.1) / adc.get("max_count_raw", 4095)
    gain = gain or analog.get("gain_nominal", 100)
    return vpc / gain * 1e6, measured


def rest_window_dev_us(events: pd.DataFrame | None, clock: dict | None) -> tuple[float, float] | None:
    """Ventana REST_START..REST_END en tiempo del nodo (µs), usando el modelo de reloj."""
    if events is None or not clock:
        return None
    ev = events.set_index("event")["t_pc_s"]
    if "REST_START" not in ev or "REST_END" not in ev:
        return None
    t0, t1 = float(np.atleast_1d(ev["REST_START"])[0]), float(np.atleast_1d(ev["REST_END"])[-1])
    # t_dev ≈ t_pc + offset (la corrección por deriva es de microsegundos en minutos)
    off, ref, ppm = clock["offset_us"], clock["t_ref_dev_us"], clock.get("drift_ppm", 0.0)
    to_dev = lambda t: t * 1e6 + off + ppm * 1e-6 * (t * 1e6 + off - ref)  # noqa: E731
    return to_dev(t0), to_dev(t1)


def spectral_metrics(x_counts: np.ndarray, fs: float, uv_per_count: float, band=(20, 300), line_hz=60.0) -> dict:
    """Ruido RMS en banda (µV, referido a la entrada) y fracción de potencia en line_hz ± 1 Hz."""
    if len(x_counts) < fs:  # < 1 s
        return dict(noise_uv_rms=None, line_frac=None)
    x = (x_counts - x_counts.mean()) * uv_per_count
    sos = signal.butter(4, band, btype="bandpass", fs=fs, output="sos")
    noise = float(np.sqrt(np.mean(signal.sosfiltfilt(sos, x) ** 2)))
    f, p = signal.welch(x, fs=fs, nperseg=int(fs))  # resolución 1 Hz
    inband = (f >= band[0]) & (f <= band[1])
    line = (f >= line_hz - 1) & (f <= line_hz + 1)
    total = p[inband].sum()
    return dict(noise_uv_rms=noise, line_frac=float(p[line].sum() / total) if total > 0 else None)


def check(value, warn=None, max_=None, lower_is_better=True) -> str:
    if value is None:
        return "info"
    if max_ is not None and value > max_:
        return "fail"
    if warn is not None and value > warn:
        return "warn"
    return "ok"


def run_qc(rec: dict, devices: dict, thr_cfg: dict) -> dict:
    thr = thr_cfg["thresholds"]
    raw, frames, meta = rec["raw"], rec["frames"], rec["meta"]
    info = meta.get("device_info") or meta.get("hello") or {}
    fs_nom = float(info.get("fs_hz", devices["acquisition"]["fs_hz"]))
    os_f = int(info.get("os", devices["acquisition"]["os_factor"]))
    spf = int(info.get("spf", devices["acquisition"]["samples_per_frame"]))
    sat_lo = devices["adc"]["saturation"]["low_count_raw"]
    sat_hi = devices["adc"]["saturation"]["high_count_raw"]
    uv_per_count, scale_measured = scale_uv_per_count(devices)
    ch_cols = [c for c in raw.columns if c.startswith("ch")]
    checks: dict[str, dict] = {}

    def add(name, value, status, unit="", note=""):
        checks[name] = dict(value=value, status=status, unit=unit, **({"note": note} if note else {}))

    # Enlace
    fr = frames if frames is not None else raw.drop_duplicates("seq")[["seq", "t_us", "flags"]]
    seq = np.unique(fr["seq"].to_numpy())
    expected = int(seq[-1] + 1) if len(seq) else 0
    lost = expected - len(seq)
    loss_pct = 100.0 * lost / expected if expected else 100.0
    gaps = np.r_[seq[0], np.diff(seq) - 1] if len(seq) else np.array([0])
    max_gap_ms = float(gaps.max() * spf / fs_nom * 1e3)
    t = fr.sort_values("seq")
    fs_eff = None
    if len(t) > 1:
        fs_eff = spf * 1e6 / ((t["t_us"].iloc[-1] - t["t_us"].iloc[0]) / (t["seq"].iloc[-1] - t["seq"].iloc[0]))
    lt = thr["packet_loss_pct"]
    add("packet_loss_pct", round(loss_pct, 4), check(loss_pct, lt["warn"], lt["max"]), "%")
    add("max_gap_ms", max_gap_ms, check(max_gap_ms, None, thr["max_gap_ms"]), "ms")
    if fs_eff is not None:
        err = abs(fs_eff / fs_nom - 1)
        add("fs_effective_hz", round(fs_eff, 3), check(err, None, thr["fs_effective_tol_frac"]), "Hz",
            f"error {100 * (fs_eff / fs_nom - 1):+.3f} % (tolerancia ±{100 * thr['fs_effective_tol_frac']:.2f} %)")
    bad = (meta.get("recording") or {}).get("bad_frames")
    if bad is not None:
        add("bad_frames", bad, "ok" if bad == 0 else "warn")

    # Nodo y reloj
    stop = meta.get("stop_stats") or {}
    ovr_flag = int((fr["flags"].to_numpy() >> 9 & 1).sum())
    overruns = max(int(stop.get("overruns") or 0), ovr_flag)
    add("overruns", overruns, check(overruns, None, thr["overrun_frames_max"]))
    clock = meta.get("clock")
    if clock:
        add("clock_rtt_us", round(clock["rtt_us"], 1), check(clock["rtt_us"], None, thr["clock_rtt_us_max"]), "µs")
        add("clock_drift_ppm", round(clock.get("drift_ppm", 0.0), 2), "info", "ppm")

    # Banderas
    flags = fr["flags"].to_numpy().astype(np.int64)
    flag_counts = {name: int((flags >> bit & 1).sum()) for bit, name in FLAG_NAMES.items()}
    flag_counts = {k: v for k, v in flag_counts.items() if v}
    warn_prefixes = tuple(thr_cfg.get("flags_warn", []))
    flagged = {k: v for k, v in flag_counts.items() if k.startswith(warn_prefixes)}
    add("flags", flag_counts, "warn" if flagged else "ok", "tramas",
        f"{', '.join(flagged)} presentes" if flagged else "")

    # Por canal: saturación, ruido en reposo, 60 Hz
    rest = rest_window_dev_us(rec["events"], clock)
    if rest:
        seg = raw[(raw["t_us"] >= rest[0]) & (raw["t_us"] <= rest[1])]
        seg_note = f"segmento REST ({(rest[1] - rest[0]) / 1e6:.1f} s)"
    else:
        seg = raw
        seg_note = "sin REST marcado: grabación completa"
    seg = seg.sort_values("t_us")
    per_ch = {}
    for c in ch_cols:
        counts = raw[c].to_numpy() / os_f
        sat = float(np.mean((counts < sat_lo) | (counts > sat_hi)))
        st = thr["saturation_frac"]
        add(f"{c}_saturation_frac", round(sat, 6), check(sat, st["warn"], st["max"]))
        sm = spectral_metrics(seg[c].to_numpy() / os_f, fs_nom, uv_per_count)
        nt, lft = thr["rest_noise_uv_rms"], thr["line_60hz_power_frac"]
        note = seg_note + ("" if scale_measured else "; escala NOMINAL (falta v_per_count/gain_total de Fase 1)")
        add(f"{c}_rest_noise_uv_rms", None if sm["noise_uv_rms"] is None else round(sm["noise_uv_rms"], 3),
            check(sm["noise_uv_rms"], nt["warn"], nt["max"]), "µV", note)
        add(f"{c}_line_60hz_frac", None if sm["line_frac"] is None else round(sm["line_frac"], 4),
            check(sm["line_frac"], lft["warn"], lft["max"]), "", seg_note)
        per_ch[c] = dict(mean_counts=float(counts.mean()), std_counts=float(counts.std()))

    worst = max((LEVEL[v["status"]] for v in checks.values()), default=0)
    return dict(source=rec["source"], verdict=VERDICT[worst], scale_measured=scale_measured,
                uv_per_count=uv_per_count, fs_nominal_hz=fs_nom, os_factor=os_f,
                duration_s=round(len(raw) / fs_nom, 2), channels=per_ch, checks=checks,
                thresholds_provisional=True)


def print_report(rep: dict) -> None:
    sym = {"ok": "✓", "warn": "!", "fail": "✗", "info": "·"}
    print(f"\n=== QC: {rep['source']} ({rep['duration_s']} s) ===")
    for name, c in rep["checks"].items():
        val = c["value"]
        val = f"{val:g}" if isinstance(val, float) else str(val)
        line = f"  {sym[c['status']]} {name:28s} {val} {c['unit']}".rstrip()
        if c.get("note"):
            line += f"   [{c['note']}]"
        print(line)
    print(f"Veredicto: {rep['verdict'].upper()}")


def qc_path(path, devices_path=None, thresholds_path=None, write=True, verbose=True) -> dict:
    devices = load_yaml(devices_path or REPO / "config" / "devices.yaml")
    thr = load_yaml(thresholds_path or REPO / "config" / "quality_thresholds.yaml")
    rec = load_recording(Path(path))
    rep = run_qc(rec, devices, thr)
    if write:
        rec["out"].parent.mkdir(parents=True, exist_ok=True)
        rec["out"].write_text(json.dumps(rep, indent=2, ensure_ascii=False))
        rep["report_path"] = str(rec["out"])
    if verbose:
        print_report(rep)
        if write:
            print(f"Informe: {rec['out']}")
    return rep


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help="carpeta de sesión o CSV de capture_raw")
    ap.add_argument("--devices", help="config/devices.yaml")
    ap.add_argument("--thresholds", help="config/quality_thresholds.yaml")
    a = ap.parse_args(argv)
    try:
        rep = qc_path(a.path, a.devices, a.thresholds)
    except (FileNotFoundError, KeyError, ValueError) as e:
        print(f"Error: {e}")
        return 2
    return {"aprobada": 0, "advertencias": 0, "rechazada": 1}[rep["verdict"]]


if __name__ == "__main__":
    sys.exit(main())
