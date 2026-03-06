#pragma once
#include <Arduino.h>

// ─── Alert Manager: threshold checks, LED + buzzer control ──────────

enum AlertLevel : uint8_t {
    ALERT_NONE    = 0,
    ALERT_WARN    = 1,   // Approaching threshold
    ALERT_CRIT    = 2,   // Out of compliance
    ALERT_SENSOR  = 3    // Probe disconnected / read error
};

void       alert_init();
AlertLevel alert_check(const float* temps, uint8_t probeCount);
void       alert_setThresholds(float warnHigh, float critHigh,
                               float warnLow, float critLow);
void       alert_showStatus(AlertLevel level);  // LED + buzzer
void       alert_silence();                     // Turn off buzzer (LED stays)
