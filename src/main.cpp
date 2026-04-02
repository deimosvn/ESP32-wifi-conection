#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <SPIFFS.h>
#include <esp_wifi.h>
#include <lwip/etharp.h>
#include <lwip/inet.h>

// --- Configuracion de la red AP (portal) ---
const char *AP_SSID = "ESP32-Portal";
const char *AP_PASS = NULL; // Red abierta

// --- Configuracion de tu red WiFi (STA) ---
const char *STA_SSID = "MARTINEZ CRUZ";
const char *STA_PASS = "chocolate06";

// DNS y servidor web
DNSServer dnsServer;
AsyncWebServer server(80);

// Estructura para guardar info de clientes conectados
struct ClientInfo {
    uint8_t mac[6];
    esp_ip4_addr_t ip;
    int8_t rssi;
    unsigned long connectTime;
    String userAgent;
    String acceptLang;
    String host;
};

#define MAX_CLIENTS 10
ClientInfo clientList[MAX_CLIENTS];
int clientCount = 0;

// Para calcular uptime legible
String getUptime() {
    unsigned long sec = millis() / 1000;
    unsigned long d = sec / 86400;
    sec %= 86400;
    unsigned long h = sec / 3600;
    sec %= 3600;
    unsigned long m = sec / 60;
    sec %= 60;
    String s = "";
    if (d > 0) s += String(d) + "d ";
    if (h > 0) s += String(h) + "h ";
    if (m > 0) s += String(m) + "m ";
    s += String(sec) + "s";
    return s;
}

