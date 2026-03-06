// ╔════════════════════════════════════════════════════════════════════╗
// ║  DairyLedger — Node Firmware                                      ║
// ║  ESP32-C3 temperature monitoring node for dairy compliance        ║
// ║                                                                   ║
// ║  Wake → Read probes → Log to SD → Send via ESP-NOW → Sleep       ║
// ╚════════════════════════════════════════════════════════════════════╝

#include "config.h"
#include "messages.h"
#include "identity.h"
#include "time_sync.h"
#include "sensor_mgr.h"
#include "sd_logger.h"
#include "sd_health.h"
#include "backfill.h"
#include "alert_mgr.h"
#include "espnow_comm.h"
#include <esp_sleep.h>
#include <esp_task_wdt.h>

// ─── Deep-sleep persistent state ────────────────────────────────────
RTC_DATA_ATTR static uint32_t _bootCount = 0;
RTC_DATA_ATTR static uint16_t _readingIntervalSec = READING_INTERVAL_SEC;

// ─── ESP-NOW Callbacks ──────────────────────────────────────────────

static void handleAck(uint32_t gatewayTime, uint8_t quality) {
    time_sync_handleAck(gatewayTime, quality);
    backfill_markSynced();
}

static void handleLabel(const char* newLabel) {
    identity_setLabel(newLabel);
}

static void handleConfig(uint16_t interval, float wh, float ch,
                         float wl, float cl) {
    if (interval > 0) {
        _readingIntervalSec = interval;
        Serial.printf("[NODE] Interval updated to %d sec\n", interval);
    }
    alert_setThresholds(wh, ch, wl, cl);
}

// ─── Setup ──────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    _bootCount++;
    Serial.printf("\n[NODE] Boot #%lu (reason: %d)\n",
                  _bootCount, esp_sleep_get_wakeup_cause());

    // Hardware watchdog — reboot if we hang
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);

    // 1. Identity (EEPROM)
    identity_init();

    // 2. Time (RTC_DATA_ATTR — survives deep sleep)
    time_sync_init();

    // 3. SD health tracking
    sd_health_init();

    // 4. Alerts (LED + buzzer)
    alert_init();

    // 5. ESP-NOW
    espnow_init();
    espnow_onAck(handleAck);
    espnow_onLabel(handleLabel);
    espnow_onConfig(handleConfig);

    // 6. Sensors
    sensor_init();

    // 7. SD card
    bool sdOk = sd_logger_init();

    // 8. Backfill
    backfill_init();
    if (_bootCount == 1 && time_sync_isValid()) {
        backfill_coldBootRecovery();
    }

    // ─── Main cycle ─────────────────────────────────────────────────

    // Read all probes
    sensor_readAll();
    esp_task_wdt_reset();

    // Get timestamp
    uint32_t now = time_sync_getTimestamp();

    // Collect readings into arrays
    uint8_t probeCount = sensor_getProbeCount();
    float temps[MAX_PROBES]  = {};
    float calibs[MAX_PROBES] = {};

    for (uint8_t i = 0; i < probeCount; i++) {
        temps[i]  = sensor_getTemp(i);
        calibs[i] = sensor_getCalibOffset(i);
    }

    // Check alerts
    AlertLevel alert = alert_check(temps, probeCount);
    alert_showStatus(alert);

    // Log to SD
    if (sdOk) {
        sd_logger_writeRow(now, temps, calibs, probeCount);
    }
    esp_task_wdt_reset();

    // Build ESP-NOW message
    SensorReading msg = {};
    msg.msg_type    = MSG_READING;
    strncpy(msg.node_id, identity_getId(), NODE_ID_LEN);
    msg.node_id[NODE_ID_LEN] = '\0';
    msg.timestamp   = now;
    msg.probe_count = probeCount;
    memcpy(msg.temperatures, temps, sizeof(float) * probeCount);
    memcpy(msg.calibration, calibs, sizeof(float) * probeCount);
    msg.sd_status   = (uint8_t)sd_health_getStatus();
    msg.pending_sync = sd_logger_getPendingCount();

    // Compute checksum over everything except the checksum byte
    msg.checksum = computeChecksum((uint8_t*)&msg, sizeof(msg) - 1);

    // Send reading
    espnow_send((uint8_t*)&msg, sizeof(msg));

    // Wait for ACK (time sync piggybacks on this)
    bool acked = espnow_waitForAck(ESPNOW_ACK_TIMEOUT_MS);

    if (acked) {
        Serial.println("[NODE] Gateway acknowledged");
        // Try sending a batch of backfill data
        backfill_sendBatch();
        esp_task_wdt_reset();
    } else {
        Serial.println("[NODE] No ACK — gateway may be offline");
    }

    // On first boot, send announce message
    if (_bootCount == 1) {
        AnnounceMessage ann = {};
        ann.msg_type    = MSG_ANNOUNCE;
        strncpy(ann.node_id, identity_getId(), NODE_ID_LEN);
        ann.node_id[NODE_ID_LEN] = '\0';
        strncpy(ann.label, identity_getLabel(), NODE_LABEL_LEN);
        ann.label[NODE_LABEL_LEN] = '\0';
        ann.probe_count = probeCount;
        ann.sd_status   = (uint8_t)sd_health_getStatus();
        ann.uptime_sec  = millis() / 1000;

        espnow_send((uint8_t*)&ann, sizeof(ann));
        delay(50);  // Let it transmit
    }

    // ─── Sleep ──────────────────────────────────────────────────────

    uint64_t sleepUs = (uint64_t)_readingIntervalSec * 1000000ULL;
    Serial.printf("[NODE] Sleeping for %d seconds...\n\n", _readingIntervalSec);
    Serial.flush();

    alert_silence();  // Turn off buzzer before sleep (LED stays on)

    esp_task_wdt_delete(NULL);
    esp_deep_sleep(sleepUs);
}

void loop() {
    // Never reached — we deep sleep in setup()
}
