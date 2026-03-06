#include "rtc_time.h"
#include "config.h"
#include <Wire.h>
#include <RTClib.h>
#include <time.h>
#include <sys/time.h>

static RTC_DS3231 _rtc;
static bool       _rtcPresent  = false;
static bool       _ntpSynced   = false;
static uint32_t   _lastNtpSync = 0;

// NTP servers
static const char* NTP_SERVER1 = "pool.ntp.org";
static const char* NTP_SERVER2 = "time.nist.gov";

void rtc_time_init() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    if (_rtc.begin()) {
        _rtcPresent = true;

        if (_rtc.lostPower()) {
            Serial.println("[RTC] DS3231 lost power — time invalid until NTP sync");
        } else {
            // Set system time from DS3231
            DateTime now = _rtc.now();
            struct timeval tv;
            tv.tv_sec  = now.unixtime();
            tv.tv_usec = 0;
            settimeofday(&tv, nullptr);

            Serial.printf("[RTC] DS3231 time: %s\n",
                          rtc_time_formatISO(now.unixtime()).c_str());
        }
    } else {
        _rtcPresent = false;
        Serial.println("[RTC] DS3231 not found on I2C!");
    }
}

bool rtc_time_hasRTC() {
    return _rtcPresent;
}

bool rtc_time_hasNTP() {
    return _ntpSynced;
}

uint32_t rtc_time_getEpoch() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint32_t)tv.tv_sec;
}

uint8_t rtc_time_getQuality() {
    if (_ntpSynced) return 2;
    if (_rtcPresent && !_rtc.lostPower()) return 1;
    return 0;
}

void rtc_time_syncNTP() {
    Serial.println("[RTC] Attempting NTP sync...");

    configTime(0, 0, NTP_SERVER1, NTP_SERVER2);

    // Wait for NTP with timeout
    int retries = 20;
    time_t now = 0;
    while (now < 1000000 && retries-- > 0) {
        delay(500);
        time(&now);
    }

    if (now > 1000000) {
        _ntpSynced = true;
        _lastNtpSync = millis();

        // Update DS3231 with NTP time
        if (_rtcPresent) {
            _rtc.adjust(DateTime(now));
            Serial.printf("[RTC] DS3231 updated from NTP: %s\n",
                          rtc_time_formatISO(now).c_str());
        }

        Serial.printf("[RTC] NTP synced: %s\n",
                      rtc_time_formatISO(now).c_str());
    } else {
        Serial.println("[RTC] NTP sync failed — using DS3231 time");
    }
}

void rtc_time_loop() {
    // Periodic NTP re-sync
    if (millis() - _lastNtpSync > NTP_SYNC_INTERVAL_MS) {
        rtc_time_syncNTP();
    }
}

String rtc_time_formatISO(uint32_t epoch) {
    time_t t = (time_t)epoch;
    struct tm* tm = gmtime(&t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    return String(buf);
}
