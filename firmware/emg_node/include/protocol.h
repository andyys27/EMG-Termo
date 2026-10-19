#pragma once
// =====================================================================
// Protocolo de datos (UDP) y de control (TCP) del nodo EMG.
// Espejo en Python: host/emgthermo/core/protocol.py
// VERSION 2: las muestras son int32 (24 bit del ADS131M04 con signo).
// =====================================================================
#include <stdint.h>
#include <stddef.h>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "El protocolo asume little-endian");

namespace proto {

constexpr uint8_t MAGIC0  = 0xE5;
constexpr uint8_t MAGIC1  = 0x6D;
constexpr uint8_t VERSION = 2;
constexpr uint8_t DEV_EMG = 1;

// Bits de flags
constexpr uint8_t FLAG_OVERRUN    = 1 << 0;   // se perdieron muestras/bloques antes de este bloque (DRDY perdido o cola llena)
constexpr uint8_t FLAG_SATURATION = 1 << 1;   // alguna muestra cerca de la escala completa
constexpr uint8_t FLAG_ADC_ERROR  = 1 << 2;   // error de lectura del ADC (CRC de trama, SPI)

#pragma pack(push, 1)
struct FrameHeader {
  uint8_t  magic[2];     // 0xE5 0x6D
  uint8_t  version;      // 2
  uint8_t  device_id;    // 1 = EMG
  uint32_t seq;          // contador de paquetes (bloques); un salto indica pérdida
  int64_t  t_us;         // reloj del dispositivo (µs) de la primera muestra del paquete
  uint16_t n_samples;    // muestras por canal
  uint8_t  n_channels;
  uint8_t  flags;
};
#pragma pack(pop)
static_assert(sizeof(FrameHeader) == 20, "FrameHeader debe medir 20 bytes");

// Cuerpo: n_samples * n_channels valores int32 (24 bit con signo, extendidos), intercalados por muestra:
//   [s0_ch1, s0_ch2, s1_ch1, s1_ch2, ...]
// Cierre: CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF) sobre encabezado + cuerpo, uint16 little-endian.
constexpr size_t CRC_BYTES = 2;

constexpr size_t frameSize(size_t n_samples, size_t n_channels) {
  return sizeof(FrameHeader) + n_samples * n_channels * sizeof(int32_t) + CRC_BYTES;
}

// CRC-16/CCITT-FALSE. Vector de prueba: "123456789" -> 0x29B1
inline uint16_t crc16_ccitt(const uint8_t* data, size_t len, uint16_t crc = 0xFFFF) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; ++b)
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
  }
  return crc;
}

// ---- Canal de control TCP: texto ASCII, un comando por línea terminada en '\n' ----
//   HELLO                      -> OK HELLO dev=1 proto=2 fw=<ver> fs=<hz> ch=<n> block=<n> adc=ADS131M04
//   PING <t0>                  -> OK PONG <t0> <t_dev_us>
//   CONFIG key=value ...       -> OK
//   ARM                        -> OK ARM noise1=<> noise2=<> sat=<0|1>
//   START <udp_port>           -> OK START <t0_dev_us>    (el destino UDP es la IP del cliente TCP)
//   STOP                       -> OK STOP blocks=<n> dropped=<n> missed=<n>
//   STATUS                     -> OK STATUS batt_mv=<> rssi=<> blocks=<> dropped=<> missed=<> state=<>
//   SYNC_PULSE                 -> OK SYNC <t_dev_us>
// Errores: ERR <codigo> <detalle>
constexpr const char* CMD_HELLO      = "HELLO";
constexpr const char* CMD_PING       = "PING";
constexpr const char* CMD_CONFIG     = "CONFIG";
constexpr const char* CMD_ARM        = "ARM";
constexpr const char* CMD_START      = "START";
constexpr const char* CMD_STOP       = "STOP";
constexpr const char* CMD_STATUS     = "STATUS";
constexpr const char* CMD_SYNC_PULSE = "SYNC_PULSE";

}  // namespace proto
