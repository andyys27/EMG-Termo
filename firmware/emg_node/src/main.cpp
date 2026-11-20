// main.cpp — Nodo EMG ESP32-S3: ADC1+DMA a 2000 Hz x 2 canales, validación, tramas UDP,
// control por TCP. El nodo NO filtra la señal; solo la marca (ver validator.h).
//
// Tareas:
//   acq_task (núcleo 1, prioridad alta): lee el ADC, valida, arma tramas y las envía por UDP.
//   loop()   (núcleo 1, Arduino):        atiende un cliente TCP con comandos de texto.
//   Wi-Fi y lwIP corren en el núcleo 0.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_timer.h"

#include "config.h"
#include "protocol.h"
#include "validator.h"
#include "adc_continuous_s3.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Falta include/secrets.h: copiar include/secrets.h.example y poner la red Wi-Fi (2.4 GHz)"
#endif

// Estado compartido 
static AdcContinuousS3 adc;
static Validator validator;
static WiFiUDP udp;
static WiFiServer server(TCP_PORT);
static WiFiClient client;

static portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool streaming = false;
static IPAddress udp_ip;
static uint16_t udp_port = UDP_DEFAULT_PORT;

struct Stats {
  uint32_t frames;
  uint32_t send_errors;
  uint32_t overruns;
  uint32_t lost_conv_at_start;
  int64_t t_start_us;
  int64_t t_stop_us;
};
static Stats stats = {};

// ARM: acumulación pedida desde loop() y llenada por acq_task
struct ArmAcc {
  volatile bool active;
  volatile bool done;
  uint32_t n;
  double sum[N_CH], sum2[N_CH];
  uint16_t mn[N_CH], mx[N_CH];
  uint16_t flags;
};
static ArmAcc arm = {};

static volatile int64_t sync_pulse_until_us = 0;

// Adquisición 
static uint16_t frame_samples[SAMPLES_PER_FRAME * N_CH];
static int frame_fill = 0;
static uint64_t frame_t0_us = 0;
static uint32_t seq = 0;
static bool pending_overrun = false;
static bool pending_retry = false;
static uint8_t tx[FRAME_BYTES];

// Última muestra, para el monitor serie
static portMUX_TYPE last_mux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t last_sample[N_CH];
static uint64_t last_t_us = 0;
static uint16_t last_flags = 0;

static void on_sample(const uint16_t* s, uint64_t t_us) {
  portENTER_CRITICAL(&last_mux);
  for (int c = 0; c < N_CH; ++c) last_sample[c] = s[c];
  last_t_us = t_us;
  portEXIT_CRITICAL(&last_mux);

  if (frame_fill == 0) frame_t0_us = t_us;
  for (int c = 0; c < N_CH; ++c) frame_samples[frame_fill * N_CH + c] = s[c];
  if (++frame_fill < SAMPLES_PER_FRAME) return;
  frame_fill = 0;

  uint16_t flags = validator.process_frame(frame_samples);
  if (pending_overrun) { flags |= FLAG_OVERRUN; pending_overrun = false; }
  if ((int64_t)frame_t0_us < sync_pulse_until_us) flags |= FLAG_SYNC_PULSE;
  last_flags = flags;

  if (arm.active) {
    for (int i = 0; i < SAMPLES_PER_FRAME; ++i)
      for (int c = 0; c < N_CH; ++c) {
        const uint16_t v = frame_samples[i * N_CH + c];
        arm.sum[c] += v;
        arm.sum2[c] += (double)v * v;
        if (v < arm.mn[c]) arm.mn[c] = v;
        if (v > arm.mx[c]) arm.mx[c] = v;
      }
    arm.flags |= flags;
    arm.n += SAMPLES_PER_FRAME;
    if (arm.n >= ARM_SAMPLES) { arm.active = false; arm.done = true; }
  }

  if (!streaming) return;
  if (pending_retry) { flags |= FLAG_SEND_RETRY; pending_retry = false; }

  frame_encode(tx, seq, frame_t0_us, flags, frame_samples);
  seq++;
  bool ok = udp.beginPacket(udp_ip, udp_port) && udp.write(tx, FRAME_BYTES) == FRAME_BYTES && udp.endPacket();
  if (ok) {
    stats.frames++;
  } else {
    stats.send_errors++;
    pending_retry = true;
  }
}

