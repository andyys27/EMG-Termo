"""Captura corta y cruda del nodo, sin sesión ni eventos (pruebas 1, 2 y 5 de la Fase 1).

  python -m emgthermo.tools.capture_raw --host <IP> --secs 60 --out hardware/measurements/corto_wifi_on.csv
  python -m emgthermo.tools.capture_raw --sim --secs 5 --out /tmp/prueba.csv

Escribe:
  <out>               emg_raw.csv  (t_us, ch1, ch2, flags, seq; sumas de os conversiones)
  <out>_frames.csv    una fila por trama
  <out>.json          HELLO, ARM, sincronía de reloj, STOP y resumen
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

from ..core.acquisition import Acquisition, print_summary
from ..core.node import NodeControl, NodeError


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host")
    ap.add_argument("--tcp-port", type=int, default=5006)
    ap.add_argument("--udp-port", type=int, default=5005)
    ap.add_argument("--secs", type=float, default=60)
    ap.add_argument("--out", required=True, help="ruta del CSV crudo")
    ap.add_argument("--txpower", type=float, help="potencia Wi-Fi del nodo en dBm")
    ap.add_argument("--no-arm", action="store_true", help="no ejecutar ARM antes de grabar")
    ap.add_argument("--note", default="", help="condiciones de la prueba (se guardan en el .json)")
    ap.add_argument("--sim", action="store_true")
    args = ap.parse_args(argv)

    sim = None
    if args.sim:
        from .simulate_device import SimNode

        sim = SimNode("127.0.0.1", 0).start_background()
        args.host, args.tcp_port = "127.0.0.1", sim.port
    if not args.host:
        ap.error("falta --host (o usar --sim)")

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    frames_path = out.with_name(out.stem + "_frames.csv")
    meta_path = out.with_suffix(".json")
    if out.exists():
        sys.exit(f"Error: {out} ya existe; elige otro nombre")

    meta: dict = {"note": args.note, "secs": args.secs, "host": args.host, "simulated": args.sim}
    acq = None
    try:
        with NodeControl(args.host, args.tcp_port) as node:
            hello = node.hello()
            meta["hello"] = hello
            print(f"Nodo: {hello.get('fw')} {hello.get('ver')}  fs={hello['fs_hz']} Hz  n_ch={hello['n_ch']}  "
                  f"os={hello.get('os')}")
            if args.txpower is not None:
                meta["txpower"] = node.txpower(args.txpower)
            if not args.no_arm:
                arm = node.arm()
                meta["arm"] = arm
                os_f = hello.get("os", 8)
                print("ARM (cuentas de 12 bits): " + "  ".join(
                    f"ch{c + 1} media {m / os_f:.0f} ± {s / os_f:.2f}" for c, (m, s) in
                    enumerate(zip(arm["mean"], arm["std"]))))
            acq = Acquisition(node, hello, out, frames_path, args.udp_port)
            acq.start()
            print(f"Grabando {args.secs:g} s… (Ctrl+C para terminar antes)")
            t_end = time.monotonic() + args.secs
            try:
                while time.monotonic() < t_end:
                    time.sleep(min(1.0, t_end - time.monotonic()))
            except KeyboardInterrupt:
                print("\nInterrumpido")
            finally:
                acq.stop()
    except (NodeError, OSError) as e:
        print(f"Error: {e}")
    finally:
        if sim:
            sim.close()

    if acq is None:
        return 1
    meta.update(clock=acq.clock(), start=acq.start_reply, stop_stats=acq.stop_stats, recording=acq.recording())
    meta_path.write_text(json.dumps(meta, indent=2, ensure_ascii=False))
    print_summary(meta["recording"], float(acq.fs), acq.stop_stats)
    print(f"\nGuardado: {out}\n          {frames_path}\n          {meta_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
