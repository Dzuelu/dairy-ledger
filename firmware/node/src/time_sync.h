#pragma once
#include <Arduino.h>

// ─── Time Sync: maintains internal RTC using gateway-provided time ──
// Persisted across deep sleep via RTC_DATA_ATTR

void     time_sync_init();
void     time_sync_handleAck(uint32_t gateway_time, uint8_t quality);
uint32_t time_sync_getTimestamp();   // Returns 0 if never synced
bool     time_sync_isValid();
uint8_t  time_sync_getQuality();     // 0=none, 1=rtc, 2=ntp
