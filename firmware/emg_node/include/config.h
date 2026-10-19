#pragma once
// =====================================================================
// Configuración del nodo EMG. Debe coincidir con config/devices.yaml
// ADC: ADS131M04 (24 bit, 4 canales simultáneos, SPI). Se usan 2 canales.
// =====================================================================
#include <stdint.h>
#include <stddef.h>

#define FW_NAME    "emg_node"
#define FW_VERSION "0.2.0"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID "CAMBIAR_SSID"
#define WIFI_PASS "CAMBIAR_CLAVE"
#endif

namespace cfg {

// ---- Adquisición ----
constexpr uint8_t DEVICE_ID = 1;   // 1 = EMG
constexpr uint8_t N_CH      = 2;   // VL y BF (el ADS131M04 tiene 4; los canales 3 y 4 se deshabilitan)
static_assert(N_CH >= 1 && N_CH <= 4, "N_CH debe estar entre 1 y 4");

// ---- ADS131M04 ----
// fs = CLKIN / (2 * OSR). Con CLKIN = 8.192 MHz y OSR = 2048  ->  fs = 2000 Hz exactos.
constexpr uint32_t ADS_CLKIN_HZ = 8192000;
constexpr uint8_t  ADS_OSR_CODE = 4;   // 0:128 1:256 2:512 3:1024 4:2048 5:4096 6:8192 7:16256

constexpr uint16_t osrFromCode(uint8_t c) {
  return c == 0 ? 128 : c == 1 ? 256 : c == 2 ? 512 : c == 3 ? 1024 : c == 4 ? 2048 : c == 5 ? 4096 : c == 6 ? 8192 : 16256;
}
constexpr uint16_t ADS_OSR = osrFromCode(ADS_OSR_CODE);
constexpr uint32_t FS_HZ   = ADS_CLKIN_HZ / (2UL * ADS_OSR);
static_assert(FS_HZ * 2UL * ADS_OSR == ADS_CLKIN_HZ, "CLKIN/OSR no da una fs entera");

// Reloj del ADC: false = el ESP32-S3 genera CLKIN con LEDC (8.192 MHz salen de un divisor fraccionario
// de 80 MHz, con algo de jitter). true = oscilador externo de 8.192 MHz conectado a CLKIN (recomendado si la
// prueba de ruido muestra degradación por el jitter del LEDC).
constexpr bool ADS_CLKIN_EXTERNAL = false;
constexpr bool ADS_XTAL_DIS       = true;    // CLKIN recibe un reloj CMOS, no un cristal (VERIFICAR en datasheet)

constexpr uint8_t  ADS_PGA_CODE = 0;         // 0:x1 1:x2 2:x4 3:x8 4:x16 5:x32 6:x64 7:x128
constexpr uint8_t  ADS_PGA_GAIN = 1;
constexpr uint32_t ADS_SPI_HZ   = 8000000;
constexpr bool     ADS_VERIFY_CRC = false;   // activar cuando el CRC coincida en la prueba de bring-up

constexpr float ADS_VREF_V   = 1.2f;                    // referencia interna
constexpr float ADC_LSB_UV   = ADS_VREF_V * 1e6f / (8388608.0f * ADS_PGA_GAIN);   // µV por cuenta, en el pin del ADC
constexpr float ANALOG_GAIN  = 400.0f;                  // PENDIENTE: ganancia analógica real del front-end (igual que devices.yaml)
constexpr int32_t SAT_ABS_COUNTS = 8300000;             // ≈ 99 % de la escala completa (2^23 = 8388608)

// ---- Bloques / paquetes ----
constexpr size_t BLOCK_SAMPLES   = 10;    // muestras por canal por paquete (5 ms a 2 kHz)
constexpr size_t BLOCK_QUEUE_LEN = 400;   // bloques en cola = 2 s de colchón ante microcortes de Wi-Fi

// ---- Red ----
constexpr uint16_t TCP_PORT         = 5000;
constexpr uint16_t DEFAULT_UDP_PORT = 5001;

// ---- Tareas FreeRTOS ----
constexpr int      SAMPLER_CORE     = 1;   // muestreo en el core 1; el Wi-Fi corre en el core 0
constexpr int      NET_CORE         = 0;
constexpr uint32_t SAMPLER_STACK    = 4096;
constexpr int      SAMPLER_PRIORITY = 20;

}  // namespace cfg
