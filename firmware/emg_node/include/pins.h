#pragma once
// =====================================================================
// Asignación de pines (ESP32-S3 SuperMini). VERIFICAR contra el pinout
// de su variante: el SuperMini expone pocos pines.
//
// Reglas:
//  - Evitar pines de arranque/strapping (GPIO0, GPIO3, GPIO45, GPIO46),
//    USB nativo (GPIO19, GPIO20) y los usados por flash/PSRAM.
//  - SPI: se usan los pines por defecto de FSPI en el S3 (IO_MUX).
// =====================================================================

namespace pins {

// ---- ADS131M04 (SPI, modo 1: CPOL=0, CPHA=1) ----
constexpr int ADS_SCK   = 12;   // SCLK
constexpr int ADS_MISO  = 13;   // DOUT
constexpr int ADS_MOSI  = 11;   // DIN
constexpr int ADS_CS    = 10;   // /CS (manual)
constexpr int ADS_DRDY  = 7;    // /DRDY (activo en bajo, interrupción por flanco de bajada)
constexpr int ADS_RESET = 8;    // /SYNC/RESET (pulso en bajo = reinicio)
constexpr int ADS_CLKIN = 6;    // CLKIN (8.192 MHz generado por LEDC). -1 si se usa oscilador externo

// ---- Otros ----
constexpr int BATT_SENSE_GPIO = 4;   // divisor resistivo de la batería (ADC1); lectura en reposo
constexpr int LED_GPIO        = -1;  // -1 = sin LED (definir según la placa)
constexpr int SYNC_OUT_GPIO   = 5;   // pulso de trigger hacia Vicon, SOLO a través de optoacoplador

}  // namespace pins
