#pragma once
#include <Arduino.h>

// ─── DS3231 RTC + NTP time management ───────────────────────────────

void    rtc_time_init();
bool    rtc_time_hasRTC();            // DS3231 detected on I2C?
bool    rtc_time_hasNTP();            // Successfully synced with NTP?
uint32_t rtc_time_getEpoch();         // Current Unix epoch
uint8_t  rtc_time_getQuality();       // 0=none, 1=rtc_only, 2=ntp_synced
void    rtc_time_syncNTP();           // Attempt NTP sync (requires WiFi STA)
void    rtc_time_loop();              // Periodic NTP re-sync
String  rtc_time_formatISO(uint32_t epoch);  // "2025-01-15T14:30:00Z"
