# Plan de trabajo v5.1: sistema EMG + video + termografía (completo)

Nov 19, 2026 · @Andres

## 1. Contexto y alcance

Este documento es el plan completo para construir y validar un sistema que mide, durante una sentadilla con peso corporal, la actividad eléctrica del vasto lateral (VL) y del bíceps femoral (BF), el ángulo de rodilla y la temperatura de la piel del muslo. Se entiende sin consultar otros documentos. La fecha límite es el **2 de diciembre de 2026**, fin del periodo de cuatro semanas que fija el protocolo (inició el 9 de noviembre); confirmarla con el docente.

| Elemento | Qué dice el protocolo BI2005-EMG-Equipo 4 (versión del 12 de noviembre de 2026) |
| --- | --- |
| Título | Patrones temporales de activación del vasto lateral y del bíceps femoral y su relación con las fases del movimiento de la rodilla y la temperatura local de la piel durante una sentadilla con peso corporal |
| Tipo de estudio | Piloto académico original, sin cálculo de potencia estadística |
| Objetivo | Comparar amplitud RMS y latencia EMG de VL y BF en tres fases de una sentadilla parcial (inicio, pico de contracción y recuperación), con ventanas de 500 ms, y relacionarlas con el movimiento de la rodilla y con la temperatura antes, durante y después del ejercicio |
| Hipótesis | El RMS del VL será mayor en la fase de pico que en inicio, recuperación y reposo; el BF mostrará menor activación (co-contracción); tras el ejercicio el RMS vuelve hacia el reposo y la temperatura de la piel difiere de la basal |
| Muestra | 1 a 2 voluntarios sanos y adultos, integrantes del equipo; 20 repeticiones por sujeto; estadística descriptiva dentro de cada sujeto |
| Ejercicio | Sentadilla parcial a 90° de flexión de rodilla (verificada con goniómetro), metrónomo a 60 bpm, 2 series de 10 repeticiones |
| Duración del registro | Aproximadamente 10 minutos por sujeto |
| Objetivos específicos | Medir el tiempo de preparación; registrar sEMG en reposo, series y recuperación; procesar offline; extraer amplitud máxima y media, frecuencia mediana (MDF) y latencia de inicio; registrar imágenes térmicas anterior y posterior del muslo antes, durante y después |

**Qué se entrega**

1. Un nodo EMG funcional (circuito analógico + ESP32-S3) con calidad de señal demostrada con mediciones.
2. Registros de 1 a 2 sujetos con EMG, video del movimiento y termogramas.
3. Tablas y gráficas: RMS máximo y medio por fase, MDF, latencia de inicio, diagramas de caja y análisis térmico por región de interés.
4. Protocolo e informe con las limitaciones declaradas.

**Glosario mínimo**

| Término | Significado |
| --- | --- |
| sEMG | Electromiografía de superficie: señal eléctrica del músculo medida sobre la piel |
| RMS | Raíz cuadrática media en una ventana de tiempo; estima la amplitud de la señal |
| MDF | Frecuencia mediana del espectro de potencia; baja con la fatiga |
| Latencia | Tiempo entre el inicio del movimiento y el inicio de la activación muscular |
| ADC / DMA | Convertidor analógico-digital / acceso directo a memoria: el ADC mide solo y deja los datos en memoria sin intervención del programa |
| UDP / TCP | Protocolos de red: UDP envía datos rápido sin confirmar; TCP envía comandos con confirmación |
| ROI | Región de interés dentro de una imagen térmica |
| DRL | Circuito de pierna derecha activa: reduce interferencia común devolviéndola al cuerpo por el electrodo de referencia |
| QC | Control de calidad de una sesión (pérdidas de datos, saturación, ruido) |
| fs | Frecuencia de muestreo; en este proyecto 2000 muestras por segundo por canal |

## 2. Estado actual y decisiones vigentes

Al 19 de noviembre el circuito está construido y el software está escrito, pero casi todo se ha probado solo con simulación. Lo que falta es comprobarlo en la realidad: compilar el firmware, medir el circuito, grabar con el nodo real, probar la detección de pose con video real y escribir el análisis térmico.

**Estado por componente**

| Componente | Estado | Qué falta |
| --- | --- | --- |
| Circuito analógico: AD620 (ganancia 100) + ISO124, banda 5–312 Hz, notch de 60 Hz twin-T de 2.º orden, offset de 1.65 V, 4 pilas de 9 V | Construido en PCB; no se puede agregar hardware | Medir ruido, ganancia, notch y rango útil del ADC |
| Firmware ESP32-S3 SuperMini | Escrito; no compilado ni probado en la placa. La lógica del validador pasa su prueba en PC | Compilar, subir, confirmar pines GPIO1 y GPIO2, medir pérdidas |
| Adquisición en Python (recepción, reloj, launcher, QC, monitor en vivo) | Probada con un nodo simulado: 0 % de pérdidas | Probar con el nodo real durante 10 min |
| Procesamiento EMG (filtros, RMS 500 ms, normalización, fases, MDF, latencia, tablas y figuras) | Probado con datos sintéticos | Probar con datos reales |
| Ángulo de rodilla por video (MediaPipe) | Código y cálculo probados con datos sintéticos; la detección sobre video real no se ha podido probar | Grabar un video real, revisar el video de depuración, validar con goniómetro |
| Termografía | Sin código | Modelo de cámara, formato de exportación, lector, ROI y análisis |
| Dashboard público (Supabase + web) | Solo esquema SQL y políticas de seguridad | Opcional, solo si sobra tiempo |
| Pruebas automáticas | 27 pruebas pasan sin hardware | Agregar pruebas del módulo térmico |

**Decisiones vigentes**

| Tema | Decisión | Razón |
| --- | --- | --- |
| Adquisición | 2 canales (VL y BF) con el ADC1 interno de la ESP32-S3 en modo continuo con DMA | La PCB ya está hecha; el DMA da tiempos de muestreo regulares |
| Muestreo | 2000 Hz por canal; el ADC mide a 32 kHz y suma 8 conversiones por muestra | El protocolo exige al menos 1 kHz; con 2 kHz el filtro de 312 Hz tiene mejor margen anti-aliasing |
| Transmisión | Datos por UDP (tramas binarias con CRC16), control por TCP, Wi-Fi 2.4 GHz | UDP no bloquea el muestreo; los huecos se detectan por número de secuencia |
| Firmware | Valida y marca la calidad; no filtra | El análisis queda reproducible y cambiable sin reprogramar |
| Movimiento | Video de celular a 120 fps + MediaPipe; Kinovea y goniómetro solo para validar | Se descartó Vicon |
| Sincronía video–EMG | 3 flexo-extensiones rápidas de rodilla al inicio | No requiere hardware extra |
| Machine learning | No se usa | Con 20 repeticiones por sujeto no hay datos para entrenar; las fases se definen con reglas |
| Termografía | Cámara manejada con su propio software; archivos asociados a la sesión por hora | No se controla por red |
| Procesamiento | Offline en Python: quitar offset, rectificar, RMS en ventanas de 500 ms, envolvente pasa-bajas 2–5 Hz | Lo pide el protocolo |
| Normalización | Promedio de picos de repeticiones válidas (el texto del protocolo dice «RMS máximo»; el software soporta ambos) | Decidido por el equipo; hay que unificar el protocolo |
| Dashboard | Público, solo lectura, solo sesiones publicadas; opcional | No es indispensable para la entrega |

## 3. Arquitectura y flujo de datos

```
 Persona
   | electrodos (VL, BF)                  Celular 120 fps        Fotric H6
   v                                           |                     |
 Circuito: AD620 x100 > ISO124 > 5-312 Hz > notch 60 Hz > offset 1.65 V
   |                                           |                     |
   v                                           v                     v
 ESP32-S3: ADC1 + DMA, 2000 Hz x 2 canales (x8 sobremuestreo)    video.mp4        imágenes/CSV
   valida y marca (SAT, FLAT, OVERRUN...), tramas de 20 muestras       |                     |
   |  UDP: datos (10 ms/trama)     ^  TCP: comandos (HELLO, PING, ARM, START, STOP)
   v                               |                                   |                     |
 PC (Python): lanzador > recorder > emg_raw.csv, frames.csv, events.csv, session.json
   |                                                                   |                     |
   +--------------------- análisis offline <-----------------------------+---------------------+
                  filtros, RMS, fases, normalización, MDF, ángulo (MediaPipe), sincronía, ΔT
                                         |
                              processed/ (tablas y figuras) > informe
                                         |
                          (opcional) Supabase > dashboard web público
```

