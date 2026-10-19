# Flujo de trabajo v3: EMG + dispositivo BTS (mockup de rodilla) + Termografía (sentadilla)

**Materia:** Bioinstrumentación
**Objetivo:** Relacionar la activación muscular (EMG) con el ángulo de rodilla y los cambios de temperatura cutánea durante una actividad terapéutica (sentadilla).
**Alcance del software:** scripts y un launcher de línea de comandos. No hay aplicación gráfica; solo una ventana mínima de monitoreo en tiempo real.

---

## 0. Decisiones confirmadas (v3)

| Tema | Decisión |
|---|---|
| Canales EMG | **2 canales**: vasto lateral (VL) y bíceps femoral (BF). Un tercer canal queda como ampliación futura |
| Microcontrolador | ESP32-S3 SuperMini |
| ADC | **ADC interno del ESP32-S3** (ver Fase 1 para mitigar sus limitaciones) |
| Transmisión | Wi-Fi mediante **hotspot** (UDP datos + TCP control) |
| Mockup / ángulo de rodilla | **Nodo independiente: dispositivo BTS** (comercial, con su propio software) |
| Normalización EMG | Por **pico de la sentadilla** (probable) |
| Termografía | Fotric H6 + AnalyzIR, en tres momentos: ANTES, DURANTE y DESPUÉS |
| Sujetos | Por definir (se dejan criterios sugeridos en la Fase 0) |

### Cambios respecto a la v2

| Tema | Cambio |
|---|---|
| Mockup | Ya **no hay firmware ni nodo ESP32 de ángulo**. Se integra el BTS mediante exportación de archivos y un **evento de sincronía** (con opción de trigger por hardware si el equipo lo permite) |
| ADC | Se elimina el ADS131M04 y su driver SPI. El muestreo es con el **ADC1 del ESP32-S3 en modo continuo con DMA**, con sobremuestreo y decimación |
| Canales | Todo el pipeline pasa de 3 a 2 canales (menos ancho de banda y datos) |
| Launcher | Maneja **dos dispositivos con enlace de red/PC (EMG, cámara en modo manual) y el BTS en modo asistido** (prompts + registro de horas) |
| Normalización | Definida la forma de calcular el pico de referencia y sus limitaciones |

> **Supuesto a confirmar:** el dispositivo BTS entrega el **ángulo de rodilla** (o datos con los que se pueda calcular) y permite **exportar los datos con tiempo** (CSV u otro formato). BTS Bioengineering tiene varios equipos (inerciales, EMG inalámbrico, captura óptica), por lo que hay que confirmar el **modelo exacto**, el **software** que usa, el **formato de exportación**, la **frecuencia de muestreo** y si tiene **entrada/salida de sincronía (trigger)**.

---

## 1. Arquitectura general

```
                      ┌───────────────────────────────────────────┐
                      │               PC (Python)                 │
                      │  launcher / orquestador (asyncio)         │
                      │   ├─ driver EMG  (TCP control + UDP datos) │
                      │   ├─ driver BTS  (modo asistido)           │
                      │   └─ driver Cámara (modo manual)           │
                      │  recorder · sincronía · tiempo real        │
                      │  procesamiento · integración               │
                      └───────┬──────────────┬───────────────┬────┘
                    Hotspot   │              │ prompts +     │ prompts +
                    Wi-Fi 2.4 │              │ archivos      │ archivos
                              │              │ exportados    │ exportados
                    ┌─────────▼──────┐  ┌────▼──────────┐ ┌──▼────────────────┐
                    │ NODO EMG       │  │ DISPOSITIVO   │ │ Fotric H6         │
                    │ ESP32-S3       │  │ BTS (mockup)  │ │ → AnalyzIR        │
                    │ ADC1 interno   │  │ ángulo rodilla│ │ → imagen + CSV    │
                    │ + front-end    │  │ + su software │ │                   │
                    └────────────────┘  └───────────────┘ └───────────────────┘
        (opcional) pulso de sincronía por GPIO del ESP32 → entrada de trigger del BTS
```

---

## 2. Transmisión: Wi-Fi por hotspot

El ESP32-S3 tiene **Wi-Fi y BLE**, pero no Bluetooth Classic. Con 2 canales, 2000 Hz y 2 bytes por muestra (ADC de 12 bit empaquetado en `uint16`) el flujo es de unos **8 kB/s** (≈ 64 kbps) más encabezados: sobrado para Wi-Fi.

**Configuración del hotspot**
- El ESP32-S3 solo opera en **2.4 GHz**: el hotspot **debe estar en banda 2.4 GHz** (en Windows, "Hotspot móvil" permite elegir la banda; en celular, desactivar 5 GHz/"banda preferida 5 GHz").
- Windows suele asignar al PC una IP fija en la red del hotspot (típicamente `192.168.137.1`; verificar).
- Firmware: el ESP32 se conecta como estación (STA). Dos formas de que encuentre a la PC:
  1. **La PC inicia la conexión TCP** al nodo y el nodo toma la IP del cliente como destino del UDP (recomendado: no depende de IPs fijas en el firmware).
  2. mDNS (`emg.local`) para que la PC encuentre al nodo.
- Registrar la IP asignada en `devices.yaml` o descubrirla con `HELLO` por mDNS.
- Mantener la PC y el nodo cerca del hotspot; evitar otras redes 2.4 GHz saturadas en el laboratorio (hacer un escaneo previo del canal Wi-Fi).
- Reducir ruido en el EMG: LDO separado para la parte analógica, desacoplo, antena lejos del front-end, potencia de TX moderada (`esp_wifi_set_max_tx_power()`).

> BLE queda solo como plan de respaldo si hubiera mucha interferencia.

---

## 3. Fases del flujo de trabajo

```
FASE 0  Protocolo y planeación
FASE 1  Hardware EMG y conversión analógico-digital (ADC interno)
FASE 2  Dispositivo BTS (mockup de rodilla)
FASE 3  Firmware del ESP32-S3
FASE 4  Comunicación y protocolo de datos
FASE 5  Launcher: inicialización y sincronización
FASE 6  Recepción, registro y visualización en tiempo real
FASE 7  Procesamiento de la señal EMG y normalización
FASE 8  Termografía Fotric (antes, durante, después)
FASE 9  Integración EMG + ángulo + termografía
FASE 10 Validación del instrumento
FASE 11 Resultados y entregables
```

---

### FASE 0. Protocolo y planeación

