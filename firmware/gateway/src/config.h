#pragma once

// ─── Pin Assignments (ESP32-C3-DevKitM-01 — Gateway Role) ───────────
// No 1-Wire (gateway has no probes)
#define PIN_SPI_CLK       6    // SD card SPI
#define PIN_SPI_MISO      5
#define PIN_SPI_MOSI      7
#define PIN_SPI_CS        10   // SD card chip select
#define PIN_I2C_SDA       3    // DS3231 RTC
#define PIN_I2C_SCL       2    // DS3231 RTC
#define PIN_BUZZER        1    // Active piezo buzzer
#define PIN_LED           8    // On-board WS2812 RGB LED

// ─── WiFi ────────────────────────────────────────────────────────────
#define WIFI_AP_SSID      "DairyLedger"
#define WIFI_AP_CHANNEL   1
#define WIFI_HOSTNAME     "dairyledger"
#define MDNS_NAME         "dairyledger"       // http://dairyledger.local

// ─── ESP-NOW ─────────────────────────────────────────────────────────
#define ESPNOW_CHANNEL    1

// ─── Timing ──────────────────────────────────────────────────────────
#define NTP_SYNC_INTERVAL_MS   3600000  // Re-sync NTP every hour
#define NODE_TIMEOUT_SEC       1800     // Mark node offline after 30 min
#define REGISTRY_SAVE_INTERVAL 60000    // Save registry to SD every 60s
#define WIFI_RECONNECT_MS      30000    // Retry STA connection every 30s

// ─── Limits ──────────────────────────────────────────────────────────
#define MAX_PROBES_PER_NODE    8
#define MIN_FREE_HEAP_BYTES    16384   // Don't add nodes below this free heap

// ─── SD Card ─────────────────────────────────────────────────────────
#define SD_DATA_DIR            "/data"
#define SD_CONFIG_FILE         "/config.json"
#define SD_MAX_FILENAME        48

// ─── Alert Thresholds (defaults, configurable per-node) ─────────────
#define DEFAULT_WARN_HIGH_C    3.3f
#define DEFAULT_CRIT_HIGH_C    5.0f
#define DEFAULT_WARN_LOW_C    -2.2f
#define DEFAULT_CRIT_LOW_C    -3.9f

// ─── Web Server ──────────────────────────────────────────────────────
#define WEB_PORT               80

// ─── ESP-NOW Message Types (shared with nodes) ──────────────────────
#define MSG_READING     0x01
#define MSG_BACKFILL    0x02
#define MSG_ACK         0x10
#define MSG_SET_LABEL   0x20
#define MSG_SET_CONFIG  0x21
#define MSG_ANNOUNCE    0x30