**Capas y responsabilidades**

| Capa | Responsabilidad | Lo que NO hace |
| --- | --- | --- |
| Circuito | Amplificar, aíslar y acondicionar la señal | No decide nada sobre la calidad |
| Firmware | Muestrear con reloj de hardware, validar localmente, marcar y enviar | No filtra ni interpreta el EMG |
| Python (adquisición) | Recibir, detectar pérdidas, sincronizar relojes, registrar eventos | No altera los datos crudos |
| Python (análisis) | Procesar sin conexión y de forma reproducible | No corre en tiempo real |
| Video y termografía | Aportan ángulo y temperatura | No se conectan al nodo; se alinean por eventos y tiempo de la PC |
| Web (opcional) | Mostrar resultados ya procesados | No procesa datos crudos |

**Por qué UDP para los datos y TCP para el control.** El flujo EMG son 100 tramas por segundo; si una se pierde, es mejor seguir que esperar una retransmisión que cause retrasos, y el número de secuencia permite saber qué faltó. Los comandos, en cambio, deben llegar completos y en orden, por eso van por TCP.

**Por qué el reloj maestro es la PC.** Video, termografía y eventos del operador se registran con la hora de la PC; el nodo mide su propio tiempo y la PC calcula el desfase y la deriva con `PING/PONG`. Así todos los datos se pueden alinear.

**Por qué el análisis es offline.** Con filtros de fase cero y ventanas centradas, el resultado es más fiel y reproducible que en tiempo real; el monitor en vivo solo sirve para comprobar que la señal es buena.

## 4. Estructura del repositorio

Un solo repositorio, con una carpeta por capa del sistema. Cada carpeta se comunica con las demás por archivos o por el protocolo de red, así que una se puede cambiar sin romper las otras. Marca de estado en las tablas: **Listo** = escrito y probado con simulación o datos sintéticos; **Sin probar en hardware** = escrito pero nunca ejecutado en la placa real; **Por escribir** = aún no existe.

```
emg-thermo-squat/
├── README.md, pyproject.toml, .gitignore
├── config/        parámetros del protocolo, de los equipos y de calidad
├── docs/          protocolo, ajustes, guía de video, consentimiento, decisiones
├── hardware/      procedimiento de mediciones del circuito y sus resultados
├── firmware/emg_node/   código de la ESP32-S3 (PlatformIO)
├── host/emgthermo/      paquete Python: adquisición, calidad, procesamiento, herramientas
│   ├── core/  launcher/  validation/  realtime/  hardware/  tools/
│   └── processing/{emg, kinematics, sync, thermal}
├── analysis/      scripts que se ejecutan de principio a fin sobre una sesión
├── tests/         pruebas automáticas (27 hoy)
├── data/          datos crudos de las sesiones (local, fuera de Git)
├── results/       figuras y tablas finales
└── cloud/ y web/  dashboard público (opcional)
```

### Raíz y configuración

| Archivo | Qué hace | Estado |
| --- | --- | --- |
| `README.md` | Resumen, comandos de uso y estado de lo probado y lo no probado | Listo |
| `pyproject.toml` | Dependencias de Python (numpy, scipy, pandas, pyyaml, matplotlib; opcionales: mediapipe y opencv para video, pyqtgraph para monitor) y configuración de pytest | Listo |
| `config/protocol_squat.yaml` | Parámetros del protocolo: 2 series × 10 repeticiones, ciclo del metrónomo, duración de reposo, pausa y recuperación, límites de las 3 fases, ventana RMS de 500 ms, envolvente a 3 Hz, detección de latencia (envolvente a 10 Hz, umbral de media + 3 desviaciones del reposo), banda de MDF (20–300 Hz), método de normalización y regla de exclusión. Los valores no definidos están marcados PENDIENTE o PROPUESTA | Listo; valores por confirmar |
| `config/devices.yaml` | Puertos de red (TCP 5006, UDP 5005), muestreo, canales, sobremuestreo ×8, datos del circuito (AD620, ganancia, banda, notch), umbrales de saturación del ADC, `v_per_count` (vacío hasta calibrarlo) y datos del video | Listo; falta calibrar |
| `config/quality_thresholds.yaml` | Umbrales de calidad: pérdida máxima de tramas, hueco máximo, fracción de saturación, ruido de reposo y proporción de 60 Hz. Provisionales | Listo; ajustar con la Fase 1 |
| `config/thermal_rois.json` | Polígonos o coordenadas de las ROI (VL, BF, rodilla y control) por vista | Por escribir |

### Documentos y hardware

| Archivo | Qué hace | Estado |
| --- | --- | --- |
| `docs/fase0_ajustes_al_protocolo.md` | Cómo se alineó el software con el protocolo, inconsistencias detectadas y texto propuesto para las secciones vacías (metodología, riesgos, protección de datos) | Listo |
| `docs/guia_grabacion_video.md` | Montaje del celular, procedimiento de grabación y validación del ángulo | Listo |
| `docs/consentimiento_informado.md` | Borrador de consentimiento con la cláusula de difusión pública; debe revisarlo el docente o comité | Borrador |
| `docs/decisiones_arquitectura.md` | Registro de decisiones y su estado | Listo; actualizar |
| `hardware/fase1_procedimiento_mediciones.md` | Los 8 pasos de medición del circuito y riesgos a revisar | Listo |
| `hardware/measurements/` | CSV y capturas de las mediciones reales (ruido, barrido, ADC) | Por crear |
| `hardware/notes_calibration.md` | `v_per_count`, ganancia total real, ruido con y sin Wi-Fi, profundidad del notch | Por escribir |

### Firmware (`firmware/emg_node/`)

| Archivo | Qué hace | Estado |
| --- | --- | --- |
| `platformio.ini` | Plataforma pioarduino (Arduino-ESP32 3.x sobre ESP-IDF 5), placa `esp32-s3-devkitc-1`, USB nativo para el monitor serie | Sin probar en hardware |
| `include/config.h` | Frecuencia de muestreo, canales, sobremuestreo, tamaño de bloque (20 muestras), pines (GPIO1 = VL, GPIO2 = BF), umbrales del validador y `USE_SYNC_PULSE` (apagado) | Sin probar en hardware |
| `include/protocol.h` | Formato de la trama UDP (encabezado de 20 bytes, datos, CRC16), banderas de calidad | Listo (espejo de `protocol.py`) |
| `include/validator.h` | Calcula por bloque las banderas SAT, DC\_OOR, FLAT y LINE sin alterar los datos | Lógica probada en PC |
| `include/adc_continuous_s3.h` | Configura el ADC1 en modo continuo con DMA y arma cada muestra sumando 8 conversiones por canal | Sin probar en hardware |
| `include/secrets.h.example` | Plantilla para el nombre y clave del hotspot; se copia a `secrets.h`, que no se versiona | Listo |
| `src/main.cpp` | Conecta al Wi-Fi, atiende los comandos TCP (HELLO, PING, ARM, START, STOP, SYNC\_PULSE, STATUS), envía las tramas UDP | Sin probar en hardware |
| `test/test_validator_host.cpp` | Prueba del validador compilable en PC con `g++` | Listo |

### Paquete Python (`host/emgthermo/`)