**Hipótesis de trabajo**
- Los músculos con mayor activación acumulada muestran mayor cambio térmico (ΔT) en su zona cutánea.
- La activación VL/BF depende del ángulo de rodilla y de la fase (descenso/ascenso).

**Protocolo de sentadilla**
- Peso corporal, profundidad objetivo (ej. 90° de flexión), cadencia con metrónomo (ej. 3 s bajar, 3 s subir).
- **3 series de 10 repeticiones**, con pausa corta entre series (se usa para la captura térmica "durante").

**Sujetos (por definir; criterios sugeridos)**
- Adultos sanos, sin lesión ni cirugía de rodilla reciente ni dolor al realizar sentadilla.
- Registrar edad, sexo, peso, estatura, pierna dominante y actividad física habitual.
- Diseño intrasujeto: el análisis térmico usa la **pierna no trabajada o una zona sin ejercicio como control**.
- Con pocos sujetos el análisis es descriptivo; definir el número cuando se conozca el tiempo de uso de la cámara y del BTS.
- Consentimiento informado y datos anonimizados (`S01`, `S02`, …).

**Línea temporal de una sesión**

```
Aclimatación (10-15 min)
  └─ T0  Termograma ANTES (sin electrodos ni gel)
      └─ Colocación de electrodos y sensores BTS
          └─ Reposo EMG (10 s)
              └─ START (launcher) + captura BTS iniciada
                  ├─ Evento de sincronía inicial
                  ├─ Serie 1 (10 rep) ── T1 Termograma DURANTE (pausa)
                  ├─ Serie 2 (10 rep) ── T2 Termograma DURANTE (pausa)
                  ├─ Serie 3 (10 rep)
                  ├─ Evento de sincronía final
                  └─ STOP EMG y BTS
                      └─ T3 Termograma DESPUÉS: 0 min, 5 min, 10 min
```

**Condiciones para termografía**
- Cuarto a 20–24 °C, sin corrientes de aire ni luz solar directa; fondo uniforme.
- Piel sin cremas, alcohol ni gel; sin ejercicio, cafeína ni tabaco 2–4 h antes.
- Emisividad de piel ≈ 0.98 (confirmar en el manual del H6); registrar temperatura ambiente, humedad, temperatura reflejada y distancia.
- Cámara en tripié con marcas en el piso para repetir distancia, altura y ángulo.

**Seguridad eléctrica**
- Nodo EMG alimentado **solo con batería LiPo**; no conectarlo a la PC por USB durante la medición (el USB es solo para programar).
- Resistencias en serie en las entradas de los electrodos.

---

### FASE 1. Hardware EMG y conversión analógico-digital (ADC interno del ESP32-S3)

**Cadena de señal**

```
Electrodos Ag/AgCl (bipolar, ~20 mm) ─ protección ─ Amp. de instrumentación ─ Pasa-altas ~20 Hz
  ─ Etapa de ganancia + nivel de offset (bias) ─ Pasa-bajas ~450 Hz (anti-aliasing)
  ─ ADC1 del ESP32-S3
Referencia: electrodo en hueso (tibia/maléolo) + opcional circuito DRL
```

**Electrodos y ubicación (guía SENIAM; verificar en el documento oficial)**

| Canal | Músculo | Ubicación aproximada |
|---|---|---|
| CH1 | Vasto lateral | ~2/3 de la línea entre la espina ilíaca anterosuperior y el borde lateral de la rótula |
| CH2 | Bíceps femoral | Punto medio entre la tuberosidad isquiática y el epicóndilo lateral de la tibia |

- Piel rasurada, limpia con alcohol y seca; impedancia objetivo < 10 kΩ.
- Marcar el sitio con plumón dermográfico y fotografiarlo. El ROI térmico va **junto a** los electrodos (el electrodo y el gel alteran la emisividad y la temperatura local).

**Componentes sugeridos**

| Bloque | Opción recomendada | Alternativa |
|---|---|---|
| Amp. de instrumentación | INA333 o INA128 | AD620; AD8232 (menos flexible) |
| Filtros | Sallen-Key 2º orden con op-amps de bajo ruido (ej. OPA2333) | Filtros activos de 4º orden |
| Alimentación | LiPo 1S + LDO de bajo ruido dedicado a la parte analógica | Dual ± con DC-DC |
| ADC | **ADC1 interno del ESP32-S3** | ADC externo SPI (plan B, ver abajo) |

#### 1.1 Uso del ADC interno: reglas para que funcione bien

El ADC interno es de 12 bit, con ruido y no linealidad mayores que los de un ADC externo, y además es sensible al ruido del Wi-Fi y de la alimentación. Es viable para EMG **si se cuidan estos puntos**:

| Punto | Recomendación |
|---|---|
| Qué ADC usar | **ADC1** (GPIO1–GPIO10 en el S3; verificar cuáles están disponibles en su SuperMini). **Evitar ADC2**: comparte recursos con el Wi-Fi y algunos de sus pines son del USB |
| Modo de captura | **Modo continuo con DMA** (`adc_continuous` de ESP-IDF), no lecturas sueltas con `analogRead()` |
| Frecuencia de muestreo | Objetivo **2000 Hz por canal**; el modo continuo permite muestrear más rápido para **sobremuestrear** (ej. ×8 → 2 canales × 2000 × 8 = 32 kHz totales; verificar el rango admitido en la documentación de ESP-IDF) |
| Sobremuestreo y decimación | Promediar N muestras por canal y por periodo de 500 µs. Reduce el ruido (aprox. √N si es ruido blanco) y gana bits efectivos |
| Atenuación y rango | Usar atenuación alta (≈12 dB) y mantener la señal en la **zona lineal** del ADC (evitar los extremos del rango; revisar en el datasheet/documentación). Centrar la señal con un **offset (bias)** cerca de la mitad del rango útil |
| Ganancia analógica | Ajustar para que el pico a pico de la sentadilla ocupe ~60–70 % del rango lineal. Punto de partida: **300–500 V/V** y afinar en el piloto |
| Anti-aliasing | Pasa-bajas analógico ~450 Hz antes del ADC, más una red RC en la entrada del ADC (para el capacitor de muestreo) |
| Ruido del Wi-Fi | Desacoplo en 3.3 V, LDO analógico separado, plano de tierra, conductores analógicos cortos y alejados de la antena y del SPI/USB |
| Calibración | Guardar **cuentas crudas**. Calibrar con una señal conocida (generador) para obtener µV por cuenta; usar la calibración de fábrica de ESP-IDF solo como apoyo |

