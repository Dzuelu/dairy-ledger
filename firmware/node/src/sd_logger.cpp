#include "sd_logger.h"
#include "sd_health.h"
#include "config.h"
#include <SPI.h>
#include <SD.h>
#include <time.h>

// Track unsynced rows across deep sleep
RTC_DATA_ATTR static uint16_t _pendingSync = 0;

static bool _cardPresent = false;

static void buildFilename(uint32_t timestamp, char* buf, size_t bufLen) {
    if (timestamp == 0) {
        // No valid time — use a catch-all file
        snprintf(buf, bufLen, "%s/unsynced.csv", SD_LOG_DIR);
        return;
    }

    time_t t = (time_t)timestamp;
    struct tm* tm = gmtime(&t);
    snprintf(buf, bufLen, "%s/%04d-%02d-%02d.csv",
             SD_LOG_DIR, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
}

bool sd_logger_init() {
    SPI.begin(PIN_SPI_CLK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);

    if (!SD.begin(PIN_SPI_CS)) {
        Serial.println("[SD] Card mount failed");
        sd_health_reportFailure();
        _cardPresent = false;
        return false;
    }

    _cardPresent = true;

    // Ensure log directory exists
    if (!SD.exists(SD_LOG_DIR)) {
        SD.mkdir(SD_LOG_DIR);
    }

    sd_health_reportSuccess();
    Serial.printf("[SD] Card mounted, type=%d, size=%lluMB\n",
                  SD.cardType(), SD.cardSize() / (1024 * 1024));
    return true;
}

bool sd_logger_writeRow(uint32_t timestamp, const float* temps,
                        const float* calibs, uint8_t probeCount) {
    if (!_cardPresent) {
        // Try re-init in case card was reinserted
        if (!sd_logger_init()) {
            return false;
        }
    }

    char filename[SD_MAX_FILENAME];
    buildFilename(timestamp, filename, sizeof(filename));

    File f = SD.open(filename, FILE_APPEND);
    if (!f) {
        sd_health_reportFailure();
        Serial.printf("[SD] Failed to open %s\n", filename);
        return false;
    }

    // Write CSV row: timestamp,t0,t1,...,tN,0
    // The trailing 0 = not yet synced (for backfill tracking)
    f.printf("%lu", timestamp);
    for (uint8_t i = 0; i < probeCount; i++) {
        if (temps[i] <= -126.0f) {
            f.print(",ERR");
        } else {
            f.printf(",%.2f", temps[i]);
        }
    }
    f.println(",0");  // synced = false
    f.close();

    _pendingSync++;
    sd_health_reportSuccess();

    Serial.printf("[SD] Wrote row to %s (pending=%d)\n", filename, _pendingSync);
    return true;
}

uint16_t sd_logger_getPendingCount() {
    return _pendingSync;
}