| Archivo | Qué hace | Estado |
| --- | --- | --- |
| `core/protocol.py` | Arma y decodifica tramas, CRC16, banderas; comparte un vector de prueba con el firmware | Listo |
| `core/clock_sync.py` | Estima el desfase y la deriva del reloj del nodo respecto a la PC con intercambios PING/PONG | Listo |
| `core/session.py` | Crea la carpeta de sesión y registra eventos con la hora de la PC | Listo |
| `core/recorder.py` | Cliente del nodo: comandos TCP, recepción UDP en un hilo, guardado de `emg_raw.csv` y `frames.csv`, copia opcional a un monitor | Listo con simulador |
| `launcher/run_session.py` | Guía la sesión: conexión, ARM, reposo, series, pausa, recuperación, marcas de tiempo; opciones `--sim`, `--auto`, `--no-thermal`, `--mirror-port` | Listo con simulador |
| `validation/qc.py` | Control de calidad: pérdidas, huecos, jitter del reloj, saturación, ruido de reposo, 60 Hz; devuelve aprobada, con advertencias o rechazada | Listo |
| `realtime/live_plot.py` | Monitor en vivo con la señal y el RMS de cada canal y contadores de calidad | Listo; no probado con nodo real |
| `hardware/circuit_model.py` | Respuesta en frecuencia teórica del circuito (filtros y notch) para comparar con lo medido | Listo |
| `processing/emg/filters.py` | Cuentas a µV, quitar offset, pasa-altas, notch opcional, RMS en ventanas, envolvente y relleno de huecos | Listo |
| `processing/emg/normalization.py` | Referencia por promedio de picos válidos o por RMS máximo, y conversión a % de la referencia | Listo |
| `processing/emg/features.py` | MDF, RMS máximo y medio por segmento, latencia de inicio | Listo |
| `processing/emg/segmentation.py` | Repeticiones por ángulo, por metrónomo o por picos del EMG; división en las 3 fases | Listo |
| `processing/kinematics/knee_angle.py` | Ángulo de flexión de rodilla a partir de cadera, rodilla y tobillo, conversión a píxeles, relleno de huecos, suavizado y resumen de calidad | Listo con datos sintéticos |
| `processing/kinematics/angle_io.py` | Lectura del CSV de ángulo (de `video_pose.py` o de Kinovea) | Listo |
| `processing/sync/align.py` | Sincronía video–EMG: correlación cruzada, localización de las 3 flexo-extensiones y deriva lineal | Listo con datos sintéticos |
| `processing/thermal/loader.py` | Lectura tolerante del CSV exportado por la cámara térmica | Por escribir |
| `processing/thermal/roi.py` | Estadísticas por ROI y ΔT | Por escribir |
| `tools/simulate_device.py` | Nodo EMG simulado para probar todo sin hardware | Listo |
| `tools/import_external_csv.py` | Convierte una captura de Biopac u osciloscopio al formato de sesión | Listo; sin probar con archivo real |
| `tools/phase1_characterize.py` | Analiza el barrido en frecuencia, el ruido y la calibración del ADC de la Fase 1 | Listo con datos sintéticos |
| `tools/video_pose.py` | Ángulo de rodilla desde video con MediaPipe; escribe el CSV, un reporte de calidad y un video de depuración | Sin probar con video real |

### Análisis, pruebas y datos

| Archivo | Qué hace | Estado |
| --- | --- | --- |
| `analysis/run_emg_pipeline.py` | Procesa una sesión completa y escribe en `processed/`: QC, referencia de normalización, características por fase, latencias, resumen por etapa, estadísticas y dos figuras | Listo con simulación |
| `analysis/run_thermal_analysis.py` | Procesa los termogramas de una sesión y escribe tablas de ΔT por ROI | Por escribir |
| `analysis/make_report.py` | Reúne las tablas y figuras de EMG, ángulo y temperatura en una carpeta de resultados | Por escribir |
| `tests/test_protocol.py`, `test_phase1.py`, `test_processing.py`, `test_knee_angle.py`, `test_end_to_end_sim.py` | Pruebas automáticas; la última corre la sesión simulada completa | Listo (27 pruebas) |
| `tests/vectors/frames.json` | Trama de ejemplo compartida con el firmware | Listo |
| `tests/test_thermal.py` | Pruebas del módulo térmico con imágenes sintéticas | Por escribir |
| `data/` | Datos crudos de cada sesión; no se versiona y se respalda aparte | Local |
| `results/` | Figuras y tablas finales para el informe | Local |
| `cloud/supabase/` | Esquema SQL y políticas de lectura pública; solo si se hace el dashboard | Opcional |

## 5. Calendario y ruta crítica (20 nov – 2 dic)

| Fecha | Qué se hace | Resultado esperado |
| --- | --- | --- |
| Vie 20 nov | Compilar y cargar el firmware; revisar fps del celular | Compila o lista de errores; IP del nodo |
| Sáb 21 nov | Corregir firmware; mediciones de Fase 1 (ruido, barrido); conseguir imagen y CSV térmicos de muestra; confirmar modelo y fps del celular | Primeras mediciones; muestra térmica |
| Dom 22 nov | Calibración del ADC, anti-aliasing; **punto de control 1: ¿el ADC interno sirve?** | Decisión go/no-go por escrito |
| Lun 23 nov | Grabación real de 10 min; `capture_raw.py`; ajuste de umbrales de QC | Sesión con pérdida < 1 % |
| Mar 24 nov | Video real con MediaPipe; validación con goniómetro; **congelar decisiones** (sección 17) | Ángulo validado; decisiones cerradas |
| Mié 25 nov | **Punto de control 2: sesión piloto completa** con todos los eventos | Sesión piloto y su pipeline |
| Jue 26 nov | Correcciones tras el piloto; módulo de termografía | Pipeline estable |
| Vie 27 nov | Termografía terminada; ensayo del día de medición | Módulo térmico con pruebas |
| 28–30 nov | Sesiones reales (1–2 sujetos); procesamiento | Datos y `processed/` finales |
| Mar 1 dic | Figuras, tablas e informe | Borrador del informe |
| Mié 2 dic | Revisión y entrega | Entrega final |

**Ruta crítica.** Firmware compilado → go/no-go del ADC → grabación real estable → piloto completo → sesiones reales. Si cualquiera se retrasa un día, se recorta la termografía detallada o el dashboard, no las sesiones reales.

**Regla.** Después del piloto (25 nov) no se cambian métodos ni umbrales.

## 6. Fase 0: protocolo y decisiones (20–24 de noviembre, en paralelo con la Fase 1)

**Objetivo.** Dejar escrito, antes de ver datos reales, qué se mide, cómo se normaliza y qué repeticiones valen. Cambiar estos criterios después de ver los resultados resta credibilidad.

**Entradas.** El borrador del protocolo del 12 de noviembre, `docs/fase0_ajustes_al_protocolo.md` y `config/protocol_squat.yaml`.

**Pasos**

1. **Unificar la normalización.** El texto del protocolo dice «normalización al RMS máximo»; el equipo indicó «promedio de los picos de las repeticiones válidas». Elegir uno y escribirlo en el protocolo. El software soporta ambos (`method: mean_valid_peaks` o `max_rms` en `protocol_squat.yaml`, o `--method` en el pipeline) y guarda las dos referencias en `reference.json`.
2. **Definir las 3 fases** (inicio, pico de contracción, recuperación) y los segundos por repetición. Con el metrónomo a 60 bpm se propone 3 s para bajar y 3 s para subir (ciclo de 6 s). Propuesta de partida: 0–33 %, 33–67 % y 67–100 % de cada repetición. Con el ángulo de video se puede definir por posición (por ejemplo, el pico es el tramo cercano a la flexión máxima).
3. **Fijar el criterio de exclusión de repeticiones.** Propuesta a discutir: es inválida la repetición con saturación o hueco de datos mayor a 20 ms, con flexión máxima a más de 15° del objetivo de 90°, o con duración que difiera más de 30 % de la mediana. Los umbrales de 15° y 30 % son una sugerencia, no un valor del protocolo.
4. **Decidir cómo se mide la latencia.** Una ventana RMS de 500 ms da resolución de ±250 ms. Propuesta: usar una envolvente más rápida (10 Hz) solo para detectar el inicio de activación (umbral = media + 3 desviaciones del reposo, 25 ms mínimo) y reservar los 500 ms para la amplitud por fase.
5. **Decidir el filtro pasa-altas digital de 20 Hz** (no está en el protocolo; reduce artefacto de movimiento porque el pasa-altas analógico llega hasta 5 Hz). Se desactiva con `highpass_hz: null`.
6. **Fijar la ubicación de los electrodos** según SENIAM. VL: aproximadamente a 2/3 de la línea entre la espina ilíaca anterosuperior y el borde lateral de la rótula. BF: punto medio entre la tuberosidad isquiática y el epicóndilo lateral de la tibia. Verificar estas descripciones en el documento oficial de SENIAM antes de la sesión. Electrodo de referencia en una zona ósea.
7. **Completar las secciones vacías del protocolo** con el texto propuesto en `docs/fase0_ajustes_al_protocolo.md` (metodología, riesgos, protección de datos) y describir el video en Materiales (celular a 120 fps, MediaPipe).
8. **Consentimiento informado firmado** por cada voluntario antes de cualquier registro. Decidir el alcance de la publicación de datos; si el sitio público no se hace, se simplifica.
9. **Seguridad eléctrica.** Confirmar cómo se alimenta la ESP32. El criterio propuesto: durante la medición con el sujeto conectado, el circuito funciona con pilas y la placa no está conectada por cable USB a la PC (el USB solo se usa para programar). Confirmarlo con el docente, porque depende de cómo está conectado el aislamiento ISO124.

