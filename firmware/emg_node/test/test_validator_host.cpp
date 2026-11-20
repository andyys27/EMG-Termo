// Prueba en la PC (sin placa) del validador y del formato de trama.
//   g++ -std=c++17 -Wall -Iinclude test/test_validator_host.cpp -o /tmp/tv && /tmp/tv
// Con --vector imprime una trama de referencia en hex (para tests/vectors/frames.json).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "protocol.h"
#include "validator.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FALLA: %s\n", msg); failures++; } else std::printf("ok   : %s\n", msg); } while (0)

static uint16_t clamp_sum(double v) {
  if (v < 0) v = 0;
  if (v > SUM_MAX) v = SUM_MAX;
  return (uint16_t)std::lround(v);
}

// Genera n_blocks bloques con f(i, c) y devuelve las banderas de la última trama.
template <typename F>
static uint16_t run(Validator& v, int n_blocks, F f) {
  uint16_t s[SAMPLES_PER_FRAME * N_CH];
  uint16_t flags = 0;
  int idx = 0;
  for (int fr = 0; fr < n_blocks * VAL_BLOCK_SAMPLES / SAMPLES_PER_FRAME; ++fr) {
    for (int i = 0; i < SAMPLES_PER_FRAME; ++i, ++idx)
      for (int c = 0; c < N_CH; ++c) s[i * N_CH + c] = clamp_sum(f(idx, c));
    flags = v.process_frame(s);
  }
  return flags;
}

int main(int argc, char** argv) {
  const double off = 2000.0 * OS_FACTOR;   // 1.65 V
  srand(1);
  auto noise = [] { return ((rand() / (double)RAND_MAX) - 0.5) * 2.0 * 3.0 * OS_FACTOR; };  // ±3 cuentas

  {
    const char* s = "123456789";
    CHECK(crc16_ccitt_false((const uint8_t*)s, 9) == 0x29B1, "CRC16/CCITT-FALSE(\"123456789\") = 0x29B1");
  }
  {
    Validator v;
    uint16_t f = run(v, 3, [&](int i, int) { return off + noise() + 50.0 * OS_FACTOR * std::sin(2 * M_PI * 80 * i / FS_HZ); });
    CHECK(f == 0, "señal normal (ruido + 80 Hz) → sin banderas");
  }
  {
    Validator v;
    uint16_t f = run(v, 3, [&](int, int c) { return c == 0 ? SUM_MAX : off + noise(); });
    CHECK((f & FLAG_SAT_CH1) && !(f & FLAG_SAT_CH2), "ch1 a 3.3 V → SAT_CH1");
    CHECK(f & FLAG_DC_OOR_CH1, "ch1 a 3.3 V → DC_OOR_CH1");
  }
  {
    Validator v;
    uint16_t f = run(v, 3, [&](int, int c) { return c == 1 ? 0.0 : off + noise(); });
    CHECK((f & FLAG_SAT_CH2) && (f & FLAG_FLAT_CH2) && (f & FLAG_DC_OOR_CH2), "ch2 a tierra → SAT, FLAT, DC_OOR en ch2");
    CHECK(!(f & (FLAG_SAT_CH1 | FLAG_FLAT_CH1)), "ch1 sin banderas cuando falla ch2");
  }
  {
    Validator v;
    uint16_t f = run(v, 3, [&](int, int) { return off; });
    CHECK((f & FLAG_FLAT_CH1) && (f & FLAG_FLAT_CH2), "constante → FLAT en ambos");
  }
  {
    Validator v;
    uint16_t f = run(v, 3, [&](int i, int c) {
      return off + noise() + (c == 0 ? 200.0 * OS_FACTOR * std::sin(2 * M_PI * 60 * i / FS_HZ) : 0.0);
    });
    CHECK((f & FLAG_LINE_CH1) && !(f & FLAG_LINE_CH2), "60 Hz fuerte en ch1 → LINE_CH1");
    CHECK(v.last_line_frac(0) > 0.9f, "fracción de 60 Hz > 0.9 en ch1");
  }
  {
    Validator v;
    run(v, 1, [&](int, int) { return off; });                 // Bloque plano
    uint16_t s[SAMPLES_PER_FRAME * N_CH];
    for (int i = 0; i < SAMPLES_PER_FRAME * N_CH; ++i) s[i] = clamp_sum(off + noise());
    CHECK(v.process_frame(s) & FLAG_FLAT_CH1, "banderas de bloque se mantienen en el bloque siguiente");
  }
  {
    uint16_t s[SAMPLES_PER_FRAME * N_CH];
    for (int i = 0; i < SAMPLES_PER_FRAME * N_CH; ++i) s[i] = (uint16_t)(16000 + i);
    uint8_t buf[FRAME_BYTES];
    size_t n = frame_encode(buf, 7, 123456789ull, FLAG_LINE_CH1, s);
    CHECK(n == 102 && FRAME_BYTES == 102, "trama de 102 bytes");
    CHECK(buf[0] == 'E' && buf[1] == 'M', "magic 'EM'");
    const uint16_t crc = buf[n - 2] | (buf[n - 1] << 8);
    CHECK(crc == crc16_ccitt_false(buf, n - 2), "CRC al final de la trama");
    if (argc > 1 && !std::strcmp(argv[1], "--vector")) {
      std::printf("seq=7 t_us=123456789 flags=%u samples=16000+i\nhex=", FLAG_LINE_CH1);
      for (size_t i = 0; i < n; ++i) std::printf("%02x", buf[i]);
      std::printf("\n");
    }
  }

  std::printf("\n%s (%d fallas)\n", failures ? "FALLÓ" : "TODO OK", failures);
  return failures ? 1 : 0;
}
