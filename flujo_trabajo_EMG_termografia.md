# Flujo de trabajo v2: EMG + Mockup de rodilla + Termografía (sentadilla)

**Materia:** Bioinstrumentación
**Objetivo:** Relacionar la activación muscular (EMG) con el ángulo de rodilla y los cambios de temperatura cutánea durante una actividad terapéutica (sentadilla).
**Músculos:** Vasto lateral (VL) y bíceps femoral (BF); tercer canal opcional (recto femoral o glúteo mayor).
**Microcontrolador:** ESP32-S3 SuperMini.
**Termografía:** Fotric H6 (o equivalente Fotric) + Fotric AnalyzIR.
**Alcance del software:** scripts y un launcher de línea de comandos. No hay aplicación gráfica; solo una ventana mínima de monitoreo en tiempo real.

---

## 0. Cambios respecto a la v1

| Tema | v1 | v2 |
|---|---|---|
| Ángulo de rodilla | IMUs / goniómetro / video | **Mockup articulado** con sensor de ángulo (nodo propio) |
| Transmisión | Por decidir | **Wi-Fi (UDP + TCP de control)** como principal; BLE como respaldo (ver sección 2) |
| Termografía | Antes y después | **Tres momentos: ANTES, DURANTE, DESPUÉS** |
| Fotric | Descripción genérica | Flujo basado en las funciones del manual: *Temperature plot*, *Histogram*, *3D Graph* y los 4 tipos de exportación |
| Firmware | Descripción general | Desglose en **módulos y tareas** del ESP32-S3 |
| Software | Scripts sueltos | **Estructura completa del proyecto** y **launcher** que inicializa los 3 dispositivos |

---

## 1. Arquitectura general

```
                          ┌────────────────────────────────────┐
                          │         PC (Python)                │
                          │  ┌──────────────────────────────┐  │
                          │  │  launcher / orquestador       │  │
                          │  │  (asyncio, máquina de estados)│  │
                          │  └───┬───────────┬───────────┬──┘  │
                          │      │           │           │     │
                          │  driver EMG  driver Mockup  driver Cámara
                          │      │           │           │     │
                          │  recorder + sincronización de relojes │
                          │  visualización RT · procesamiento     │
                          └──────┼───────────┼───────────┼─────┘
                       Wi-Fi UDP │  Wi-Fi UDP│           │ (manual o SDK) + archivos exportados
                          (datos)│  (datos)  │           │
                       TCP (ctrl)│  TCP(ctrl)│           │
                    ┌────────────▼───┐  ┌────▼──────────┐ ┌▼───────────────────┐
                    │ NODO EMG       │  │ NODO MOCKUP   │ │ Fotric H6          │
                    │ ESP32-S3       │  │ ESP32-S3      │ │ → AnalyzIR         │
                    │ + ADC externo  │  │ + sensor de   │ │ → CSV / imágenes   │
                    │ + front-end    │  │   ángulo      │ │   radiométricas    │
                    └────────────────┘  └───────────────┘ └────────────────────┘
```

> **Supuesto a confirmar:** el mockup es un nodo independiente (su propio ESP32-S3). Si prefieren usar un solo ESP32 para EMG + ángulo, el ángulo se lee en el mismo reloj que el EMG (mejor sincronía, menos dispositivos), pero el mockup queda cableado al equipo del EMG. El software de este documento funciona en ambos casos; solo cambia el driver del mockup.

---

## 2. Decisión: ¿Bluetooth o Wi-Fi?

El ESP32-S3 soporta **Wi-Fi y Bluetooth LE 5**, pero **no Bluetooth Classic (SPP)**. "Bluetooth" aquí significa BLE.

### 2.1 Requisitos de datos

| Concepto | Valor |
|---|---|
| EMG | 3 canales × 2000 Hz × 4 B (int32) = 24 kB/s |
| Mockup | 1 canal × 200 Hz × 4 B ≈ 1 kB/s |
| Con encabezados | ≈ 30 kB/s ≈ 250 kbps |

### 2.2 Comparación

| Criterio | Wi-Fi (UDP + TCP) | BLE |
|---|---|---|
| Ancho de banda | Sobrado | Justo; depende de MTU, PHY 2M e intervalo de conexión |
| Latencia y jitter | Bajo | Variable; el stack del PC (p. ej. `bleak`) añade incertidumbre |
| Pérdida de paquetes | Posible en UDP (se detecta con contador de secuencia) | Reintentos integrados, pero con latencia |
| Varios dispositivos al PC | Sencillo (mismo router/hotspot) | Más complejo con varias conexiones |
| Consumo | Mayor | Menor |
| Ruido en el front-end analógico | Ráfagas de TX pueden meter ruido por alimentación | Menor |
| Complejidad de software | Baja (sockets estándar) | Media (GATT, fragmentación) |

### 2.3 Recomendación
- **Wi-Fi como enlace principal:** UDP para datos y TCP para comandos de control.
- Topología: **router o hotspot del PC** (no SoftAP por ESP32: el PC no puede conectarse a dos SoftAP a la vez). IPs fijas o mDNS (`emg.local`, `mockup.local`).
- Mitigar el ruido de Wi-Fi sobre el EMG: ADC **externo** (no el ADC interno), LDO separado para la parte analógica, desacoplo generoso, antena lejos de la zona analógica y `esp_wifi_set_max_tx_power()` moderado.
- Mantener BLE como alternativa documentada solo si el aula tiene mucha interferencia Wi-Fi.

---

## 3. Fases del flujo de trabajo

```
FASE 0  Protocolo y planeación
FASE 1  Hardware EMG y conversión analógico-digital
FASE 2  Mockup de rodilla (ángulo)
FASE 3  Firmware del ESP32-S3 (EMG y mockup)
FASE 4  Comunicación y protocolo de datos
FASE 5  Launcher: inicialización y sincronización de los 3 dispositivos
FASE 6  Recepción, registro y visualización en tiempo real
FASE 7  Procesamiento de la señal EMG
FASE 8  Termografía Fotric (antes, durante, después)
FASE 9  Integración EMG + ángulo + termografía
FASE 10 Validación del instrumento
FASE 11 Resultados y entregables
```

