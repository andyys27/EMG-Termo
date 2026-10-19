#pragma once
// =====================================================================
// ADS131M04: constantes y funciones puras (sin Arduino/FreeRTOS), para poder
// probarlas en PC. Valores tomados del datasheet (familia ADS131M0x):
// VERIFICAR contra el datasheet durante el bring-up; el driver relee cada
// registro que escribe para detectar discrepancias.
// =====================================================================
#include <stddef.h>
#include <stdint.h>
#include "config.h"
#include "protocol.h"

namespace ads {

// Palabras de 24 bit (configuración por defecto, WLENGTH = 24 bit).
constexpr uint8_t N_ADC_CH    = 4;
constexpr uint8_t WORDS       = N_ADC_CH + 2;    // respuesta/STATUS + 4 canales + CRC
constexpr size_t  FRAME_BYTES = WORDS * 3;       // 18 bytes

// Comandos
constexpr uint16_t CMD_NULL    = 0x0000;
constexpr uint16_t CMD_RESET   = 0x0011;
constexpr uint16_t CMD_STANDBY = 0x0022;
constexpr uint16_t CMD_WAKEUP  = 0x0033;
constexpr uint16_t CMD_LOCK    = 0x0555;
constexpr uint16_t CMD_UNLOCK  = 0x0655;
constexpr uint16_t RESET_READY = 0xFF04;         // respuesta tras RESET (0xFF00 | nº de canales)

inline uint16_t cmdRreg(uint8_t addr, uint8_t n = 1) { return 0xA000 | (uint16_t(addr) << 7) | (n - 1); }
inline uint16_t cmdWreg(uint8_t addr, uint8_t n = 1) { return 0x6000 | (uint16_t(addr) << 7) | (n - 1); }
inline uint16_t ackWreg(uint8_t addr, uint8_t n = 1) { return 0x4000 | (uint16_t(addr) << 7) | (n - 1); }

// Registros
constexpr uint8_t REG_ID      = 0x00;
constexpr uint8_t REG_STATUS  = 0x01;
constexpr uint8_t REG_MODE    = 0x02;
constexpr uint8_t REG_CLOCK   = 0x03;
constexpr uint8_t REG_GAIN    = 0x04;
constexpr uint8_t REG_CFG     = 0x06;
constexpr uint8_t REG_CH0_CFG = 0x09;            // CHn_CFG = 0x09 + 5*n

constexpr uint8_t regChCfg(uint8_t ch) { return REG_CH0_CFG + 5 * ch; }

// CLOCK: CHn_EN en bits 8..11, XTAL_DIS bit 7, OSR[2:0] bits 4:2, PWR[1:0] bits 1:0 (0b10 = alta resolución).
// El valor por defecto del datasheet es 0x0F0E (4 canales, OSR=1024, alta resolución).
constexpr uint16_t clockRegValue(uint8_t n_ch, uint8_t osr_code, bool xtal_dis) {
  uint16_t v = 0;
  for (uint8_t c = 0; c < n_ch; ++c) v |= static_cast<uint16_t>(1u << (8 + c));
  if (xtal_dis) v |= (1u << 7);
  v |= static_cast<uint16_t>((osr_code & 0x7) << 2);
  v |= 0x2;
  return v;
}

// GAIN: PGAGAINn en 3 bits, con n = 0 en bits 2:0, n = 1 en 6:4, n = 2 en 10:8, n = 3 en 14:12.
constexpr uint16_t gainRegValue(uint8_t code) {
  return static_cast<uint16_t>((code & 7) | ((code & 7) << 4) | ((code & 7) << 8) | ((code & 7) << 12));
}

// CHn_CFG: MUX[1:0] bits 1:0 -> 00 entrada normal, 01 entradas en corto (medición del piso de ruido).
constexpr uint16_t CH_MUX_NORMAL = 0x0000;
constexpr uint16_t CH_MUX_SHORT  = 0x0001;

// ---- Lectura de tramas ----
inline uint32_t word24(const uint8_t* f, size_t i) {
  return (uint32_t(f[3 * i]) << 16) | (uint32_t(f[3 * i + 1]) << 8) | f[3 * i + 2];
}
inline uint16_t word16(const uint8_t* f, size_t i) {   // palabras de 16 bit alineadas a la izquierda
  return static_cast<uint16_t>((uint16_t(f[3 * i]) << 8) | f[3 * i + 1]);
}
inline int32_t signExtend24(uint32_t v) {
  return (v & 0x800000u) ? static_cast<int32_t>(v | 0xFF000000u) : static_cast<int32_t>(v);
}
// Muestra del canal ch (0..3): palabras 1..4 de la trama de salida.
inline int32_t sample(const uint8_t* f, uint8_t ch) { return signExtend24(word24(f, 1 + ch)); }

// CRC de la trama de salida: CRC-CCITT (0x1021, semilla 0xFFFF) sobre todos los bytes previos a la
// palabra de CRC (incluye el relleno). VERIFICAR en bring-up: por eso cfg::ADS_VERIFY_CRC parte en false.
inline bool frameCrcOk(const uint8_t* f) {
  const size_t crcWord = WORDS - 1;
  const uint16_t calc = proto::crc16_ccitt(f, crcWord * 3);
  return calc == word16(f, crcWord);
}

}  // namespace ads