**Resolución estimada (para dimensionar la ganancia)**
- LSB del ADC ≈ rango útil / 4096 ≈ 0.7–0.8 mV en el pin.
- Referido a la entrada con ganancia ≈ 400: ≈ 2 µV por cuenta, y con sobremuestreo ×8 el escalón efectivo baja a menos de 1 µV.
- El EMG superficial tiene señal de cientos de µV a pocos mV, por lo que esta resolución es suficiente **si el ruido analógico y de alimentación está controlado**. (Valores aproximados: confirmarlos midiendo en la Fase 10.)

**Pruebas de decisión (go/no-go) antes de seguir**
1. Entradas del amplificador en corto, Wi-Fi apagado: medir ruido RMS en cuentas.
2. Igual, con Wi-Fi transmitiendo a 2000 Hz.
3. Señal senoidal de ~1 mVpp (atenuada) a 100 Hz: ¿se ve limpia y con amplitud correcta?
4. Si el ruido con Wi-Fi arruina la señal, **plan B**: ADC externo por SPI (ADS131M04 o similar). Por eso el firmware define una **interfaz `AdcSource`** y el resto del código no depende del ADC concreto.

**Parámetros de muestreo**
- **fs = 2000 Hz por canal** (mínimo 1000 Hz; la banda útil llega a 450 Hz).
- El ADC multiplexa los canales, así que hay un pequeño desfase entre CH1 y CH2 (microsegundos a decenas de µs); es despreciable frente a la dinámica del EMG, pero se documenta.
- Datos enviados: **cuentas crudas** (`uint16`) ya decimadas a fs. La conversión a µV se hace en la PC.

**Pinout del SuperMini:** el SuperMini expone pocos pines y algunos tienen funciones especiales (USB nativo, *strapping*). Revisar el pinout de **su versión exacta** antes de asignar los dos canales de ADC1, el LED y la lectura de batería.

---

### FASE 2. Dispositivo BTS (mockup de rodilla)

El BTS es un equipo comercial con su propio software de adquisición. En este proyecto **no se programa un nodo para el ángulo**: el BTS graba en paralelo y sus datos se integran con el EMG por sincronía.

**Preguntas a resolver con el laboratorio / manual BTS**

| Pregunta | Por qué importa |
|---|---|
| ¿Modelo y software del BTS? | Define cómo se inicia la captura y en qué formato se exporta |
| ¿Entrega directamente el ángulo de rodilla? | Si no, hay que calcularlo (por ejemplo con orientaciones de segmentos) |
| ¿Frecuencia de muestreo? | Para remuestrear y alinear con el EMG |
| ¿Formato de exportación (CSV, TXT, otro)? | Para escribir `bts_loader.py` |
| ¿Tiene entrada/salida de trigger (sincronía)? | Permite sincronía por hardware con un pulso del ESP32 |
| ¿Tiene API o se controla solo desde su software? | Define si el launcher puede iniciar el BTS o solo asistir al operador |

**Integración en el flujo (modo asistido por defecto)**
1. El launcher indica "iniciar captura BTS" y registra la hora PC.
2. Se inicia el EMG (`START`) y se ejecuta un **evento de sincronía** visible en ambos sistemas.
3. Al final se repite el evento de sincronía y se detiene BTS y EMG.
4. Se exportan los datos del BTS y se copian a `data/<sesión>/bts/raw/`.

**Evento de sincronía recomendado**
- De pie: **3 flexo-extensiones rápidas de rodilla** (o una patada corta) al inicio y al final. Producen picos claros de velocidad angular en el BTS y ráfagas de EMG en VL/BF.
- La alineación se calcula por **correlación cruzada** entre la envolvente EMG y |dθ/dt|.
- Con dos eventos (inicio y fin) se estima **desfase y deriva** relativa entre relojes y se corrige linealmente.
- **Opción por hardware** (solo si el BTS tiene entrada/salida de trigger): un GPIO del ESP32 emite un pulso en `START` hacia la entrada del BTS; se verifica con el evento de golpe.

**Convención de ángulo:** 0° = extensión completa, positivo en flexión. Verificar que la configuración del BTS use esta convención (o convertirla en `bts_loader.py`). Fases: **descenso** (dθ/dt > 0) y **ascenso** (dθ/dt < 0).

---

### FASE 3. Firmware del ESP32-S3 (nodo EMG)

**Lenguaje y entorno:** C++ con **PlatformIO** (Arduino-ESP32 o ESP-IDF) y FreeRTOS. Se recomienda ESP-IDF o la API `adc_continuous` para el modo continuo con DMA.

#### 3.1 Partes de la adquisición dentro del microcontrolador

| # | Módulo | Función | Contexto |
|---|---|---|---|
| 1 | Inicialización | Pines, ADC1, Wi-Fi, estado inicial | `setup()` |
| 2 | **Fuente de ADC (`AdcSource`)** | Interfaz abstracta: `begin()`, `read(block)`; implementación `AdcContinuousS3` (ADC1 + DMA). Permite cambiar por un ADC externo sin tocar el resto | Módulo |
| 3 | Captura continua | `adc_continuous` a una tasa total = `fs × N_canales × OS` (OS = factor de sobremuestreo) y callback de "buffer listo" | DMA + callback |
| 4 | Decimador | Separa canales, promedia OS muestras y entrega `uint16` a fs por canal | Tarea de muestreo (**core 1**, prioridad alta) |
| 5 | Timestamp | Al `START` guarda `t0 = esp_timer_get_time()`; cada muestra recibe `t0 + n/fs` y se compara periódicamente con el reloj para detectar deriva | Tarea de muestreo |
| 6 | Cola / buffer circular | Desacopla muestreo y red; cuenta overruns | FreeRTOS |
| 7 | Empaquetador | Agrupa N muestras (ej. 10 = 5 ms) en una trama con encabezado y CRC16 | Tarea de red |
| 8 | Transmisión UDP | Envía tramas; maneja reconexión del Wi-Fi | **Core 0** |
| 9 | Canal de control TCP | `HELLO`, `PING`, `CONFIG`, `ARM`, `START`, `STOP`, `STATUS`, `SYNC_PULSE` | Tarea comandos |
| 10 | Sincronización | Responde `PING` con el reloj local; genera pulso de trigger hacia el BTS (opcional) | Tarea comandos |
| 11 | Monitoreo | Batería, LED de estado, RSSI, overruns, paquetes enviados | Tarea baja prioridad |
| 12 | Autodiagnóstico | Al armar: nivel de reposo, saturación, ruido RMS por canal | `ARM` |

