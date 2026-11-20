// adc_continuous_s3.h — ADC1 en modo continuo con DMA (ESP-IDF 5, driver esp_adc).
// Entrega muestras ya sumadas (OS_FACTOR conversiones por canal) y el tiempo de cada una
// anclado al reloj del nodo mediante la interrupción de "conversion frame done".
//
// No usar analogRead() en ADC1 mientras esto corre.
#pragma once
#include <Arduino.h>
#include "esp_adc/adc_continuous.h"
#include "esp_timer.h"
#include "config.h"

class AdcContinuousS3 {
 public:
  // Devuelve ESP_OK o el error del driver.
  esp_err_t begin() {
    adc_continuous_handle_cfg_t hcfg = {};
    hcfg.max_store_buf_size = ADC_POOL_BYTES;
    hcfg.conv_frame_size = ADC_CONV_FRAME_BYTES;
    esp_err_t err = adc_continuous_new_handle(&hcfg, &handle_);
    if (err != ESP_OK) return err;

    adc_digi_pattern_config_t pattern[N_CH] = {};
    for (int c = 0; c < N_CH; ++c) {
      adc_unit_t unit;
      adc_channel_t chan;
      err = adc_continuous_io_to_channel(CH_GPIO[c], &unit, &chan);
      if (err != ESP_OK) return err;
      if (unit != ADC_UNIT_1) return ESP_ERR_INVALID_ARG;   // Solo ADC1 (ADC2 choca con Wi-Fi)
      chan_[c] = (uint8_t)chan;
      pattern[c].atten = ADC_ATTEN_DB_12;
      pattern[c].channel = (uint8_t)chan;
      pattern[c].unit = ADC_UNIT_1;
      pattern[c].bit_width = ADC_BITWIDTH_12;
    }

    adc_continuous_config_t cfg = {};
    cfg.pattern_num = N_CH;
    cfg.adc_pattern = pattern;
    cfg.sample_freq_hz = ADC_TOTAL_HZ;
    cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    err = adc_continuous_config(handle_, &cfg);
    if (err != ESP_OK) return err;

    adc_continuous_evt_cbs_t cbs = {};
    cbs.on_conv_done = on_conv_done;
    cbs.on_pool_ovf = on_pool_ovf;
    err = adc_continuous_register_event_callbacks(handle_, &cbs, this);
    if (err != ESP_OK) return err;

    reset_accum();
    return adc_continuous_start(handle_);
  }

  // Lee lo que haya en el pool y llama a sink(sample[N_CH], t_us) por cada muestra completa.
  // Devuelve true si hubo desbordamiento desde la última llamada.
  template <typename Sink>
  bool poll(Sink&& sink, uint32_t timeout_ms) {
    uint32_t got = 0;
    esp_err_t err = adc_continuous_read(handle_, buf_, sizeof(buf_), &got, timeout_ms);
    bool ovf = take_overflow();
    if (ovf) {
      // Se perdieron datos: se vacía el pool y se resincroniza el índice con la ISR.
      // (Carrera de unos µs si llega un bloque justo entre flush y la lectura; se acepta.)
      reset_accum();
      adc_continuous_flush_pool(handle_);
      portENTER_CRITICAL(&mux_);
      consumed_conv_ = isr_conv_total_;
      portEXIT_CRITICAL(&mux_);
      return true;
    }
    if (err != ESP_OK || got == 0) return false;

    // Instantánea del ancla temporal: la conversión número isr_conv_total_ terminó en isr_t_us_.
    portENTER_CRITICAL(&mux_);
    const uint64_t anchor_conv = isr_conv_total_;
    const int64_t anchor_t = isr_t_us_;
    portEXIT_CRITICAL(&mux_);

    const uint32_t n_conv = got / ADC_BYTES_PER_CONV;
    for (uint32_t i = 0; i < n_conv; ++i) {
      const adc_digi_output_data_t* d = (const adc_digi_output_data_t*)&buf_[i * ADC_BYTES_PER_CONV];
      const uint64_t conv_idx = consumed_conv_++;
      const int c = chan_index(d->type2.channel);
      if (c < 0 || d->type2.unit != 0) { bad_conv_++; continue; }
      // Mantener el patrón ch1, ch2, ch1, ... ; si se desalinea, se reinicia el acumulado.
      if (c != expect_ch_) { reset_accum(); if (c != 0) continue; }
      if (cnt_[c] == 0 && c == 0) first_conv_idx_ = conv_idx;
      acc_[c] += d->type2.data;
      cnt_[c]++;
      expect_ch_ = (c + 1) % N_CH;
      if (c == N_CH - 1 && cnt_[c] == OS_FACTOR) {
        uint16_t s[N_CH];
        for (int k = 0; k < N_CH; ++k) s[k] = (uint16_t)acc_[k];
        // Tiempo = centro del bloque de OS_FACTOR*N_CH conversiones, a partir del ancla.
        const double center = (double)first_conv_idx_ + (OS_FACTOR * N_CH - 1) / 2.0;
        const double dt_us = ((double)anchor_conv - 1.0 - center) * 1e6 / ADC_TOTAL_HZ;
        sink(s, (uint64_t)(anchor_t - (int64_t)dt_us));
        for (int k = 0; k < N_CH; ++k) { acc_[k] = 0; cnt_[k] = 0; }
      }
    }
    return ovf;
  }