String macToString(const uint8_t *mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

// Buscar o agregar cliente por MAC
int findOrAddClient(const uint8_t *mac) {
    for (int i = 0; i < clientCount; i++) {
        if (memcmp(clientList[i].mac, mac, 6) == 0) return i;
    }
    if (clientCount < MAX_CLIENTS) {
        memcpy(clientList[clientCount].mac, mac, 6);
        clientList[clientCount].connectTime = millis();
        clientList[clientCount].rssi = 0;
        clientList[clientCount].userAgent = "";
        clientList[clientCount].acceptLang = "";
        clientList[clientCount].host = "";
        return clientCount++;
    }
    return -1;
}

// Obtener RSSI de un cliente por MAC
int8_t getClientRSSI(const uint8_t *mac) {
    wifi_sta_list_t staList;
    esp_wifi_ap_get_sta_list(&staList);
    for (int i = 0; i < staList.num; i++) {
        if (memcmp(staList.sta[i].mac, mac, 6) == 0) {
            return staList.sta[i].rssi;
        }
    }
    return 0;
}

// Estimar distancia por RSSI (muy aproximado)
String estimateDistance(int8_t rssi) {
    if (rssi == 0) return "N/A";
    // Path loss model: d = 10^((|RSSI| - A) / (10 * n))
    // A = RSSI a 1 metro (~-40), n = path loss exponent (~2.5 indoor)
    double dist = pow(10.0, ((-40.0) - rssi) / (10.0 * 2.5));
    if (dist < 1.0) return "<1m";
    if (dist > 30.0) return ">30m";
    return String(dist, 1) + "m";
}

// Deducir fabricante por OUI (primeros 3 bytes MAC)
String getVendorFromMAC(const uint8_t *mac) {
    // OUI database parcial - fabricantes comunes
    uint32_t oui = ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | mac[2];
    switch (oui) {
        // Apple
        case 0x3C0754: case 0xA4B197: case 0x28CF0A: case 0xF0B479:
        case 0xDCA4CA: case 0x80E650: case 0xA860B6: case 0xC869CD:
        case 0xF4F951: case 0x1C36BB: case 0x843835: case 0xD0A637:
        case 0xF0DCE2: case 0xB065BD: case 0x14BD61: case 0xE49ADC:
            return "Apple";
        // Samsung
        case 0x00216A: case 0x14568E: case 0x24DB96: case 0x30CBF8:
        case 0x4CE676: case 0x5C3A45: case 0x84119E: case 0x94350A:
        case 0xA0CBFD: case 0xBC4486: case 0xCC3A61: case 0xD8E0E1:
        case 0xF05B7B: case 0xF80CF3: case 0x002567: case 0x6C2F2C:
            return "Samsung";
        // Xiaomi
        case 0x286C07: case 0x640980: case 0x7C49EB: case 0x9C9D7E:
        case 0xB0E235: case 0xD4970B: case 0xFC64BA: case 0x58A2B5:
        case 0x74237F: case 0x8C882B: case 0xA086C6: case 0x2C574B:
            return "Xiaomi";
        // Huawei
        case 0x000FE2: case 0x002568: case 0x049F06: case 0x0C37DC:
        case 0x10B1F8: case 0x20A6CD: case 0x30D17E: case 0x48AD08:
        case 0x5C7D5E: case 0x70728B: case 0x881DFC: case 0xACCF85:
        case 0xC8D15E: case 0xE0247F: case 0xF44C7F: case 0xFC48EF:
            return "Huawei";
        // Google
        case 0x3C5AB4: case 0xF4F5D8: case 0xA47733: case 0x54608D:
            return "Google";
        // OnePlus/OPPO
        case 0x94652D: case 0xC0EEB8: case 0xDC6FBE: case 0x6C5C3D:
            return "OnePlus/OPPO";
        // Motorola
        case 0x001A66: case 0x00247C: case 0x40786A: case 0x68C44D:
        case 0xE4907E: case 0xF8F1B6:
            return "Motorola";
        // LG
        case 0x000631: case 0x0019A1: case 0x10F96F: case 0x30766F:
        case 0x58A2B4: case 0x88C9D0: case 0xA8B86E: case 0xCC2D8C:
            return "LG";
        // Sony
        case 0x001315: case 0x0019C1: case 0x04764E: case 0x283F69:
        case 0x78843C: case 0xB4527E: case 0xFC0FE6:
            return "Sony";
        // Intel (laptops/PC)
        case 0x001E64: case 0x001E65: case 0x001F3A: case 0x3C970E:
        case 0x5CE0C5: case 0x685D43: case 0x7C5CF8: case 0x84A6C8:
        case 0x9442C5: case 0xA44E31: case 0xB4D5BD: case 0xDC536C:
            return "Intel";
        // Realtek
        case 0x000AEB: case 0x001CFE: case 0x00E04C: case 0x48027E:
        case 0x506583: case 0x7CC2C6: case 0x9CE33F:
            return "Realtek";
        // Qualcomm
        case 0x001832: case 0x04D6AA: case 0x242FD0: case 0x708BCD:
            return "Qualcomm";
        // MediaTek
        case 0x000CE7: case 0x001972: case 0x008E73: case 0x24114E:
            return "MediaTek";
        default:
            return "Desconocido";
    }
}

// Detectar tipo de dispositivo por User-Agent
String detectDeviceType(const String &ua) {
    if (ua.length() == 0) return "Desconocido";
    String lower = ua;
    lower.toLowerCase();
    if (lower.indexOf("iphone") >= 0) return "iPhone";
    if (lower.indexOf("ipad") >= 0) return "iPad";
    if (lower.indexOf("macintosh") >= 0) return "Mac";
    if (lower.indexOf("android") >= 0) {
        if (lower.indexOf("mobile") >= 0) return "Android Phone";
        return "Android Tablet";
    }
    if (lower.indexOf("windows phone") >= 0) return "Windows Phone";
    if (lower.indexOf("windows") >= 0) return "Windows PC";
    if (lower.indexOf("linux") >= 0) return "Linux PC";
    if (lower.indexOf("cros") >= 0) return "Chromebook";
    return "Otro";
}

// Detectar navegador por User-Agent
String detectBrowser(const String &ua) {
    if (ua.length() == 0) return "Desconocido";
    if (ua.indexOf("Edg/") >= 0) return "Edge";
    if (ua.indexOf("OPR/") >= 0 || ua.indexOf("Opera") >= 0) return "Opera";
    if (ua.indexOf("Chrome/") >= 0) return "Chrome";
    if (ua.indexOf("Safari/") >= 0 && ua.indexOf("Chrome") < 0) return "Safari";
    if (ua.indexOf("Firefox/") >= 0) return "Firefox";
    if (ua.indexOf("CaptiveNetworkSupport") >= 0) return "Captive Portal (iOS)";
    if (ua.indexOf("Microsoft NCSI") >= 0) return "NCSI (Windows)";
    if (ua.indexOf("dalvik") >= 0) return "Android System";
    return "Otro";
}

// Detectar OS por User-Agent
String detectOS(const String &ua) {
    if (ua.length() == 0) return "Desconocido";
    String lower = ua;
    lower.toLowerCase();
    if (lower.indexOf("iphone os") >= 0 || lower.indexOf("cpu os") >= 0) {
        // Extraer version de iOS
        int idx = ua.indexOf("OS ");
        if (idx >= 0) {
            int end = ua.indexOf(" ", idx + 3);
            if (end < 0) end = ua.indexOf(")", idx + 3);
            if (end > idx + 3) {
                String ver = ua.substring(idx + 3, end);
                ver.replace("_", ".");
                return "iOS " + ver;
            }
        }
        return "iOS";
    }
    if (lower.indexOf("android") >= 0) {
        int idx = lower.indexOf("android ");
        if (idx >= 0) {
            int end = ua.indexOf(";", idx + 8);
            if (end < 0) end = ua.indexOf(")", idx + 8);
            if (end > idx + 8) return "Android " + ua.substring(idx + 8, end);
        }
        return "Android";
    }
    if (lower.indexOf("windows nt 10") >= 0) return "Windows 10/11";
    if (lower.indexOf("windows nt 6.3") >= 0) return "Windows 8.1";
    if (lower.indexOf("windows nt 6.1") >= 0) return "Windows 7";
    if (lower.indexOf("mac os x") >= 0) return "macOS";
    if (lower.indexOf("cros") >= 0) return "Chrome OS";
    if (lower.indexOf("linux") >= 0) return "Linux";
    return "Desconocido";
}

// Extraer modelo de dispositivo de User-Agent (Android)
String extractDeviceModel(const String &ua) {
    // Android UA format: ... Android X.X; MODEL Build/...
    int idx = ua.indexOf("; ");
    if (idx < 0) return "";
    // Buscar despues de "Android X.X; "
    int androidIdx = ua.indexOf("Android");
    if (androidIdx >= 0) {
        int semicolon = ua.indexOf(";", androidIdx);
        if (semicolon >= 0) {
            int buildIdx = ua.indexOf(" Build/", semicolon);
            if (buildIdx > semicolon + 2) {
                return ua.substring(semicolon + 2, buildIdx);
            }
            int parenIdx = ua.indexOf(")", semicolon);
            if (parenIdx > semicolon + 2) {
                return ua.substring(semicolon + 2, parenIdx);
            }
        }
    }
    return "";
}

// Clase para manejar las peticiones del captive portal
class CaptiveRequestHandler : public AsyncWebHandler {
public:
    CaptiveRequestHandler() {}
    virtual ~CaptiveRequestHandler() {}

    bool canHandle(AsyncWebServerRequest *request) {
        String url = request->url();
        if (url == "/" || url == "/info" || url.startsWith("/api/")) {
            return false;
        }
        return true;
    }

    void handleRequest(AsyncWebServerRequest *request) {
        // Capturar headers del cliente antes de redirigir
        IPAddress clientIP = request->client()->remoteIP();
        wifi_sta_list_t staList;
        esp_wifi_ap_get_sta_list(&staList);
        for (int i = 0; i < staList.num; i++) {
            int idx = findOrAddClient(staList.sta[i].mac);
            if (idx >= 0) {
                clientList[idx].rssi = staList.sta[i].rssi;
                if (request->hasHeader("User-Agent"))
                    clientList[idx].userAgent = request->header("User-Agent");
                if (request->hasHeader("Accept-Language"))
                    clientList[idx].acceptLang = request->header("Accept-Language");
                if (request->hasHeader("Host"))
                    clientList[idx].host = request->header("Host");
            }
        }
        // Enviar pagina directamente en vez de redirect
        // Esto funciona mejor en algunos dispositivos
        request->send(200, "text/html",
            "<!DOCTYPE html><html><head>"
            "<meta http-equiv='refresh' content='0; url=http://192.168.4.1/'>"
            "</head><body><script>window.location.href='http://192.168.4.1/';</script>"
            "<a href='http://192.168.4.1/'>Toca aqui para abrir el portal</a>"
            "</body></html>");
    }
};

// Middleware para capturar headers de cada request
void captureClientHeaders(AsyncWebServerRequest *request) {
    wifi_sta_list_t staList;
    esp_wifi_ap_get_sta_list(&staList);
    for (int i = 0; i < staList.num; i++) {
        int idx = findOrAddClient(staList.sta[i].mac);
        if (idx >= 0) {
            clientList[idx].rssi = staList.sta[i].rssi;
            if (request->hasHeader("User-Agent") && clientList[idx].userAgent.length() == 0)
                clientList[idx].userAgent = request->header("User-Agent");
            if (request->hasHeader("Accept-Language") && clientList[idx].acceptLang.length() == 0)
                clientList[idx].acceptLang = request->header("Accept-Language");
            if (request->hasHeader("Host") && clientList[idx].host.length() == 0)
                clientList[idx].host = request->header("Host");
        }
    }
}

// Actualizar lista de clientes con info de bajo nivel
void updateClientList() {
    wifi_sta_list_t staList;
    tcpip_adapter_sta_list_t tcpList;
    esp_wifi_ap_get_sta_list(&staList);
    tcpip_adapter_get_sta_list(&staList, &tcpList);

    for (int i = 0; i < tcpList.num; i++) {
        int idx = findOrAddClient(tcpList.sta[i].mac);
        if (idx >= 0) {
            clientList[idx].ip = tcpList.sta[i].ip;
            // Actualizar RSSI
            for (int j = 0; j < staList.num; j++) {
                if (memcmp(staList.sta[j].mac, tcpList.sta[i].mac, 6) == 0) {
                    clientList[idx].rssi = staList.sta[j].rssi;
                    break;
                }
            }
        }
    }
}

// Escape JSON string
String jsonEscape(const String &s) {
    String out = s;
    out.replace("\\", "\\\\");
    out.replace("\"", "\\\"");
    out.replace("\n", "\\n");
    out.replace("\r", "\\r");
    out.replace("\t", "\\t");
    return out;
}

// === NETWORK SCANNER ===
// Estructura para dispositivos encontrados en la red local
struct NetDevice {
    IPAddress ip;
    uint8_t mac[6];
    bool active;
    unsigned long lastSeen;
    String vendor;
};

#define MAX_NET_DEVICES 50
NetDevice netDevices[MAX_NET_DEVICES];
int netDeviceCount = 0;
bool scanInProgress = false;
unsigned long lastScanTime = 0;

// ARP ping: intenta conectar por TCP a un puerto comun para forzar entrada ARP
bool arpPing(IPAddress ip) {
    WiFiClient client;
    client.setTimeout(80);
    bool result = client.connect(ip, 80, 80);
    client.stop();
    return result;
}

// Obtener MAC de la tabla ARP del ESP32
bool getARPMac(IPAddress ip, uint8_t *mac) {
    // Usa la API de lwIP para buscar en la tabla ARP
    ip4_addr_t ipAddr;
    ipAddr.addr = (uint32_t)ip;

    struct eth_addr *ethAddr = NULL;
    ip4_addr_t *ipAddrRet = NULL;

    // Buscar en tabla ARP
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (etharp_get_entry(i, &ipAddrRet, NULL, &ethAddr) == 0) {
            if (ipAddrRet && ipAddrRet->addr == ipAddr.addr && ethAddr) {
                memcpy(mac, ethAddr->addr, 6);
                return true;
            }
        }
    }
    return false;
}