**Reglas de diseño**
- En la ruta de muestreo (callback del ADC y decimador) **no** se imprime por Serial ni se hace red.
- El tamaño del buffer DMA y de la cola debe tolerar picos de latencia del Wi-Fi sin perder muestras.

**Esqueleto de la tarea de muestreo**

```cpp
// firmware/emg_node/src/sampler.cpp
void samplerTask(void*) {
  uint16_t block[BLOCK_SAMPLES * N_CH];
  uint32_t n = 0;
  for (;;) {
    // 1) obtener del ADC (modo continuo/DMA) un bloque ya decimado a fs
    size_t got = adc->readDecimated(block, BLOCK_SAMPLES);   // abstracción AdcSource
    // 2) timestamp = t0 + n / fs
    for (size_t i = 0; i < got; ++i) {
      Sample s;
      s.seq = n++;
      s.t_us = t0_us + (int64_t)s.seq * 1000000LL / FS;
      s.ch[0] = block[i * N_CH + 0];
      s.ch[1] = block[i * N_CH + 1];
      if (xQueueSend(sampleQ, &s, 0) != pdTRUE) overruns++;   // nunca bloquear
    }
  }
}
```

---

### FASE 4. Comunicación y protocolo de datos

**Canales:** UDP para datos (flujo continuo) y TCP para comandos.

**Trama de datos (binaria, little-endian)**

```
Offset  Tamaño  Campo
0       2       magic (0xE5 0x6D)
2       1       versión de protocolo
3       1       device_id (1 = EMG)
4       4       seq (número de paquete)
8       8       t_us (timestamp de la primera muestra, reloj del dispositivo)
16      2       n_samples
18      1       n_channels (2)
19      1       flags (saturación, overrun, etc.)
20      N       datos: n_samples × n_channels × uint16
20+N    2       CRC16
```

**Comandos de control (TCP)**

| Comando | Respuesta | Uso |
|---|---|---|
| `HELLO` | id, versión, fs, canales, firmware | Descubrimiento |
| `PING <t0>` | `PONG <t0> <t_dev_us>` | Sincronización de reloj |
| `CONFIG {fs, os, canales}` | `OK` | Configuración antes de armar |
| `ARM` | `OK` + diagnóstico (ruido, saturación) | Prepara buffers y ADC |
| `START` / `STOP` | `OK` | Inicia/detiene el flujo (en `START` el nodo toma la IP del cliente como destino UDP) |
| `SYNC_PULSE` | `OK` | Pulso de sincronía por GPIO (opcional, hacia el BTS) |
| `STATUS` | batería, paquetes, overruns, RSSI | Monitoreo |

**Detección de pérdidas:** el contador `seq` permite medir paquetes perdidos; el CRC descarta paquetes corruptos. Ambos se guardan en `session.json`.

---

### FASE 5. Launcher: inicialización y sincronización

**Lenguaje:** Python 3.11+ (`asyncio`). Interfaz: línea de comandos.

```
python -m emgthermo.launcher.run_session --subject S01 --protocol config/protocol_squat.yaml
```

**Dispositivos y modo de control**

| Dispositivo | Control desde el launcher |
|---|---|
| Nodo EMG | **Automático** (TCP + UDP) |
| BTS | **Asistido**: prompts, registro de hora PC de inicio/fin, verificación de archivos exportados; opcional pulso de trigger |
| Cámara Fotric | **Asistido (manual)**: prompts por captura, registro de hora y verificación de archivos |

> Si el BTS o el H6 ofrecieran API/SDK, se añade un modo `sdk` en `bts_device.py` o `thermal_camera.py` sin cambiar el resto del launcher. Aún no confirmado.

#### 5.1 Máquina de estados

```
INIT → DISCOVER → CHECK → SYNC → ARM → THERMAL_PRE → BTS_START → RUN → (THERMAL_DURING ×2) → STOP → THERMAL_POST → SAVE → END
```

| Estado | Qué hace |
|---|---|
| INIT | Carga YAML, crea la carpeta de sesión, abre el log |
| DISCOVER | Conecta en paralelo (`asyncio.gather`) al nodo EMG; prepara los drivers de BTS y cámara |
| CHECK | EMG: versión, batería, ruido en reposo, saturación. BTS/cámara: confirmación del operador (batería, memoria, parámetros configurados) |
| SYNC | Estima el *offset* de reloj del EMG con 30–50 intercambios `PING/PONG` |
| ARM | `CONFIG` y `ARM` al nodo; abre el recorder; abre monitor opcional |
| THERMAL_PRE | "Capturar termograma ANTES"; confirmación con tecla y registro de hora |
| BTS_START | "Iniciar captura BTS"; confirmación y registro de hora |
| RUN | `START` al EMG; guía el **evento de sincronía inicial**; marca series con teclas; en las pausas pide los termogramas DURANTE (T1, T2); guía el **evento de sincronía final** |
| STOP | `STOP` al EMG; "detener captura BTS" con registro de hora |
| THERMAL_POST | Temporizador guiado para capturas a 0, 5 y 10 min |
| SAVE | Escribe `session.json`, `events.csv`, `thermal/index.csv` y estadísticas de pérdida |

> Si el EMG falla en CHECK no se pasa a RUN. Si se desconecta durante RUN, se marca en el log y se continúa registrando eventos.

#### 5.2 Sincronización de relojes

**EMG ↔ PC:** estimación de *offset* con `PING/PONG`.

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
```

**BTS ↔ EMG:** sincronía por **evento** (y opcionalmente por trigger) y alineación por correlación cruzada (`processing/align.py`). Se corrige desfase y deriva con los eventos de inicio y fin.

**Cámara:** no se sincroniza por red; cada captura queda registrada con la hora PC (tecla del operador) y se compara con la hora interna de los archivos exportados.

#### 5.3 Esqueleto del launcher

```python
# host/emgthermo/launcher/run_session.py
import asyncio
from emgthermo.core.config import load_config
from emgthermo.core.session import Session
from emgthermo.devices import EmgNode, BtsDevice, ThermalCamera

