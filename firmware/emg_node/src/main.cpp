// =====================================================================
// Nodo EMG: punto de entrada
//  - env "bringup": ADS131M04 + muestreo + estadísticas por Serial (sin Wi-Fi). Sirve para
//    verificar registros, medir el piso de ruido y comprobar la señal.
//  - env "emg_node": firmware completo (Wi-Fi, comandos, sincronía): PENDIENTE
// =====================================================================
#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include "config.h"
#include "pins.h"
#include "ads131m04.h"
#include "sampler.h"

#if defined(BRINGUP_MODE)

static SPIClass spiBus(FSPI);
static Ads131m04 adc(spiBus, pins::ADS_CS, pins::ADS_DRDY, pins::ADS_RESET, pins::ADS_CLKIN);
static QueueHandle_t blockQ;
static Sampler* sampler;

// Estadística por segundo (Welford) en cuentas de 24 bit
struct Stats {
  uint64_t n = 0;
  double mean[cfg::N_CH] = {};
  double m2[cfg::N_CH] = {};
  uint32_t blocks = 0, gaps = 0, sat = 0, adcErr = 0;
};
static Stats st;
static bool haveSeq = false, dumpCsv = false, shortOn = false;
static uint32_t lastSeq = 0, tLast = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("# EMG node BRING-UP (ADS131M04, sin Wi-Fi)");
  Serial.printf("# fs=%u Hz, canales=%u, bloque=%u muestras, LSB=%.4f uV (pin del ADC), ganancia analogica=%.0f\n",
                (unsigned)cfg::FS_HZ, (unsigned)cfg::N_CH, (unsigned)cfg::BLOCK_SAMPLES, cfg::ADC_LSB_UV, cfg::ANALOG_GAIN);
  Serial.println("# Comandos: s = entradas en corto (ruido), n = entradas normales, d = volcado CSV on/off");

  blockQ = xQueueCreate(cfg::BLOCK_QUEUE_LEN, sizeof(SampleBlock));
  if (!blockQ) {
    Serial.println("ERROR: no se pudo crear la cola");
    while (true) delay(1000);
  }
  if (!adc.begin()) {
    Serial.println("ERROR: el ADS131M04 no se inicializo correctamente (ver lineas '# ADS')");
    while (true) delay(1000);
  }
  sampler = new Sampler(&adc, blockQ);
  if (!sampler->begin()) {
    Serial.println("ERROR: no se pudo crear la tarea de muestreo");
    while (true) delay(1000);
  }
  Serial.println("# t_ms,blocks,gaps,dropped,missed_drdy,adc_err,sat_blocks,"
                 "ch1_mean_cnt,ch1_std_cnt,ch1_std_uV_in,ch2_mean_cnt,ch2_std_cnt,ch2_std_uV_in");
  sampler->start();
  tLast = millis();
}

void loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 's' || c == 'n') {
      sampler->stop();
      shortOn = (c == 's');
      const bool ok = adc.setInputShort(shortOn);
      Serial.printf("# entradas %s (%s)\n", shortOn ? "EN CORTO" : "NORMALES", ok ? "ok" : "fallo al escribir");
      xQueueReset(blockQ);
      haveSeq = false;
      st = Stats();
      sampler->start();
    } else if (c == 'd') {
      dumpCsv = !dumpCsv;
      Serial.printf("# volcado CSV %s\n", dumpCsv ? "ON" : "OFF");
    }
  }

  SampleBlock b;
  while (xQueueReceive(blockQ, &b, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (haveSeq && b.seq != lastSeq + 1) st.gaps += b.seq - lastSeq - 1;
    haveSeq = true;
    lastSeq = b.seq;
    ++st.blocks;
    if (b.flags & proto::FLAG_SATURATION) ++st.sat;
    if (b.flags & proto::FLAG_ADC_ERROR) ++st.adcErr;
    for (uint16_t i = 0; i < b.n; ++i) {
      ++st.n;
      for (size_t c = 0; c < cfg::N_CH; ++c) {
        const double v = b.data[i][c];
        const double d = v - st.mean[c];
        st.mean[c] += d / st.n;
        st.m2[c] += d * (v - st.mean[c]);
      }
      if (dumpCsv) {
        char l[64];
        int len = snprintf(l, sizeof(l), "%lld", (long long)(b.t_us + (int64_t)i * 1000000 / cfg::FS_HZ));
        for (size_t c = 0; c < cfg::N_CH; ++c) len += snprintf(l + len, sizeof(l) - len, ",%ld", (long)b.data[i][c]);
        Serial.println(l);
      }
    }
  }

  if (millis() - tLast >= 1000) {
    tLast += 1000;
    if (!dumpCsv) {
      char line[200];
      int len = snprintf(line, sizeof(line), "%lu,%u,%u,%u,%u,%u,%u", (unsigned long)millis(), (unsigned)st.blocks,
                         (unsigned)st.gaps, (unsigned)sampler->droppedBlocks(), (unsigned)sampler->missedConversions(),
                         (unsigned)st.adcErr, (unsigned)st.sat);
      for (size_t c = 0; c < cfg::N_CH && st.n > 1; ++c) {
        const double sd = sqrt(st.m2[c] / (st.n - 1));
        len += snprintf(line + len, sizeof(line) - len, ",%.1f,%.2f,%.4f", st.mean[c], sd,
                        sd * cfg::ADC_LSB_UV / cfg::ANALOG_GAIN);
      }
      Serial.println(line);
    }
    st = Stats();
  }
}

#elif defined(NODE_MODE)

void setup() {
  // TODO: Wi-Fi (STA al hotspot), servidor TCP de comandos, destino UDP, timesync, monitor
}
void loop() {}

#else
#error "Definir BRINGUP_MODE o NODE_MODE (ver platformio.ini)"
#endif
