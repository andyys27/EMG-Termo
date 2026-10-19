#pragma once
// Bloque de muestras producido por el muestreador y consumido por el empaquetador.
// Sin dependencias de FreeRTOS (se puede compilar en PC para pruebas).
#include <stdint.h>
#include "config.h"

struct SampleBlock {
  uint32_t seq;                                   // índice de bloque (siempre incrementa, aunque se descarte)
  int64_t  t_us;                                  // µs del reloj del dispositivo, primera muestra del bloque
  uint8_t  flags;                                 // proto::FLAG_*
  uint16_t n;                                     // muestras válidas (<= BLOCK_SAMPLES)
  int32_t  data[cfg::BLOCK_SAMPLES][cfg::N_CH];   // cuentas de 24 bit con signo, por muestra y canal
};