**Archivos que se tocan.** `config/protocol_squat.yaml`, `docs/consentimiento_informado.md`, el documento del protocolo.

**Validación.** El protocolo v1.1 con las 5 decisiones (pasos 1 a 5) respondidas por escrito, aprobado por el equipo y el docente, y el consentimiento revisado. Fecha límite: 24 de noviembre.

**Entregable.** Protocolo actualizado y `protocol_squat.yaml` sin ningún `PENDIENTE` en los parámetros que usa el pipeline.

**Si no se alcanza.** Se congelan los valores propuestos y se declaran como tales en el informe.

## 7. Fase 1: circuito analógico y ADC (21–22 de noviembre)

**Objetivo.** Demostrar con mediciones que la señal llega al ADC limpia y sin saturar, y obtener los números que el software necesita: voltios por cuenta del ADC, ganancia total real y ruido de reposo. De esta fase sale la decisión del domingo 22: si el ADC interno es suficiente.

**Qué hace el circuito** (cada canal): electrodos → protección → AD620 con ganancia 100 → aislamiento ISO124 → pasa-altas de 2.º orden en 5 Hz → pasa-bajas de 2.º orden en 312 Hz → notch twin-T de 2.º orden en 60 Hz → offset de 1.65 V → ADC de la ESP32-S3. Todo con pilas, y con un electrodo de referencia (DRL). La banda de 5–312 Hz y el notch de 60 Hz quitan deriva y la interferencia de la red eléctrica.

**Tres riesgos que esta fase debe aclarar**

1. **Resolución.** Con 3.3 V y 12 bits cada cuenta vale 3.3 / 4095 = 0.81 mV a la salida; con ganancia 100 son unos **8 µV por cuenta en la entrada**. El ruido de reposo del EMG está en el orden de 5–15 µV, así que el reposo casi no se resuelve. Una contracción de 1 mV produce unas 120 cuentas, suficiente. Si hay ganancia variable, medir si conviene subirla sin saturar.
2. **Offset de los electrodos.** Un offset de 50–300 mV multiplicado por 100 puede llevar la salida a un riel. Verificar con electrodos reales en reposo que no ocurra.
3. **Rango del ADC.** El ADC del ESP32-S3 no es lineal cerca de los extremos y con atenuación alta su rango útil es algo menor que 3.3 V. Medirlo, y proteger el pin para que nunca supere 3.3 V.

**Equipo necesario.** Generador de funciones, osciloscopio, multímetro, fuente de tensión variable o divisor resistivo, el Biopac del laboratorio (para pruebas preliminares) y un cable de entradas en corto.

**Pruebas** (procedimiento detallado en `hardware/fase1_procedimiento_mediciones.md`)

| # | Prueba | Cómo | Herramienta | Criterio propuesto |
| --- | --- | --- | --- | --- |
| 1 | Ruido con Wi-Fi apagado | Entradas en corto, 60 s | `phase1_characterize.py noise` | Ruido 20–300 Hz ≤ 10 µV RMS referido a la entrada |
| 2 | Ruido con Wi-Fi encendido | Igual, con el firmware transmitiendo | `phase1_characterize.py noise` con ambos archivos | Relación de ruido (con/sin Wi-Fi) ≤ 1.3 |
| 3 | Barrido en frecuencia | Senoidal de \~1 mVpp a 2, 5, 10, 20, 50, 60, 100, 200, 312, 500 y 800 Hz; anotar `freq_hz, vin_mvpp, vout_mvpp` | `phase1_characterize.py sweep` | Ganancia en banda ±10 % de 100; −3 dB cerca de 5 y 312 Hz; notch ≥ 20 dB a 60 Hz |
| 4 | CMRR | La misma senoidal (60 y 100 Hz, 1 Vpp) en ambas entradas | Cálculo manual (salida / entrada) | Lo más alto posible; anotar el valor |
| 5 | Calibración del ADC | Tensiones DC de 0.3, 0.6, 1.0, 1.4, 1.65, 2.0, 2.5, 2.9 y 3.2 V medidas con multímetro; grabar las cuentas del nodo | `phase1_characterize.py adc` | `v_per_count` medido; zona lineal identificada |
| 6 | Anti-aliasing | Senoidales de 700 a 1900 Hz con fs = 2000 Hz | Revisar el espectro de la captura | Sin componentes falsos por debajo de 312 Hz |
| 7 | Prueba fisiológica | Electrodos reales; reposo y contracción voluntaria de VL y BF | Monitor en vivo | Sin saturación; contracción claramente visible; se ve el 60 Hz residual |
| 8 | Comparación con Biopac | La misma contracción en ambos sistemas | `import_external_csv.py` + pipeline | RMS por ventanas de 500 ms comparable |

Los comandos tienen esta forma (siempre desde la carpeta del repositorio, con `export PYTHONPATH=host`; en Windows `set PYTHONPATH=host`):

```
python -m emgthermo.tools.phase1_characterize noise corto_wifi_off.csv corto_wifi_on.csv --column ch1 --os 8 --v-per-count <medido> --gain <ganancia total>
python -m emgthermo.tools.phase1_characterize sweep barrido.csv --gain 100
python -m emgthermo.tools.phase1_characterize adc adc_dc.csv --os 8
```

**Nota sobre la captura.** Las pruebas 1, 2 y 5 necesitan cuentas crudas del nodo. Para eso se escribirá `tools/capture_raw.py`, una herramienta corta que graba N segundos del nodo en `emg_raw.csv` sin correr una sesión completa (ver Fase 3). Mientras el firmware no funcione, las pruebas 1, 3 y 5 se pueden hacer con el osciloscopio o el Biopac.

**Archivos que se tocan.** `hardware/measurements/` (guardar aquí cada CSV y captura de pantalla, con fecha y condiciones en el nombre), `hardware/notes_calibration.md`, `config/devices.yaml` (`v_per_count`, `gain_total`) y `config/quality_thresholds.yaml` (umbral de ruido y de 60 Hz basados en lo medido).

**Validación.** Las 8 pruebas hechas y registradas; `v_per_count` y la ganancia real escritos en la configuración; decisión por escrito sobre el ADC interno.

**Decisión go/no-go (domingo 22 de noviembre).** Si el ruido con Wi-Fi arruina la señal o el rango lineal es demasiado pequeño, se pasa al plan B: grabar el EMG con Biopac u osciloscopio e importarlo con `import_external_csv.py` (ver riesgos).

**Entregable.** Carpeta de mediciones y `notes_calibration.md` con una tabla de resultados.

## 8. Fase 2: firmware del nodo ESP32-S3 (20–23 de noviembre)

**Objetivo.** Que la ESP32-S3 SuperMini lea los dos canales a 2000 Hz, valide cada bloque y lo envíe por UDP, y que acepte comandos de control por TCP. **Estado honesto:** el firmware está escrito pero **no se ha compilado ni probado en hardware**. Esta fase es compilarlo, corregir lo que falle y demostrar que funciona.

**Herramientas.** VS Code + extensión PlatformIO, con la plataforma **pioarduino** (Arduino-ESP32 3.x sobre ESP-IDF 5, necesaria para la S3 y para el driver `adc_continuous`). Placa: `esp32-s3-devkitc-1`, con `ARDUINO_USB_CDC_ON_BOOT=1` para que el puerto USB sirva de monitor serie.

**Pasos de compilación y carga**