// Escanear la red local buscando dispositivos activos
void scanNetwork() {
    if (WiFi.status() != WL_CONNECTED || scanInProgress) return;
    scanInProgress = true;

    IPAddress localIP = WiFi.localIP();
    IPAddress subnet = WiFi.subnetMask();
    IPAddress gateway = WiFi.gatewayIP();

    Serial.println("[SCAN] Escaneando red " + String(STA_SSID) + "...");

    // Calcular rango de red
    uint32_t netAddr = (uint32_t)localIP & (uint32_t)subnet;
    uint32_t broadcast = netAddr | ~((uint32_t)subnet);

    // Marcar todos como inactivos
    for (int i = 0; i < netDeviceCount; i++) {
        netDevices[i].active = false;
    }

    // Escanear IPs en el rango
    int found = 0;
    for (uint32_t addr = netAddr + 1; addr < broadcast && found < MAX_NET_DEVICES; addr++) {
        IPAddress targetIP(
            (addr) & 0xFF,
            (addr >> 8) & 0xFF,
            (addr >> 16) & 0xFF,
            (addr >> 24) & 0xFF
        );

        // Intentar conexion para forzar ARP
        arpPing(targetIP);

        uint8_t mac[6] = {0};
        if (getARPMac(targetIP, mac)) {
            // Buscar si ya existe
            int idx = -1;
            for (int i = 0; i < netDeviceCount; i++) {
                if (netDevices[i].ip == targetIP) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0 && netDeviceCount < MAX_NET_DEVICES) {
                idx = netDeviceCount++;
            }
            if (idx >= 0) {
                netDevices[idx].ip = targetIP;
                memcpy(netDevices[idx].mac, mac, 6);
                netDevices[idx].active = true;
                netDevices[idx].lastSeen = millis();
                netDevices[idx].vendor = getVendorFromMAC(mac);
                found++;
            }
        }
        yield(); // Evitar watchdog
    }

    Serial.printf("[SCAN] Encontrados %d dispositivos\n", found);
    scanInProgress = false;
    lastScanTime = millis();
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ESP32 Captive Portal + Network Scanner ===");

    // Iniciar SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("Error montando SPIFFS");
        return;
    }
    Serial.println("SPIFFS montado correctamente");

    // Modo STA+AP: se conecta a tu WiFi Y crea su propia red
    WiFi.mode(WIFI_AP_STA);

    // Iniciar AP
    WiFi.softAP(AP_SSID, AP_PASS, 1, 0, MAX_CLIENTS);
    delay(100);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("AP SSID: ");
    Serial.println(AP_SSID);

    // Conectar a tu red WiFi
    Serial.print("Conectando a ");
    Serial.print(STA_SSID);
    WiFi.begin(STA_SSID, STA_PASS);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConectado a " + String(STA_SSID));
        Serial.print("STA IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("Gateway: ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("RSSI: ");
        Serial.println(WiFi.RSSI());
    } else {
        Serial.println("\nNo se pudo conectar a " + String(STA_SSID));
    }

    IPAddress apIP = WiFi.softAPIP();

    // DNS server - redirige todo a nuestra IP (solo para el AP)
    dnsServer.start(53, "*", apIP);

    // --- Rutas del servidor web ---

    // Pagina principal
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->send(SPIFFS, "/index.html", "text/html");
    });

    // Pagina de info
    server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->send(SPIFFS, "/info.html", "text/html");
    });

    // API: estadisticas en tiempo real del ESP32
    server.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        updateClientList();
        String json = "{";
        json += "\"uptime\":\"" + getUptime() + "\",";
        json += "\"freeHeap\":\"" + String(ESP.getFreeHeap() / 1024) + " KB\",";
        json += "\"minFreeHeap\":\"" + String(ESP.getMinFreeHeap() / 1024) + " KB\",";
        json += "\"heapSize\":\"" + String(ESP.getHeapSize() / 1024) + " KB\",";
        json += "\"clients\":" + String(WiFi.softAPgetStationNum()) + ",";
        json += "\"chipTemp\":\"" + String(temperatureRead(), 1) + " C\",";
        json += "\"cpuFreq\":\"" + String(ESP.getCpuFreqMHz()) + " MHz\",";
        json += "\"sdkVersion\":\"" + String(ESP.getSdkVersion()) + "\",";
        // WiFi STA info
        json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
        json += "\"wifiSSID\":\"" + jsonEscape(String(STA_SSID)) + "\",";
        if (WiFi.status() == WL_CONNECTED) {
            json += "\"wifiIP\":\"" + WiFi.localIP().toString() + "\",";
            json += "\"wifiGateway\":\"" + WiFi.gatewayIP().toString() + "\",";
            json += "\"wifiRSSI\":" + String(WiFi.RSSI()) + ",";
            json += "\"wifiChannel\":" + String(WiFi.channel()) + ",";
            json += "\"wifiDNS\":\"" + WiFi.dnsIP().toString() + "\",";
            json += "\"netDevices\":" + String(netDeviceCount);
        } else {
            json += "\"wifiIP\":\"\",";
            json += "\"wifiGateway\":\"\",";
            json += "\"wifiRSSI\":0,";
            json += "\"wifiChannel\":0,";
            json += "\"wifiDNS\":\"\",";
            json += "\"netDevices\":0";
        }
        json += "}";
        request->send(200, "application/json", json);
    });

    // API: info detallada del chip ESP32
    server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        String json = "{";
        json += "\"Chip Model\":\"" + String(ESP.getChipModel()) + "\",";
        json += "\"Chip Revision\":" + String(ESP.getChipRevision()) + ",";
        json += "\"CPU Cores\":" + String(ESP.getChipCores()) + ",";
        json += "\"CPU Freq\":\"" + String(ESP.getCpuFreqMHz()) + " MHz\",";
        json += "\"Flash Size\":\"" + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB\",";
        json += "\"Flash Speed\":\"" + String(ESP.getFlashChipSpeed() / 1000000) + " MHz\",";
        json += "\"PSRAM Size\":\"" + String(ESP.getPsramSize() / 1024) + " KB\",";
        json += "\"Free PSRAM\":\"" + String(ESP.getFreePsram() / 1024) + " KB\",";
        json += "\"Heap Size\":\"" + String(ESP.getHeapSize() / 1024) + " KB\",";
        json += "\"Free Heap\":\"" + String(ESP.getFreeHeap() / 1024) + " KB\",";
        json += "\"Min Free Heap\":\"" + String(ESP.getMinFreeHeap() / 1024) + " KB\",";
        json += "\"Sketch Size\":\"" + String(ESP.getSketchSize() / 1024) + " KB\",";
        json += "\"Free Sketch\":\"" + String(ESP.getFreeSketchSpace() / 1024) + " KB\",";
        json += "\"SPIFFS Total\":\"" + String(SPIFFS.totalBytes() / 1024) + " KB\",";
        json += "\"SPIFFS Used\":\"" + String(SPIFFS.usedBytes() / 1024) + " KB\",";
        json += "\"Chip Temp\":\"" + String(temperatureRead(), 1) + " C\",";
        json += "\"SDK Version\":\"" + String(ESP.getSdkVersion()) + "\",";
        json += "\"AP IP\":\"" + WiFi.softAPIP().toString() + "\",";
        json += "\"AP MAC\":\"" + WiFi.softAPmacAddress() + "\",";
        json += "\"AP SSID\":\"" + String(AP_SSID) + "\",";
        json += "\"Clients\":" + String(WiFi.softAPgetStationNum()) + ",";
        // WiFi STA info
        json += "\"STA SSID\":\"" + jsonEscape(String(STA_SSID)) + "\",";
        json += "\"STA Status\":\"" + String(WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado") + "\",";
        if (WiFi.status() == WL_CONNECTED) {
            json += "\"STA IP\":\"" + WiFi.localIP().toString() + "\",";
            json += "\"STA Gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
            json += "\"STA DNS\":\"" + WiFi.dnsIP().toString() + "\",";
            json += "\"STA MAC\":\"" + WiFi.macAddress() + "\",";
            json += "\"STA RSSI\":\"" + String(WiFi.RSSI()) + " dBm\",";
            json += "\"STA Channel\":\"" + String(WiFi.channel()) + "\",";
            json += "\"STA BSSID\":\"" + WiFi.BSSIDstr() + "\",";
        } else {
            json += "\"STA IP\":\"-\",";
            json += "\"STA Gateway\":\"-\",";
            json += "\"STA DNS\":\"-\",";
            json += "\"STA MAC\":\"" + WiFi.macAddress() + "\",";
            json += "\"STA RSSI\":\"-\",";
            json += "\"STA Channel\":\"-\",";
            json += "\"STA BSSID\":\"-\",";
        }
        json += "\"Uptime\":\"" + getUptime() + "\"";
        json += "}";
        request->send(200, "application/json", json);
    });

    // API: lista de clientes conectados con toda su info
    server.on("/api/clients", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        updateClientList();

        String json = "[";
        wifi_sta_list_t staList;
        tcpip_adapter_sta_list_t tcpList;
        esp_wifi_ap_get_sta_list(&staList);
        tcpip_adapter_get_sta_list(&staList, &tcpList);

        for (int i = 0; i < tcpList.num; i++) {
            if (i > 0) json += ",";
            int idx = findOrAddClient(tcpList.sta[i].mac);
            String mac = macToString(tcpList.sta[i].mac);
            char ipStr[16];
            snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
                IP2STR(&tcpList.sta[i].ip));

            json += "{";
            json += "\"mac\":\"" + mac + "\",";
            json += "\"ip\":\"" + String(ipStr) + "\",";
            json += "\"rssi\":" + String(staList.sta[i].rssi) + ",";
            json += "\"distance\":\"" + estimateDistance(staList.sta[i].rssi) + "\",";
            json += "\"vendor\":\"" + getVendorFromMAC(tcpList.sta[i].mac) + "\",";

            if (idx >= 0) {
                String ua = clientList[idx].userAgent;
                json += "\"deviceType\":\"" + detectDeviceType(ua) + "\",";
                json += "\"browser\":\"" + detectBrowser(ua) + "\",";
                json += "\"os\":\"" + detectOS(ua) + "\",";
                json += "\"model\":\"" + jsonEscape(extractDeviceModel(ua)) + "\",";
                json += "\"lang\":\"" + jsonEscape(clientList[idx].acceptLang) + "\",";
                json += "\"userAgent\":\"" + jsonEscape(ua) + "\",";
                unsigned long connSec = (millis() - clientList[idx].connectTime) / 1000;
                json += "\"connectedFor\":\"" + String(connSec) + "s\"";
            } else {
                json += "\"deviceType\":\"Desconocido\",";
                json += "\"browser\":\"Desconocido\",";
                json += "\"os\":\"Desconocido\",";
                json += "\"model\":\"\",";
                json += "\"lang\":\"\",";
                json += "\"userAgent\":\"\",";
                json += "\"connectedFor\":\"0s\"";
            }
            json += "}";
        }
        json += "]";
        request->send(200, "application/json", json);
    });

    // API: recibir datos del navegador del cliente (JavaScript)
    server.on("/api/clientdata", HTTP_POST, [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        // Solo logear los datos recibidos
        String body = "";
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        Serial.println("Client data received: " + body);
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // API: dispositivos encontrados en la red WiFi local
    server.on("/api/network", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{";
        json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
        json += "\"ssid\":\"" + jsonEscape(String(STA_SSID)) + "\",";
        json += "\"scanning\":" + String(scanInProgress ? "true" : "false") + ",";
        if (WiFi.status() == WL_CONNECTED) {
            json += "\"localIP\":\"" + WiFi.localIP().toString() + "\",";
            json += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
            json += "\"subnet\":\"" + WiFi.subnetMask().toString() + "\",";
            json += "\"dns\":\"" + WiFi.dnsIP().toString() + "\",";
            json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
            json += "\"channel\":" + String(WiFi.channel()) + ",";
            json += "\"bssid\":\"" + WiFi.BSSIDstr() + "\",";
            json += "\"staMac\":\"" + WiFi.macAddress() + "\",";
        }
        json += "\"devices\":[";
        bool first = true;
        for (int i = 0; i < netDeviceCount; i++) {
            if (!netDevices[i].active) continue;
            if (!first) json += ",";
            first = false;
            json += "{";
            json += "\"ip\":\"" + netDevices[i].ip.toString() + "\",";
            json += "\"mac\":\"" + macToString(netDevices[i].mac) + "\",";
            json += "\"vendor\":\"" + netDevices[i].vendor + "\",";
            unsigned long ago = (millis() - netDevices[i].lastSeen) / 1000;
            json += "\"lastSeen\":\"" + String(ago) + "s ago\",";
            // Marcar si es el gateway o el propio ESP32
            String role = "";
            if (WiFi.status() == WL_CONNECTED) {
                if (netDevices[i].ip == WiFi.gatewayIP()) role = "Router/Gateway";
                else if (netDevices[i].ip == WiFi.localIP()) role = "ESP32 (este)";
            }
            json += "\"role\":\"" + role + "\"";
            json += "}";
        }
        json += "]}";
        request->send(200, "application/json", json);
    });

    // API: forzar un nuevo escaneo de red
    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (WiFi.status() != WL_CONNECTED) {
            request->send(200, "application/json", "{\"ok\":false,\"msg\":\"No conectado a WiFi\"}");
            return;
        }
        if (scanInProgress) {
            request->send(200, "application/json", "{\"ok\":false,\"msg\":\"Escaneo en progreso\"}");
            return;
        }
        request->send(200, "application/json", "{\"ok\":true,\"msg\":\"Escaneo iniciado\"}");
        // El escaneo se ejecutara en el proximo ciclo del loop
        lastScanTime = 0; // Forzar escaneo
    });

    // === CAPTIVE PORTAL DETECTION ===
    // Android: espera un 204 en /generate_204. Si recibe otra cosa, muestra portal.
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->redirect("http://192.168.4.1/");
    });
    server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->redirect("http://192.168.4.1/");
    });
    // Google connectivity check
    server.on("/connectivitycheck", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->redirect("http://192.168.4.1/");
    });

    // Apple iOS/macOS: espera "Success" en estas URLs
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->send(200, "text/html",
            "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    server.on("/hotspotdetect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->send(200, "text/html",
            "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    server.on("/library/test/success.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->send(200, "text/html",
            "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    server.on("/captive-portal", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->redirect("http://192.168.4.1/");
    });

    // Windows: NCSI (Network Connectivity Status Indicator)
    server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->redirect("http://192.168.4.1/");
    });
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->redirect("http://192.168.4.1/");
    });
    server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->redirect("http://192.168.4.1/");
    });
    server.on("/redirect", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->redirect("http://192.168.4.1/");
    });

    // Firefox
    server.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->send(200, "text/plain", "success");
    });
    server.on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        captureClientHeaders(request);
        request->redirect("http://192.168.4.1/");
    });

    // Handler generico para captive portal
    server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);

    server.begin();
    Serial.println("Servidor web iniciado!");
    Serial.println("Conectate a la red '" + String(AP_SSID) + "' y abre cualquier pagina.");

    // Primer escaneo de red despues de un momento
    if (WiFi.status() == WL_CONNECTED) {
        delay(2000);
        scanNetwork();
    }
}

void loop() {
    dnsServer.processNextRequest();

    // Actualizar lista de clientes AP cada 3 segundos
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 3000) {
        lastUpdate = millis();
        updateClientList();

        int numClients = WiFi.softAPgetStationNum();
        if (numClients > 0) {
            Serial.printf("[%s] AP Clientes: %d | Heap: %d KB | Temp: %.1f C\n",
                getUptime().c_str(), numClients,
                ESP.getFreeHeap() / 1024, temperatureRead());
        }
    }

    // Escanear red cada 30 segundos
    if (WiFi.status() == WL_CONNECTED && !scanInProgress &&
        (millis() - lastScanTime > 30000)) {
        scanNetwork();
    }

    // Reconectar WiFi si se desconecta
    static unsigned long lastReconnect = 0;
    if (WiFi.status() != WL_CONNECTED && millis() - lastReconnect > 15000) {
        lastReconnect = millis();
        Serial.println("[WIFI] Reconectando a " + String(STA_SSID) + "...");
        WiFi.begin(STA_SSID, STA_PASS);
    }
}
