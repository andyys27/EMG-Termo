import csv
import json

import numpy as np
import pytest

from host.emgthermo.core.clock_sync import ClockModel, SyncPoint, measure
from host.emgthermo.core.node import NodeControl
from host.emgthermo.core.protocol import FrameError, crc16_ccitt_false, decode_frame, encode_frame
from host.emgthermo.launcher import run_session
from host.emgthermo.tools import capture_raw
from host.emgthermo.tools.simulate_device import SimNode


def test_crc_reference():
    assert crc16_ccitt_false(b"123456789") == 0x29B1


def test_frame_roundtrip_and_errors():
    s = np.arange(40, dtype=np.uint16).reshape(20, 2)
    pkt = encode_frame(7, 123456, s, flags=0x30)
    assert len(pkt) == 102
    fr = decode_frame(pkt)
    assert (fr.seq, fr.t_us, fr.flags, fr.n_ch, fr.n_samples) == (7, 123456, 0x30, 2, 20)
    assert np.array_equal(fr.samples, s)
    bad = bytearray(pkt)
    bad[30] ^= 1
    with pytest.raises(FrameError) as e:
        decode_frame(bytes(bad))
    assert e.value.kind == "crc"
    with pytest.raises(FrameError):
        decode_frame(pkt[:50])


def test_clock_model_drift():
    a = SyncPoint(1000.0, 0, 500, 400, 10, 3)
    b = SyncPoint(1100.0, 1_000_000, 500, 400, 10, 3)
    m = ClockModel.from_points(a, b)
    assert m.drift_ppm == pytest.approx(100)
    assert m.dev_to_pc_us(1_000_000) == pytest.approx(1_000_000 - 1100)


def test_clock_measure_against_sim():
    sim = SimNode("127.0.0.1", 0, drift_ppm=0).start_background()
    try:
        with NodeControl("127.0.0.1", sim.port) as node:
            sp = measure(node, 10)
        assert sp.rtt_us < 50_000
    finally:
        sim.close()


def test_session_auto_sim(tmp_path, free_udp_port):
    rc = run_session.main(["--sim", "--auto", "--time-scale", "0.05", "--subject", "TEST",
                           "--data-dir", str(tmp_path), "--udp-port", str(free_udp_port), "--sync-pings", "8"])
    assert rc == 0
    (folder,) = tmp_path.iterdir()
    meta = json.loads((folder / "session.json").read_text())
    for key in ("clock", "device_info", "recording", "stop_stats"):
        assert meta[key], key
    rec = meta["recording"]
    assert rec["frames"] > 50 and rec["loss_pct"] == 0 and rec["bad_frames"] == 0
    events = [r["event"] for r in csv.DictReader(open(folder / "events.csv"))]
    assert events[0] == "PREP_START" and "EMG_START" in events and "EMG_STOP" in events
    assert events.index("REST_START") < events.index("REST_END") < events.index("EMG_STOP")
    with open(folder / "emg_raw.csv") as f:
        rows = sum(1 for _ in f) - 1
    assert rows == rec["samples"] == rec["frames"] * 20


def test_capture_raw_sim(tmp_path, free_udp_port):
    out = tmp_path / "cap.csv"
    rc = capture_raw.main(["--sim", "--secs", "1", "--out", str(out), "--udp-port", str(free_udp_port)])
    assert rc == 0
    meta = json.loads(out.with_suffix(".json").read_text())
    assert 80 <= meta["recording"]["frames"] <= 110
    assert meta["arm"]["n"] == 2000
