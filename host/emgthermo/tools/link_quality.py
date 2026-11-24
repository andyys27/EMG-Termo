"""link_quality.py — Calidad del enlace Wi-Fi y del ADC del nodo EMG.

Uso:
  # Grabar 30 s y analizar (opcionalmente fijando la potencia TX del nodo)
  python -m emgthermo.tools.link_quality run --ip 192.168.1.50 --secs 30 --txpower 19.5 --save tx_alta.npz
  python -m emgthermo.tools.link_quality run --ip 192.168.1.50 --secs 30 --txpower 2    --save tx_baja.npz

  # Comparar dos grabaciones (p. ej. entrada en corto con TX alta vs. baja)
  python -m emgthermo.tools.link_quality compare tx_alta.npz tx_baja.npz

Reporta:
  Enlace: tramas perdidas (huecos en seq), duplicadas/desordenadas, CRC malos, jitter de llegada,
          continuidad de t_us, RSSI y contadores del nodo (send_errors, overruns, lost_conv).
  ADC:    por canal, media, ruido RMS y pico a pico (en cuentas de 12 bits) y los picos del espectro.

Para medir el ruido propio del ADC, poner la entrada en corto a la referencia (1.65 V).
Requiere numpy; matplotlib es opcional (--plot).
"""
import argparse
import json
import select
import socket
import sys
import time

import numpy as np

from ..core.node import NodeControl
from ..core.protocol import FLAG_NAMES, FrameError, decode_frame


def record(args):
    ctl = NodeControl(args.ip, args.tcp_port)
    hello = ctl.cmd("HELLO", "OK HELLO")
    print(f"Nodo: {hello['fw']} {hello['ver']}  fs={hello['fs_hz']} Hz  n_ch={hello['n_ch']}  os={hello['os']}")
    if args.txpower is not None:
        print("TXPOWER:", ctl.cmd(f"TXPOWER {args.txpower:g}", "OK TXPOWER"))

    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    udp.bind(("", args.udp_port))
    udp.setblocking(False)

    frames, arrivals, statuses = [], [], []
    ctl.cmd(f"START {args.udp_port}", "OK START")
    t0 = time.time()
    next_status = t0 + 1.0
    try:
        while time.time() - t0 < args.secs:
            r, _, _ = select.select([udp], [], [], 0.05)
            if r:
                while True:
                    try:
                        pkt = udp.recv(2048)
                    except BlockingIOError:
                        break
                    arrivals.append(time.perf_counter())
                    frames.append(pkt)
            if time.time() >= next_status:
                st = ctl.cmd("STATUS", "OK STATUS")
                st["t_host"] = time.time() - t0
                statuses.append(st)
                next_status += 1.0
    finally:
        stop = ctl.cmd("STOP", "OK STOP")
        # Vaciar lo que quede en el socket
        time.sleep(0.1)
        while True:
            try:
                frames.append(udp.recv(2048))
                arrivals.append(time.perf_counter())
            except BlockingIOError:
                break
    return hello, frames, np.array(arrivals), statuses, stop


def decode(frames, arrivals):
    seqs, tus, flags, data, arr = [], [], [], [], []
    bad_crc = bad_hdr = 0
    n_ch = n_s = os_f = None
    for pkt, ta in zip(frames, arrivals):
        try:
            fr = decode_frame(pkt)
        except FrameError as e:
            if e.kind == "crc":
                bad_crc += 1
            else:
                bad_hdr += 1
            continue
        n_ch, n_s, os_f = fr.n_ch, fr.n_samples, fr.os_factor
        seqs.append(fr.seq)
        tus.append(fr.t_us)
        flags.append(fr.flags)
        arr.append(ta)
        data.append(fr.samples)
    return dict(seq=np.array(seqs, np.int64), t_us=np.array(tus, np.int64), flags=np.array(flags, np.int64),
                arrival=np.array(arr), data=np.array(data), bad_crc=bad_crc, bad_hdr=bad_hdr,
                n_ch=n_ch, n_s=n_s, os=os_f)


