#pragma once
// Tarea de muestreo: toma muestras del AdcSource, arma bloques con número de secuencia y
// timestamp y los envía a una cola. No hace red ni Serial.
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "adc_source.h"
#include "sample_block.h"

class Sampler {
 public:
  Sampler(AdcSource* adc, QueueHandle_t blockQueue);

  bool begin();          // crea la tarea (queda en espera de start())
  void start();          // inicia ADC y captura; fija t0
  void stop();           // detiene y espera a que la tarea termine el ciclo

  bool running() const { return running_; }
  int64_t t0_us() const { return t0_us_; }
  uint32_t blocksProduced() const { return blockSeq_; }
  uint32_t droppedBlocks() const { return dropped_; }
  uint32_t missedConversions() const { return adc_->overruns(); }   // DRDY perdidos (rellenados)

 private:
  static void taskTrampoline(void* self) { static_cast<Sampler*>(self)->run(); }
  void run();

  AdcSource* adc_;
  QueueHandle_t blockQ_;
  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t startSem_ = nullptr;
  SemaphoreHandle_t stoppedSem_ = nullptr;

  volatile bool running_ = false;
  volatile int64_t t0_us_ = 0;
  volatile uint32_t blockSeq_ = 0;
  volatile uint32_t dropped_ = 0;
  uint64_t sampleIdx_ = 0;
  uint8_t pendingFlags_ = 0;
  uint32_t lastOverruns_ = 0;
};
