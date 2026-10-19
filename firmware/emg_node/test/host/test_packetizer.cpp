// Pruebas en PC (sin ESP32): CRC, empaquetado (int32) y funciones puras del ADS131M04.
// Compilar y ejecutar:  bash test/host/run_host_tests.sh
#include <stdio.h>
#include <string.h>
#include "../../src/packetizer.h"
#include "../../src/ads131m04_defs.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++fails; } } while (0)

int main() {
  // 1) Vector de prueba CRC-16/CCITT-FALSE
  const uint8_t v[] = {'1','2','3','4','5','6','7','8','9'};
  CHECK(proto::crc16_ccitt(v, sizeof(v)) == 0x29B1, "crc vector 123456789");

  // 2) Empaquetado con valores negativos (24 bit con signo)
  SampleBlock b;
  memset(&b, 0, sizeof(b));
  b.seq = 7; b.t_us = 123456789LL; b.flags = proto::FLAG_SATURATION; b.n = cfg::BLOCK_SAMPLES;
  for (size_t i = 0; i < cfg::BLOCK_SAMPLES; ++i)
    for (size_t c = 0; c < cfg::N_CH; ++c) b.data[i][c] = (int32_t)(c == 0 ? -8000000 + (int)i : 8000000 - (int)i);

  uint8_t buf[MAX_FRAME_BYTES];
  const size_t n = packetize(b, buf, sizeof(buf));
  CHECK(n == MAX_FRAME_BYTES, "tamano de trama");
  CHECK(n == 20 + cfg::BLOCK_SAMPLES * cfg::N_CH * 4 + 2, "tamano esperado = 20 + datos*4 + 2");
  uint16_t crc_frame; memcpy(&crc_frame, buf + n - 2, 2);
  CHECK(proto::crc16_ccitt(buf, n - 2) == crc_frame, "crc de la trama");
  CHECK(buf[2] == 2, "version de protocolo = 2");

  FILE* f = fopen("/tmp/frame.bin", "wb");
  if (f) { fwrite(buf, 1, n, f); fclose(f); }

  // 3) ADS131M04: sanidad del mapa de bits del registro CLOCK contra el valor por defecto del datasheet
  CHECK(ads::clockRegValue(4, 3, false) == 0x0F0E, "CLOCK por defecto (4 canales, OSR 1024, alta resolucion) = 0x0F0E");
  CHECK(ads::clockRegValue(2, 4, true) == (0x0300 | 0x0080 | (4 << 2) | 0x2), "CLOCK 2 canales, OSR 2048, XTAL_DIS");
  CHECK(ads::gainRegValue(0) == 0x0000 && ads::gainRegValue(7) == 0x7777, "GAIN");
  CHECK(ads::cmdRreg(0x03) == 0xA180 && ads::cmdWreg(0x03) == 0x6180 && ads::ackWreg(0x03) == 0x4180, "comandos RREG/WREG");
  CHECK(ads::regChCfg(0) == 0x09 && ads::regChCfg(1) == 0x0E && ads::regChCfg(3) == 0x18, "direcciones CHn_CFG");
  CHECK(cfg::FS_HZ == 2000, "fs = 2000 Hz con CLKIN 8.192 MHz y OSR 2048");

  // 4) ADS131M04: trama sintetica (respuesta, 4 canales de 24 bit, CRC)
  uint8_t fr[ads::FRAME_BYTES] = {0};
  const int32_t vals[4] = {-1, 8388607, -8388608, 1234};
  for (int c = 0; c < 4; ++c) {
    const uint32_t u = (uint32_t)vals[c] & 0xFFFFFF;
    fr[3 * (1 + c)] = u >> 16; fr[3 * (1 + c) + 1] = (u >> 8) & 0xFF; fr[3 * (1 + c) + 2] = u & 0xFF;
  }
  const uint16_t crc = proto::crc16_ccitt(fr, 15);
  fr[15] = crc >> 8; fr[16] = crc & 0xFF;
  for (int c = 0; c < 4; ++c) CHECK(ads::sample(fr, c) == vals[c], "extension de signo 24 bit");
  CHECK(ads::frameCrcOk(fr), "CRC de trama ADS (auto-consistente)");
  fr[5] ^= 0x01;
  CHECK(!ads::frameCrcOk(fr), "CRC de trama ADS detecta error");

  printf("%s (frame=%zu bytes, header=%zu)\n", fails ? "FALLO" : "OK", n, sizeof(proto::FrameHeader));
  return fails;
}
