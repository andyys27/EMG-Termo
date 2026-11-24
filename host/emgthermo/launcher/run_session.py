"""Lanzador de sesiones: conecta al nodo, graba EMG y registra los eventos del operador.

  # Prueba sin hardware (nodo simulado, secuencia automática 10× más rápida)
  python -m emgthermo.launcher.run_session --sim --auto --time-scale 0.1 --subject TEST

  # Sesión real
  python -m emgthermo.launcher.run_session --host <IP> --subject S01 [--no-thermal] [--mirror-port 5105]

Salida en data/<sujeto>_<fecha>/: emg_raw.csv, frames.csv, events.csv, session.json.
En modo interactivo se escribe un comando y Enter (escribir `h` para ver la lista).
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

from ..core.acquisition import Acquisition, print_summary
from ..core.node import NodeControl, NodeError
from ..core.session import Session

REPO = Path(__file__).resolve().parents[3]

# Eventos de inicio/fin: cada pulsación alterna _START / _END
TOGGLES = {
    "s": "SYNC_MOVE", "r": "REST", "m": "MVC", "1": "SERIE_1", "2": "SERIE_2",
    "p": "PAUSA", "v": "RECOVERY",
}
# Eventos puntuales
SINGLES = {
    "e": "ELECTRODES_PLACED", "a": "ACTIVITY_OBSERVED", "vid": "VIDEO_START",
}
THERMAL = {"t0": "THERMAL_T0_PRE", "t1": "THERMAL_T1_PAUSA", "t3": "THERMAL_T3_POST"}

HELP = """
Comandos (escribir y Enter):
  arm        ARM: mide el offset de reposo (solo antes de grabar)
  go         sincroniza reloj, START y empieza a grabar (EMG_START)
  stop / q   STOP, sincronía final y cierre (EMG_STOP)
  st         estado: tramas, pérdida, RSSI
  {toggles}
  {singles}
  {thermal}
  n <texto>  nota libre (NOTE)
  h          esta ayuda
