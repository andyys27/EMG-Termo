#pragma once
// Interfaz de fuente de ADC. Permite cambiar de ADC (p. ej. otro externo) sin tocar el
// muestreador, el empaquetador ni la red.
#include <stddef.h>
#include <stdint.h>

class AdcSource {
 public:
  virtual ~AdcSource() = default;

  virtual bool begin() = 0;   // configura reloj y registros del ADC; no inicia la captura
  virtual bool start() = 0;   // inicia la captura (se llama desde la tarea que luego hace read())
  virtual void stop() = 0;

  // Entrega hasta max_samples muestras en out[n][N_CH] (int32, intercalado por muestra). Espera hasta
  // timeout_ms si no hay datos. Devuelve cuántas muestras escribió (0 si hubo timeout).
  // Si se pierden conversiones, el driver rellena con la última muestra (mantiene la línea de tiempo)
  // y lo reporta en overruns().
  virtual size_t read(int32_t* out, size_t max_samples, uint32_t timeout_ms) = 0;

  virtual uint32_t overruns() const = 0;   // conversiones perdidas acumuladas
  virtual bool takeSaturation() = 0;       // true si hubo saturación desde la última llamada (y la limpia)
  virtual bool takeError() { return false; }   // true si hubo error de lectura desde la última llamada (y la limpia)
};
