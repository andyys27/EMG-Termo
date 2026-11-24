// protocol.h — Contrato binario nodo → PC. Debe coincidir con host/emgthermo/core/protocol.py.
// Sin dependencias de Arduino: se compila también en la PC (test/test_validator_host.cpp).
//
// Trama UDP (little-endian, 102 bytes con N_CH=2 y 20 muestras):
//   off  tam  campo
//   0    2    magic      0x4D45 ("EM" en bytes 'E','M')
//   2    1    version    PROTOCOL_VERSION
//   3    1    n_ch
//   4    1    n_samples
//   5    1    os_factor
//   6    2    flags      ver FLAG_*
//   8    4    seq        contador de tramas desde START (empieza en 0)
//   12   8    t_us       tiempo del nodo (esp_timer) de la PRIMERA muestra
//   20   2*n_ch*n_samples  muestras uint16 intercaladas: s0ch1, s0ch2, s1ch1, ...
//   fin  2    crc16      CRC16/CCITT-FALSE de todos los bytes anteriores
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "config.h"

#define FRAME_MAGIC        0x4D45u
#define FRAME_HEADER_BYTES 20
#define FRAME_DATA_BYTES   (2 * N_CH * SAMPLES_PER_FRAME)
#define FRAME_BYTES        (FRAME_HEADER_BYTES + FRAME_DATA_BYTES + 2)

// Banderas (uint16). Las de canal tienen un bit por canal: bit0 = ch1, bit1 = ch2.
enum : uint16_t {
  FLAG_SAT_CH1      = 1u << 0,   // Alguna muestra cerca de 0 o del máximo
  FLAG_SAT_CH2      = 1u << 1,
  FLAG_FLAT_CH1     = 1u << 2,   // Señal plana en el último bloque (electrodo suelto, corto)
  FLAG_FLAT_CH2     = 1u << 3,
  FLAG_DC_OOR_CH1   = 1u << 4,   // Media del bloque lejos de 1.65 V
  FLAG_DC_OOR_CH2   = 1u << 5,
  FLAG_LINE_CH1     = 1u << 6,   // Mucha potencia a 60 Hz (Goertzel)
  FLAG_LINE_CH2     = 1u << 7,
  FLAG_BATT_LOW     = 1u << 8,   // Reservado
  FLAG_OVERRUN      = 1u << 9,   // Se perdieron conversiones (pool del DMA lleno)
  FLAG_SYNC_PULSE   = 1u << 10,  // Pulso de sincronía activo durante esta trama
  FLAG_SEND_RETRY   = 1u << 11,  // La trama anterior no se pudo enviar
};

static_assert(FRAME_HEADER_BYTES == 2 + 1 + 1 + 1 + 1 + 2 + 4 + 8, "cabecera de 20 B");

// CRC16/CCITT-FALSE: poly 0x1021, init 0xFFFF, sin reflejar, xorout 0. CRC("123456789") = 0x29B1.
static inline uint16_t crc16_ccitt_false(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; ++b)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

static inline void put_u16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static inline void put_u32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (v >> (8 * i)) & 0xFF; }
static inline void put_u64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (v >> (8 * i)) & 0xFF; }

// samples: [SAMPLES_PER_FRAME][N_CH] intercaladas. Devuelve FRAME_BYTES.
static inline size_t frame_encode(uint8_t* out, uint32_t seq, uint64_t t_us, uint16_t flags,
                                  const uint16_t* samples) {
  put_u16(out + 0, FRAME_MAGIC);
  out[2] = PROTOCOL_VERSION;
  out[3] = N_CH;
  out[4] = SAMPLES_PER_FRAME;
  out[5] = OS_FACTOR;
  put_u16(out + 6, flags);
  put_u32(out + 8, seq);
  put_u64(out + 12, t_us);
  for (int i = 0; i < N_CH * SAMPLES_PER_FRAME; ++i) put_u16(out + FRAME_HEADER_BYTES + 2 * i, samples[i]);
  const size_t body = FRAME_HEADER_BYTES + FRAME_DATA_BYTES;
  put_u16(out + body, crc16_ccitt_false(out, body));
  return FRAME_BYTES;
}

// Comandos TCP (una línea de texto ASCII, terminada en '\n'). Respuestas en una línea:
//   HELLO            → OK HELLO {"fw":..,"ver":..,"proto":..,"fs_hz":..,"n_ch":..,"os":..,"spf":..,"mac":..}
//   PING             → PONG <t_us>
//   ARM              → OK ARM {"n":..,"mean":[..],"std":[..],"min":[..],"max":[..],"flags":..}   (en sumas)
//   START [puerto]   → OK START {"port":..,"t_us":..}
//   STOP             → OK STOP {"frames":..,"send_errors":..,"overruns":..,"lost_conv":..,"t_start_us":..,"t_stop_us":..}
//   STATUS           → OK STATUS {"streaming":..,"frames":..,"seq_next":..,"send_errors":..,"overruns":..,
//                                 "lost_conv":..,"bad_conv":..,"rssi":..,"tx_power_qdbm":..,"heap":..,"uptime_us":..}
//   TXPOWER <dBm>    → OK TXPOWER {"qdbm":..}   (potencia Wi-Fi en cuartos de dBm)
//   SYNC_PULSE       → OK SYNC_PULSE {"t_us":..}   (solo con USE_SYNC_PULSE=1)
//   otro             → ERR <motivo>