1. Abrir la carpeta `firmware/emg_node/` en VS Code (PlatformIO la reconoce por `platformio.ini`).
2. Copiar `include/secrets.h.example` a `include/secrets.h` y poner el nombre y la clave de la red Wi-Fi. Debe ser una red de **2.4 GHz** (un hotspot del celular o un router); la ESP32-S3 no usa 5 GHz. `secrets.h` no se sube al repositorio.
3. Compilar (`pio run`). Los errores que aparezcan se envían tal cual para corregirlos (ver sección 19).
4. Conectar la placa por USB. Si no aparece el puerto, mantener presionado el botón **BOOT**, conectar el cable y soltar: entra en modo de descarga.
5. Cargar (`pio run -t upload`) y abrir el monitor serie (`pio device monitor`). Debe imprimir la IP asignada.
6. Anotar la IP; es el valor de `--host` en el lanzador.

**Qué hace cada archivo**

| Archivo | Qué contiene | Por qué es necesario |
| --- | --- | --- |
| `platformio.ini` | Plataforma pioarduino, placa, opciones de compilación y de monitor | Sin esto PlatformIO no sabe compilar para la S3 |
| `include/config.h` | Constantes: pines de los canales, `FS_HZ=2000`, `N_CH=2`, `OS_FACTOR=8`, `ADC_TOTAL_HZ=32000`, muestras por trama (20), puertos, `USE_SYNC_PULSE` | Un solo lugar para cambiar parámetros |
| `include/protocol.h` | Estructura de la trama (cabecera de 20 bytes, muestras, CRC16/CCITT-FALSE), banderas y respuestas | Define el contrato con Python; debe coincidir con `core/protocol.py` |
| `include/validator.h` | Lógica que decide las banderas de cada bloque (saturación, plano, fuera de rango DC, batería baja, 60 Hz con Goertzel) | Es lo que permite al nodo marcar problemas sin filtrar la señal |
| `include/adc_continuous_s3.h` | Envoltura del ADC1 en modo continuo con DMA: configura los pines, arranca la conversión y entrega la **suma** de 8 conversiones por canal | Da el muestreo a 2000 Hz con reloj de hardware, sin que la CPU lo temporice |
| `include/secrets.h.example` | Plantilla de credenciales Wi-Fi | Evita publicar la clave |
| `src/main.cpp` | Flujo principal: Wi-Fi, servidor TCP, bucle que lee el ADC, arma tramas de 20 muestras (10 ms; 102 bytes), calcula CRC y envía por UDP | Une todo lo anterior |
| `test/test_validator_host.cpp` | Prueba del validador en la PC (no necesita placa) | Verifica la lógica antes de depender del hardware |

**Flujo de `main.cpp`.** (1) Se conecta al Wi-Fi y abre el servidor TCP. (2) Espera comandos: `HELLO` (identificación y versión), `PING` (para la sincronía de reloj; responde con su tiempo interno), `ARM` (mide el nivel de reposo para comprobar el offset de 1.65 V), `START <puerto>` (empieza a enviar UDP a la IP del cliente y al puerto indicado), `STOP` (detiene y devuelve estadísticas), `SYNC_PULSE` (solo si `USE_SYNC_PULSE=1`; por defecto desactivado) y `STATUS`. (3) Mientras transmite, el bucle vacía el DMA, y cuando junta 20 muestras completas arma la trama con número de secuencia, tiempo del nodo en µs y banderas, calcula el CRC y la envía. (4) Si el DMA se desborda, marca `OVERRUN` en la trama siguiente; Python lo ve en la calidad.

**Prueba en la PC sin hardware**

```
g++ -std=c++17 -Iinclude test/test_validator_host.cpp -o /tmp/tv && /tmp/tv
```

**Criterios de aceptación**

| # | Criterio | Cómo se verifica |
| --- | --- | --- |
| 1 | Compila sin errores | `pio run` |
| 2 | Conecta al Wi-Fi y muestra la IP | Monitor serie |
| 3 | `HELLO`, `PING`, `ARM`, `START`, `STOP` responden | Lanzador o `nc <IP> 5000` |
| 4 | `ARM` da una media cercana a 2000 cuentas (1.65 V) por canal | Respuesta de `ARM` con entradas en corto |
| 5 | 10 min de transmisión sin `OVERRUN` ni pérdida mayor a 1 % | `stop_stats` y `session.json` |
| 6 | La frecuencia de muestreo efectiva está dentro de ±0.1 % de 2000 Hz | Calculada con `t_us` de las tramas (la hace el QC) |
| 7 | Las banderas aparecen cuando se provoca el fallo | Conectar el pin a 3.3 V (SAT), a tierra (FLAT) y con 60 Hz (LINE) |

**Riesgos conocidos.** (a) La frecuencia máxima del ADC continuo en la S3 con dos canales (32 kHz en total) debe confirmarse; si no se alcanza, se baja el sobremuestreo a ×4 (`OS_FACTOR=4`, 16 kHz) y se ajusta la escala. (b) La atenuación `ADC_ATTEN_DB_12` da un rango útil cercano a 3.1 V; se verifica con la prueba 5 de la Fase 1. (c) Los nombres exactos de la API pueden variar entre versiones de Arduino-ESP32 3.x.

**Validación.** Los 7 criterios cumplidos. **Entregable.** Firmware compilado, con la IP anotada y un registro de la prueba de 10 min.

## 9. Fase 3: adquisición en Python (22–23 de noviembre)

**Objetivo.** Grabar en la PC una sesión completa del nodo (EMG + eventos + sincronía de reloj) con control de calidad, y poder verla en vivo. Esta parte **sí está probada con un nodo simulado** (27 pruebas automáticas, sesión simulada con 0 % de pérdida); falta probarla con el nodo real.

**Preparación de la PC**

1. Python 3.10 o superior; desde la carpeta del repositorio: `pip install -e .` (dependencias en `pyproject.toml`). Definir `PYTHONPATH=host` si no se instala.
2. PC y nodo en la **misma red 2.4 GHz** (hotspot del celular o router sin aislamiento de clientes).
3. Windows: permitir el puerto **UDP 5005** en el firewall (o aceptar el aviso la primera vez que corre Python). Si no llegan datos, casi siempre es esto.
4. Anotar la IP del nodo (monitor serie).

**Cómo funciona el lanzador** (`launcher/run_session.py`). Conecta por TCP, ejecuta `HELLO`, mide la sincronía de reloj con varios `PING`/`PONG` (se usa el de menor tiempo de ida y vuelta, y deriva lineal), hace `ARM`, y al empezar `START` recibe las tramas por UDP. Guarda `emg_raw.csv`, `frames.csv`, `events.csv` y `session.json`. Mientras corre, el operador marca los eventos con teclas (REST, SERIE, PAUSA, RECOVERY, SYNC\_MOVE, termografías T0/T1/T3). La lista de eventos y su orden están en la sección 15.

**Comandos**

```
# Prueba sin hardware (simulador, 10 veces más rápido)
python -m emgthermo.launcher.run_session --sim --auto --time-scale 0.1 --subject TEST

# Sesión real
python -m emgthermo.launcher.run_session --host <IP> --subject S01 [--no-thermal] [--mirror-port 5105]

# Monitor en vivo (otra terminal; usa --mirror-port 5105 en el lanzador)
python -m emgthermo.realtime.live_plot --port 5105
```

`--no-thermal` omite las marcas de termografía; `--mirror-port` reenvía copia de las tramas al monitor sin afectar la grabación.

**Calidad (`validation/qc.py`).** Tras cada sesión calcula pérdida de paquetes, huecos mayores a 20 ms, fracción saturada, banderas, ruido en reposo, fracción de potencia a 60 Hz y frecuencia de muestreo efectiva. Los umbrales están en `config/quality_thresholds.yaml` (hoy provisionales: pérdida ≤ 1 %, hueco ≤ 20 ms, saturación ≤ 0.1 %, ruido en reposo ≤ 10 µV RMS, 60 Hz ≤ 20 %). Se ajustan con las mediciones de la Fase 1 y después se congelan.

**Herramienta por escribir: `tools/capture_raw.py`.** Graba N segundos del nodo directamente a `emg_raw.csv`, sin sesión ni eventos. Se usa en las pruebas 1, 2 y 5 de la Fase 1 (ruido y calibración del ADC). Es corta (reutiliza `core/recorder.py`); se escribe en cuanto el firmware funcione.

**Archivos de esta fase**