async def main(args):
    cfg = load_config(args.protocol)
    session = Session.create(subject=args.subject, cfg=cfg)

    emg, bts, cam = EmgNode(cfg.emg), BtsDevice(cfg.bts), ThermalCamera(cfg.camera)
    await asyncio.gather(emg.connect(), bts.connect(), cam.connect())
    await asyncio.gather(emg.check(), bts.check(), cam.check())

    await emg.sync_clock()
    await emg.arm()

    await cam.prompt_capture("PRE", session)               # termograma ANTES
    await bts.prompt_start(session)                         # iniciar captura BTS
    await emg.start(); session.recorder.start()
    await session.run_protocol(cam, bts, emg)               # sync inicial, series, DURANTE, sync final
    await emg.stop()
    await bts.prompt_stop(session)
    await cam.prompt_post_sequence(session, times_min=[0, 5, 10])   # DESPUÉS
    session.save()

if __name__ == "__main__":
    asyncio.run(main(parse_args()))
```

---

### FASE 6. Recepción, registro y visualización en tiempo real

**Recepción (`recorder.py`)**
- Socket UDP asíncrono; valida magic, versión y CRC.
- Detecta huecos en `seq` y los registra.
- Convierte `t_dev` a tiempo PC con el *offset*.
- Guarda **siempre cuentas crudas** y metadatos de conversión; el filtrado es offline.

**Visualización en tiempo real (`live_plot.py`)**
- Ventana mínima con `pyqtgraph`, 20–30 FPS, ventana deslizante de 3–5 s.
- Paneles: EMG cruda de VL y BF, **envolvente en línea** (rectificación + RMS causal) y indicadores de calidad (RMS en reposo, saturación, paquetes perdidos, batería, RSSI).
- El ángulo del BTS **no** está disponible en tiempo real en esta ventana (solo se integra offline), salvo que el BTS ofrezca un flujo de datos. Para guiar la cadencia se puede usar el metrónomo.
- Filtros en tiempo real **causales** (`sosfilt`); el análisis final usa fase cero (`sosfiltfilt`).

---

### FASE 7. Procesamiento de la señal EMG y normalización

**Pipeline**

```
Cuentas → µV (calibración) → quitar DC → pasa-banda 20–450 Hz (Butterworth 4º, fase cero)
→ notch 60 Hz → control de calidad → rectificación → envolvente RMS (100–250 ms)
→ normalización por pico de la sentadilla → segmentación por repetición (ángulo BTS alineado)
→ características
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

#### 7.1 Normalización por pico de la sentadilla

**Procedimiento (por sujeto y por músculo)**
1. Calcular la envolvente suavizada (RMS 150–250 ms) del registro completo.
2. Obtener el **pico de cada repetición** válida.
3. Definir el **valor de referencia** como el **promedio de los picos de las repeticiones válidas** (o de las 3 más altas; se elige un criterio y se aplica igual en todos los sujetos).
4. `EMG_norm (%pico) = envolvente / referencia × 100`.

**Recomendaciones**
- Excluir repeticiones con artefacto antes de calcular la referencia, para que un pico espurio no distorsione todo.
- Calcular la referencia sobre **todas las series** (no solo la serie 1) y reportarlo; si la fatiga reduce la amplitud en las últimas series, se vería como valores < 100 %.
- Guardar también el **RMS en µV sin normalizar**.

**Limitaciones que hay que declarar**
- La normalización por pico **no permite comparar amplitud absoluta entre sujetos** ni entre músculos (cada músculo vale 100 % de sí mismo).
- La razón VL/BF en %pico no equivale a una razón de fuerzas o de actividad relativa real.
- Como la normalización elimina la ganancia del equipo, una calibración absoluta es menos crítica, pero sigue siendo necesaria para reportar µV y SNR.
- Para el análisis EMG–temperatura, se recomienda usar **iEMG normalizado** (para comparar series) y mantener el iEMG en µV·s para comparar músculos dentro del mismo sujeto (con la reserva de la impedancia de cada electrodo).

#### 7.2 Segmentación y características

- Con el ángulo del BTS ya alineado, `find_peaks` detecta la flexión máxima de cada repetición; se separan **descenso** y **ascenso**.
- Se remuestrea a 0–100 % del ciclo y por rangos angulares (bins de 10°).
- **Tiempo:** RMS, MAV, **iEMG**, pico de envolvente, tiempo al pico, inicio de activación (reposo + 3 SD).
- **Frecuencia:** MNF y MDF por ventanas (Welch); una caída indica fatiga.
- **Coordinación (2 canales):** razón VL/BF e índice de co-contracción `CCI = 2·∫min(VL,BF) / ∫(VL+BF)` (con envolventes normalizadas, interpretarlo con la reserva de la sección 7.1).
- **Por serie:** iEMG acumulado en cada serie (para relacionarlo con T1, T2 y T3).
- **Calidad:** SNR (contracción/reposo), saturación, pico de 60 Hz en el espectro, repeticiones descartadas.

---

### FASE 8. Termografía Fotric (ANTES, DURANTE, DESPUÉS)

#### 8.1 Los tres momentos

| Momento | Capturas | Objetivo |
|---|---|---|
| **ANTES** | T0: vistas anterior, lateral y posterior, ambas piernas; sin electrodos ni gel | Línea base |
| **DURANTE** | T1 al terminar la serie 1; T2 al terminar la serie 2 (≤ 30 s de pausa, sujeto de pie, misma posición) | Evolución térmica con el trabajo acumulado |
| **DESPUÉS** | T3a: 0 min (< 1–2 min tras la serie 3), T3b: 5 min, T3c: 10 min | Respuesta máxima y recuperación |

**Notas sobre "DURANTE"**
- Durante el movimiento el sujeto sube y baja en el encuadre y los electrodos y sensores BTS cubren zonas, por lo que la medición confiable es en **pausas**.
- Si el H6 graba video o secuencias con intervalo (confirmar en el manual), se puede registrar toda la serie y usar *Temperature plot* para ver T vs tiempo de un ROI (revisando los ROI cuadro por cuadro por el movimiento).
- Tener en cuenta que los **sensores/bandas del BTS** pueden cubrir parte del muslo o la rodilla: definir los ROI en zonas libres y de forma consistente en T0 a T3.

#### 8.2 Funciones de Fotric AnalyzIR y cómo se usan