---

### FASE 0. Protocolo y planeación

**Hipótesis de trabajo**
- Los músculos con mayor activación acumulada (iEMG) muestran mayor cambio térmico (ΔT) en su zona cutánea.
- La activación VL/BF depende del ángulo de rodilla y de la fase (descenso/ascenso).

**Protocolo de sentadilla**
- Peso corporal, profundidad objetivo (ej. 90° de flexión), cadencia con metrónomo (ej. 3 s bajar, 3 s subir).
- **3 series de 10 repeticiones**, con pausa corta entre series (la pausa se usa para la captura térmica "durante").

**Línea temporal de una sesión**

```
Aclimatación (10-15 min)
  └─ T0  Termograma ANTES (sin electrodos ni gel)
      └─ Colocación de electrodos y calibración del mockup
          └─ Registro de reposo y MVC (EMG)
              └─ START (launcher: los 3 dispositivos)
                  ├─ Serie 1 (10 rep) ── T1 Termograma DURANTE (pausa)
                  ├─ Serie 2 (10 rep) ── T2 Termograma DURANTE (pausa)
                  └─ Serie 3 (10 rep) ── STOP EMG/mockup
                      └─ T3 Termograma DESPUÉS: 0 min, 5 min, 10 min
```

**Condiciones para termografía**
- Cuarto a 20–24 °C, sin corrientes de aire ni luz solar directa; fondo uniforme.
- Piel sin cremas, alcohol ni gel; sin ejercicio, cafeína ni tabaco 2–4 h antes.
- Emisividad de piel ≈ 0.98 (confirmar en el manual del H6); registrar temperatura ambiente, humedad, temperatura reflejada y distancia.
- Cámara en tripié con **marcas en el piso** para repetir distancia, altura y ángulo.

**Seguridad eléctrica**
- Todo el sistema que toca al sujeto va con **batería LiPo**; nunca conectado a la red.
- No conectar el nodo EMG por USB a la PC durante la medición (solo para programar). La transmisión inalámbrica ayuda en esto.
- Resistencias en serie en las entradas de los electrodos.

---

### FASE 1. Hardware EMG y conversión analógico-digital

**Cadena de señal**

```
Electrodos Ag/AgCl (bipolar, 20 mm) ─ protección ─ Amp. de instrumentación ─ Pasa-altas 20 Hz
  ─ Ganancia ─ Pasa-bajas 450 Hz (anti-aliasing) ─ ADC externo (SPI) ─ ESP32-S3
Referencia: electrodo en hueso (tibia/maléolo) + opcional circuito DRL
```

**Electrodos y ubicación (guía SENIAM; verificar en el documento oficial)**

| Canal | Músculo | Ubicación aproximada |
|---|---|---|
| CH1 | Vasto lateral | ~2/3 de la línea entre la espina ilíaca anterosuperior y el borde lateral de la rótula |
| CH2 | Bíceps femoral | Punto medio entre la tuberosidad isquiática y el epicóndilo lateral de la tibia |
| CH3 (opcional) | Recto femoral o glúteo mayor | Según se defina |

- Piel rasurada, limpia con alcohol y seca; impedancia objetivo < 10 kΩ.
- Marcar el sitio con plumón dermográfico y fotografiarlo: el ROI térmico debe quedar **junto a** los electrodos (el electrodo y su gel alteran la emisividad y la temperatura local).

**Componentes sugeridos**

| Bloque | Opción recomendada | Alternativa |
|---|---|---|
| Amp. de instrumentación | INA333 o INA128 | AD620; módulo AD8232 (menos flexible) |
| Filtros | Sallen-Key 2º orden (op-amps de bajo ruido, ej. OPA2333) | Filtros activos de 4º orden |
| Ganancia total | 200–1000 V/V (ajustar al rango del ADC) | — |
| ADC | **ADS131M04** (24 bit, 4 canales simultáneos, 3.3 V, SPI) | ADS1256 (no simultáneo y módulos de 5 V); ADS1298 (AFE completo, pero reduce el diseño analógico propio) |
| Alimentación | LiPo 1S + LDO de bajo ruido dedicado a la parte analógica | Dual ± con convertidor DC-DC |

> **Evitar:** ADC interno del ESP32-S3 (ruido y no linealidad) y ADS1115 (demasiado lento para 3 canales a 1–2 kHz).
> **Nota ADS131M04:** requiere un reloj externo CLKIN (del orden de unos MHz); el ESP32-S3 puede generarlo con LEDC o I2S. Revisar en el datasheet el rango de entrada diferencial y el modo común admitido para que el front-end sea compatible.

**Parámetros de muestreo**
- **fs = 1000–2000 Hz por canal** (banda útil 20–450 Hz → Nyquist > 900 Hz).
- Con ADC simultáneo no hay desfase entre canales (importante para comparar tiempos de activación VL vs BF).
- Conversión a voltaje: `V_entrada = (cuentas · Vref / 2^(N-1)) / Ganancia_total` (ajustar según formato del ADC: complemento a 2, PGA interno, etc.).