static void acq_task(void*) {
  for (;;) {
    if (adc.poll(on_sample, 20)) {
      pending_overrun = true;
      frame_fill = 0;   // La trama parcial tendría un hueco: se descarta
      if (streaming) stats.overruns++;
    }
  }
}

// Comandos TCP
static void reply(const String& s) {
  if (client && client.connected()) client.print(s + "\n");
  Serial.println("> " + s);
}

static String json_arr_u(const uint16_t* v) {
  String s = "[";
  for (int c = 0; c < N_CH; ++c) { s += String(v[c]); if (c < N_CH - 1) s += ","; }
  return s + "]";
}
static String json_arr_f(const double* v) {
  String s = "[";
  for (int c = 0; c < N_CH; ++c) { s += String(v[c], 2); if (c < N_CH - 1) s += ","; }
  return s + "]";
}

static void cmd_hello() {
  String s = "OK HELLO {\"fw\":\"" FW_NAME "\",\"ver\":\"" FW_VERSION "\",\"proto\":" + String(PROTOCOL_VERSION) +
             ",\"fs_hz\":" + String(FS_HZ) + ",\"n_ch\":" + String(N_CH) + ",\"os\":" + String(OS_FACTOR) +
             ",\"spf\":" + String(SAMPLES_PER_FRAME) + ",\"adc_hz\":" + String(ADC_TOTAL_HZ) +
             ",\"sync_pulse\":" + String(USE_SYNC_PULSE) + ",\"mac\":\"" + WiFi.macAddress() + "\"}";
  reply(s);
}

static void cmd_arm() {
  if (streaming) { reply("ERR ARM no permitido durante START"); return; }
  memset((void*)&arm, 0, sizeof(arm));
  for (int c = 0; c < N_CH; ++c) arm.mn[c] = 0xFFFF;
  arm.active = true;
  const uint32_t t_end = millis() + 3000;
  while (!arm.done && millis() < t_end) delay(10);
  if (!arm.done) { arm.active = false; reply("ERR ARM sin datos del ADC"); return; }
  double mean[N_CH], sd[N_CH];
  for (int c = 0; c < N_CH; ++c) {
    mean[c] = arm.sum[c] / arm.n;
    const double v = arm.sum2[c] / arm.n - mean[c] * mean[c];
    sd[c] = v > 0 ? sqrt(v) : 0;
  }
  reply("OK ARM {\"n\":" + String(arm.n) + ",\"mean\":" + json_arr_f(mean) + ",\"std\":" + json_arr_f(sd) +
        ",\"min\":" + json_arr_u(arm.mn) + ",\"max\":" + json_arr_u(arm.mx) + ",\"flags\":" + String(arm.flags) + "}");
}

static void cmd_start(const String& arg) {
  if (streaming) { reply("ERR ya transmitiendo"); return; }
  long p = arg.length() ? arg.toInt() : UDP_DEFAULT_PORT;
  if (p <= 0 || p > 65535) { reply("ERR puerto invalido"); return; }
  portENTER_CRITICAL(&state_mux);
  udp_ip = client.remoteIP();
  udp_port = (uint16_t)p;
  seq = 0;
  stats = {};
  stats.lost_conv_at_start = adc.lost_conversions();
  stats.t_start_us = esp_timer_get_time();
  streaming = true;
  portEXIT_CRITICAL(&state_mux);
  reply("OK START {\"port\":" + String(udp_port) + ",\"ip\":\"" + udp_ip.toString() + "\",\"t_us\":" +
        String((uint64_t)stats.t_start_us) + "}");
}

