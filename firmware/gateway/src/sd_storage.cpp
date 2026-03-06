#include "sd_storage.h"
#include "config.h"
#include <SPI.h>
#include <SD.h>
#include <time.h>

static bool _cardOk = false;

void sd_storage_init() {
    SPI.begin(PIN_SPI_CLK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);

    if (!SD.begin(PIN_SPI_CS)) {
        Serial.println("[SD] Card mount failed");
        _cardOk = false;
        return;
    }

    _cardOk = true;

    // Ensure base directories exist
    if (!SD.exists(SD_DATA_DIR)) SD.mkdir(SD_DATA_DIR);

    Serial.printf("[SD] Card ready, size=%lluMB\n",
                  SD.cardSize() / (1024 * 1024));
}

bool sd_storage_logReading(const char* nodeId, uint32_t timestamp,
                           const float* temps, uint8_t probeCount) {
    if (!_cardOk) return false;

    // Create node directory: /data/<nodeId>/
    char nodeDir[SD_MAX_FILENAME];
    snprintf(nodeDir, sizeof(nodeDir), "%s/%s", SD_DATA_DIR, nodeId);
    if (!SD.exists(nodeDir)) SD.mkdir(nodeDir);

    // Daily file: /data/<nodeId>/YYYY-MM-DD.csv
    char filename[SD_MAX_FILENAME];
    if (timestamp > 1000000) {
        time_t t = (time_t)timestamp;
        struct tm* tm = gmtime(&t);
        snprintf(filename, sizeof(filename), "%s/%04d-%02d-%02d.csv",
                 nodeDir, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    } else {
        snprintf(filename, sizeof(filename), "%s/unsynced.csv", nodeDir);
    }

    File f = SD.open(filename, FILE_APPEND);
    if (!f) {
        Serial.printf("[SD] Failed to open %s\n", filename);
        return false;
    }

    f.printf("%lu", timestamp);
    for (uint8_t i = 0; i < probeCount; i++) {
        if (temps[i] <= -126.0f) {
            f.print(",ERR");
        } else {
            f.printf(",%.2f", temps[i]);
        }
    }
    f.println();
    f.close();

    return true;
}

String sd_storage_getCSV(const char* nodeId, const char* date) {
    if (!_cardOk) return "";

    char filename[SD_MAX_FILENAME];
    snprintf(filename, sizeof(filename), "%s/%s/%s.csv",
             SD_DATA_DIR, nodeId, date);

    File f = SD.open(filename, FILE_READ);
    if (!f) return "";

    String content = f.readString();
    f.close();
    return content;
}

String sd_storage_listDates(const char* nodeId) {
    if (!_cardOk) return "[]";

    char nodeDir[SD_MAX_FILENAME];
    snprintf(nodeDir, sizeof(nodeDir), "%s/%s", SD_DATA_DIR, nodeId);

    File dir = SD.open(nodeDir);
    if (!dir || !dir.isDirectory()) return "[]";

    String json = "[";
    bool first = true;

    File entry;
    while ((entry = dir.openNextFile())) {
        String name = entry.name();
        if (name.endsWith(".csv") && name != "unsynced.csv") {
            if (!first) json += ",";
            // Strip .csv extension
            name = name.substring(0, name.length() - 4);
            json += "\"" + name + "\"";
            first = false;
        }
        entry.close();
    }
    dir.close();

    json += "]";
    return json;
}

bool sd_storage_clearNodeData(const char* nodeId) {
    if (!_cardOk) return false;

    char nodeDir[SD_MAX_FILENAME];
    snprintf(nodeDir, sizeof(nodeDir), "%s/%s", SD_DATA_DIR, nodeId);

    File dir = SD.open(nodeDir);
    if (!dir || !dir.isDirectory()) return true;  // Nothing to clear

    // Delete all files in the node directory
    File entry;
    while ((entry = dir.openNextFile())) {
        char filePath[SD_MAX_FILENAME];
        snprintf(filePath, sizeof(filePath), "%s/%s", nodeDir, entry.name());
        entry.close();
        SD.remove(filePath);
    }
    dir.close();

    // Remove the directory itself
    SD.rmdir(nodeDir);

    Serial.printf("[SD] Cleared all data for node %s\n", nodeId);
    return true;
}