| Archivo | Función |
| --- | --- |
| `core/protocol.py` | Decodifica tramas, verifica CRC y banderas; contrapartida de `protocol.h` |
| `core/clock_sync.py` | Estima desfase y deriva entre el reloj del nodo y el de la PC |
| `core/session.py` | Carpeta de la sesión, `events.csv`, `session.json` |
| `core/recorder.py` | Recibe UDP, detecta tramas perdidas por `seq`, escribe `emg_raw.csv` y `frames.csv` |
| `launcher/run_session.py` | Flujo completo del operador |
| `validation/qc.py` | Informe de calidad |
| `realtime/live_plot.py` | Gráfica en vivo (solo verificación visual) |
| `tools/simulate_device.py` | Nodo falso para probar sin hardware |
| `tools/import_external_csv.py` | Convierte un CSV de Biopac u osciloscopio al formato de la sesión (plan B) |

**Criterios de aceptación**

| # | Criterio |
| --- | --- |
| 1 | La prueba simulada termina con 0 % de pérdida y sin errores |
| 2 | Con el nodo real, grabación de 10 min con pérdida < 1 % y sin huecos > 20 ms |
| 3 | `session.json` tiene `clock`, `device_info`, `recording` y `stop_stats` completos |
| 4 | `events.csv` trae todos los eventos marcados en orden |
| 5 | El monitor en vivo muestra ambos canales sin saturar |
| 6 | `qc_report.json` coherente con lo observado |

**Entregable.** Una sesión de 10 min en `data/` con su informe de calidad.

## 10. Fase 4: procesamiento EMG, video y sincronía (23–25 de noviembre)

**Objetivo.** Convertir una sesión cruda en resultados: RMS por fase de la sentadilla, normalización, frecuencia mediana, latencia de inicio y gráficas, usando el ángulo de rodilla obtenido del video del celular. Todo offline y reproducible con un solo comando. El procesamiento EMG está probado con datos sintéticos; la detección con MediaPipe **no se ha probado con video real** (no hubo acceso a internet para descargar el modelo).

### 10.1 Cadena de procesamiento EMG

| Paso | Qué hace | Archivo | Parámetros |
| --- | --- | --- | --- |
| 1 | Convierte cuentas a µV en la entrada (resta el offset, divide entre la ganancia) | `hardware/circuit_model.py` | `v_per_count`, ganancia total |
| 2 | Filtro pasa-banda de fase cero (`sosfiltfilt`) | `processing/emg/filters.py` | 20–300 Hz propuesto (ver decisión sobre 20 Hz) |
| 3 | RMS en ventanas | `filters.py` | 500 ms, 50 % de traslape |
| 4 | Envolvente (pasa-bajas) | `filters.py` | 3 Hz |
| 5 | Segmentación de repeticiones | `segmentation.py` | ángulo > metrónomo > picos del EMG |
| 6 | División en 3 fases por repetición | `segmentation.py` | 0–33 / 33–67 / 67–100 % (propuesta) |
| 7 | RMS máximo y medio por fase; MDF | `features.py` | MDF con Welch en 20–300 Hz |
| 8 | Referencia de normalización | `normalization.py` | `mean_valid_peaks` (por defecto) o `max_rms` |
| 9 | Latencia de inicio de activación | `features.py` (`detect_onset`) | envolvente de 10 Hz, línea base = media + 3 DE del reposo, mínimo 25 ms |
| 10 | Estadística y gráficas | `analysis/run_emg_pipeline.py` | descriptiva y diagramas de caja |

**Normalización.** `mean_valid_peaks`: la referencia de cada músculo es el promedio de los picos de RMS de las repeticiones válidas; es el criterio indicado por el equipo. `max_rms`: el máximo de todas las ventanas de la sesión, como dice el texto del protocolo. Ambos usan las mismas ventanas, así que se pueden comparar. La máscara de repeticiones válidas depende del criterio de exclusión (pendiente; ver fases 0 y sección 17).

### 10.2 Video y ángulo de rodilla

**Grabación** (`docs/guia_grabacion_video.md`). Celular a 120 fps, fijo en trípode, cámara perpendicular al plano sagital, a la altura de la cadera, a unos 2–3 m; fondo liso, ropa ajustada, cadera, rodilla y tobillo visibles todo el tiempo; buena luz para que el obturador sea rápido. Grabar un lado (el de los electrodos). Antes de la sesión verificar que el celular realmente exporte 120 fps (pendiente: modelo y formato, 21 de noviembre).

**Detección.** `tools/video_pose.py` usa MediaPipe Tasks `PoseLandmarker` (la API antigua `mp.solutions` ya no existe en la versión 0.10.33). Toma cadera, rodilla y tobillo (derecha 24/26/28, izquierda 23/25/27), calcula el ángulo en **píxeles** (no en coordenadas normalizadas, que distorsionan por la relación de aspecto), define flexión = 180° − ángulo interno, procesa 1 de cada 2 cuadros y guarda el CSV de ángulo y un video de depuración.

```
python -m emgthermo.tools.video_pose video.mp4 --side right --fps 120 --out angulo.csv --debug-video debug.mp4
```

Si falla la descarga del modelo, bajar `pose_landmarker_full.task` desde la documentación oficial de MediaPipe y pasarlo con la opción del modelo (ver `--help`).

**Validación del ángulo con goniómetro.** Con la persona quieta en 0°, 45° y 90° de flexión (medidos con goniómetro), grabar 5 s en cada posición y comparar. Criterio propuesto: error ≤ 5°. Es un ángulo 2D: si la cámara no es perpendicular, el error aumenta; se declara como limitación.

**Archivos.** `processing/kinematics/knee_angle.py` (cálculo, suavizado, velocidad angular), `angle_io.py` (lectura y escritura del CSV de ángulo y su `.quality.json`), `tools/video_pose.py`.

### 10.3 Sincronía video–EMG

Al inicio de la grabación, la persona hace **3 flexo-extensiones rápidas de rodilla**, marcadas con `SYNC_MOVE_START` y `SYNC_MOVE_END`. `processing/sync/align.py` correlaciona (correlación cruzada normalizada) la envolvente del EMG con la velocidad angular del video en ese tramo y calcula el desfase. Si el resultado no es confiable, se puede dar a mano con `--angle-offset-s`. No hace falta hardware extra ni pulso eléctrico.

### 10.4 Ejecución y salidas

```
python analysis/run_emg_pipeline.py --session data/S01_<fecha> --angle-csv angulo.csv [--method max_rms] [--angle-offset-s X]
```

Salidas en `data/<sesión>/processed/`: `qc_report.json`, `features_phase.csv`, `onsets.csv`, `reference.json`, `summary_stages.csv`, `stats_by_phase.csv`, `timeline.png` y `boxplot_phases.png` (columnas en la sección 14). Sin `--angle-csv`, las repeticiones se segmentan con el metrónomo o con picos del EMG, y el resultado se marca como de menor confianza.

**Interpretación contra la hipótesis.** VL: RMS normalizado mayor en la fase de pico que en las fases inicial y de recuperación y que en reposo. BF: menor que VL en la misma fase (coactivación). Post-ejercicio: el RMS vuelve hacia el valor de reposo. Con 1–2 sujetos el análisis es **descriptivo**; no se hacen pruebas de hipótesis con pretensión inferencial.

**Sin aprendizaje automático.** La clasificación de fases se hace por umbrales sobre el ángulo; con tan pocos datos un modelo no se justifica.

**Criterios de aceptación**

| # | Criterio |
| --- | --- |
| 1 | `video_pose` produce un CSV de ángulo con visibilidad alta (`vis_min`) y sin saltos en un video real |
| 2 | Error del ángulo ≤ 5° frente al goniómetro en 0°, 45° y 90° |
| 3 | El desfase por SYNC\_MOVE es estable (misma respuesta en dos intentos) |
| 4 | El pipeline corre de principio a fin sobre la sesión real y genera todas las salidas |
| 5 | Se detectan las 20 repeticiones esperadas (2 × 10) |
| 6 | Las gráficas se entienden y los valores son fisiológicamente razonables |

**Entregable.** Carpeta `processed/` de la sesión piloto, con la verificación del ángulo.

## 11. Fase 5: termografía (24–27 de noviembre)

**Objetivo.** Medir la temperatura de la piel del muslo antes, durante y después del ejercicio con la cámara Fotric H6 y compararla con el EMG. La cámara se maneja con su propio software; el lanzador solo registra la hora de cada captura. **Este módulo no está escrito todavía** y necesita una imagen y un CSV de muestra de la H6 para definir el formato real.