def link_report(d, fs, statuses, stop):
    seq = d["seq"]
    print("\n=== Enlace ===")
    if len(seq) == 0:
        print("No llegó ninguna trama válida.")
        return
    expected = int(seq.max()) + 1
    uniq = np.unique(seq)
    lost = expected - len(uniq)
    dup = len(seq) - len(uniq)
    reorder = int(np.sum(np.diff(seq) < 0))
    print(f"Tramas recibidas: {len(seq)}  esperadas: {expected}  enviadas según nodo: {stop.get('frames')}")
    print(f"Perdidas: {lost} ({100 * lost / expected:.3f} %)   duplicadas: {dup}   desordenadas: {reorder}")
    print(f"CRC malos: {d['bad_crc']}   cabecera inválida: {d['bad_hdr']}")
    order = np.argsort(seq, kind="stable")
    gaps = np.diff(seq[order])
    if np.any(gaps > 1):
        g = gaps[gaps > 1] - 1
        print(f"Ráfagas de pérdida: {len(g)}  la mayor: {g.max()} tramas ({g.max() * d['n_s'] / fs * 1e3:.0f} ms)")

    # Continuidad del reloj del nodo: cada trama debe avanzar n_s/fs
    frame_us = d["n_s"] / fs * 1e6
    dts = np.diff(d["t_us"][order]) / np.maximum(gaps, 1)
    err = dts - frame_us
    print(f"t_us por trama: media {dts.mean():.1f} µs (ideal {frame_us:.0f}), desviación máx {np.abs(err).max():.1f} µs")

    # Jitter de llegada al PC
    ia = np.diff(d["arrival"]) * 1e3
    p = np.percentile(ia, [50, 95, 99, 99.9])
    print(f"Intervalo de llegada (ms): mediana {p[0]:.2f}  p95 {p[1]:.2f}  p99 {p[2]:.2f}  p99.9 {p[3]:.2f}  máx {ia.max():.1f}")
    # Retardo relativo: llegada - tiempo del nodo (sin offset absoluto) → buffer necesario
    rel = (d["arrival"] - d["t_us"] / 1e6) * 1e3
    rel -= rel.min()
    print(f"Retardo relativo (ms): p99 {np.percentile(rel, 99):.1f}  máx {rel.max():.1f}  → buffer sugerido ≥ {rel.max() * 1.5:.0f} ms")

    print(f"Nodo: send_errors={stop.get('send_errors')}  overruns={stop.get('overruns')}  "
          f"lost_conv={stop.get('lost_conv')}  bad_conv={stop.get('bad_conv')}")
    if statuses:
        rssi = np.array([s["rssi"] for s in statuses])
        print(f"RSSI (dBm): media {rssi.mean():.1f}  mín {rssi.min()}  máx {rssi.max()}"
              + ("   ⚠ < -70 dBm: esperar pérdidas" if rssi.min() < -70 else ""))
        if "tx_power_qdbm" in statuses[0]:
            print(f"Potencia TX: {statuses[0]['tx_power_qdbm'] / 4:g} dBm")

    counts = {FLAG_NAMES[b]: int(np.sum((d["flags"] >> b) & 1)) for b in FLAG_NAMES}
    counts = {k: v for k, v in counts.items() if v}
    print("Banderas:", counts if counts else "ninguna")


def signal_metrics(x, fs, os_f, n_peaks=5):
    """x: sumas (uint16) de un canal. Devuelve métricas en cuentas de una conversión (12 bits)."""
    v = x.astype(np.float64) / os_f
    ac = v - v.mean()
    n = len(ac)
    win = np.hanning(n)
    spec = np.abs(np.fft.rfft(ac * win)) ** 2 / (fs * np.sum(win ** 2))
    spec[1:-1] *= 2
    f = np.fft.rfftfreq(n, 1 / fs)
    m = f > 1  # ignorar DC
    idx = np.argsort(spec[m])[::-1]
    peaks, used = [], []
    for i in idx:
        fi = f[m][i]
        if all(abs(fi - u) > 2 for u in used):
            used.append(fi)
            peaks.append((fi, spec[m][i]))
        if len(peaks) == n_peaks:
            break
    noise_floor = np.median(spec[m])
    return dict(mean=v.mean(), rms=ac.std(), ptp=np.ptp(ac), f=f, psd=spec,
                peaks=[(fi, 10 * np.log10(p / noise_floor)) for fi, p in peaks])


