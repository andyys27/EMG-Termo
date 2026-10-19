# Firmware del nodo EMG (ESP32-S3 SuperMini + ADS131M04)

C++ con PlatformIO (Arduino-ESP32 3.x / ESP-IDF 5.x vía *pioarduino*) y FreeRTOS.

## Estado de los módulos

| Archivo | Estado |
|---|---|
| `include/config.h`, `pins.h`, `protocol.h` | Listos (revisar pines contra su SuperMini). Protocolo **versión 2** (muestras `int32`) |
| `src/adc_source.h` | Interfaz del ADC |
| `src/ads131m04_defs.h` | Constantes y funciones puras del ADC. **Verificadas en PC** |
| `src/ads131m04.*` | Driver SPI + CLKIN (LEDC) + DRDY. Pasa revisión de sintaxis con stubs; **no compilado ni probado en el ESP32-S3** |
| `src/sampler.*` | Tarea de muestreo (bloques, secuencia, timestamps). Revisión de sintaxis con stubs |
| `src/packetizer.*`, `sample_block.h` | Listos y verificados en PC (CRC y trama contra Python) |
| `src/main.cpp` | Modo `bringup` listo; modo `emg_node` pendiente |
| `net_wifi`, `command_handler`, `timesync`, `monitor` | Pendientes |
| `legacy_internal_adc/` | Plan A descartado (ADC interno); no se compila |

## Cableado ADS131M04 ↔ ESP32-S3 (según `include/pins.h`; verificar con su SuperMini)

| ADS131M04 | ESP32-S3 | Nota |
|---|---|---|
| SCLK | GPIO12 | SPI modo 1 (CPOL=0, CPHA=1), 8 MHz |
| DOUT | GPIO13 (MISO) | |
| DIN | GPIO11 (MOSI) | |
| /CS | GPIO10 | |
| /DRDY | GPIO7 | interrupción por flanco de bajada |
| /SYNC/RESET | GPIO8 | pulso en bajo = reinicio |
| CLKIN | GPIO6 | 8.192 MHz (LEDC) o salida de un oscilador externo |
| AVDD / DVDD | 3.3 V | **LDO analógico separado** y desacoplo en cada pin de alimentación |
| GND | GND | un solo punto de unión entre tierra analógica y digital |

Evitar los pines GPIO0, 3, 45, 46 (arranque) y GPIO19, 20 (USB).

## Bring-up (primera prueba, sin Wi-Fi)

```
cp include/secrets.h.example include/secrets.h
pio run -e bringup -t upload
pio device monitor
```

1. Al arrancar se imprimen líneas `# ADS:`: frecuencia real de CLKIN y fs, el registro ID (debe indicar 4 canales) y, para CLOCK y GAIN, el valor escrito, el valor leído y `ok`/`DISCREPANCIA`. **Cualquier discrepancia significa que el mapa de bits del registro no coincide con el datasheet**: corregir en `ads131m04_defs.h`.
2. Cada segundo se imprime: bloques (esperado ≈ 200), huecos de `seq`, descartados, DRDY perdidos, errores de ADC, bloques saturados y, por canal, media y desviación estándar en cuentas y en µV referidos a la entrada del amplificador.
3. **Piso de ruido del ADC:** enviar `s` por el monitor (entradas del ADC en corto internamente). La desviación estándar es el ruido del ADC solo. Enviar `n` para volver a las entradas normales.
4. **Ruido del sistema:** con los electrodos en corto en el front-end (sin sujeto), comparar contra el punto 3.
5. Con Wi-Fi activo (módulo de red pendiente), repetir y comparar.
6. `d` activa/desactiva un volcado CSV (`t_us,ch1,ch2`) para graficar en la PC.

## Puntos a verificar contra el datasheet durante el bring-up

- Mapa de bits de CLOCK y GAIN y direcciones de `CHn_CFG` (el driver relee lo que escribe).
- Bit `XTAL_DIS` al alimentar CLKIN con un reloj CMOS (`cfg::ADS_XTAL_DIS`).
- Rango de entrada diferencial y modo común admitido, para diseñar el nivel de bias del front-end.
- Cálculo del CRC de la trama de salida (`cfg::ADS_VERIFY_CRC` parte en `false`; activarlo cuando `frameCrcOk()` coincida con tramas reales).
- **Jitter de CLKIN generado por LEDC** (divisor fraccionario de 80 MHz). Si el ruido empeora frente a un reloj limpio, usar un oscilador externo de 8.192 MHz y poner `cfg::ADS_CLKIN_EXTERNAL = true` (y `pins::ADS_CLKIN = -1`).

## Pruebas en PC (sin ESP32)

```
bash test/host/run_host_tests.sh
```
Verifica el CRC16 (vector `123456789` → `0x29B1`), el tamaño y el CRC de la trama, el mapa de bits de CLOCK frente al valor por defecto del datasheet (`0x0F0E`), los comandos RREG/WREG y la extensión de signo de 24 bit. El volcado `/tmp/frame.bin` se decodifica en Python con `struct.unpack("<2sBBIqHBB", ...)` y las muestras como `int32`.

## Notas
- `fs = CLKIN / (2·OSR)`; con 8.192 MHz y OSR 2048 son **2000 Hz exactos**.
- Si se pierde algún DRDY, el driver rellena con la última muestra (la línea de tiempo no se comprime) y marca el bloque con `FLAG_OVERRUN`; el análisis debe tratar esos bloques como inválidos.
- La conversión a µV en la entrada del amplificador es `cuentas · lsb_uv_at_adc / ganancia_analógica_real`.
- Los parámetros de `config.h` deben coincidir con `config/devices.yaml`.