  uint32_t lost_conversions() const { return lost_conv_; }
  uint32_t bad_conversions() const { return bad_conv_; }
  void flush() { adc_continuous_flush_pool(handle_); }

 private:
  adc_continuous_handle_t handle_ = nullptr;
  uint8_t chan_[N_CH] = {};
  uint8_t buf_[ADC_CONV_FRAME_BYTES];

  uint32_t acc_[N_CH] = {};
  uint16_t cnt_[N_CH] = {};
  int expect_ch_ = 0;
  uint64_t first_conv_idx_ = 0;
  uint64_t consumed_conv_ = 0;
  uint32_t bad_conv_ = 0;

  // Compartido con la ISR
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  volatile uint64_t isr_conv_total_ = 0;   // Conversiones que entraron al pool
  volatile int64_t isr_t_us_ = 0;          // Tiempo al terminar la última
  volatile bool ovf_ = false;
  volatile uint32_t lost_conv_ = 0;

  int chan_index(uint32_t ch) const {
    for (int c = 0; c < N_CH; ++c) if (chan_[c] == ch) return c;
    return -1;
  }

  void reset_accum() {
    for (int k = 0; k < N_CH; ++k) { acc_[k] = 0; cnt_[k] = 0; }
    expect_ch_ = 0;
  }

  bool take_overflow() {
    portENTER_CRITICAL(&mux_);
    bool o = ovf_;
    ovf_ = false;
    portEXIT_CRITICAL(&mux_);
    return o;
  }

  static bool IRAM_ATTR on_conv_done(adc_continuous_handle_t, const adc_continuous_evt_data_t* e, void* user) {
    AdcContinuousS3* self = (AdcContinuousS3*)user;
    const int64_t t = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&self->mux_);
    self->isr_conv_total_ += e->size / ADC_BYTES_PER_CONV;
    self->isr_t_us_ = t;
    portEXIT_CRITICAL_ISR(&self->mux_);
    return false;
  }

  // El pool estaba lleno: este bloque se descartó y no debe contarse.
  static bool IRAM_ATTR on_pool_ovf(adc_continuous_handle_t, const adc_continuous_evt_data_t* e, void* user) {
    AdcContinuousS3* self = (AdcContinuousS3*)user;
    portENTER_CRITICAL_ISR(&self->mux_);
    const uint32_t n = e->size / ADC_BYTES_PER_CONV;
    if (self->isr_conv_total_ >= n) self->isr_conv_total_ -= n;
    self->lost_conv_ += n;
    self->ovf_ = true;
    portEXIT_CRITICAL_ISR(&self->mux_);
    return false;
  }
};