| Función del manual | Uso en el proyecto |
|---|---|
| **Temperature plot** | Perfil de temperatura vs. tiempo de un ROI (máx., mín. y fórmulas propias, por ejemplo promedio del ROI menos el control) a través de ANTES → DURANTE → DESPUÉS, si hay video/secuencia |
| **Histogram** | Distribución de temperaturas por ROI: comparar ANTES vs DESPUÉS (desplazamiento y ensanchamiento) |
| **3D Graph** | Superficie térmica del muslo/rodilla para figuras y localización de zonas de mayor y menor temperatura; es cualitativo |

#### 8.3 Exportaciones y cuál usar

| Exportación (manual) | Contiene temperaturas | Uso |
|---|---|---|
| **Single Frame Image** | Sí (re-analizable) | **Archivo maestro** de cada captura: conservar siempre |
| **Single Frame Data (.csv)** | Sí (matriz del cuadro) | **Entrada para el análisis en Python** |
| **BPM Non-Radiometric** | **No** | Solo imágenes ilustrativas; **no sirve para análisis** |
| **Report** | Según plantilla | Reporte rápido por captura; complemento del reporte de la materia |

Por cada captura exportar **Single Frame Image + Single Frame Data (.csv)** con el mismo nombre base.

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
3. Leer T media, máx. y mín.; usar *Histogram* y *3D Graph*; anotar zonas de mayor y menor temperatura.

**Camino B: en Python (reproducible)**
- Cargar el `.csv` de Single Frame Data con lectura tolerante (detectar encabezados, separador y decimales; validar dimensiones y rango plausible de temperatura).
- ROIs como polígonos en `thermal_rois.json`, con marcas anatómicas de referencia; alinear imágenes (traslación) antes de calcular ΔT por píxel.

```python
# host/emgthermo/processing/thermal_roi.py
import numpy as np

def roi_stats(T, mask):
    v = T[mask]
    return {"mean": v.mean(), "max": v.max(), "min": v.min(), "std": v.std()}

def delta_roi(T_pre, T_post, mask):
    return T_post[mask].mean() - T_pre[mask].mean()
```

**Métricas:** T media, máx., mín., desviación estándar, ΔT vs ANTES, ΔT vs control y asimetría entre piernas.

**Interpretación:** los cambios de temperatura de piel en ejercicio corto son pequeños (décimas de °C) y pueden ser negativos al inicio por sudor/vasoconstricción. Se interpretan **ΔT y diferencias contra el control**, declarando la incertidumbre de la cámara y del ROI.

---

### FASE 9. Integración EMG + ángulo (BTS) + termografía

EMG y ángulo son continuos (ms); la termografía es discreta (T0, T1, T2, T3). La integración se hace **por serie y por zona**.

**Tabla maestra (una fila por sujeto, músculo/ROI y momento)**

| Sujeto | ROI/Músculo | Momento | iEMG acumulado (µV·s) | iEMG acumulado (%pico·s) | RMS (%pico) | MDF | T media | T máx | T mín | ΔT vs ANTES | ΔT vs control |
|---|---|---|---|---|---|---|---|---|---|---|---|

**Análisis propuestos**
1. **EMG vs ángulo:** envolvente VL y BF vs ángulo y vs % del ciclo; rango angular de máxima activación; co-contracción por rango.
2. **EMG vs temperatura (dosis-respuesta):** iEMG acumulado hasta la serie k vs T del ROI en T0, T1, T2, T3a.
3. **Rankings:** orden de músculos por iEMG vs orden de ROIs por ΔT; ¿las zonas de mayor/menor temperatura coinciden con mayor/menor activación?
4. **Correlación:** Spearman (preferible con pocos datos); reportar con cautela por el tamaño de muestra.
5. **Fatiga vs temperatura:** caída de MDF por serie vs ΔT.
6. **Asimetría:** pierna trabajada vs control.
7. **Recuperación:** ΔT en T3a, T3b y T3c.

**Figuras sugeridas:** termogramas ANTES/DURANTE/DESPUÉS con ROIs; histogramas; gráfica combinada ángulo + envolventes; barras iEMG vs ΔT; dispersión iEMG vs ΔT; curva T(t) con iEMG acumulado superpuesto.

**Limitaciones a declarar:** muestra pequeña; la temperatura cutánea no es la del músculo; crosstalk del EMG; variabilidad de electrodos y ROI; efecto del gel y de los sensores BTS sobre la piel; normalización por pico (sección 7.1); sincronía EMG–BTS por evento (precisión limitada por el evento y la frecuencia del BTS).

---

### FASE 10. Validación del instrumento

| Prueba | Método | Referencia |
|---|---|---|
| **Ruido del ADC interno** | Entradas en corto, Wi-Fi apagado vs. transmitiendo | Sin aumento notable con Wi-Fi; pocos µV RMS referidos a la entrada |
| Linealidad del ADC | Escalones DC / rampa | Sin zonas no lineales en el rango de trabajo |
| Ganancia y respuesta en frecuencia | Senoidal conocida, barrido 5–1000 Hz | Error < ~5 %; banda 20–450 Hz |
| CMRR | Misma señal en ambas entradas | > 80 dB deseable |
| Estabilidad de fs y pérdida de paquetes | Contador `seq`, jitter de `t_us`, sesión larga de 10 min | 0 pérdidas ideales |
| Desfase CH1–CH2 | Misma señal a ambos canales | Despreciable frente al EMG |
| **Sincronía EMG–BTS** | Eventos de inicio y fin; correlación cruzada | Desfase estable; deriva pequeña entre los dos eventos |
| Ángulo BTS | Comparar con goniómetro manual en 0°, 45°, 90°, 120° | Error de pocos grados |
| Prueba fisiológica | Contracción vs reposo; sentadilla piloto | SNR claro y activación coherente con el ángulo |

> **Criterio de decisión:** si el ruido con Wi-Fi del ADC interno hace inutilizable la señal tras aplicar las mitigaciones de la Fase 1, activar el plan B (ADC externo) sin cambiar el resto del software.

---

### FASE 11. Resultados y entregables

