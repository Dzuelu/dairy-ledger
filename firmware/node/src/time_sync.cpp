#include "time_sync.h"
#include "config.h"
#include <sys/time.h>

// Survive deep sleep
RTC_DATA_ATTR static bool     _time_valid = false;
RTC_DATA_ATTR static uint8_t  _time_quality = 0;  // 0=none 1=rtc 2=ntp

void time_sync_init() {
    // Nothing to do — RTC_DATA_ATTR preserves state across deep sleep.
    // On cold boot (power loss), _time_valid = false automatically.
    if (_time_valid) {
        Serial.printf("[TIME] Resuming from deep sleep, quality=%d\n", _time_quality);
    } else {
        Serial.println("[TIME] No valid time (cold boot or never synced)");
    }
}

void time_sync_handleAck(uint32_t gateway_time, uint8_t quality) {
    if (gateway_time == 0) {
        Serial.println("[TIME] Gateway has no time — skipping sync");
        return;
    }

    struct timeval tv;
    tv.tv_sec  = gateway_time;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    _time_valid  = true;
    _time_quality = quality;

    Serial.printf("[TIME] Synced to %lu (quality=%d)\n", gateway_time, quality);
}

uint32_t time_sync_getTimestamp() {
    if (!_time_valid) {
        return 0;  // Gateway will re-timestamp with its own clock
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)tv.tv_sec;
}

bool time_sync_isValid() {
    return _time_valid;
}

uint8_t time_sync_getQuality() {
    return _time_quality;
}