def adc_report(d, fs, label=""):
    print(f"\n=== ADC {label}===")
    # Concatenar solo tramas consecutivas más largas para no meter huecos en la FFT
    order = np.argsort(d["seq"], kind="stable")
    seq = d["seq"][order]
    data = d["data"][order]
    breaks = np.where(np.diff(seq) != 1)[0]
    starts = np.r_[0, breaks + 1]
    ends = np.r_[breaks + 1, len(seq)]
    k = np.argmax(ends - starts)
    block = data[starts[k]:ends[k]].reshape(-1, d["n_ch"])
    print(f"Segmento continuo más largo: {len(block) / fs:.1f} s")
    out = []
    for c in range(d["n_ch"]):
        mt = signal_metrics(block[:, c], fs, d["os"])
        out.append(mt)
        lsb_mv = 3300 / 4096
        print(f"ch{c + 1}: media {mt['mean']:.1f}  ruido RMS {mt['rms']:.2f} cuentas ({mt['rms'] * lsb_mv:.2f} mV)  "
              f"pico a pico {mt['ptp']:.1f}")
        print("      picos (Hz, dB sobre el piso): " + ", ".join(f"{fi:.1f} ({db:.0f})" for fi, db in mt["peaks"]))
    return out


def cmd_run(args):
    hello, frames, arrivals, statuses, stop = record(args)
    d = decode(frames, arrivals)
    fs = hello["fs_hz"]
    link_report(d, fs, statuses, stop)
    if len(d["seq"]):
        metrics = adc_report(d, fs)
        if args.save:
            np.savez_compressed(args.save, seq=d["seq"], t_us=d["t_us"], flags=d["flags"], arrival=d["arrival"],
                                data=d["data"], fs=fs, os=d["os"], n_ch=d["n_ch"], n_s=d["n_s"],
                                bad_crc=d["bad_crc"], bad_hdr=d["bad_hdr"],
                                meta=json.dumps(dict(hello=hello, stop=stop, statuses=statuses, txpower=args.txpower)))
            print(f"\nGuardado en {args.save}")
        if args.plot:
            plot([(args.save or "run", fs, metrics)])


def load(path):
    z = np.load(path, allow_pickle=False)
    d = {k: z[k] for k in ("seq", "t_us", "flags", "arrival", "data")}
    d.update(n_ch=int(z["n_ch"]), n_s=int(z["n_s"]), os=int(z["os"]), bad_crc=int(z["bad_crc"]),
             bad_hdr=int(z["bad_hdr"]))
    return d, int(z["fs"]), json.loads(str(z["meta"]))


def cmd_compare(args):
    runs = []
    for p in args.files:
        d, fs, meta = load(p)
        link_report(d, fs, meta["statuses"], meta["stop"])
        runs.append((p, fs, adc_report(d, fs, f"[{p}] ")))
    print("\n=== Comparación de ruido RMS (cuentas) ===")
    base = runs[0][2]
    for name, _, mts in runs:
        cells = []
        for c, mt in enumerate(mts):
            ratio = mt["rms"] / base[c]["rms"] if base[c]["rms"] else float("nan")
            cells.append(f"ch{c + 1} {mt['rms']:.2f} (x{ratio:.2f})")
        print(f"{name:30s} " + "   ".join(cells))
    print("Si el ruido cambia >10 % entre condiciones de Wi-Fi con la entrada en corto, hay acoplamiento.")
    if args.plot:
        plot(runs)


def plot(runs):
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib no instalado; omito la gráfica")
        return
    n_ch = len(runs[0][2])
    fig, axes = plt.subplots(n_ch, 1, sharex=True, figsize=(10, 3 * n_ch), squeeze=False)
    for name, _, mts in runs:
        for c, mt in enumerate(mts):
            axes[c, 0].semilogy(mt["f"][1:], mt["psd"][1:], lw=0.8, label=name)
    for c in range(n_ch):
        axes[c, 0].set_ylabel(f"ch{c + 1} PSD (cuentas²/Hz)")
        axes[c, 0].grid(True, which="both", alpha=0.3)
        axes[c, 0].legend()
    axes[-1, 0].set_xlabel("Frecuencia (Hz)")
    plt.tight_layout()
    plt.show()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="grabar del nodo y analizar")
    r.add_argument("--ip", required=True)
    r.add_argument("--tcp-port", type=int, default=5006)
    r.add_argument("--udp-port", type=int, default=5005)
    r.add_argument("--secs", type=float, default=30)
    r.add_argument("--txpower", type=float, help="dBm: 2 5 7 8.5 11 13 15 17 18.5 19 19.5")
    r.add_argument("--save", help="archivo .npz para guardar la grabación")
    r.add_argument("--plot", action="store_true")
    c = sub.add_parser("compare", help="comparar grabaciones .npz")
    c.add_argument("files", nargs="+")
    c.add_argument("--plot", action="store_true")
    args = ap.parse_args()
    try:
        cmd_run(args) if args.cmd == "run" else cmd_compare(args)
    except (OSError, RuntimeError, TimeoutError) as e:
        sys.exit(f"Error: {e}")


if __name__ == "__main__":
    main()
