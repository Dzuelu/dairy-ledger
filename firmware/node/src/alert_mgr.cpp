#include "alert_mgr.h"
#include "config.h"
#include <Adafruit_NeoPixel.h>

// On-board WS2812 RGB LED on GPIO8
static Adafruit_NeoPixel _led(1, PIN_LED, NEO_GRB + NEO_KHZ800);

static float _warnHigh = DEFAULT_WARN_HIGH_C;
static float _critHigh = DEFAULT_CRIT_HIGH_C;
static float _warnLow  = DEFAULT_WARN_LOW_C;
static float _critLow  = DEFAULT_CRIT_LOW_C;

void alert_init() {
    // Buzzer
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    // RGB LED
    _led.begin();
    _led.setBrightness(30);  // Don't blind anyone in a dark fridge
    _led.clear();
    _led.show();

    Serial.printf("[ALERT] Thresholds: warn [%.1f, %.1f] crit [%.1f, %.1f]\n",
                  _warnLow, _warnHigh, _critLow, _critHigh);
}

AlertLevel alert_check(const float* temps, uint8_t probeCount) {
    AlertLevel worst = ALERT_NONE;

    for (uint8_t i = 0; i < probeCount; i++) {
        // Disconnected probe
        if (temps[i] <= -126.0f) {
            worst = ALERT_SENSOR;
            continue;
        }

        // Critical range
        if (temps[i] >= _critHigh || temps[i] <= _critLow) {
            if (worst < ALERT_CRIT) worst = ALERT_CRIT;
            continue;
        }

        // Warning range
        if (temps[i] >= _warnHigh || temps[i] <= _warnLow) {
            if (worst < ALERT_WARN) worst = ALERT_WARN;
        }
    }

    return worst;
}

void alert_setThresholds(float warnHigh, float critHigh,
                         float warnLow, float critLow) {
    _warnHigh = warnHigh;
    _critHigh = critHigh;
    _warnLow  = warnLow;
    _critLow  = critLow;

    Serial.printf("[ALERT] Updated thresholds: warn [%.1f, %.1f] crit [%.1f, %.1f]\n",
                  _warnLow, _warnHigh, _critLow, _critHigh);
}

void alert_showStatus(AlertLevel level) {
    switch (level) {
        case ALERT_NONE:
            _led.setPixelColor(0, _led.Color(0, 20, 0));    // Dim green
            digitalWrite(PIN_BUZZER, LOW);
            break;

        case ALERT_WARN:
            _led.setPixelColor(0, _led.Color(40, 30, 0));   // Yellow
            // Brief chirp
            digitalWrite(PIN_BUZZER, HIGH);
            delay(100);
            digitalWrite(PIN_BUZZER, LOW);
            break;

        case ALERT_CRIT:
            _led.setPixelColor(0, _led.Color(40, 0, 0));    // Red
            // Urgent beep pattern
            for (int i = 0; i < 3; i++) {
                digitalWrite(PIN_BUZZER, HIGH);
                delay(200);
                digitalWrite(PIN_BUZZER, LOW);
                delay(100);
            }
            break;

        case ALERT_SENSOR:
            _led.setPixelColor(0, _led.Color(0, 0, 40));    // Blue = sensor error
            digitalWrite(PIN_BUZZER, HIGH);
            delay(500);
            digitalWrite(PIN_BUZZER, LOW);
            break;
    }

    _led.show();
}

void alert_silence() {
    digitalWrite(PIN_BUZZER, LOW);
}
