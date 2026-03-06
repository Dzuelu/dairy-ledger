#pragma once
#include <Arduino.h>

// ─── SD Storage: log incoming readings to CSV files per node ────────

void sd_storage_init();
bool sd_storage_logReading(const char* nodeId, uint32_t timestamp,
                           const float* temps, uint8_t probeCount);
String sd_storage_getCSV(const char* nodeId, const char* date);  // YYYY-MM-DD
String sd_storage_listDates(const char* nodeId);                 // JSON array
bool   sd_storage_clearNodeData(const char* nodeId);             // Delete all data for a node
