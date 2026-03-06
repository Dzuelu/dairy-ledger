#pragma once
#include <Arduino.h>

// ─── SD Logger: CSV writing with daily file rotation ────────────────
// Format: timestamp,probe0,probe1,...,probeN,synced

bool sd_logger_init();
bool sd_logger_writeRow(uint32_t timestamp, const float* temps,
                        const float* calibs, uint8_t probeCount);
uint16_t sd_logger_getPendingCount();