1. Diagrama a bloques, esquemáticos y BOM.
2. Firmware del nodo EMG, launcher y scripts de procesamiento (repositorio).
3. Datos crudos por sesión (EMG, exportaciones BTS, termografía) con metadatos.
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
│   └── emg_node/
│       ├── platformio.ini
│       ├── include/
│       │   ├── config.h
│       │   ├── pins.h
│       │   └── protocol.h
│       └── src/
│           ├── main.cpp
│           ├── adc_source.h
│           ├── adc_continuous_s3.cpp / .h
│           ├── sampler.cpp / .h
│           ├── packetizer.cpp / .h
│           ├── net_wifi.cpp / .h
│           ├── command_handler.cpp / .h
│           ├── timesync.cpp / .h
│           └── monitor.cpp / .h
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
│       │   ├── bts_device.py
│       │   └── thermal_camera.py
│       ├── realtime/
│       │   ├── live_plot.py
│       │   └── causal_filters.py
│       ├── processing/
│       │   ├── emg_filters.py
│       │   ├── emg_features.py
│       │   ├── normalization.py
│       │   ├── segmentation.py
│       │   ├── bts_loader.py
│       │   ├── align.py
│       │   ├── thermal_loader.py
│       │   ├── thermal_roi.py
│       │   ├── fusion.py
│       │   └── stats.py
│       └── tools/
│           ├── simulate_device.py
│           └── validate_hardware.py
├── analysis/
│   ├── 01_qc_emg.py
│   ├── 02_emg_features.py
│   ├── 03_align_bts_emg.py
│   ├── 04_thermal_rois.py
│   ├── 05_fusion_report.py
│   └── notebooks/ (opcional)
├── data/
│   └── S01_YYYYMMDD/
│       ├── session.json
│       ├── events.csv
│       ├── emg_raw.csv
│       ├── bts/
│       │   └── raw/ (exportación del BTS)
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
│   ├── test_align.py
│   └── test_thermal_loader.py
└── docs/
    ├── protocolo_experimental.md
    └── consentimiento_informado.md