"""


def load_stages(path: Path) -> list[tuple[str, float]]:
    """(evento, duración_s) de config/protocol_squat.yaml; valores por defecto si no se puede leer."""
    default = [("REST", 15), ("MVC", 15), ("PAUSA", 15), ("SERIE_1", 15), ("RECOVERY", 15)]
    try:
        import yaml

        cfg = yaml.safe_load(path.read_text())
        return [(s["event"], float(s["duration_s"])) for s in cfg["stages"]]
    except Exception as e:  # noqa: BLE001
        print(f"⚠ no se pudieron leer las etapas de {path} ({e}); uso las de por defecto")
        return default


class Operator:
    def __init__(self, args, session: Session, node: NodeControl, hello: dict):
        self.args, self.session, self.node, self.hello = args, session, node, hello
        self.acq: Acquisition | None = None
        self.open_toggles: set[str] = set()
        self.arm_result: dict | None = None

    # --- acciones -------------------------------------------------------------------------------
    def arm(self) -> None:
        if self.acq and self.acq.running:
            print("ARM solo antes de grabar")
            return
        res = self.node.arm()
        self.arm_result = res
        os_f = self.hello.get("os", 8)
        means = [m / os_f for m in res["mean"]]
        self.session.event("ARM", "mean_raw=" + "/".join(f"{m:.0f}" for m in means))
        for c, m in enumerate(means):
            if not 1000 <= m <= 3100:
                print(f"  ⚠ ch{c + 1}: media {m:.0f} cuentas, lejos de ~2000 (1.65 V). ¿AFE conectado y polarizado?")
        self.session.meta.setdefault("arm_history", []).append(res)

    def go(self) -> None:
        if self.acq:
            print("Ya se grabó o se está grabando en esta sesión")
            return
        self.acq = Acquisition(self.node, self.hello, self.session.raw_path, self.session.frames_path,
                               self.args.udp_port, self.args.mirror_port, self.args.sync_pings)
        self.session.event("CLOCK_SYNC_START")
        self.acq.start()
        sp = self.acq.sync_start
        print(f"  reloj: offset {sp.offset_us / 1e6:.3f} s, RTT {sp.rtt_us / 1e3:.1f} ms")
        self.session.event("EMG_START", f"t_us={self.acq.start_reply.get('t_us')}")

    def stop(self) -> None:
        if not (self.acq and self.acq.running):
            return
        for name in sorted(self.open_toggles):
            self.session.event(f"{name}_END", "cerrado automáticamente")
        self.open_toggles.clear()
        self.acq.stop()
        self.session.event("EMG_STOP")

    def toggle(self, name: str) -> None:
        if name in self.open_toggles:
            self.open_toggles.remove(name)
            self.session.event(f"{name}_END")
        else:
            if not (self.acq and self.acq.running):
                print(f"  (aviso: {name}_START sin grabación en curso)")
            self.open_toggles.add(name)
            self.session.event(f"{name}_START")

    def status(self) -> None:
        if self.acq and self.acq.running:
            r = self.acq.recording()
            print(f"  tramas {r['frames']}  pérdida {r['loss_pct']:.2f} %  hueco máx {r['max_gap_ms']:.0f} ms  "
                  f"inválidas {r['bad_frames']}")
        else:
            print("  sin grabación en curso")
        try:
            st = self.node.status()
            print(f"  nodo: RSSI {st.get('rssi')} dBm  overruns {st.get('overruns')}  "
                  f"send_errors {st.get('send_errors')}")
        except NodeError as e:
            print(f"  ⚠ STATUS falló: {e}")

    # --- modos -------------------------------------------------------------------------------
    def interactive(self) -> None:
        thermal = {} if self.args.no_thermal else THERMAL
        fmt = lambda d, suf="": "  ".join(f"{k}={v}{suf}" for k, v in d.items())  # noqa: E731
        help_txt = HELP.format(toggles="Inicio/fin: " + fmt(TOGGLES), singles="Puntuales: " + fmt(SINGLES),
                               thermal=("Térmicas: " + fmt(thermal)) if thermal else "")
        print(help_txt)
        while True:
            try:
                line = input("> ").strip()
            except EOFError:
                line = "q"
            if not line:
                continue
            key, _, rest = line.partition(" ")
            key = key.lower()
            try:
                if key in ("q", "stop", "quit", "exit"):
                    if self.acq and self.acq.running:
                        self.stop()
                    return
                elif key == "arm":
                    self.arm()
                elif key == "go":
                    self.go()
                elif key == "st":
                    self.status()
                elif key in ("h", "help", "?"):
                    print(help_txt)
                elif key == "n":
                    self.session.event("NOTE", rest)
                elif key in TOGGLES:
                    self.toggle(TOGGLES[key])
                elif key in SINGLES:
                    self.session.event(SINGLES[key])
                elif key in thermal:
                    self.session.event(thermal[key])
                else:
                    print("  comando desconocido (h = ayuda)")
            except NodeError as e:
                print(f"  ⚠ {e}")

    def auto(self) -> None:
        """Secuencia automática según las etapas del protocolo (para pruebas)."""
        scale = self.args.time_scale
        stages = load_stages(Path(self.args.protocol))
        wait = lambda s: time.sleep(s * scale)  # noqa: E731
        self.session.event("ELECTRODES_PLACED", "auto")
        self.arm()
        if not self.args.no_thermal:
            self.session.event("THERMAL_T0_PRE", "auto")
        self.session.event("VIDEO_START", "auto")
        self.go()
        self.toggle("SYNC_MOVE")
        wait(3)
        self.toggle("SYNC_MOVE")
        for name, dur in stages:
            self.toggle(name)
            if name == "PAUSA" and not self.args.no_thermal:
                self.session.event("THERMAL_T1_PAUSA", "auto")
            wait(dur)
            self.toggle(name)
        self.stop()
        if not self.args.no_thermal:
            self.session.event("THERMAL_T3_POST", "auto")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", help="IP del nodo (monitor serie)")
    ap.add_argument("--tcp-port", type=int, default=5006)
    ap.add_argument("--udp-port", type=int, default=5005)
    ap.add_argument("--subject", required=True, help="código del sujeto, p. ej. S01")
    ap.add_argument("--data-dir", default=str(REPO / "data"))
    ap.add_argument("--no-thermal", action="store_true", help="omitir eventos de termografía")
    ap.add_argument("--mirror-port", type=int, help="reenviar copia de las tramas a 127.0.0.1:<puerto> (monitor)")
    ap.add_argument("--txpower", type=float, help="potencia Wi-Fi del nodo en dBm")
    ap.add_argument("--sync-pings", type=int, default=30)
    ap.add_argument("--sim", action="store_true", help="usar un nodo simulado local")
    ap.add_argument("--auto", action="store_true", help="secuencia automática de eventos")
    ap.add_argument("--time-scale", type=float, default=1.0, help="multiplica las duraciones en --auto")
    ap.add_argument("--no-qc", action="store_true", help="no correr el QC al terminar")
    ap.add_argument("--protocol", default=str(REPO / "config" / "protocol_squat.yaml"))
    args = ap.parse_args(argv)

    sim = None
    if args.sim:
        from ..tools.simulate_device import SimNode

        sim = SimNode("127.0.0.1", 0).start_background()
        args.host, args.tcp_port = "127.0.0.1", sim.port
        print(f"Nodo simulado en 127.0.0.1:{sim.port}")
    if not args.host:
        ap.error("falta --host (o usar --sim)")

    session = Session.create(args.data_dir, args.subject)
    print(f"Sesión: {session.folder}")
    session.event("PREP_START")
    op = None
    try:
        node = NodeControl(args.host, args.tcp_port)
        hello = node.hello()
        session.event("SYSTEM_CONNECTED", f"{hello.get('fw')} {hello.get('ver')}")
        if hello.get("proto") not in (None, 1):
            print(f"⚠ el nodo usa protocolo {hello.get('proto')}; este programa espera 1")
        if args.txpower is not None:
            session.meta["txpower"] = node.txpower(args.txpower)
        session.meta.update(device_info=hello, host=args.host, protocol_version=hello.get("proto"),
                            fs_hz=hello.get("fs_hz"), os_factor=hello.get("os"), n_ch=hello.get("n_ch"),
                            simulated=bool(args.sim))
        op = Operator(args, session, node, hello)
        op.auto() if args.auto else op.interactive()
    except KeyboardInterrupt:
        print("\nInterrumpido; cerrando la grabación…")
    except (NodeError, OSError) as e:
        print(f"Error: {e}")
    finally:
        if op:
            op.stop()
            session.meta["arm"] = op.arm_result
            if op.acq:
                session.meta.update(clock=op.acq.clock(), stop_stats=op.acq.stop_stats,
                                    start=op.acq.start_reply, recording=op.acq.recording())
        session.close()
        if sim:
            sim.close()

    if op and op.acq:
        print_summary(session.meta["recording"], float(op.hello["fs_hz"]), session.meta.get("stop_stats"))
        if not args.no_qc:
            try:
                from ..validation.qc import qc_path

                qc_path(session.folder)
            except Exception as e:  # noqa: BLE001  El QC nunca debe impedir guardar la sesión
                print(f"⚠ QC falló: {e}")
    print(f"\nGuardado en {session.folder}")
    return 0 if op and op.acq else 1


if __name__ == "__main__":
    sys.exit(main())
