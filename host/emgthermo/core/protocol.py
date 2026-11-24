"""Protocolo binario nodo → PC. Contrapartida de firmware/emg_node/include/protocol.h.

Trama UDP (little-endian):
  off  tam  campo
  0    2    magic      0x4D45
  2    1    version
  3    1    n_ch
  4    1    n_samples
  5    1    os_factor
  6    2    flags
  8    4    seq
  12   8    t_us       tiempo del nodo de la PRIMERA muestra
  20   2*n_ch*n_samples  muestras uint16 intercaladas (s0ch1, s0ch2, s1ch1, ...)
  fin  2    crc16      CRC16/CCITT-FALSE de todo lo anterior
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

import numpy as np

MAGIC = 0x4D45
PROTOCOL_VERSION = 1
HEADER = struct.Struct("<HBBBBHIQ")  # magic, ver, n_ch, n_samples, os, flags, seq, t_us
HEADER_BYTES = HEADER.size  # 20
assert HEADER_BYTES == 20

FLAG_NAMES = {
    0: "SAT_CH1", 1: "SAT_CH2", 2: "FLAT_CH1", 3: "FLAT_CH2", 4: "DC_OOR_CH1", 5: "DC_OOR_CH2",
    6: "LINE_CH1", 7: "LINE_CH2", 8: "BATT_LOW", 9: "OVERRUN", 10: "SYNC_PULSE", 11: "SEND_RETRY",
}
FLAG = {name: 1 << bit for bit, name in FLAG_NAMES.items()}


def _crc_table() -> list[int]:
    table = []
    for i in range(256):
        crc = i << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
        table.append(crc)
    return table


_CRC_TABLE = _crc_table()


def crc16_ccitt_false(data: bytes) -> int:
    """CRC16/CCITT-FALSE (poly 0x1021, init 0xFFFF). crc16_ccitt_false(b"123456789") == 0x29B1."""
    crc = 0xFFFF
    for b in data:
        crc = ((crc << 8) & 0xFFFF) ^ _CRC_TABLE[(crc >> 8) ^ b]
    return crc


class FrameError(ValueError):
    """Trama inválida. `kind` es 'header' (tamaño/magic/versión) o 'crc'."""

    def __init__(self, kind: str, msg: str):
        super().__init__(msg)
        self.kind = kind


@dataclass
class Frame:
    version: int
    n_ch: int
    n_samples: int
    os_factor: int
    flags: int
    seq: int
    t_us: int
    samples: np.ndarray  # (n_samples, n_ch) uint16, sumas de os_factor conversiones

    def sample_times_us(self, fs_hz: float) -> np.ndarray:
        """Tiempo del nodo de cada muestra (µs), con fs nominal dentro de la trama."""
        return self.t_us + np.round(np.arange(self.n_samples) * 1e6 / fs_hz).astype(np.int64)


def decode_frame(pkt: bytes) -> Frame:
    if len(pkt) < HEADER_BYTES + 2:
        raise FrameError("header", f"trama corta ({len(pkt)} B)")
    magic, ver, n_ch, n_s, os_f, flags, seq, t_us = HEADER.unpack_from(pkt)
    if magic != MAGIC:
        raise FrameError("header", f"magic 0x{magic:04X}")
    if ver != PROTOCOL_VERSION:
        raise FrameError("header", f"versión de protocolo {ver} (se esperaba {PROTOCOL_VERSION})")
    n_data = n_ch * n_s
    if len(pkt) != HEADER_BYTES + 2 * n_data + 2:
        raise FrameError("header", f"tamaño {len(pkt)} B no corresponde a n_ch={n_ch}, n_samples={n_s}")
    (crc_rx,) = struct.unpack_from("<H", pkt, len(pkt) - 2)
    if crc16_ccitt_false(pkt[:-2]) != crc_rx:
        raise FrameError("crc", f"CRC inválido en seq={seq}")
    samples = np.frombuffer(pkt, "<u2", n_data, HEADER_BYTES).reshape(n_s, n_ch).copy()
    return Frame(ver, n_ch, n_s, os_f, flags, seq, t_us, samples)


def encode_frame(seq: int, t_us: int, samples: np.ndarray, os_factor: int = 8, flags: int = 0) -> bytes:
    """Inverso de decode_frame. samples: (n_samples, n_ch). Lo usan el simulador y las pruebas."""
    samples = np.asarray(samples, dtype="<u2")
    n_s, n_ch = samples.shape
    body = HEADER.pack(MAGIC, PROTOCOL_VERSION, n_ch, n_s, os_factor, flags, seq, t_us) + samples.tobytes()
    return body + struct.pack("<H", crc16_ccitt_false(body))


def flag_names(flags: int) -> list[str]:
    return [name for bit, name in FLAG_NAMES.items() if flags >> bit & 1]
