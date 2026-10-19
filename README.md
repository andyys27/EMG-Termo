# emg-thermo-squat

Adquisición de **EMG de 2 canales** (vasto lateral y bíceps femoral) durante una sentadilla, sincronizada con el **ángulo de rodilla del sistema de captura de movimiento Vicon (cámaras IR Bonita)** y con **termografía Fotric H6** en tres momentos (ANTES, DURANTE, DESPUÉS).

Proyecto de Bioinstrumentación. No incluye aplicación gráfica: solo firmware, un launcher de línea de comandos, monitoreo mínimo en tiempo real y scripts de análisis.

## Arquitectura

```
PC (Python, launcher) ──Wi-Fi 2.4 GHz (hotspot)──▶ Nodo EMG: ESP32-S3 SuperMini + ADS131M04
        │                                            (UDP datos + TCP control)
        ├── Vicon Nexus (cámaras Bonita): ángulo de rodilla (marcadores 3D)
        │     inicio/paro por UDP (opcional, formato por verificar) o manual; exportación CSV
        └── Fotric H6 + AnalyzIR: termogramas (modo asistido/manual)
```

| Dispositivo | Control desde el launcher |
|---|---|
| Nodo EMG | Automático (TCP + UDP) |
| Vicon (Bonita) | Manual asistido, o automático por UDP si Nexus tiene habilitado "Start/Stop over network" |
| Fotric H6 | Manual asistido (prompts, hora de cada captura, verificación de archivos) |

## Decisiones del proyecto

| Tema | Decisión |
|---|---|
| Canales EMG | 2 (VL, BF) |
| MCU | ESP32-S3 SuperMini |
| ADC | **ADS131M04** (24 bit, 4 canales simultáneos, SPI); se usan 2 canales |
| Transmisión | Wi-Fi por hotspot (2.4 GHz) |
| Ángulo de rodilla | Vicon (Bonita) vía Nexus |
| Normalización EMG | **Promedio de los picos** de las repeticiones válidas |
| Termografía | Fotric H6; exportar *Single Frame Image* + *Single Frame Data (.csv)* |
| Sujetos | Por definir |

## Estructura

```
config/      Configuración (YAML/JSON): dispositivos, protocolo, ROIs térmicos
firmware/    Firmware del nodo EMG (C++, PlatformIO)
host/        Paquete Python emgthermo (launcher, drivers, procesamiento)  [pendiente]
data/        Datos por sesión (no se versiona)
results/     Figuras y tablas
```

## Puesta en marcha

### 1. Firmware (bring-up del ADC)
Ver `firmware/emg_node/README.md` (cableado ADS131M04 ↔ ESP32-S3 y pruebas de ruido).

```
cd firmware/emg_node
cp include/secrets.h.example include/secrets.h     # SSID/clave del hotspot (se usa en la fase de red)
pio run -t upload
pio device monitor
```

### 2. Entorno Python

```
python -m venv .venv
.venv\Scripts\activate        # Windows   (Linux/macOS: source .venv/bin/activate)
pip install -r requirements.txt
```

### 3. Launcher (cuando esté el paquete `host/`)

```
python -m emgthermo.launcher.run_session --subject S01 --protocol config/protocol_squat.yaml
```

## Estado

| Componente | Estado |
|---|---|
| Archivos de raíz y `config/` | Listos (valores marcados `PENDIENTE`/`TODO` por definir) |
| Firmware: protocolo v2, configuración, pines, definiciones del ADS131M04 | Escritos y probados en PC |
| Firmware: driver ADS131M04, muestreo, bring-up por USB | Escritos; sintaxis revisada con stubs; **sin compilar ni probar en hardware** |
| Firmware: Wi-Fi, comandos TCP, sincronía, monitoreo | Pendiente |
| `host/emgthermo` (launcher, recorder, drivers, procesamiento) | Pendiente |

## Notas importantes
- El ESP32-S3 solo usa **2.4 GHz**; configurar el hotspot en esa banda.
- Nada conectado al sujeto debe alimentarse de la red eléctrica; el nodo EMG va con batería.
- `firmware/emg_node/include/secrets.h` no se versiona.