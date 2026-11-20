// config.h — Parámetros del nodo EMG. Un solo lugar para cambiarlos.
// Debe coincidir con config/devices.yaml (lado Python).
#pragma once
#include <stdint.h>

#define FW_NAME     "emg_node"
#define FW_VERSION  "0.2.0"
#define PROTOCOL_VERSION 1

// Canales
// GPIO3 = ADC1_CH2 (VL), GPIO4 = ADC1_CH3 (BF).
#define N_CH 2
static const int CH_GPIO[N_CH] = {3, 4};

// Muestreo
#define FS_HZ        2000                        // Muestras por segundo por canal
#define OS_FACTOR    8                           // Conversiones sumadas por muestra (plan B: 4)
#define ADC_TOTAL_HZ (FS_HZ * OS_FACTOR * N_CH)  // 32000 conversiones/s en total
#define SAMPLES_PER_FRAME 20                     // 10 ms por trama

#define ADC_RAW_MAX  4095
#define SUM_MAX      (ADC_RAW_MAX * OS_FACTOR)   // 32760 con OS_FACTOR=8

// Red 
#define TCP_PORT          5006   // Control (comandos de texto)
#define UDP_DEFAULT_PORT  5005   // Datos; START <puerto> puede cambiarlo
#define HOSTNAME          "emg-node"

// Validador (en cuentas de una conversión, 12 bits)
#define VAL_BLOCK_SAMPLES   200    // 100 ms; 60 Hz cae exacto en el bin 6
#define VAL_SAT_LOW_RAW     20
#define VAL_SAT_HIGH_RAW    4075
#define VAL_FLAT_PTP_RAW    1      // Pico a pico <= 1 cuenta en todo el bloque → FLAT
#define VAL_DC_LOW_RAW      1000   // Offset esperado ~2000 (1.65 V)
#define VAL_DC_HIGH_RAW     3100
#define VAL_LINE_FRAC       0.30f  // Fracción de potencia AC en 60 Hz → LINE
#define VAL_LINE_HZ         60.0f

// ARM
#define ARM_SAMPLES 2000            // 1 s de reposo para medir el offset

// Opcionales
#define USE_SYNC_PULSE 0            // Decisión 10: desactivado
#define SYNC_PULSE_GPIO 5           // GPIO4 ahora es el canal BF

// Monitor serie: imprime una muestra del ADC cada SERIAL_ADC_PERIOD_MS con hora ISO 8601.
// 0 = desactivado. No imprimir las 2000 muestras/s: el puerto serie no da abasto.
#define SERIAL_ADC_PERIOD_MS 100
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define TZ_INFO      "CST6"         // Centro de México (UTC-6, sin horario de verano)
#define SYNC_PULSE_MS   100

#define BATT_SENSE 0                // Sin medición de batería (BATT_LOW queda reservado)

// Buffers del ADC
#define ADC_BYTES_PER_CONV 4                                   // SOC_ADC_DIGI_RESULT_BYTES (S3)
#define ADC_CONV_FRAME_BYTES (SAMPLES_PER_FRAME * N_CH * OS_FACTOR * ADC_BYTES_PER_CONV) // 1280 B
#define ADC_POOL_BYTES (ADC_CONV_FRAME_BYTES * 32)             // ~320 ms de margen

static_assert(ADC_TOTAL_HZ >= 611 && ADC_TOTAL_HZ <= 83333, "ADC fuera del rango de la S3");
static_assert((uint32_t)SUM_MAX <= 0xFFFF, "La suma no cabe en uint16");
static_assert(VAL_BLOCK_SAMPLES % SAMPLES_PER_FRAME == 0, "Bloque debe ser múltiplo de la trama");
