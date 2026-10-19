#include "sampler.h"

#include <string.h>
#include "esp_timer.h"
#include "config.h"
#include "protocol.h"

Sampler::Sampler(AdcSource* adc, QueueHandle_t blockQueue) : adc_(adc), blockQ_(blockQueue) {}

bool Sampler::begin() {
  startSem_ = xSemaphoreCreateBinary();
  stoppedSem_ = xSemaphoreCreateBinary();
  if (!startSem_ || !stoppedSem_) return false;
  return xTaskCreatePinnedToCore(taskTrampoline, "sampler", cfg::SAMPLER_STACK, this,
                                 cfg::SAMPLER_PRIORITY, &task_, cfg::SAMPLER_CORE) == pdPASS;
}

void Sampler::start() {
  running_ = true;
  xSemaphoreGive(startSem_);
}

void Sampler::stop() {
  running_ = false;
  xSemaphoreTake(stoppedSem_, pdMS_TO_TICKS(500));
}

void Sampler::run() {
  for (;;) {
    xSemaphoreTake(startSem_, portMAX_DELAY);

    blockSeq_ = 0;
    dropped_ = 0;
    sampleIdx_ = 0;
    pendingFlags_ = 0;
    lastOverruns_ = 0;

    if (!adc_->start()) {
      running_ = false;
      xSemaphoreGive(stoppedSem_);
      continue;
    }
    t0_us_ = esp_timer_get_time();

    SampleBlock blk;
    memset(&blk, 0, sizeof(blk));
    size_t fill = 0;
    int32_t tmp[cfg::BLOCK_SAMPLES][cfg::N_CH];

    while (running_) {
      const size_t got = adc_->read(&tmp[0][0], cfg::BLOCK_SAMPLES - fill, 20);

      const uint32_t ov = adc_->overruns();
      if (ov != lastOverruns_) {
        pendingFlags_ |= proto::FLAG_OVERRUN;
        lastOverruns_ = ov;
      }
      if (got == 0) continue;

      if (fill == 0) {   // primera muestra del bloque: sellar secuencia y tiempo
        blk.seq = blockSeq_;
        blk.t_us = t0_us_ + static_cast<int64_t>(sampleIdx_ * 1000000ULL / cfg::FS_HZ);
      }
      memcpy(&blk.data[fill][0], &tmp[0][0], got * cfg::N_CH * sizeof(int32_t));
      fill += got;
      sampleIdx_ += got;

      if (fill == cfg::BLOCK_SAMPLES) {
        blk.n = static_cast<uint16_t>(fill);
        blk.flags = pendingFlags_ | (adc_->takeSaturation() ? proto::FLAG_SATURATION : 0) |
                    (adc_->takeError() ? proto::FLAG_ADC_ERROR : 0);
        pendingFlags_ = 0;
        blockSeq_ = blockSeq_ + 1;                 // siempre avanza: un hueco en seq delata una pérdida
        if (xQueueSend(blockQ_, &blk, 0) != pdTRUE) {
          dropped_ = dropped_ + 1;                 // cola llena: se descarta el bloque (nunca se bloquea el muestreo)
          pendingFlags_ |= proto::FLAG_OVERRUN;
        }
        fill = 0;
      }
    }

    adc_->stop();
    xSemaphoreGive(stoppedSem_);
  }
}
