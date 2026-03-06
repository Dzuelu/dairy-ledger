// ╔════════════════════════════════════════════════════════════════════╗
// ║  DairyLedger — Gateway Firmware                                    ║
// ║  ESP32-C3 central hub: ESP-NOW receiver, web dashboard, RTC       ║
// ║                                                                   ║
// ║  WiFi STA+AP │ ESPAsyncWebServer │ DS3231 RTC │ NTP sync         ║
// ╚════════════════════════════════════════════════════════════════════╝

#include "config.h"
#include "rtc_time.h"
#include "node_registry.h"
#include "espnow_recv.h"
#include "sd_storage.h"
#include "web_server.h"
#include "alert_mgr.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <Preferences.h>

static Preferences _prefs;
static String      _staSsid;
static String      _staPass;
static bool        _staConfigured = false;
static uint32_t    _lastReconnect = 0;

// ─── WiFi Setup: STA+AP dual mode ───────────────────────────────────

// Start open AP (always runs, no password for easy fallback access)
static void startAP() {
    WiFi.softAP(WIFI_AP_SSID, NULL, WIFI_AP_CHANNEL);  // Open network
    Serial.printf("[WIFI] AP started: %s (IP: %s) — open network\n",
                  WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}

// Attempt STA connection (non-blocking after initial setup)
static bool trySTAConnect(bool blocking) {
    if (!_staConfigured) return false;

    WiFi.begin(_staSsid.c_str(), _staPass.c_str());
    Serial.printf("[WIFI] Connecting to %s...\n", _staSsid.c_str());

    if (blocking) {
        int retries = 40;  // 20 seconds
        while (WiFi.status() != WL_CONNECTED && retries-- > 0) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI] Connected! IP: %s\n",
                      WiFi.localIP().toString().c_str());

        // mDNS
        if (MDNS.begin(MDNS_NAME)) {
            MDNS.addService("http", "tcp", WEB_PORT);
            Serial.printf("[WIFI] mDNS: http://%s.local\n", MDNS_NAME);
        }
        return true;
    }

    Serial.println("[WIFI] STA connection failed — AP still available");
    return false;
}

static void setupWiFi() {
    _prefs.begin("dairyledger", true);  // Read-only
    _staSsid = _prefs.getString("wifi_ssid", "");
    _staPass = _prefs.getString("wifi_pass", "");
    _prefs.end();

    _staConfigured = (_staSsid.length() > 0);

    if (_staConfigured) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_AP);
    }

    // AP always runs (open, no password)
    startAP();

    // Try STA if configured (blocking on first boot)
    if (_staConfigured) {
        trySTAConnect(true);
    } else {
        Serial.println("[WIFI] No home WiFi configured — AP-only mode");
    }

    // Lock ESP-NOW to our channel
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

// Called from loop() — reconnects STA if it dropped
static void wifiReconnectLoop() {
    if (!_staConfigured) return;
    if (WiFi.status() == WL_CONNECTED) return;

    uint32_t now = millis();
    if (now - _lastReconnect < WIFI_RECONNECT_MS) return;
    _lastReconnect = now;

    Serial.println("[WIFI] STA disconnected — attempting reconnect...");
    trySTAConnect(false);
}

// ─── Setup ──────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n[GATEWAY] DairyLedger Gateway starting...");

    // Watchdog
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);

    // 1. WiFi (must be first — ESP-NOW depends on it)
    setupWiFi();

    // 2. RTC (DS3231 + NTP)
    rtc_time_init();
    if (WiFi.status() == WL_CONNECTED) {
        rtc_time_syncNTP();
    }

    // 3. SD card
    sd_storage_init();

    // 4. Node registry (loads from SD)
    registry_init();

    // 5. ESP-NOW receiver
    espnow_recv_init();

    // 6. Alerts (LED + buzzer)
    gateway_alert_init();

    // 7. Web server (must be last — needs registry + SD ready)
    web_server_init();

    Serial.println("[GATEWAY] Ready!");
    Serial.printf("[GATEWAY] Dashboard: http://%s/  or  http://%s.local/\n",
                  WiFi.softAPIP().toString().c_str(), MDNS_NAME);
}

// ─── Main Loop ──────────────────────────────────────────────────────

void loop() {
    esp_task_wdt_reset();

    // Process incoming ESP-NOW messages
    espnow_recv_loop();

    // Check node timeouts + save registry
    registry_loop();

    // Update alert status
    gateway_alert_loop();

    // WiFi STA reconnect (AP always stays up)
    wifiReconnectLoop();

    // Periodic NTP re-sync (only when STA connected)
    if (WiFi.status() == WL_CONNECTED) {
        rtc_time_loop();
    }

    delay(10);  // Small yield
}
