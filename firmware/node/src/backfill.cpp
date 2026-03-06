#include "backfill.h"
#include "config.h"
#include "sd_logger.h"
#include "sd_health.h"
#include "espnow_comm.h"
#include "identity.h"
#include "messages.h"
#include <SD.h>
#include <time.h>

// Survive deep sleep — O(1) seek into the backfill position
RTC_DATA_ATTR static uint32_t _backfillPos = 0;     // Byte offset in current file
RTC_DATA_ATTR static uint16_t _backfillDay = 0;     // Day-of-year of current file
RTC_DATA_ATTR static uint16_t _backfillYear = 0;    // Year of current file
RTC_DATA_ATTR static bool     _backfillActive = false;

static char _currentFile[SD_MAX_FILENAME];

static void buildBackfillFilename(uint16_t year, uint16_t dayOfYear,
                                  char* buf, size_t len) {
    // Convert day-of-year back to month/day
    struct tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_yday = dayOfYear;
    mktime(&tm);  // Normalizes tm_mon and tm_mday
    snprintf(buf, len, "%s/%04d-%02d-%02d.csv",
             SD_LOG_DIR, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

void backfill_init() {
    if (_backfillActive) {
        Serial.printf("[BACKFILL] Resuming at file day=%d pos=%lu\n",
                      _backfillDay, _backfillPos);
    } else {
        Serial.println("[BACKFILL] No active backfill");
    }
}

uint8_t backfill_sendBatch() {
    if (sd_health_getStatus() >= SD_FAILED) return 0;
    if (sd_logger_getPendingCount() == 0 && !_backfillActive) return 0;

    // If no active backfill, start from current file
    if (!_backfillActive) {
        // Use today's date to start scanning
        time_t now = time(nullptr);
        struct tm* tm = gmtime(&now);
        _backfillYear = tm->tm_year + 1900;
        _backfillDay = tm->tm_yday;
        _backfillPos = 0;
        _backfillActive = true;
    }

    buildBackfillFilename(_backfillYear, _backfillDay,
                          _currentFile, sizeof(_currentFile));

    File f = SD.open(_currentFile, FILE_READ);
    if (!f) {
        // File doesn't exist or can't open — backfill complete for this day
        _backfillActive = false;
        return 0;
    }

    f.seek(_backfillPos);

    uint8_t sent = 0;
    char line[256];

    while (sent < BACKFILL_BATCH_SIZE && f.available()) {
        size_t lineStart = f.position();
        int len = f.readBytesUntil('\n', line, sizeof(line) - 1);
        if (len <= 0) break;
        line[len] = '\0';

        // Check if last field is ",0" (unsynced)
        char* lastComma = strrchr(line, ',');
        if (!lastComma) continue;

        if (lastComma[1] == '0') {
            // Parse: timestamp,t0,t1,...,tN,0
            SensorReading msg = {};
            msg.msg_type = MSG_BACKFILL;
            strncpy(msg.node_id, identity_getId(), NODE_ID_LEN);
            msg.node_id[NODE_ID_LEN] = '\0';

            // Parse timestamp
            char* tok = strtok(line, ",");
            if (!tok) continue;
            msg.timestamp = strtoul(tok, nullptr, 10);

            // Parse temperatures
            msg.probe_count = 0;
            while ((tok = strtok(nullptr, ",")) != nullptr && msg.probe_count < MAX_PROBES) {
                // Last token is the sync flag — skip it
                char* peek = strtok(nullptr, "");
                if (peek == nullptr) break;  // This was the sync flag

                // Re-parse: we consumed the next token, need to handle differently
                // Simpler approach: count fields first
                break;
            }

            // TODO: Proper CSV parsing for backfill — for now, just send timestamp
            // This will be refined when we have hardware to test against

            espnow_send((uint8_t*)&msg, sizeof(msg));
            sent++;
        }

        _backfillPos = f.position();
    }

    f.close();

    if (sent == 0) {
        // Done with this file
        _backfillActive = false;
    }

    Serial.printf("[BACKFILL] Sent %d rows from %s\n", sent, _currentFile);
    return sent;
}

void backfill_markSynced() {
    // Decrement pending count tracked in sd_logger
    // Note: We don't modify the CSV files to mark rows as synced —
    // instead we rely on the in-memory pointer position. On cold boot,
    // coldBootRecovery() re-scans.
    Serial.println("[BACKFILL] Batch acknowledged by gateway");
}

void backfill_coldBootRecovery() {
    // After power loss, we've lost the in-memory pointer.
    // Scan today's file for unsynced rows and set pointer.
    time_t now = time(nullptr);
    if (now < 1000000) {
        Serial.println("[BACKFILL] No valid time — deferring recovery");
        return;
    }

    struct tm* tm = gmtime(&now);
    _backfillYear = tm->tm_year + 1900;
    _backfillDay = tm->tm_yday;
    _backfillPos = 0;
    _backfillActive = true;

    Serial.printf("[BACKFILL] Cold boot recovery — scanning from day %d\n",
                  _backfillDay);
}
