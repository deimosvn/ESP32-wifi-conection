# ESP32 Captive Portal & Network Scanner

Portal cautivo con interfaz web y escáner de red local usando ESP32 en modo **STA+AP** (punto de acceso + cliente WiFi simultáneamente).

## Características

- **Captive Portal** — Página web automática al conectarse al AP del ESP32
- **Detección de dispositivos** — Identifica fabricante, SO, navegador y modelo de cada cliente conectado
- **Escáner de red** — Descubre dispositivos en tu red WiFi local mediante ARP
- **Fingerprinting del navegador** — Recopila información del hardware vía JavaScript (GPU, pantalla, batería, sensores)
- **Dashboard en tiempo real** — Estadísticas del ESP32, clientes AP y dispositivos de red
- **Detección de captive portal** — Compatible con Android, iOS, Windows y Firefox

## Hardware

- ESP32 (probado con ESP32-D0WD-V3)
- Cable USB (CP2102 / CH340)

## Instalación

### Requisitos

- [PlatformIO](https://platformio.org/) (CLI o extensión de VS Code)

### Clonar y compilar

```bash
git clone https://github.com/deimosvn/ESP32-wifi-conection.git
cd ESP32-wifi-conection
```

### Configurar WiFi

Edita `src/main.cpp` y cambia las credenciales de tu red:

```cpp
const char *STA_SSID = "TU_RED_WIFI";
const char *STA_PASS = "TU_CONTRASEÑA";
```

### Subir al ESP32

```bash
# Compilar y subir firmware
pio run --target upload

# Subir archivos web (SPIFFS)
pio run --target uploadfs
```

## Uso

1. El ESP32 crea la red WiFi **ESP32-Portal** (abierta)
2. Conéctate desde cualquier dispositivo
3. Se abre automáticamente el portal web (o visita `192.168.4.1`)
4. El dashboard muestra:
   - Información de tu dispositivo (fingerprint del navegador)
   - Clientes conectados al AP con señal RSSI y fabricante
   - Dispositivos encontrados en tu red local

## Estructura del proyecto

```
├── src/
│   └── main.cpp          # Firmware principal
├── data/
│   ├── index.html        # Dashboard principal
│   └── info.html         # Info detallada del ESP32
└── platformio.ini        # Configuración PlatformIO
```

## Endpoints API

| Endpoint | Método | Descripción |
|---|---|---|
| `/api/stats` | GET | Estadísticas en tiempo real |
| `/api/info` | GET | Info detallada del chip |
| `/api/clients` | GET | Clientes conectados al AP |
| `/api/clientdata` | POST | Recibe fingerprint del navegador |
| `/api/network` | GET | Dispositivos en la red local |
| `/api/scan` | GET | Forzar escaneo de red |

## Tecnologías

- **Framework:** Arduino (ESP-IDF)
- **Servidor web:** ESPAsyncWebServer
- **Filesystem:** SPIFFS
- **Build system:** PlatformIO

## Licencia

MIT