**Condiciones de captura**

- Sala a 20–24 °C, sin corrientes de aire ni luz solar directa; aclimatación de la persona de 10–15 min en reposo con el muslo descubierto.
- Sin cremas, sin ejercicio previo intenso, sin sol reciente en la zona.
- Distancia y ángulo fijos entre capturas (marca en el piso o trípode con altura anotada).
- Emisividad de la piel: **0.98** (valor habitual; confirmar con la documentación de la H6). Anotar temperatura ambiente, temperatura reflejada, distancia y humedad.
- Los electrodos aparecen en la imagen; evitar que cubran las ROI o excluir esa zona.

**Momentos y vistas**

| Momento | Cuándo | Evento |
| --- | --- | --- |
| T0 (pre) | Tras aclimatación, antes del ejercicio | `THERMAL_T0_PRE` |
| T1 (pausa) | Entre la serie 1 y la serie 2 | `THERMAL_T1_PAUSA` |
| T3 (post) | Al terminar la recuperación | `THERMAL_T3_POST` |

Vistas: **ANT** (cara anterior del muslo, sobre el vasto lateral) y **POS** (cara posterior, sobre el bíceps femoral). Para evitar demoras, cada captura debe tardar menos de 30 s en cada momento.

**Regiones de interés (ROI):** VL, BF, rodilla y una zona de control (por ejemplo, antebrazo o la otra pierna). Se definen en `config/thermal_rois.json` y se usan las mismas en todos los momentos. Resultado por ROI: temperatura media, máxima y desviación estándar; y el cambio ΔT respecto a T0.

**Incertidumbre.** Se toman dos capturas consecutivas en reposo (antes de T0) para estimar la variación por repetición. El ΔT solo se interpreta si supera esa variación y la exactitud de la cámara.

**Archivos por escribir**

| Archivo | Función |
| --- | --- |
| `host/emgthermo/processing/thermal/loader.py` | Lee la exportación de la H6 (matriz de temperaturas en CSV o imagen radiométrica) |
| `host/emgthermo/processing/thermal/roi.py` | Aplica las ROI y calcula estadísticos |
| `analysis/run_thermal_analysis.py` | Une `thermal/index.csv` con las ROI y genera tabla y gráficas |
| `config/thermal_rois.json` | Coordenadas de cada ROI por vista |
| `tests/test_thermal.py` | Pruebas con una matriz sintética |
| `analysis/make_report.py` | Tablas y figuras finales (se usa en la Fase 7) |

**Índice de capturas.** `data/<sesión>/thermal/index.csv` con columnas `file, moment, view, t_pc_s, emissivity, t_ambient_c, t_reflected_c, distance_m, humidity_pct`. El tiempo `t_pc_s` se toma de `events.csv`.

**Alcance de la comparación con el EMG.** Con tres momentos y uno o dos sujetos, la relación con el EMG es **cualitativa**: se describe si el músculo con más activación también tuvo mayor cambio de temperatura. La temperatura de la piel no es la del músculo y responde con retraso; se declara en las limitaciones.

**Plan B.** Si la exportación de la H6 no se puede leer, analizar las ROI con el software del fabricante (AnalyzIR) y pasar los valores a mano a una tabla con el mismo formato.

**Criterios de aceptación**

| # | Criterio |
| --- | --- |
| 1 | Se tienen T0, T1 y T3 en ANT y POS, con metadatos completos |
| 2 | El módulo lee la exportación real y las ROI quedan sobre el músculo correcto (verificado visualmente) |
| 3 | Las pruebas pasan y el ΔT se calcula con su incertidumbre |
| 4 | Hay una tabla y una figura de temperatura por momento |

**Entregable.** `thermal/index.csv`, tabla de ΔT por ROI y figura. **Insumos que se necesitan:** una imagen de muestra y su CSV exportado, y confirmar si la H6 puede grabar secuencias.

## 12. Fase 6: dashboard público (opcional, solo si sobra tiempo)

**Objetivo.** Publicar los resultados procesados en una página web de solo lectura. **No es necesaria para el informe ni para la entrega**; se hace solo después de cerrar las fases 0–5 y la 7.

**Decisiones vigentes.** El acceso es público, de solo lectura, y muestra solo sesiones con `published = true`. La web nunca procesa datos crudos: consume los resultados que genera Python (`features_phase.csv`, `summary_stages.csv`, imágenes) ya subidos a la nube.

**Diseño.** Base de datos en Supabase (PostgreSQL) con política de seguridad por filas (RLS): lectura pública solo de sesiones publicadas; escritura solo con una clave privada del equipo que nunca va en el frontend. La web (React en Vercel) lista sesiones y muestra gráficas por fase y músculo. Los datos son de personas: se publican solo con consentimiento firmado, con código de sujeto (S01) y sin nombres ni imágenes de rostro (ver `docs/consentimiento_informado.md`).

**Pendiente.** El código de la nube (`cloud/`) existe como esbozo y no se incluye en la entrega actual. Si se decide hacerlo, se escribe después del 30 de noviembre.

## 13. Fase 7: validación final, reproducibilidad y entregables (30 nov – 2 dic)

**Objetivo.** Cerrar el proyecto con resultados que otra persona pueda reproducir y un informe que declare lo que se midió y lo que no.

**Pasos**

1. Congelar la versión: umbrales, parámetros y método no cambian después de la sesión piloto; si algo cambia, se rehace todo con la misma versión.
2. Correr el pipeline sobre todas las sesiones con un solo comando y archivar `processed/`.
3. Guardar `protocol_version`, la versión del código (hash de git) y las dependencias exactas junto con los resultados.
4. Generar tablas y figuras finales con `analysis/make_report.py` (por escribir).
5. Revisar que cada cifra del informe se pueda rastrear a un archivo de `processed/`.
6. Redactar el informe y revisar con el docente.

**Esquema del informe.** Introducción e hipótesis; métodos (circuito, firmware, adquisición, video, termografía, procesamiento); resultados (calidad de datos, EMG por fase, MDF, latencias, temperatura); discusión frente a la hipótesis; limitaciones (sección 18); conclusiones; anexos (consentimiento, mediciones de la Fase 1, protocolo).

**Entregables.** Informe; repositorio con código y configuración; datos procesados y, si el consentimiento lo permite, crudos; figuras; mediciones de la Fase 1; presentación si se pide.

**Criterio de cierre.** Una persona ajena al equipo puede, con el repositorio y los datos, repetir el análisis y obtener las mismas tablas y figuras.

## 14. Formatos de datos

Cada sesión vive en `data/<sujeto>_<fecha>/`. Los archivos crudos no se modifican nunca; todo lo derivado va en `processed/`.

| Archivo | Columnas / contenido |
| --- | --- |
| `emg_raw.csv` | `t_us, ch1, ch2, flags, seq`. Los valores son **sumas de 8 conversiones** (0–32760); `t_us` es el tiempo del nodo |
| `frames.csv` | `seq, t_us, flags, n, t_recv_s` (una fila por trama recibida) |
| `events.csv` | `t_pc_s, event, detail`. Eventos: PREP\_START, SYSTEM\_CONNECTED, ELECTRODES\_PLACED, ARM, ACTIVITY\_OBSERVED, THERMAL\_T0\_PRE, VIDEO\_START, CLOCK\_SYNC\_START, EMG\_START, SYNC\_PULSE\_START, SYNC\_MOVE\_START/END, REST\_START/END, SERIE\_k\_START/END, THERMAL\_T1\_PAUSA, PAUSA\_START/END, RECOVERY\_START/END, SYNC\_PULSE\_END, EMG\_STOP, THERMAL\_T3\_POST |
| `session.json` | sujeto, `t0_pc_us`, `clock{offset_us, t_ref_dev_us, drift_ppm, rtt_us}`, `device_info`, `arm`, `stop_stats`, `recording{frames, expected_frames, bad_frames, samples, loss_pct}`, `protocol_version`, `analog`, `os_factor`, `fs_hz` |
| `angulo.csv` | `time_s, knee_flexion_deg, knee_flexion_raw_deg, ang_vel_deg_s, vis_min`, coordenadas de cadera, rodilla y tobillo; más `angulo.quality.json` |
| `thermal/index.csv` | `file, moment, view, t_pc_s, emissivity, t_ambient_c, t_reflected_c, distance_m, humidity_pct` (por escribir) |

