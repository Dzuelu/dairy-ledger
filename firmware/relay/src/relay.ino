// ╔════════════════════════════════════════════════════════════════════╗
// ║  DairyLedger — Relay Firmware                                      ║
// ║  ESP32-C3 passive ESP-NOW repeater                                ║
// ║                                                                   ║
// ║  Receives any ESP-NOW message and re-broadcasts it.               ║
// ║  Place between nodes and gateway to extend range.                 ║
// ║  No configuration needed — just power on.                        ║
// ╚════════════════════════════════════════════════════════════════════╝

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Adafruit_NeoPixel.h>

#define PIN_LED         8     // On-board WS2812 RGB LED
#define ESPNOW_CHANNEL  1
#define LED_FLASH_MS    50    // Brief flash on relay

static Adafruit_NeoPixel led(1, PIN_LED, NEO_GRB + NEO_KHZ800);
static const uint8_t BROADCAST[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Track recently relayed messages to avoid infinite loops
#define DEDUP_SIZE 16
struct DedupeEntry {
    uint8_t  mac[6];
    uint8_t  firstByte;
    uint32_t timestamp;
};

static DedupeEntry _dedup[DEDUP_SIZE];
static uint8_t     _dedupIdx = 0;

static bool isDuplicate(const uint8_t* mac, uint8_t firstByte) {
    uint32_t now = millis();
    for (int i = 0; i < DEDUP_SIZE; i++) {
        if (_dedup[i].timestamp > 0 &&
            (now - _dedup[i].timestamp) < 500 &&  // 500ms window
            memcmp(_dedup[i].mac, mac, 6) == 0 &&
            _dedup[i].firstByte == firstByte) {
            return true;
        }
    }
    return false;
}

static void addDedup(const uint8_t* mac, uint8_t firstByte) {
    _dedup[_dedupIdx].timestamp = millis();
    memcpy(_dedup[_dedupIdx].mac, mac, 6);
    _dedup[_dedupIdx].firstByte = firstByte;
    _dedupIdx = (_dedupIdx + 1) % DEDUP_SIZE;
}

// ─── ESP-NOW Callbacks ──────────────────────────────────────────────

static void onRecv(const esp_now_recv_info_t* info,
                   const uint8_t* data, int len) {
    if (len < 1) return;

    // Skip if we already relayed this recently (prevents loops)
    if (isDuplicate(info->src_addr, data[0])) return;

    // Re-broadcast
    esp_now_send(BROADCAST, data, len);
    addDedup(info->src_addr, data[0]);

    // Flash LED cyan to show relay activity
    led.setPixelColor(0, led.Color(0, 20, 20));
    led.show();
}

static void onSend(const uint8_t* mac, esp_now_send_status_t status) {
    // Turn off LED after send
    led.setPixelColor(0, led.Color(0, 5, 0));  // Dim green = idle
    led.show();
}

// ─── Setup ──────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n[RELAY] DairyLedger Relay starting...");

    // LED
    led.begin();
    led.setBrightness(20);
    led.setPixelColor(0, led.Color(0, 5, 0));  // Dim green = ready
    led.show();

    // WiFi in STA mode for ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[RELAY] ESP-NOW init failed!");
        led.setPixelColor(0, led.Color(40, 0, 0));  // Red = error
        led.show();
        return;
    }

    // Add broadcast peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    esp_now_register_recv_cb(onRecv);
    esp_now_register_send_cb(onSend);

    // Clear dedup table
    memset(_dedup, 0, sizeof(_dedup));

    Serial.printf("[RELAY] Ready on channel %d\n", ESPNOW_CHANNEL);
    Serial.println("[RELAY] Relaying all ESP-NOW messages...");
}

void loop() {
    // Nothing to do — everything happens in callbacks
    delay(100);
}
