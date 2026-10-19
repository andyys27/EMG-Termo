#include "ads131m04.h"

#include "driver/ledc.h"
#include "pins.h"
#include "esp_timer.h"

using namespace ads;

Ads131m04::Ads131m04(SPIClass& spi, int pinCs, int pinDrdy, int pinReset, int pinClkin)
    : spi_(&spi), cs_(pinCs), drdy_(pinDrdy), rst_(pinReset), clkinPin_(pinClkin) {}


// Reloj CLKIN por LEDC: 1 bit de resolución (50 % de ciclo útil), divisor fraccionario de 80 MHz.
bool Ads131m04::startClkin() {
  if (cfg::ADS_CLKIN_EXTERNAL || clkinPin_ < 0) {
    clkinHz_ = cfg::ADS_CLKIN_HZ;   // se asume el oscilador externo en la frecuencia nominal
    return true;
  }
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_1_BIT;
  t.timer_num = LEDC_TIMER_0;
  t.freq_hz = cfg::ADS_CLKIN_HZ;
  t.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&t) != ESP_OK) return false;

  ledc_channel_config_t c = {};
  c.gpio_num = clkinPin_;
  c.speed_mode = LEDC_LOW_SPEED_MODE;
  c.channel = LEDC_CHANNEL_0;
  c.intr_type = LEDC_INTR_DISABLE;
  c.timer_sel = LEDC_TIMER_0;
  c.duty = 1;       // 1 de 2 niveles = 50 %
  c.hpoint = 0;
  if (ledc_channel_config(&c) != ESP_OK) return false;

  clkinHz_ = ledc_get_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
  return true;
}

bool Ads131m04::hardReset() {
  digitalWrite(rst_, LOW);
  delay(2);
  digitalWrite(rst_, HIGH);
  delay(10);
  uint8_t rx[FRAME_BYTES];
  for (int i = 0; i < 20; ++i) {
    xferFrame(CMD_NULL, 0, rx);
    if (word16(rx, 0) == RESET_READY) return true;
    delay(1);
  }
  return false;   // no es fatal: begin() verifica con el registro ID
}

void Ads131m04::xferFrame(uint16_t cmd, uint16_t arg, uint8_t* rx) {
  uint8_t tx[FRAME_BYTES] = {0};
  tx[0] = cmd >> 8;
  tx[1] = cmd & 0xFF;
  tx[3] = arg >> 8;      // palabra 1: primer dato de un WREG
  tx[4] = arg & 0xFF;
  spi_->beginTransaction(SPISettings(cfg::ADS_SPI_HZ, MSBFIRST, SPI_MODE1));
  digitalWrite(cs_, LOW);
  spi_->transferBytes(tx, rx, FRAME_BYTES);
  digitalWrite(cs_, HIGH);
  spi_->endTransaction();
}

bool Ads131m04::writeRegister(uint8_t addr, uint16_t value) {
  if (running_) return false;
  uint8_t rx[FRAME_BYTES];
  xferFrame(cmdWreg(addr), value, rx);
  xferFrame(CMD_NULL, 0, rx);
  return word16(rx, 0) == ackWreg(addr);
}

bool Ads131m04::readRegister(uint8_t addr, uint16_t* value) {
  if (running_) return false;
  uint8_t rx[FRAME_BYTES];
  xferFrame(cmdRreg(addr), 0, rx);
  xferFrame(CMD_NULL, 0, rx);
  *value = word16(rx, 0);
  return true;
}

bool Ads131m04::setInputShort(bool on) {
  bool ok = true;
  for (uint8_t c = 0; c < cfg::N_CH; ++c) ok &= writeRegister(regChCfg(c), on ? CH_MUX_SHORT : CH_MUX_NORMAL);
  return ok;
}

