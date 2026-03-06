#include "sd_health.h"

// Survive deep sleep
RTC_DATA_ATTR static SDHealth _status    = SD_UNKNOWN;
RTC_DATA_ATTR static uint16_t _failCount = 0;

static const uint16_t DEGRADED_THRESHOLD = 3;  // Consecutive fails → degraded
static const uint16_t FAILED_THRESHOLD   = 10; // Consecutive fails → failed

void sd_health_init() {
    // RTC_DATA_ATTR handles persistence across deep sleep.
    // On cold boot, _status resets to SD_UNKNOWN and _failCount to 0.
    Serial.printf("[SD_HEALTH] Status: %d, fail count: %d\n", _status, _failCount);
}

SDHealth sd_health_getStatus() {
    return _status;
}

void sd_health_reportSuccess() {
    _failCount = 0;
    _status = SD_OK;
}

void sd_health_reportFailure() {
    _failCount++;

    if (_failCount >= FAILED_THRESHOLD) {
        _status = SD_FAILED;
    } else if (_failCount >= DEGRADED_THRESHOLD) {
        _status = SD_DEGRADED;
    }

    Serial.printf("[SD_HEALTH] Write failed (count=%d, status=%d)\n",
                  _failCount, _status);
}

uint16_t sd_health_getFailCount() {
    return _failCount;
}
