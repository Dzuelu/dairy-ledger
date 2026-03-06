#pragma once
#include <Arduino.h>

// ─── Gateway alert: LED + buzzer status for gateway itself ──────────

void gateway_alert_init();
void gateway_alert_loop();     // Check all nodes for threshold violations
void gateway_alert_silence();  // Acknowledge / silence buzzer
