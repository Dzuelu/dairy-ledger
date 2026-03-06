#pragma once

// ─── Pin Assignments (ESP32-C3-DevKitM-01) ──────────────────────────
#define PIN_ONEWIRE       4    // DS18B20 data line (4.7kΩ pull-up to 3.3V)
#define PIN_SPI_CLK       6    // SD card SPI
#define PIN_SPI_MISO      5
#define PIN_SPI_MOSI      7
#define PIN_SPI_CS        10   // SD card chip select
#define PIN_BUZZER        1    // Active piezo buzzer
#define PIN_LED           8    // On-board WS2812 RGB LED

// ─── Timing ──────────────────────────────────────────────────────────
#define READING_INTERVAL_SEC     900   // 15 minutes (configurable via gateway)
#define DEEP_SLEEP_US            (READING_INTERVAL_SEC * 1000000ULL)
#define ESPNOW_ACK_TIMEOUT_MS   500   // How long to wait for gateway ACK
#define ESPNOW_CHANNEL           1    // Default WiFi/ESP-NOW channel

// ─── Sensor ──────────────────────────────────────────────────────────
#define MAX_PROBES               8    // Max DS18B20 probes per node
#define ONEWIRE_RESOLUTION       12   // 12-bit = 0.0625°C resolution
#define SENSOR_READ_TIMEOUT_MS   1000 // Timeout for DS18B20 conversion

// ─── Alert Thresholds (defaults, overridable from gateway) ──────────
#define DEFAULT_WARN_HIGH_C      3.3f
#define DEFAULT_CRIT_HIGH_C      5.0f
#define DEFAULT_WARN_LOW_C      -2.2f
#define DEFAULT_CRIT_LOW_C      -3.9f

// ─── SD Card ─────────────────────────────────────────────────────────
#define SD_LOG_DIR               "/logs"
#define SD_MAX_FILENAME          32

// ─── Backfill ────────────────────────────────────────────────────────
#define BACKFILL_BATCH_SIZE      5    // Rows to backfill per cycle

// ─── Identity ────────────────────────────────────────────────────────
#define NODE_ID_LEN              6    // Characters in auto-generated ID
#define NODE_LABEL_LEN           32   // Max human-readable label length
#define EEPROM_SIZE              64   // Bytes reserved for identity
#define EEPROM_MAGIC             0xA5 // Marker for valid EEPROM data

// ─── Watchdog ────────────────────────────────────────────────────────
#define WDT_TIMEOUT_SEC          30   // Reboot if loop takes > 30s

// ─── ESP-NOW Message Types ───────────────────────────────────────────
#define MSG_READING     0x01   // Node → Gateway: sensor reading
#define MSG_BACKFILL    0x02   // Node → Gateway: historical unsynced reading
#define MSG_ACK         0x10   // Gateway → Node: acknowledge receipt
#define MSG_SET_LABEL   0x20   // Gateway → Node: push new label to EEPROM
#define MSG_SET_CONFIG  0x21   // Gateway → Node: push threshold/interval changes
#define MSG_ANNOUNCE    0x30   // Node → Gateway: "I exist" (first boot / periodic)