**Resultados en `processed/`**

| Archivo | Contenido |
| --- | --- |
| `qc_report.json` | Métricas de calidad y veredicto |
| `features_phase.csv` | `muscle, series, rep, phase, rms_mean_uv, rms_max_uv, rms_mean_pct, rms_max_pct, mdf_hz, n_windows` |
| `onsets.csv` | `muscle, series, rep, latency_s_from_rep_start, latency_ref` |
| `reference.json` | Método y valor de normalización por músculo |
| `summary_stages.csv` | Resumen por etapa (reposo, series, pausa, recuperación) |
| `stats_by_phase.csv` | Estadística descriptiva por músculo y fase |
| `timeline.png`, `boxplot_phases.png` | Figuras |

## 15. Día de medición (12 de noviembre en el protocolo; las sesiones reales se fijan en el calendario)

**Roles.** Operador de PC (lanzador y eventos), responsable de video, responsable de termografía, y la persona voluntaria. Con dos integrantes, uno hace video y termografía y el otro la PC; ensayar antes.

**Antes de empezar (checklist)**

- Baterías nuevas o cargadas (4 × 9 V); medir voltaje. **El circuito funciona solo con baterías.**
- **No conectar el USB de la placa a la PC durante la medición** de la persona (seguridad eléctrica); alimentar la placa de forma aislada. Confirmar con el docente.
- Consentimiento firmado.
- Piel preparada (limpia, rasurada si hace falta, alcohol) y electrodos según SENIAM (VL, BF; referencia en zona ósea).
- Hotspot encendido, PC conectada, firewall abierto, IP del nodo anotada.
- Celular en trípode, 120 fps, con espacio; cámara térmica cargada y con emisividad configurada.
- Metrónomo a 60 bpm; goniómetro para verificar los 90° de la sentadilla parcial.

**Secuencia**

1. PREP\_START. Conectar el sistema (SYSTEM\_CONNECTED), colocar electrodos (ELECTRODES\_PLACED), `ARM` y mirar el monitor en vivo; pedir una contracción breve para comprobar la señal (ACTIVITY\_OBSERVED).
2. Aclimatación y captura térmica T0 (ANT y POS).
3. Iniciar el video (VIDEO\_START), sincronía de reloj (CLOCK\_SYNC\_START) y EMG\_START.
4. **Sincronía:** 3 flexo-extensiones rápidas de rodilla (SYNC\_MOVE\_START/END).
5. Reposo de 60 s de pie e inmóvil (REST\_START/END); define la línea base.
6. Serie 1: 10 sentadillas al ritmo del metrónomo (SERIE\_1\_START/END).
7. Pausa: captura térmica T1 (PAUSA\_START/END).
8. Serie 2: 10 sentadillas (SERIE\_2\_START/END).
9. Recuperación en reposo (RECOVERY\_START/END).
10. EMG\_STOP, fin del video y captura T3.
11. Copiar la carpeta de la sesión, ejecutar el QC de inmediato; si es inaceptable, repetir antes de que la persona se vaya.

**Después.** Respaldar los datos (dos copias) y anotar incidentes en las notas de la sesión.

## 16. Riesgos y planes B

| Riesgo | Probabilidad / impacto | Se detecta con | Plan B |
| --- | --- | --- | --- |
| El firmware no compila o el ADC continuo no llega a 32 kHz | Media / alto | Fase 2, 20–21 nov | Bajar a `OS_FACTOR=4`; corregir API según el error; último recurso, ADC por temporizador sin DMA |
| Ruido del Wi-Fi o resolución del ADC insuficientes | Media / alto | Fase 1, go/no-go 22 nov | Grabar con Biopac u osciloscopio e importar con `import_external_csv.py`; el resto del pipeline no cambia |
| Pérdida de paquetes UDP | Media / medio | `loss_pct` y huecos en QC | Hotspot dedicado y cercano; repetir la grabación |
| MediaPipe falla con el video real (oclusión, ropa, luz) | Media / medio | Fase 4, 24 nov | Ajustar encuadre y ropa; segmentar con metrónomo o picos del EMG; último recurso, marcar el ángulo a mano con Kinovea |
| El celular no graba a 120 fps estables | Baja / medio | 21 nov | Usar 60 fps (suficiente para sentadillas a 60 bpm) |
| Exportación térmica ilegible | Media / medio | Fase 5 | Usar AnalyzIR y capturar valores de ROI a mano |
| Offset de electrodos satura el canal | Baja / alto | Fase 1, prueba 7 | Mejor preparación de piel; esperar a que se estabilice; revisar DRL |
| Aparece ruido eléctrico de red | Media / medio | `LINE` y QC | Alejar cables de la fuente de red, trenzar cables, tierra de la persona |
| Falta de tiempo | Alta / alto | Revisión diaria | Recortar: dashboard fuera, termografía más simple, análisis descriptivo mínimo |
| Sujeto no disponible el día de la sesión | Media / alto | 25 nov | Un integrante del equipo como voluntario; reservar días de respaldo |

## 17. Decisiones pendientes con fecha límite

| # | Decisión | Opciones | Fecha límite |
| --- | --- | --- | --- |
| 1 | Normalización | `mean_valid_peaks` (equipo) o `max_rms` (texto del protocolo); el código hace ambas | 24 nov |
| 2 | Definición de fases y metrónomo | 0–33 / 33–67 / 67–100 % de la repetición, con 60 bpm | 24 nov |
| 3 | Criterio de exclusión de repeticiones | Propuesta: inválida si hay saturación o hueco > 20 ms, flexión máxima a más de 15° de 90°, o duración a más de 30 % de la mediana | 24 nov |
| 4 | Modelo de celular y formato de exportación (fps reales) | Confirmar 120 fps | 21 nov |
| 5 | Alcance del consentimiento (publicación y retiro de datos) | Definir con el docente | 24 nov |
| 6 | Ventana de latencia de inicio | Dentro de cada repetición desde el inicio del movimiento | 24 nov |
| 7 | Pasa-altas del análisis | 20 Hz (propuesta) frente a 5 Hz del circuito | 24 nov |
| 8 | Banda declarada | 5–312 Hz (medida) o 5–300 Hz (texto) | 22 nov |
| 9 | Número y perfil de sujetos | 1 o 2 del equipo | 24 nov |
| 10 | Pulso de sincronía físico | Desactivado por defecto (`USE_SYNC_PULSE=0`) | Ya decidido |

Después de la sesión piloto (25 nov) **no se cambian métodos**.

## 18. Limitaciones que se declaran en el informe

- **Pocos sujetos** (1–2, del equipo): resultados descriptivos, sin generalización ni inferencia.
- **Filtro notch a 60 Hz:** quita energía del EMG cerca de 60 Hz y sesga la frecuencia mediana; la MDF se interpreta con cautela.
- **Ángulo 2D por video:** depende de la perpendicularidad de la cámara y de la detección de puntos; error esperado de varios grados.
- **Normalización por picos de la propia sesión:** no es una contracción voluntaria máxima; los porcentajes son relativos a la sesión y no comparables entre personas.
- **Temperatura de piel ≠ temperatura muscular;** responde con retraso y depende del ambiente.
- **ADC interno de 12 bits (≈ 8 µV por cuenta):** limita la resolución en reposo y añade ruido de cuantización.
- **Sincronía video–EMG por movimiento,** no por pulso eléctrico: precisión de decenas de milisegundos.
- **Firmware y módulo de video** validados con poca evidencia real; se informa lo que se verificó.

## 19. Insumos necesarios y siguiente paso inmediato

**Lo que se necesita del equipo**

- Errores de compilación del firmware (texto completo) y lo que imprime el monitor serie.
- Archivos de ruido, barrido y ADC de la Fase 1 (CSV y capturas del osciloscopio).
- Un video corto de sentadilla (10 s) y el `debug.mp4` generado.
- Una imagen térmica de muestra y su CSV exportado, más si la H6 graba secuencias.
- Respuestas a las decisiones de la sección 17.

**Lo que se escribe a continuación.** `tools/capture_raw.py`; el módulo de termografía (cuando llegue la muestra); `analysis/make_report.py`; `hardware/notes_calibration.md`.

**Siguiente paso inmediato (20 de noviembre).** Compilar el firmware y cargar la placa; enviar el resultado del compilador.