bool Ads131m04::begin() {
  pinMode(cs_, OUTPUT);
  digitalWrite(cs_, HIGH);
  pinMode(rst_, OUTPUT);
  digitalWrite(rst_, HIGH);
  pinMode(drdy_, INPUT);

  if (!startClkin()) {
    Serial.println("# ADS: ERROR al generar CLKIN (LEDC)");
    return false;
  }
  Serial.printf("# ADS: CLKIN = %.0f Hz -> fs = %.3f Hz\n", clkinHz_, clkinHz_ / (2.0 * cfg::ADS_OSR));
  delay(5);

  spi_->begin(pins::ADS_SCK, pins::ADS_MISO, pins::ADS_MOSI, -1);
  if (!hardReset()) Serial.println("# ADS: aviso, no se vio la respuesta 0xFF04 tras el reset");

  uint16_t id = 0;
  readRegister(REG_ID, &id);
  Serial.printf("# ADS: ID = 0x%04X (canales = %u)\n", id, (id >> 8) & 0x0F);
  if (((id >> 8) & 0x0F) != N_ADC_CH) {
    Serial.println("# ADS: ERROR, el ID no corresponde a un ADS131M04 (revisar SPI, alimentación y CLKIN)");
    return false;
  }

  struct { uint8_t addr; uint16_t value; const char* name; } regs[] = {
      {REG_CLOCK, clockRegValue(cfg::N_CH, cfg::ADS_OSR_CODE, cfg::ADS_XTAL_DIS), "CLOCK"},
      {REG_GAIN, gainRegValue(cfg::ADS_PGA_CODE), "GAIN"},
  };
  bool ok = true;
  for (const auto& r : regs) {
    uint16_t rb = 0;
    const bool acked = writeRegister(r.addr, r.value);
    readRegister(r.addr, &rb);
    const bool match = (rb == r.value);
    Serial.printf("# ADS: %s <- 0x%04X, lectura 0x%04X, ack=%s, %s\n", r.name, r.value, rb, acked ? "ok" : "NO",
                  match ? "ok" : "DISCREPANCIA");
    ok &= match;
  }
  for (uint8_t c = 0; c < cfg::N_CH; ++c) writeRegister(regChCfg(c), CH_MUX_NORMAL);

  uint16_t st = 0, mode = 0;
  readRegister(REG_STATUS, &st);
  readRegister(REG_MODE, &mode);
  Serial.printf("# ADS: STATUS = 0x%04X, MODE = 0x%04X\n", st, mode);
  return ok;
}

void IRAM_ATTR Ads131m04::isr(void* arg) {
  auto* self = static_cast<Ads131m04*>(arg);
  self->isrTs_ = esp_timer_get_time();
  BaseType_t woken = pdFALSE;
  TaskHandle_t w = self->waiter_;
  if (w) vTaskNotifyGiveFromISR(w, &woken);
  if (woken) portYIELD_FROM_ISR();
}

bool Ads131m04::start() {
  waiter_ = xTaskGetCurrentTaskHandle();
  curPending_ = false;
  holdPending_ = 0;
  missed_ = 0;
  errors_ = 0;
  sat_ = err_ = false;
  for (auto& v : last_) v = 0;
  ulTaskNotifyTake(pdTRUE, 0);            // limpia notificaciones viejas
  running_ = true;
  attachInterruptArg(drdy_, &Ads131m04::isr, this, FALLING);
  return true;
}

void Ads131m04::stop() {
  detachInterrupt(drdy_);
  running_ = false;
  ulTaskNotifyTake(pdTRUE, 0);
}

bool Ads131m04::takeSaturation() {
  const bool s = sat_;
  sat_ = false;
  return s;
}

bool Ads131m04::takeError() {
  const bool e = err_;
  err_ = false;
  return e;
}

size_t Ads131m04::drainPending(int32_t* out, size_t max) {
  size_t w = 0;
  while (holdPending_ > 0 && w < max) {                       // primero el relleno de las muestras perdidas
    for (uint8_t c = 0; c < cfg::N_CH; ++c) out[w * cfg::N_CH + c] = last_[c];
    ++w;
    --holdPending_;
  }
  if (holdPending_ == 0 && curPending_ && w < max) {          // luego la muestra actual
    for (uint8_t c = 0; c < cfg::N_CH; ++c) {
      out[w * cfg::N_CH + c] = cur_[c];
      last_[c] = cur_[c];
    }
    ++w;
    curPending_ = false;
  }
  return w;
}

size_t Ads131m04::read(int32_t* out, size_t max_samples, uint32_t timeout_ms) {
  if (max_samples == 0) return 0;

  size_t w = drainPending(out, max_samples);
  if (w > 0) return w;                                         // entregar lo pendiente antes de esperar

  const uint32_t n = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms));
  if (n == 0) return 0;

  uint8_t rx[FRAME_BYTES];
  xferFrame(CMD_NULL, 0, rx);                                  // la trama trae la última conversión

  if (cfg::ADS_VERIFY_CRC && !frameCrcOk(rx)) {
    ++errors_;
    err_ = true;                                               // se entrega igual (marcada) para no romper la línea de tiempo
  }
  for (uint8_t c = 0; c < cfg::N_CH; ++c) {
    cur_[c] = sample(rx, c);
    const int32_t a = cur_[c] < 0 ? -cur_[c] : cur_[c];
    if (a >= cfg::SAT_ABS_COUNTS) sat_ = true;
  }
  curPending_ = true;
  if (n > 1) {                                                 // DRDY perdidos: se rellenan con la última muestra
    holdPending_ = n - 1;
    missed_ = missed_ + (n - 1);
  }
  return drainPending(out, max_samples);
}
