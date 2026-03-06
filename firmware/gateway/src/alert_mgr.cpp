#include "alert_mgr.h"
#include "config.h"
#include "node_registry.h"
#include <Adafruit_NeoPixel.h>

static Adafruit_NeoPixel _led(1, PIN_LED, NEO_GRB + NEO_KHZ800);

enum GatewayAlert : uint8_t {
    GW_OK      = 0,   // All nodes in range
    GW_WARN    = 1,   // A node has warning temps
    GW_CRIT    = 2,   // A node has critical temps
    GW_OFFLINE = 3    // A node is offline
};

static GatewayAlert _currentAlert = GW_OK;
static bool         _buzzerAck    = false;  // User acknowledged

void gateway_alert_init() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    _led.begin();
    _led.setBrightness(30);
    _led.setPixelColor(0, _led.Color(0, 20, 0));  // Green = startup OK
    _led.show();
}

void gateway_alert_loop() {
    GatewayAlert worst = GW_OK;

    for (uint16_t i = 0; i < registry_getNodeCount(); i++) {
        NodeInfo* n = registry_getNodeByIndex(i);
        if (!n) continue;

        if (!n->online) {
            if (worst < GW_OFFLINE) worst = GW_OFFLINE;
            continue;
        }

        for (uint8_t p = 0; p < n->probe_count; p++) {
            float t = n->lastTemps[p];
            if (t <= -126.0f) continue;  // Sensor error — node handles this

            if (t >= n->crit_high || t <= n->crit_low) {
                if (worst < GW_CRIT) worst = GW_CRIT;
            } else if (t >= n->warn_high || t <= n->warn_low) {
                if (worst < GW_WARN) worst = GW_WARN;
            }
        }
    }

    // Only update LED/buzzer if status changed
    if (worst == _currentAlert) return;

    _currentAlert = worst;
    _buzzerAck = false;  // New alert resets acknowledgment

    switch (_currentAlert) {
        case GW_OK:
            _led.setPixelColor(0, _led.Color(0, 20, 0));    // Green
            digitalWrite(PIN_BUZZER, LOW);
            break;

        case GW_WARN:
            _led.setPixelColor(0, _led.Color(40, 30, 0));   // Yellow
            if (!_buzzerAck) {
                digitalWrite(PIN_BUZZER, HIGH);
                delay(100);
                digitalWrite(PIN_BUZZER, LOW);
            }
            break;

        case GW_CRIT:
            _led.setPixelColor(0, _led.Color(40, 0, 0));    // Red
            if (!_buzzerAck) {
                for (int i = 0; i < 3; i++) {
                    digitalWrite(PIN_BUZZER, HIGH);
                    delay(200);
                    digitalWrite(PIN_BUZZER, LOW);
                    delay(100);
                }
            }
            break;

        case GW_OFFLINE:
            _led.setPixelColor(0, _led.Color(20, 0, 20));   // Purple
            if (!_buzzerAck) {
                digitalWrite(PIN_BUZZER, HIGH);
                delay(300);
                digitalWrite(PIN_BUZZER, LOW);
            }
            break;
    }

    _led.show();
}

void gateway_alert_silence() {
    _buzzerAck = true;
    digitalWrite(PIN_BUZZER, LOW);
}