**Pinout del SuperMini:** el SuperMini expone pocos pines y algunos tienen funciones especiales (USB nativo, pines de arranque/*strapping*). Revisar el pinout de **su versión exacta** antes de asignar SPI, DRDY, CLKIN y LED.

---

### FASE 2. Mockup de rodilla (ángulo)

El mockup es un mecanismo articulado (muslo y pierna con eje en la rodilla) cuyo sensor mide el ángulo de flexión.

| Opción de sensor | Resolución típica | Comentario |
|---|---|---|
| **AS5600** (encoder magnético, I²C) | 12 bit (~0.09°) | Sin contacto, sin desgaste; **recomendado** |
| Potenciómetro de precisión (ADC) | Depende del ADC | Barato; con ruido y desgaste |
| IMU en cada segmento | Alta, pero con fusión | Más complejo; deriva |

**Diseño y calibración**
- Eje del sensor alineado con el eje articular; imán centrado.
- Convención: **0° = extensión completa**, ángulo positivo en flexión.
- Calibración estática con goniómetro manual en 0°, 45°, 90°, 120° → ajuste lineal (offset y ganancia).
- Muestreo 100–200 Hz; filtro pasa-bajas ~6 Hz offline para derivar velocidad.
- Fases de la repetición: **descenso** (dθ/dt > 0) y **ascenso** (dθ/dt < 0).

---

### FASE 3. Firmware del ESP32-S3

**Lenguaje y entorno:** C++ con **PlatformIO** (framework Arduino-ESP32 o ESP-IDF). Con FreeRTOS para separar muestreo y red.

#### 3.1 Partes de la adquisición dentro del microcontrolador (nodo EMG)

| # | Módulo | Función | Núcleo / contexto |
|---|---|---|---|
| 1 | Inicialización | Pines, SPI, reloj CLKIN, Wi-Fi, estados | `setup()` |
| 2 | Driver ADC | Configurar ADS131M04 (ganancias, OSR, filtro) y leer tramas por SPI | Tarea muestreo |
| 3 | Interrupción DRDY | Avisar que hay muestra nueva (semáforo, solo un `give` en la ISR) | ISR |
| 4 | Tarea de muestreo | Leer la trama SPI, asignar número de secuencia y timestamp (`esp_timer_get_time()`), enviar a cola | **Core 1, prioridad alta** |
| 5 | Buffer circular / cola | Desacoplar el muestreo (tiempo real) de la red | FreeRTOS |
| 6 | Empaquetador | Agrupar N muestras (ej. 10 = 5 ms a 2 kHz) en una trama binaria con CRC | Tarea red |
| 7 | Transmisión | Enviar por UDP; reintentos de socket y reconexión Wi-Fi | **Core 0** (Wi-Fi) |
| 8 | Canal de control | TCP: comandos `PING`, `ARM`, `START`, `STOP`, `CONFIG`, `STATUS` | Tarea comandos |
| 9 | Sincronización de tiempo | Responder `PING` con timestamp local para estimar el *offset* con la PC | Tarea comandos |
| 10 | Monitoreo | Nivel de batería, LED de estado, contador de paquetes perdidos/overruns | Tarea baja prioridad |
| 11 | Autodiagnóstico | Verificar presencia del ADC, saturación, ruido en reposo | Al armar |
| 12 | (Opcional) OTA / modo calibración | Actualizar firmware sin cable; generar señal de prueba | — |

**Presupuesto de tiempo (referencia a 2 kHz):** el periodo es 500 µs. Una trama del ADC de unos pocos bytes a varios MHz de SPI toma decenas de µs, por lo que sobra margen **si la ISR y la tarea de muestreo no hacen nada más** (nada de `Serial.print` ni de red en esa ruta).

**Esqueleto de la tarea de muestreo**

```cpp
// firmware/emg_node/src/sampler.cpp
void IRAM_ATTR onDrdy() {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(drdySem, &woken);
  if (woken) portYIELD_FROM_ISR();
}

void samplerTask(void*) {
  uint32_t seq = 0;
  for (;;) {
    xSemaphoreTake(drdySem, portMAX_DELAY);
    int64_t t_us = esp_timer_get_time();
    Sample s;
    ads131.readFrame(s.ch);            // 3 canales int32
    s.seq = seq++;  s.t_us = t_us;
    if (xQueueSend(sampleQ, &s, 0) != pdTRUE) overruns++;   // nunca bloquear
  }
}
```

#### 3.2 Nodo mockup

| # | Módulo | Función |
|---|---|---|
| 1 | Init | I²C, Wi-Fi, conexión al AS5600 |
| 2 | Lectura de ángulo | Timer a 200 Hz → lectura del sensor → ángulo en grados (con offset de calibración) |
| 3 | Empaquetador y TX | Mismo formato de trama que el EMG (otro `device_id`) |
| 4 | Comandos | Mismos `PING/ARM/START/STOP/STATUS` |
| 5 | Calibración | Comando para fijar el cero (extensión completa) |

---

### FASE 4. Comunicación y protocolo de datos

**Canales**
- **UDP** (puerto por dispositivo): flujo continuo de muestras.
- **TCP**: comandos de control y respuestas.

**Trama de datos (binaria, little-endian)**

```
Offset  Tamaño  Campo
0       2       magic (0xE5 0x6D)
2       1       versión de protocolo
3       1       device_id (1=EMG, 2=MOCKUP)
4       4       seq (número de paquete)
8       8       t_us (timestamp de la primera muestra, reloj del dispositivo)
16      2       n_samples
18      1       n_channels
19      1       flags (saturación, overrun, etc.)
20      N       datos: n_samples × n_channels × int32
20+N    2       CRC16
```

**Comandos de control (TCP, texto o binario corto)**

| Comando | Respuesta | Uso |
|---|---|---|
| `HELLO` | id, versión, fs, canales, firmware | Descubrimiento |
| `PING <t0>` | `PONG <t0> <t_dev_us>` | Sincronización de relojes |
| `CONFIG {fs, ganancias, ...}` | `OK` | Configuración antes de armar |
| `ARM` | `OK` / error | Prepara buffers y ADC |
| `START` / `STOP` | `OK` | Inicia/detiene el flujo |
| `STATUS` | batería, paquetes, overruns, RSSI | Monitoreo |

**Detección de pérdidas:** el contador `seq` permite contar paquetes perdidos en la PC; el CRC descarta paquetes corruptos. En el registro se guarda cuántos se perdieron por dispositivo.

---

### FASE 5. Launcher: inicialización y sincronización de los 3 dispositivos

**Lenguaje:** Python 3.11+ (`asyncio`). Interfaz: **línea de comandos** (sin GUI).

**Comando de uso**

```
python -m emgthermo.launcher.run_session --subject S01 --protocol config/protocol_squat.yaml
```

#### 5.1 Máquina de estados

```
INIT → DISCOVER → CHECK → SYNC → ARM → THERMAL_PRE → RUN → (THERMAL_DURING ×2) → STOP → THERMAL_POST → SAVE → END
```

| Estado | Qué hace el launcher |
|---|---|
| INIT | Carga configuración YAML, crea la carpeta de sesión, abre el log |
| DISCOVER | Conecta **en paralelo** (`asyncio.gather`) al nodo EMG, al mockup y prepara el driver de cámara |
| CHECK | Autodiagnóstico: versión de firmware, batería, ADC presente, sensor de ángulo respondiendo, espacio en disco |
| SYNC | Estima el *offset* de reloj de cada nodo respecto a la PC con 30–50 intercambios `PING/PONG` (usa el de menor RTT) |
| ARM | Envía `CONFIG` y `ARM`; abre el recorder; muestra monitor opcional |
| THERMAL_PRE | Indica "capturar termograma ANTES"; el operador confirma con una tecla; se registra la hora PC |
| RUN | Envía `START` a EMG y mockup casi a la vez; registra eventos con teclas (inicio/fin de serie, marca manual) |
| THERMAL_DURING | En la pausa entre series: "capturar termograma DURANTE (serie k)" con registro de hora |
| STOP | `STOP` a ambos nodos y cierra archivos |
| THERMAL_POST | Temporizador guiado: "capturar a 0, 5 y 10 min" |
| SAVE | Escribe `session.json`, `events.csv`, `thermal/index.csv`, estadísticas de pérdida de paquetes |

> **Condición de arranque:** no se pasa a RUN si algún dispositivo falla CHECK. Si el EMG o el mockup se desconectan durante RUN, el launcher lo marca en el log y sigue registrando el resto.

#### 5.2 Sincronización de relojes (EMG, mockup, PC)

```python
# host/emgthermo/core/clock_sync.py
import time

async def estimate_offset(dev, n=40):
    best = None
    for _ in range(n):
        t0 = time.monotonic_ns() // 1000            # PC, µs
        t_dev = await dev.ping(t0)                  # µs del dispositivo al responder
        t2 = time.monotonic_ns() // 1000
        rtt = t2 - t0
        offset = t_dev - (t0 + t2) / 2             # dev ≈ pc + offset
        if best is None or rtt < best[0]:
            best = (rtt, offset)
    return best[1], best[0]                         # offset, RTT mínimo

# Tiempo PC de una muestra:  t_pc = t_dev - offset
```

- Respaldo físico de sincronía: **evento de golpe/marca** al inicio (el sujeto hace una contracción brusca o se presiona un pulsador) visible en EMG y en el ángulo; sirve para verificar la sincronía a posteriori.
- Reloj de la cámara: no se sincroniza por red. Se registra la hora PC de cada captura (tecla del operador) y se compara con la hora interna de los archivos exportados.

#### 5.3 Driver de cámara (`thermal_camera.py`)

Hay dos modos, elegidos en la configuración:

| Modo | Qué hace | Cuándo usarlo |
|---|---|---|
| `manual` (por defecto) | Muestra instrucciones, registra la hora al presionar una tecla, nombra y verifica los archivos exportados después | Siempre disponible |
| `sdk` | Disparo/lectura directa desde la PC | **Solo si Fotric ofrece SDK o API para el H6** |

> No pude confirmar si el H6 tiene control remoto o SDK. En otros modelos Fotric (cámaras fijas) existe API/SDK, pero en las portátiles el flujo habitual es pasar archivos a AnalyzIR por Wi-Fi o cable. Pregunten al proveedor o revisen el manual. El launcher funciona completo en modo `manual`.

#### 5.4 Esqueleto del launcher

```python
# host/emgthermo/launcher/run_session.py
import asyncio
from emgthermo.core.config import load_config
from emgthermo.core.session import Session
from emgthermo.devices import EmgNode, MockupNode, ThermalCamera

async def main(args):
    cfg = load_config(args.protocol)
    session = Session.create(subject=args.subject, cfg=cfg)

    emg, mock, cam = EmgNode(cfg.emg), MockupNode(cfg.mockup), ThermalCamera(cfg.camera)
    await asyncio.gather(emg.connect(), mock.connect(), cam.connect())   # en paralelo
    await asyncio.gather(emg.check(), mock.check(), cam.check())

    await asyncio.gather(emg.sync_clock(), mock.sync_clock())
    await asyncio.gather(emg.arm(), mock.arm())

    await cam.prompt_capture("PRE", session)                              # termograma ANTES
    await asyncio.gather(emg.start(), mock.start(), session.recorder.start())
    await session.run_protocol(cam)                                       # series + termograma DURANTE
    await asyncio.gather(emg.stop(), mock.stop())
    await cam.prompt_post_sequence(session, times_min=[0, 5, 10])         # DESPUÉS
    session.save()

if __name__ == "__main__":
    asyncio.run(main(parse_args()))
```

---

### FASE 6. Recepción, registro y visualización en tiempo real

**Recepción (`recorder.py`)**
- Un socket UDP por dispositivo; hilo/tarea que valida magic, versión y CRC.
- Detecta huecos en `seq` y los registra.
- Convierte `t_dev` a tiempo PC con el *offset* de la sincronización.
- Guarda **siempre el dato crudo** (cuentas) y los metadatos de conversión; el filtrado es offline.

**Visualización en tiempo real (`live_plot.py`)**
- Ventana mínima con `pyqtgraph`, 20–30 FPS, ventana deslizante de 3–5 s.
- Paneles: EMG cruda por canal, **envolvente en línea** (rectificación + RMS causal), ángulo de rodilla.
- Indicadores de calidad: RMS en reposo, saturación, paquetes perdidos, batería y RSSI.
- Los filtros en tiempo real son **causales** (`sosfilt`); el análisis final usa fase cero (`sosfiltfilt`).

---

### FASE 7. Procesamiento de la señal EMG (offline)

**Calibración fisiológica por sujeto:** reposo (10 s) y MVC de VL y BF (3 repeticiones de 3–5 s). Si no hay MVC confiable, normalizar al pico de la envolvente en la sentadilla y reportarlo como "% del pico".

**Pipeline**

```
Cuentas → mV → quitar DC → pasa-banda 20–450 Hz (Butterworth 4º, fase cero)
→ notch 60 Hz → control de calidad → rectificación → envolvente RMS (100–250 ms)
→ normalización → segmentación por repetición (ángulo) → características
```

```python
# host/emgthermo/processing/emg_filters.py
from scipy.signal import butter, sosfiltfilt, iirnotch, filtfilt
import numpy as np

def preprocess_emg(x, fs, band=(20, 450), notch=60, order=4):
    x = x - np.mean(x)
    x = sosfiltfilt(butter(order, band, btype="bandpass", fs=fs, output="sos"), x)
    b, a = iirnotch(notch, Q=30, fs=fs)
    return filtfilt(b, a, x)

def envelope_rms(x, fs, win_ms=150):
    w = max(1, int(fs * win_ms / 1000))
    return np.sqrt(np.convolve(x**2, np.ones(w) / w, mode="same"))
```

**Segmentación:** con el ángulo del mockup, `find_peaks` detecta cada flexión máxima; cada repetición se divide en descenso y ascenso y se remuestrea a 0–100 % del ciclo o por rangos angulares (bins de 10°).

**Características**
- Tiempo: RMS, MAV, **iEMG**, pico de envolvente, tiempo al pico, inicio de activación (reposo + 3 SD).
- Frecuencia: MNF y MDF por ventanas (Welch). Su caída indica fatiga.
- Coordinación: razón VL/BF, índice de co-contracción `CCI = 2·∫min(VL,BF) / ∫(VL+BF)`.
- **Por serie:** iEMG acumulado en cada serie (clave para relacionarlo con los termogramas DURANTE).

**Calidad:** SNR (contracción/reposo), saturación, pico de 60 Hz en el espectro, repeticiones descartadas.

---

### FASE 8. Termografía Fotric (ANTES, DURANTE, DESPUÉS)

#### 8.1 Los tres momentos

| Momento | Capturas | Objetivo |
|---|---|---|
| **ANTES** | T0: vistas anterior, lateral y posterior, ambas piernas; sin electrodos ni gel | Línea base |
| **DURANTE** | T1 al terminar la serie 1; T2 al terminar la serie 2 (≤ 30 s de pausa, sujeto de pie, misma posición) | Evolución térmica con el trabajo acumulado |
| **DESPUÉS** | T3a: 0 min (< 1–2 min tras la serie 3), T3b: 5 min, T3c: 10 min | Respuesta máxima y recuperación |

**Notas sobre "DURANTE"**
- Durante el movimiento el sujeto sube y baja en el encuadre y los electrodos y cables cubren la zona, por lo que la medición confiable es en **pausas**.
- Si el H6 permite grabar video o secuencia con intervalo (confirmar en el manual), se puede grabar toda la serie y usar *Temperature plot* para ver T vs tiempo de un ROI; los ROI deben revisarse cuadro por cuadro por el movimiento del cuerpo.
- Se registra la hora PC de cada captura (launcher) y el número de serie asociado.

#### 8.2 Funciones de Fotric AnalyzIR y cómo se usan

| Función del manual | Para qué se usa en este proyecto |
|---|---|
| **Temperature plot** | Perfil de temperatura vs. tiempo de un ROI (máx., mín. y fórmulas propias, por ejemplo promedio de ROI menos ROI de control) a través de la secuencia ANTES → DURANTE → DESPUÉS, si se tiene video/secuencia |
| **Histogram** | Distribución de temperaturas por ROI: comparar ANTES vs DESPUÉS (desplazamiento y ensanchamiento del histograma) y ver zonas más calientes/más frías |
| **3D Graph** | Visualización de la superficie térmica del muslo/rodilla; útil para figuras del reporte y localizar zonas de mayor y menor temperatura; es cualitativo |

#### 8.3 Exportaciones y cuál usar

| Exportación (manual) | Contiene temperaturas | Uso en el proyecto |
|---|---|---|
| **Single Frame Image** | Sí (re-analizable) | **Archivo maestro** de cada captura: conservar siempre |
| **Single Frame Data (.csv)** | Sí (matriz de temperaturas del cuadro) | **Entrada para el análisis en Python** (ROIs, ΔT, mapas) |
| **BPM Non-Radiometric** | **No** | Solo imágenes ilustrativas; **no** sirve para análisis |
| **Report** | Según plantilla | Reporte rápido por captura; complemento al reporte de la materia |

**Regla de oro:** por cada captura, exportar **Single Frame Image + Single Frame Data (.csv)**, con el mismo nombre base.

**Convención de nombres**

```
S01_T0_PRE_ANT.csv        S01_T0_PRE_LAT.csv        S01_T0_PRE_POS.csv
S01_T1_DUR_S1_ANT.csv     S01_T2_DUR_S2_ANT.csv
S01_T3a_POST0_ANT.csv     S01_T3b_POST5_ANT.csv     S01_T3c_POST10_ANT.csv
```

#### 8.4 Análisis de ROIs

**Camino A: en AnalyzIR (rápido, para la clase)**
1. Importar la imagen radiométrica y verificar emisividad, temperatura reflejada, distancia y humedad (mismos valores en todas).
2. Definir ROIs: VL, BF, rodilla/perirrotuliana y **control** (zona no trabajada o pierna contralateral).
3. Leer T media, máx. y mín. de cada ROI; usar *Histogram* y *3D Graph*; anotar zonas de mayor y menor temperatura.

**Camino B: en Python (reproducible)**
- Cargar el `.csv` de Single Frame Data (la lectura debe ser tolerante: detectar líneas de encabezado, separador y decimales, y validar dimensiones y rango de temperatura plausible).
- Definir ROIs como polígonos en un JSON por vista, usando **marcas anatómicas** como referencia; para ΔT por píxel, alinear imágenes (traslación) antes de restar.

```python
# host/emgthermo/processing/thermal_roi.py
import numpy as np

def roi_stats(T, mask):
    v = T[mask]
    return {"mean": v.mean(), "max": v.max(), "min": v.min(), "std": v.std()}

def delta_roi(T_pre, T_post, mask):
    return T_post[mask].mean() - T_pre[mask].mean()
```

**Métricas por ROI y momento:** T media, T máx, T mín, desviación estándar, ΔT respecto a ANTES, ΔT respecto al control y asimetría entre piernas.

**Interpretación:** los cambios de temperatura de piel en ejercicio corto son pequeños (del orden de décimas de °C) y pueden ser negativos al inicio por sudor/vasoconstricción. Por eso se interpretan **ΔT y diferencias contra el control**, y se declara la incertidumbre de la cámara y del ROI.

---

### FASE 9. Integración EMG + ángulo + termografía

El EMG y el ángulo se miden en continuo (ms); la termografía en instantes discretos (T0, T1, T2, T3). La integración se hace **por serie y por zona**.

**Tabla maestra (una fila por sujeto, músculo/ROI y momento)**

| Sujeto | ROI/Músculo | Momento | iEMG acumulado | RMS (% MVC) | MDF | T media | T máx | T mín | ΔT vs ANTES | ΔT vs control |
|---|---|---|---|---|---|---|---|---|---|---|

**Análisis propuestos**
1. **EMG vs ángulo:** envolvente VL y BF vs ángulo y vs % del ciclo; rango angular de máxima activación; co-contracción por rango.
2. **EMG vs temperatura "dosis-respuesta":** iEMG acumulado hasta la serie k vs T del ROI en T0, T1, T2, T3a (un punto por momento).
3. **Rankings:** orden de músculos por iEMG vs orden de ROIs por ΔT; ¿las zonas de mayor/menor temperatura coinciden con mayor/menor activación?
4. **Correlación:** Spearman (preferible con pocos datos) entre iEMG y ΔT; reportar con cautela por el tamaño de muestra.
5. **Fatiga vs temperatura:** caída de MDF por serie vs ΔT.
6. **Asimetría:** pierna trabajada vs control.
7. **Recuperación:** ΔT en T3a, T3b y T3c.

**Figuras sugeridas:** termogramas ANTES/DURANTE/DESPUÉS con ROIs; histogramas; gráfica combinada ángulo + envolventes; barras iEMG vs ΔT; dispersión iEMG vs ΔT; curva T(t) con iEMG acumulado superpuesto.

**Limitaciones a declarar:** muestra pequeña, la temperatura cutánea no es la del músculo, crosstalk del EMG, variabilidad de electrodos y ROI, efecto de gel/electrodos en la piel, diferencias de grasa, vello y sudoración.

---

### FASE 10. Validación del instrumento

| Prueba | Método | Referencia |
|---|---|---|
| Ganancia y linealidad | Senoidal conocida atenuada (ej. 100 Hz, ~1 mVpp) | Error < ~5 % |
| Respuesta en frecuencia | Barrido 5–1000 Hz | Banda 20–450 Hz |
| CMRR | Misma señal en ambas entradas | > 80 dB deseable |
| Ruido de entrada | Entradas en corto | Pocos µV RMS |
| Estabilidad de fs y pérdida de paquetes | Contador `seq`, jitter de `t_us`, sesión larga de 10 min | 0 pérdidas ideales |
| **Efecto de Wi-Fi sobre el ruido** | Comparar ruido con Wi-Fi transmitiendo vs en reposo | Sin aumento notable |
| Sincronía EMG–ángulo | Evento de golpe/marca simultáneo | Diferencia pequeña y estable |
| Calibración del mockup | Comparar con goniómetro manual | Error angular de pocos grados |
| Prueba fisiológica | Contracción vs reposo | SNR claro |

---

### FASE 11. Resultados y entregables

1. Diagrama a bloques, esquemáticos y BOM.
2. Firmware (nodo EMG y nodo mockup), launcher y scripts de procesamiento (repositorio).
3. Datos crudos por sesión con metadatos.
4. Resultados de validación.
5. Termogramas (imagen radiométrica + CSV) e índice de capturas.
6. Gráficas EMG, ángulo y termografía; tabla maestra.
7. Análisis integrado, discusión, limitaciones y conclusiones.

---

## 4. Estructura completa del proyecto

```
emg-thermo-squat/
├── README.md
├── requirements.txt
├── pyproject.toml
├── config/
│   ├── devices.yaml
│   ├── protocol_squat.yaml
│   └── thermal_rois.json
├── firmware/
│   ├── common/
│   │   └── protocol.h
│   ├── emg_node/
│   │   ├── platformio.ini
│   │   ├── include/
│   │   │   ├── config.h
│   │   │   └── pins.h
│   │   └── src/
│   │       ├── main.cpp
│   │       ├── ads131m04.cpp / .h
│   │       ├── sampler.cpp / .h
│   │       ├── packetizer.cpp / .h
│   │       ├── net_wifi.cpp / .h
│   │       ├── command_handler.cpp / .h
│   │       ├── timesync.cpp / .h
│   │       └── monitor.cpp / .h
│   └── mockup_node/
│       ├── platformio.ini
│       ├── include/ (config.h, pins.h)
│       └── src/
│           ├── main.cpp
│           ├── angle_sensor.cpp / .h
│           ├── packetizer.cpp / .h
│           ├── net_wifi.cpp / .h
│           └── command_handler.cpp / .h
├── hardware/
│   ├── schematics/            (KiCad o similar)
│   ├── bom.csv
│   └── notes_calibration.md
├── host/
│   └── emgthermo/
│       ├── __init__.py
│       ├── launcher/
│       │   ├── run_session.py
│       │   ├── state_machine.py
│       │   └── cli.py
│       ├── core/
│       │   ├── config.py
│       │   ├── protocol.py
│       │   ├── clock_sync.py
│       │   ├── recorder.py
│       │   ├── session.py
│       │   └── events.py
│       ├── devices/
│       │   ├── base.py
│       │   ├── emg_node.py
│       │   ├── mockup_node.py
│       │   └── thermal_camera.py
│       ├── realtime/
│       │   ├── live_plot.py
│       │   └── causal_filters.py
│       ├── processing/
│       │   ├── emg_filters.py
│       │   ├── emg_features.py
│       │   ├── segmentation.py
│       │   ├── thermal_loader.py
│       │   ├── thermal_roi.py
│       │   ├── fusion.py
│       │   └── stats.py
│       └── tools/
│           ├── simulate_device.py
│           ├── validate_hardware.py
│           └── calibrate_mockup.py
├── analysis/
│   ├── 01_qc_emg.py
│   ├── 02_emg_features.py
│   ├── 03_thermal_rois.py
│   ├── 04_fusion_report.py
│   └── notebooks/ (opcional, exploración)
├── data/
│   └── S01_YYYYMMDD/
│       ├── session.json
│       ├── events.csv
│       ├── emg_raw.csv
│       ├── angle_raw.csv
│       ├── thermal/
│       │   ├── index.csv
│       │   └── raw/ (Single Frame Image + CSV de Fotric)
│       └── processed/
├── results/
│   ├── figures/
│   └── tables/
├── tests/
│   ├── test_protocol.py
│   ├── test_clock_sync.py
│   ├── test_filters.py
│   └── test_thermal_loader.py
└── docs/
    ├── protocolo_experimental.md
    └── consentimiento_informado.md
```

### 4.1 Descripción de cada archivo

#### Raíz y configuración

| Archivo | Lenguaje | Contenido |
|---|---|---|
| `README.md` | Markdown | Descripción, instalación, cómo correr el launcher, cómo cargar firmware |
| `requirements.txt` / `pyproject.toml` | Texto / TOML | `numpy`, `scipy`, `pandas`, `pyserial`, `pyqtgraph`, `PyQt6`, `pyyaml`, `matplotlib`, `pytest` |
| `config/devices.yaml` | YAML | IPs/hosts, puertos UDP/TCP, `device_id`, fs, canales, ganancias, factor de conversión a mV, modo de cámara (`manual`/`sdk`) |
| `config/protocol_squat.yaml` | YAML | Series, repeticiones, cadencia, profundidad objetivo, momentos térmicos (T0, T1, T2, T3a/b/c), vistas, tiempos de espera |
| `config/thermal_rois.json` | JSON | Polígonos de ROI por vista (VL, BF, rodilla, control) |

#### Firmware común

| Archivo | Lenguaje | Contenido |
|---|---|---|
| `firmware/common/protocol.h` | C++ (header) | Estructura de trama, constantes de magic/versión, `device_id`, códigos de comando, CRC16; **compartido por ambos nodos** y espejado en `core/protocol.py` |

#### Nodo EMG (ESP32-S3)

| Archivo | Lenguaje | Contenido |
|---|---|---|
| `platformio.ini` | INI | Placa ESP32-S3, framework, flags de compilación, librerías |
| `include/config.h` | C++ | fs, tamaño de bloque, SSID/clave (o archivo secreto), puertos, prioridades de tareas |
| `include/pins.h` | C++ | Pines SPI, CS, DRDY, CLKIN, LED, lectura de batería (según pinout del SuperMini) |
| `src/main.cpp` | C++ | `setup()`: inicia módulos y crea tareas FreeRTOS; `loop()` casi vacío |
| `src/ads131m04.cpp/.h` | C++ | Driver SPI del ADC: registros, ganancia, OSR, lectura de trama, CRC del ADC, generación de CLKIN |
| `src/sampler.cpp/.h` | C++ | ISR DRDY, tarea de muestreo, timestamp, cola de muestras |
| `src/packetizer.cpp/.h` | C++ | Agrupa N muestras, arma la trama con encabezado y CRC |
| `src/net_wifi.cpp/.h` | C++ | Conexión y reconexión Wi-Fi, socket UDP de datos, servidor TCP de control |
| `src/command_handler.cpp/.h` | C++ | Interpreta `HELLO/PING/CONFIG/ARM/START/STOP/STATUS` y cambia el estado del nodo |
| `src/timesync.cpp/.h` | C++ | Respuesta a `PING` con timestamp local; contadores |
| `src/monitor.cpp/.h` | C++ | Batería, LED de estado, overruns, paquetes enviados, RSSI |

#### Nodo mockup (ESP32-S3)

| Archivo | Lenguaje | Contenido |
|---|---|---|
| `src/main.cpp` | C++ | Inicialización y tareas |
| `src/angle_sensor.cpp/.h` | C++ | Lectura del AS5600 por I²C, offset de cero, conversión a grados, filtro simple |
| `src/packetizer.cpp/.h`, `net_wifi.cpp/.h`, `command_handler.cpp/.h` | C++ | Igual que en el nodo EMG, con `device_id = 2` y fs de 200 Hz; incluye comando `SET_ZERO` |

#### Hardware

| Archivo | Formato | Contenido |
|---|---|---|
| `hardware/schematics/` | KiCad/PDF | Esquemáticos del front-end, filtros, alimentación y conexión al ADC |
| `hardware/bom.csv` | CSV | Lista de componentes |
| `hardware/notes_calibration.md` | Markdown | Procedimiento de calibración de ganancia, del mockup y registro de resultados |

#### Software en la PC (`host/emgthermo/`, Python)

| Archivo | Contenido |
|---|---|
| `launcher/run_session.py` | Punto de entrada: orquesta conexión, chequeos, sincronía, arranque, protocolo, paro y guardado |
| `launcher/state_machine.py` | Estados y transiciones (INIT → … → END), manejo de errores y reintentos |
| `launcher/cli.py` | Argumentos de línea de comandos y mensajes al operador (prompts de captura térmica con temporizador) |
| `core/config.py` | Carga y validación de YAML |
| `core/protocol.py` | Parseo de tramas con `struct`, verificación de CRC, comandos de control |
| `core/clock_sync.py` | Estimación de *offset* y RTT; conversión de `t_dev` a tiempo PC |
| `core/recorder.py` | Recepción UDP asíncrona, detección de pérdidas, escritura a disco por bloques |
| `core/session.py` | Crea la carpeta, `session.json`, índice de eventos y de capturas térmicas |
| `core/events.py` | Registro de marcas (inicio/fin de serie, golpe de sincronía, capturas térmicas) |
| `devices/base.py` | Clase base `Device` con `connect/check/sync_clock/arm/start/stop` |
| `devices/emg_node.py` | Driver del nodo EMG (TCP + UDP) |
| `devices/mockup_node.py` | Driver del mockup (incluye `set_zero`) |
| `devices/thermal_camera.py` | Modo `manual` (prompts, hora de cada captura, verificación de archivos) y modo `sdk` (si existe) |
| `realtime/live_plot.py` | Ventana `pyqtgraph` con EMG, envolvente y ángulo |
| `realtime/causal_filters.py` | Filtros `sosfilt` con estado para tiempo real |
| `processing/emg_filters.py` | Pasa-banda, notch, rectificación, envolvente, normalización |
| `processing/emg_features.py` | RMS, MAV, iEMG, MNF/MDF, CCI, inicio de activación |
| `processing/segmentation.py` | Detección de repeticiones y fases con el ángulo; remuestreo por ciclo y por rango angular |
| `processing/thermal_loader.py` | Lectura robusta de los CSV de Fotric y del `index.csv` |
| `processing/thermal_roi.py` | Máscaras, estadísticos por ROI, ΔT, alineación simple |
| `processing/fusion.py` | Une EMG por serie con temperaturas por momento; genera la tabla maestra |
| `processing/stats.py` | Spearman/Pearson, intervalos, pruebas simples |
| `tools/simulate_device.py` | Simulador de nodo (EMG sintético + ángulo senoidal) para probar el launcher **sin hardware** |
| `tools/validate_hardware.py` | Automatiza pruebas de la Fase 10 (ruido, ganancia, pérdida de paquetes) |
| `tools/calibrate_mockup.py` | Guía la calibración del ángulo con puntos conocidos y calcula offset/ganancia |

#### Análisis (`analysis/`, Python)

| Script | Contenido |
|---|---|
| `01_qc_emg.py` | Calidad de señal, espectros, repeticiones descartadas |
| `02_emg_features.py` | Pipeline completo y características por repetición/serie |
| `03_thermal_rois.py` | Estadísticos térmicos por ROI y momento, histogramas |
| `04_fusion_report.py` | Tabla maestra, correlaciones y figuras finales |

#### Pruebas (`tests/`, Python con `pytest`)
- `test_protocol.py`: tramas válidas, CRC erróneo, pérdida de `seq`.
- `test_clock_sync.py`: offset conocido con retardos simulados.
- `test_filters.py`: senoidal de 60 Hz eliminada, de 100 Hz conservada.
- `test_thermal_loader.py`: lectura de un CSV de ejemplo de Fotric.

### 4.2 Formatos de datos

**`emg_raw.csv`**: `t_pc_s, seq, ch1_counts, ch2_counts, ch3_counts, flags`
**`angle_raw.csv`**: `t_pc_s, seq, angle_deg`
**`events.csv`**: `t_pc_s, evento, detalle` (START, SERIE_1_INI, SERIE_1_FIN, CAPTURA_T1, …)
**`thermal/index.csv`**: `archivo, momento, serie, vista, t_pc_s, emisividad, T_amb, T_refl, distancia, humedad`
**`session.json`**: sujeto, fecha, firmware, fs, ganancias, offsets de reloj, pérdidas de paquetes, ubicación de electrodos, condiciones ambientales

### 4.3 Resumen de lenguajes

| Parte | Lenguaje |
|---|---|
| Firmware ESP32-S3 | C++ (PlatformIO + Arduino-ESP32/ESP-IDF, FreeRTOS) |
| Launcher, comunicación, grabación | Python (`asyncio`, `socket`, `struct`) |
| Tiempo real | Python (`pyqtgraph`, `numpy`, `scipy`) |
| Procesamiento y análisis | Python (`numpy`, `scipy`, `pandas`, `matplotlib`) |
| Configuración | YAML / JSON |
| Termografía | Fotric AnalyzIR (GUI) + Python para los CSV |

---

## 5. Checklist por sesión

**Antes**
- [ ] Baterías de ambos nodos y de la cámara cargadas
- [ ] Router/hotspot activo, nodos conectados
- [ ] Mockup calibrado (cero en extensión completa)
- [ ] Sujeto aclimatado y piel preparada
- [ ] Cámara configurada, tripié en marcas
- [ ] `python -m emgthermo.launcher.run_session` en CHECK sin errores

**Durante**
- [ ] T0 (ANTES) antes de poner electrodos
- [ ] Reposo + MVC
- [ ] Series con metrónomo; T1 y T2 en las pausas
- [ ] Monitoreo sin saturación ni pérdida de paquetes

**Después**
- [ ] T3a (0 min), T3b (5 min), T3c (10 min)
- [ ] Exportar de Fotric: Single Frame Image + Single Frame Data (.csv) de cada captura
- [ ] Respaldar la carpeta de sesión

---

## 6. Cronograma sugerido

| Semana | Actividad |
|---|---|
| 1 | Protocolo, selección de componentes, simulación del front-end |
| 2 | Front-end analógico y ADC; prueba en protoboard |
| 3 | Firmware del nodo EMG (muestreo + Wi-Fi); `simulate_device.py` y recepción en PC |
| 4 | Mockup + firmware del nodo ángulo; launcher y sincronía |
| 5 | Validación (Fase 10) y sesión piloto |
| 6 | Sesiones experimentales con termografía |
| 7 | Procesamiento, integración, reporte y presentación |

---

## 7. Decisiones pendientes

1. ¿El mockup es un nodo independiente o va conectado al ESP32 del EMG?
2. ¿Qué sensor de ángulo usará el mockup (AS5600, potenciómetro, IMU)?
3. ¿Hay red Wi-Fi disponible en el laboratorio o se usará hotspot del PC?
4. ¿El H6 graba video/secuencia con intervalo? ¿Tiene SDK o control remoto?
5. ¿2 o 3 canales de EMG? ¿Cuál será el tercer músculo?
6. ¿Normalización por MVC o por el pico de la sentadilla?
7. ¿Número de sujetos y series?
8. ¿ADC: ADS131M04 o una alternativa disponible en su proveedor?