static void cmd_stop() {
  if (!streaming) { reply("ERR no se estaba transmitiendo"); return; }
  streaming = false;
  delay(20);   // Dejar que la tarea termine la trama en curso
  stats.t_stop_us = esp_timer_get_time();
  reply("OK STOP {\"frames\":" + String(stats.frames) + ",\"seq_next\":" + String(seq) +
        ",\"send_errors\":" + String(stats.send_errors) + ",\"overruns\":" + String(stats.overruns) +
        ",\"lost_conv\":" + String(adc.lost_conversions() - stats.lost_conv_at_start) +
        ",\"bad_conv\":" + String(adc.bad_conversions()) +
        ",\"t_start_us\":" + String((uint64_t)stats.t_start_us) + ",\"t_stop_us\":" + String((uint64_t)stats.t_stop_us) + "}");
}

static void cmd_status() {
  reply("OK STATUS {\"streaming\":" + String(streaming ? "true" : "false") + ",\"frames\":" + String(stats.frames) +
        ",\"overruns\":" + String(stats.overruns) + ",\"rssi\":" + String(WiFi.RSSI()) +
        ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"uptime_us\":" + String((uint64_t)esp_timer_get_time()) + "}");
}

static void cmd_sync_pulse() {
#if USE_SYNC_PULSE
  const int64_t t = esp_timer_get_time();
  sync_pulse_until_us = t + SYNC_PULSE_MS * 1000LL;
  digitalWrite(SYNC_PULSE_GPIO, HIGH);
  reply("OK SYNC_PULSE {\"t_us\":" + String((uint64_t)t) + "}");
#else
  reply("ERR SYNC_PULSE desactivado (USE_SYNC_PULSE=0)");
#endif
}

static void handle_line(String line) {
  line.trim();
  if (!line.length()) return;
  const int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  cmd.toUpperCase();
  arg.trim();

  if (cmd == "PING") { client.print("PONG " + String((uint64_t)esp_timer_get_time()) + "\n"); return; }  // Sin log: latencia mínima
  Serial.println("< " + line);
  if (cmd == "HELLO") cmd_hello();
  else if (cmd == "ARM") cmd_arm();
  else if (cmd == "START") cmd_start(arg);
  else if (cmd == "STOP") cmd_stop();
  else if (cmd == "STATUS") cmd_status();
  else if (cmd == "SYNC_PULSE") cmd_sync_pulse();
  else reply("ERR comando desconocido: " + cmd);
}

// Monitor serie con hora ISO 8601
// Convierte un tiempo del nodo (esp_timer, µs) a "AAAA-MM-DDThh:mm:ss.mmm±hh:mm".
// Sin hora NTP todavía, imprime el tiempo desde el arranque: "+sssss.mmm".
static void format_iso(char* out, size_t n, uint64_t t_us) {
  struct timeval now;
  gettimeofday(&now, nullptr);
  const int64_t now_us = (int64_t)now.tv_sec * 1000000LL + now.tv_usec;
  if (now.tv_sec < 1700000000) {   // Reloj sin sincronizar
    snprintf(out, n, "+%llu.%03llu", (unsigned long long)(t_us / 1000000ULL), (unsigned long long)(t_us / 1000ULL % 1000ULL));
    return;
  }
  const int64_t wall_us = now_us - (esp_timer_get_time() - (int64_t)t_us);
  const time_t sec = (time_t)(wall_us / 1000000LL);
  struct tm tmv;
  localtime_r(&sec, &tmv);
  char date[24], tz[8];
  strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &tmv);
  strftime(tz, sizeof(tz), "%z", &tmv);   // -0600
  snprintf(out, n, "%s.%03d%.3s:%.2s", date, (int)(wall_us / 1000 % 1000), tz, tz + 3);
}