```

### 4.1 Descripción de cada archivo

#### Raíz y configuración

| Archivo | Lenguaje | Contenido |
|---|---|---|
| `README.md` | Markdown | Descripción, instalación, uso del launcher y carga del firmware |
| `requirements.txt` / `pyproject.toml` | Texto / TOML | `numpy`, `scipy`, `pandas`, `pyqtgraph`, `PyQt6`, `pyyaml`, `matplotlib`, `pytest` (más `zeroconf` si se usa mDNS) |
| `config/devices.yaml` | YAML | Host/IP y puertos del nodo EMG, fs, canales, factor de sobremuestreo, factores de conversión a µV, rutas de exportación del BTS, modo de BTS y de cámara (`manual`/`sdk`) |
| `config/protocol_squat.yaml` | YAML | Series, repeticiones, cadencia, profundidad, eventos de sincronía, momentos térmicos (T0, T1, T2, T3a/b/c), vistas, tiempos de espera |
| `config/thermal_rois.json` | JSON | Polígonos de ROI por vista (VL, BF, rodilla, control) |

#### Firmware del nodo EMG (ESP32-S3)

| Archivo | Lenguaje | Contenido |
|---|---|---|
| `platformio.ini` | INI | Placa ESP32-S3, framework, flags, librerías |
| `include/config.h` | C++ | fs, factor OS, tamaño de bloque, SSID/clave del hotspot (idealmente en archivo no versionado), puertos, prioridades |
| `include/pins.h` | C++ | Pines de ADC1 (2 canales), LED, lectura de batería, GPIO de trigger (opcional) |
| `include/protocol.h` | C++ | Estructura de trama, magic/versión, comandos, CRC16 (espejado en `core/protocol.py`) |
| `src/main.cpp` | C++ | Inicia módulos y crea tareas FreeRTOS |
| `src/adc_source.h` | C++ | Interfaz `AdcSource` (permite cambiar a un ADC externo) |
| `src/adc_continuous_s3.cpp/.h` | C++ | Implementación con ADC1 en modo continuo (DMA), separación de canales, sobremuestreo y decimación |
| `src/sampler.cpp/.h` | C++ | Tarea de muestreo, timestamps, cola, conteo de overruns |
| `src/packetizer.cpp/.h` | C++ | Agrupa muestras y arma la trama con CRC |
| `src/net_wifi.cpp/.h` | C++ | Conexión y reconexión al hotspot, socket UDP, servidor TCP, mDNS |
| `src/command_handler.cpp/.h` | C++ | Interpreta `HELLO/PING/CONFIG/ARM/START/STOP/STATUS/SYNC_PULSE` y gestiona estados |
| `src/timesync.cpp/.h` | C++ | Respuesta a `PING`, pulso de trigger por GPIO |
| `src/monitor.cpp/.h` | C++ | Batería, LED, RSSI, estadísticas |

#### Hardware

| Archivo | Formato | Contenido |
|---|---|---|
| `hardware/schematics/` | KiCad/PDF | Front-end, filtros, bias, alimentación y conexión al ADC1 |
| `hardware/bom.csv` | CSV | Lista de componentes |
| `hardware/notes_calibration.md` | Markdown | Calibración de ganancia (µV/cuenta) y resultados de ruido con y sin Wi-Fi |

#### Software en la PC (`host/emgthermo/`, Python)

| Archivo | Contenido |
|---|---|
| `launcher/run_session.py` | Punto de entrada: orquesta conexión, chequeos, sincronía, arranque, protocolo, paro y guardado |
| `launcher/state_machine.py` | Estados y transiciones, manejo de errores |
| `launcher/cli.py` | Argumentos y mensajes al operador (prompts de BTS y cámara con temporizadores) |
| `core/config.py` | Carga y validación de YAML |
| `core/protocol.py` | Parseo de tramas con `struct`, CRC y comandos |
| `core/clock_sync.py` | *Offset* y RTT del nodo EMG; conversión de tiempos |
| `core/recorder.py` | Recepción UDP asíncrona, detección de pérdidas, escritura a disco |
| `core/session.py` | Carpeta de sesión, `session.json`, índices de eventos y de capturas |
| `core/events.py` | Marcas: inicio/fin de serie, eventos de sincronía, capturas térmicas, inicio/fin BTS |
| `devices/base.py` | Clase base `Device` (`connect/check/sync_clock/arm/start/stop`) |
| `devices/emg_node.py` | Driver del nodo EMG (TCP + UDP) |
| `devices/bts_device.py` | Modo asistido: prompts de inicio/fin, hora PC, verificación de archivos exportados; trigger opcional; modo `sdk` futuro |
| `devices/thermal_camera.py` | Modo asistido: prompts por captura, hora, verificación de archivos; modo `sdk` futuro |
| `realtime/live_plot.py` | Ventana `pyqtgraph` con EMG, envolvente y calidad |
| `realtime/causal_filters.py` | Filtros `sosfilt` con estado |
| `processing/emg_filters.py` | Pasa-banda, notch, rectificación, envolvente |
| `processing/normalization.py` | Pico de referencia por músculo (criterio fijo), %pico, exclusión de repeticiones espurias |
| `processing/emg_features.py` | RMS, MAV, iEMG, MNF/MDF, CCI, inicio de activación |
| `processing/segmentation.py` | Repeticiones y fases a partir del ángulo; remuestreo por ciclo y por rango angular |
| `processing/bts_loader.py` | Lectura del archivo exportado del BTS (formato por confirmar), conversión de convención y unidades |
| `processing/align.py` | Alineación EMG–BTS por correlación cruzada con eventos de inicio y fin; corrección de desfase y deriva |
| `processing/thermal_loader.py` | Lectura robusta de los CSV de Fotric y de `index.csv` |
| `processing/thermal_roi.py` | Máscaras, estadísticos por ROI, ΔT, alineación simple |
| `processing/fusion.py` | Une EMG por serie con temperaturas por momento; tabla maestra |
| `processing/stats.py` | Spearman/Pearson, intervalos |
| `tools/simulate_device.py` | Simulador de nodo EMG (señal sintética) y de archivo BTS para probar el launcher **sin hardware** |
| `tools/validate_hardware.py` | Pruebas de ruido, ganancia y pérdida de paquetes (Fase 10) |

#### Análisis (`analysis/`, Python)

| Script | Contenido |
|---|---|
| `01_qc_emg.py` | Calidad, espectros, repeticiones descartadas |
| `02_emg_features.py` | Filtrado, normalización y características por repetición/serie |
| `03_align_bts_emg.py` | Alineación y verificación gráfica de la sincronía |
| `04_thermal_rois.py` | Estadísticos por ROI y momento, histogramas |
| `05_fusion_report.py` | Tabla maestra, correlaciones y figuras finales |

#### Pruebas (`tests/`, `pytest`)
`test_protocol.py` (tramas, CRC, pérdida de `seq`), `test_clock_sync.py` (offset conocido), `test_filters.py` (60 Hz eliminado, 100 Hz conservado), `test_align.py` (desfase sintético recuperado), `test_thermal_loader.py` (CSV de ejemplo).

### 4.2 Formatos de datos

- **`emg_raw.csv`**: `t_pc_s, seq, ch1_counts, ch2_counts, flags`
- **`bts/raw/`**: archivo(s) exportado(s) por el software BTS, sin modificar
- **`processed/angle_aligned.csv`**: `t_emg_s, angle_deg` (ángulo del BTS alineado y remuestreado al tiempo del EMG)
- **`events.csv`**: `t_pc_s, evento, detalle` (START, BTS_INI, SYNC_INI, SERIE_1_INI, CAPTURA_T1, …, SYNC_FIN, BTS_FIN)
- **`thermal/index.csv`**: `archivo, momento, serie, vista, t_pc_s, emisividad, T_amb, T_refl, distancia, humedad`
- **`session.json`**: sujeto, fecha, firmware, fs, OS, ganancia y calibración, *offset* de reloj, pérdidas de paquetes, modelo/configuración del BTS, ubicación de electrodos, condiciones ambientales

### 4.3 Resumen de lenguajes

| Parte | Lenguaje |
|---|---|
| Firmware ESP32-S3 | C++ (PlatformIO + Arduino-ESP32/ESP-IDF, FreeRTOS) |
| Launcher, comunicación, grabación | Python (`asyncio`, `socket`, `struct`) |
| Tiempo real | Python (`pyqtgraph`, `numpy`, `scipy`) |
| Procesamiento y análisis | Python (`numpy`, `scipy`, `pandas`, `matplotlib`) |
| Configuración | YAML / JSON |
| BTS | Software del fabricante (adquisición/exportación) + Python para los datos |
| Termografía | Fotric AnalyzIR (GUI) + Python para los CSV |

---

## 5. Checklist por sesión

**Antes**
- [ ] Batería del nodo EMG y de la cámara cargadas; BTS listo con su software
- [ ] Hotspot en **2.4 GHz** activo; nodo EMG conectado
- [ ] Sujeto aclimatado y piel preparada
- [ ] Cámara configurada y en marcas; ROIs previstos en zonas libres de sensores
- [ ] Launcher en CHECK sin errores (ruido en reposo y saturación aceptables)

**Durante**
- [ ] T0 (ANTES) antes de poner electrodos y sensores
- [ ] Captura BTS iniciada; evento de sincronía inicial
- [ ] Series con metrónomo; T1 y T2 en las pausas
- [ ] Evento de sincronía final; paro de EMG y BTS
- [ ] Monitoreo sin saturación ni pérdida de paquetes

**Después**
- [ ] T3a (0 min), T3b (5 min), T3c (10 min)
- [ ] Exportar del BTS y de Fotric (Single Frame Image + Single Frame Data .csv)
- [ ] Respaldar la carpeta de sesión

---

## 6. Cronograma sugerido

| Semana | Actividad |
|---|---|
| 1 | Protocolo, selección de componentes, simulación del front-end; **confirmar modelo/exportación del BTS** |
| 2 | Front-end analógico y **pruebas go/no-go del ADC interno** (ruido con y sin Wi-Fi) |
| 3 | Firmware del nodo EMG (ADC continuo + Wi-Fi); `simulate_device.py` y recepción en PC |
| 4 | Launcher, sincronía y `bts_loader.py` / `align.py` con datos de prueba del BTS |
| 5 | Validación (Fase 10) y sesión piloto |
| 6 | Sesiones experimentales con termografía |
| 7 | Procesamiento, integración, reporte y presentación |

---

## 7. Decisiones pendientes

1. **Modelo y software del BTS**, formato de exportación, frecuencia de muestreo y si entrega el ángulo de rodilla directamente.
2. ¿El BTS tiene **entrada/salida de trigger** para sincronía por hardware?
3. ¿Dónde se coloca el BTS en la pierna y cómo se evita que tape los ROI térmicos y los electrodos?
4. ¿El H6 graba video/secuencia con intervalo? ¿Tiene SDK o control remoto?
5. Criterio exacto del pico de normalización (promedio de picos o las 3 más altas) y si se calcula sobre todas las series.
6. Número y perfil de sujetos.
7. Pines disponibles del SuperMini para 2 canales de ADC1.
8. Resultado de las pruebas de ruido del ADC interno (si es no-go, pasar al ADC externo).