#pragma once
// Driver del ADS131M04 por SPI para el ESP32-S3 (Arduino-ESP32 3.x / ESP-IDF 5.x).
//  - Genera CLKIN con LEDC (o se usa un oscilador externo, ver config.h).
//  - Interrupción /DRDY -> notificación a la tarea de muestreo -> lectura de la trama por SPI.
//  - Usar begin() desde setup(); start(), read() y stop() desde la MISMA tarea (la del muestreador).
#include <Arduino.h>
#include <SPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "adc_source.h"
#include "ads131m04_defs.h"
#include "config.h"

class Ads131m04 : public AdcSource {
 public:
  Ads131m04(SPIClass& spi, int pinCs, int pinDrdy, int pinReset, int pinClkin);

  bool begin() override;
  bool start() override;
  void stop() override;
  size_t read(int32_t* out, size_t max_samples, uint32_t timeout_ms) override;
  uint32_t overruns() const override { return missed_; }
  bool takeSaturation() override;
  bool takeError() override;

  // Utilidades de bring-up. NO llamar con la captura en curso (devuelven false si running_).
  bool readRegister(uint8_t addr, uint16_t* value);
  bool writeRegister(uint8_t addr, uint16_t value);
  bool setInputShort(bool on);                  // entradas en corto: mide el piso de ruido del ADC
  double clkinActualHz() const { return clkinHz_; }
  uint32_t readErrors() const { return errors_; }

 private:
  static void IRAM_ATTR isr(void* arg);
  bool startClkin();
  bool hardReset();
  void xferFrame(uint16_t cmd, uint16_t arg, uint8_t* rx);
  size_t drainPending(int32_t* out, size_t max);

  SPIClass* spi_;
  int cs_, drdy_, rst_, clkinPin_;

  volatile TaskHandle_t waiter_ = nullptr;
  volatile int64_t isrTs_ = 0;
  volatile bool running_ = false;

  double clkinHz_ = 0;
  volatile uint32_t missed_ = 0;
  uint32_t errors_ = 0;
  bool sat_ = false;
  bool err_ = false;

  // Muestra actual y relleno pendiente (mantiene la línea de tiempo si se pierde algún DRDY)
  int32_t cur_[cfg::N_CH] = {};
  int32_t last_[cfg::N_CH] = {};
  bool curPending_ = false;
  uint32_t holdPending_ = 0;
};
