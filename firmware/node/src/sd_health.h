#pragma once
#include <Arduino.h>

// ─── SD Health: track card status across deep sleep ─────────────────

enum SDHealth : uint8_t {
    SD_UNKNOWN   = 0,  // Not yet attempted
    SD_OK        = 1,  // Last write succeeded
    SD_DEGRADED  = 2,  // Intermittent failures
    SD_FAILED    = 3,  // Card absent or dead
    SD_NO_CARD   = 4   // Never detected
};

void     sd_health_init();
SDHealth sd_health_getStatus();
void     sd_health_reportSuccess();
void     sd_health_reportFailure();
uint16_t sd_health_getFailCount();
