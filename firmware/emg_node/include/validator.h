// validator.h — Marca la calidad de la señal SIN filtrarla. Sin dependencias de Arduino.
//
// SAT se decide por trama (inmediato). FLAT, DC_OOR y LINE se calculan sobre bloques de
// VAL_BLOCK_SAMPLES (100 ms) y se aplican a las tramas del bloque siguiente.
#pragma once
#include <stdint.h>
#include <math.h>
#include "config.h"
#include "protocol.h"

class Validator {
 public:
  Validator() { reset(); }

  void reset() {
    block_flags_ = 0;
    n_ = 0;
    for (int c = 0; c < N_CH; ++c) clear_ch(c);
    const float w = 2.0f * (float)M_PI * VAL_LINE_HZ / (float)FS_HZ;
    coeff_ = 2.0f * cosf(w);
  }

  // samples: [SAMPLES_PER_FRAME][N_CH] en sumas de OS_FACTOR conversiones.
  // Devuelve las banderas de calidad para esta trama.
  uint16_t process_frame(const uint16_t* samples) {
    uint16_t frame_flags = 0;
    for (int i = 0; i < SAMPLES_PER_FRAME; ++i) {
      for (int c = 0; c < N_CH; ++c) {
        const uint16_t s = samples[i * N_CH + c];
        if (s <= (uint32_t)VAL_SAT_LOW_RAW * OS_FACTOR || s >= (uint32_t)VAL_SAT_HIGH_RAW * OS_FACTOR)
          frame_flags |= (uint16_t)(FLAG_SAT_CH1 << c);
        accumulate(c, s);
      }
      if (++n_ == VAL_BLOCK_SAMPLES) close_block();
    }
    return frame_flags | block_flags_;
  }

  // Para pruebas: fracción de potencia en 60 Hz del último bloque.
  float last_line_frac(int c) const { return last_line_frac_[c]; }

 private:
  struct Ch {
    uint16_t mn, mx;
    double sum, sum2;
    float q1, q2;   // Estados de Goertzel
  } ch_[N_CH];

  uint16_t block_flags_;
  int n_;
  float coeff_;
  float last_line_frac_[N_CH] = {0};

  void clear_ch(int c) {
    ch_[c].mn = 0xFFFF; ch_[c].mx = 0;
    ch_[c].sum = 0; ch_[c].sum2 = 0;
    ch_[c].q1 = 0; ch_[c].q2 = 0;
  }

  void accumulate(int c, uint16_t s) {
    Ch& h = ch_[c];
    if (s < h.mn) h.mn = s;
    if (s > h.mx) h.mx = s;
    h.sum += s;
    h.sum2 += (double)s * s;
    // Goertzel sobre la señal con un offset fijo para no perder precisión en float
    const float x = (float)s - (float)(2048 * OS_FACTOR);
    const float q0 = coeff_ * h.q1 - h.q2 + x;
    h.q2 = h.q1;
    h.q1 = q0;
  }

  void close_block() {
    uint16_t f = 0;
    const double n = (double)VAL_BLOCK_SAMPLES;
    for (int c = 0; c < N_CH; ++c) {
      Ch& h = ch_[c];
      const double mean = h.sum / n;
      const double var = h.sum2 / n - mean * mean;   // Potencia AC media

      if ((uint32_t)(h.mx - h.mn) <= (uint32_t)VAL_FLAT_PTP_RAW * OS_FACTOR) f |= (uint16_t)(FLAG_FLAT_CH1 << c);

      const double mean_raw = mean / OS_FACTOR;
      if (mean_raw < VAL_DC_LOW_RAW || mean_raw > VAL_DC_HIGH_RAW) f |= (uint16_t)(FLAG_DC_OOR_CH1 << c);

      // |X|^2 de Goertzel. Se resta la componente DC de la entrada desplazada: con un bin
      // exacto (60 Hz = bin 6 con N=200) la DC no aporta, así que basta con |X|^2.
      const double mag2 = (double)h.q1 * h.q1 + (double)h.q2 * h.q2 - (double)coeff_ * h.q1 * h.q2;
      const double p_line = 2.0 * mag2 / (n * n);    // Potencia de la senoidal de 60 Hz
      const float frac = (var > 1e-9) ? (float)(p_line / var) : 0.0f;
      last_line_frac_[c] = frac;
      if (frac > VAL_LINE_FRAC) f |= (uint16_t)(FLAG_LINE_CH1 << c);

      clear_ch(c);
    }
    block_flags_ = f;
    n_ = 0;
  }
};