static void serial_adc_monitor() {
#if SERIAL_ADC_PERIOD_MS > 0
  static uint32_t next_ms = 0;
  if ((int32_t)(millis() - next_ms) < 0) return;
  next_ms = millis() + SERIAL_ADC_PERIOD_MS;
  uint16_t s[N_CH];
  uint64_t t;
  portENTER_CRITICAL(&last_mux);
  for (int c = 0; c < N_CH; ++c) s[c] = last_sample[c];
  t = last_t_us;
  portEXIT_CRITICAL(&last_mux);
  if (!t) return;
  char iso[40];
  format_iso(iso, sizeof(iso), t);
  // Suma de OS_FACTOR conversiones y su equivalente en cuentas de 12 bits
  Serial.printf("%s ch1(GPIO%d)=%u (%.1f) ch2(GPIO%d)=%u (%.1f) flags=0x%04x\n", iso,
                CH_GPIO[0], s[0], s[0] / (float)OS_FACTOR, CH_GPIO[1], s[1], s[1] / (float)OS_FACTOR, last_flags);
#endif
}

// Arranque
static void wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);   // Sin ahorro de energía: menos latencia y pérdidas
#ifdef WIFI_EAP_USERNAME
  // WPA2-Enterprise PEAP (red de la escuela). Sin certificado de CA: no valida el servidor.
#ifndef WIFI_EAP_IDENTITY
#define WIFI_EAP_IDENTITY WIFI_EAP_USERNAME
#endif
  WiFi.begin(WIFI_SSID, WPA2_AUTH_PEAP, WIFI_EAP_IDENTITY, WIFI_EAP_USERNAME, WIFI_EAP_PASSWORD);
#else
  WiFi.begin(WIFI_SSID, WIFI_PASS);   // WPA2-Personal (hotspot o router)
#endif
  Serial.printf("Conectando a %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\nIP: %s  (TCP %d, RSSI %d dBm)\n", WiFi.localIP().toString().c_str(), TCP_PORT, WiFi.RSSI());
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.printf("\n%s %s — fs=%d Hz x %d ch, OS x%d (ADC %d Hz)\n", FW_NAME, FW_VERSION, FS_HZ, N_CH, OS_FACTOR, ADC_TOTAL_HZ);

#if USE_SYNC_PULSE
  pinMode(SYNC_PULSE_GPIO, OUTPUT);
  digitalWrite(SYNC_PULSE_GPIO, LOW);
#endif

  esp_err_t err = adc.begin();
  if (err != ESP_OK) {
    Serial.printf("ERROR ADC: %s. Revisar pines/frecuencia (plan B: OS_FACTOR=4).\n", esp_err_to_name(err));
    for (;;) delay(1000);
  }
  xTaskCreatePinnedToCore(acq_task, "acq", 8192, nullptr, configMAX_PRIORITIES - 2, nullptr, 1);

  wifi_connect();
  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);   // Hora real para el monitor serie
  udp.begin(0);
  server.begin();
  server.setNoDelay(true);
}

void loop() {
  serial_adc_monitor();
#if USE_SYNC_PULSE
  if (sync_pulse_until_us && esp_timer_get_time() >= sync_pulse_until_us) {
    digitalWrite(SYNC_PULSE_GPIO, LOW);
    sync_pulse_until_us = 0;
  }
#endif

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi perdido; reconectando");
    WiFi.reconnect();
    delay(500);
    return;
  }

  if (!client || !client.connected()) {
    WiFiClient c = server.accept();
    if (c) {
      if (streaming) cmd_stop();   // Cliente anterior se fue sin STOP
      client = c;
      client.setNoDelay(true);
      Serial.printf("Cliente %s\n", client.remoteIP().toString().c_str());
    }
    delay(2);
    return;
  }

  static String line;
  while (client.available()) {
    char ch = (char)client.read();
    if (ch == '\n') { handle_line(line); line = ""; }
    else if (ch != '\r' && line.length() < 128) line += ch;
  }
  delay(1);
}