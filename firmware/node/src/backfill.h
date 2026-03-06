#pragma once
#include <Arduino.h>

// ─── Backfill: sequential read of unsynced rows from SD ─────────────
// Uses RTC_DATA_ATTR pointers for O(1) seek across deep sleep cycles

void     backfill_init();
uint8_t  backfill_sendBatch();    // Returns number of rows sent this cycle
void     backfill_markSynced();   // Called after gateway ACK
void     backfill_coldBootRecovery(); // Scan SD for unsynced rows after power loss
