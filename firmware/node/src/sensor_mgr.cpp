#include "sensor_mgr.h"
#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>

static OneWire           _oneWire(PIN_ONEWIRE);
static DallasTemperature _sensors(&_oneWire);
static ProbeInfo         _probes[MAX_PROBES];
static uint8_t           _probeCount = 0;

void sensor_init() {
    _sensors.begin();
    _sensors.setResolution(ONEWIRE_RESOLUTION);
    _sensors.setWaitForConversion(true);

    _probeCount = min((int)_sensors.getDeviceCount(), MAX_PROBES);

    for (uint8_t i = 0; i < _probeCount; i++) {
        _sensors.getAddress(_probes[i].address, i);
        _probes[i].calibOffset = 0.0f;
        _probes[i].lastReading = -127.0f;
        _probes[i].valid = false;
    }

    Serial.printf("[SENSOR] Found %d probe(s) on GPIO%d\n", _probeCount, PIN_ONEWIRE);

    // Print addresses for wiring identification
    for (uint8_t i = 0; i < _probeCount; i++) {
        Serial.printf("[SENSOR]   Probe %d: ", i);
        for (uint8_t j = 0; j < 8; j++) {
            Serial.printf("%02X", _probes[i].address[j]);
        }
        Serial.println();
    }
}

uint8_t sensor_getProbeCount() {
    return _probeCount;
}

void sensor_readAll() {
    _sensors.requestTemperatures();

    for (uint8_t i = 0; i < _probeCount; i++) {
        float raw = _sensors.getTempC(_probes[i].address);

        if (raw == DEVICE_DISCONNECTED_C || raw < -55.0f || raw > 125.0f) {
            _probes[i].valid = false;
            _probes[i].lastReading = -127.0f;
            Serial.printf("[SENSOR] Probe %d: DISCONNECTED\n", i);
        } else {
            _probes[i].valid = true;
            _probes[i].lastReading = raw + _probes[i].calibOffset;
            Serial.printf("[SENSOR] Probe %d: %.2f°C (raw %.2f + offset %.2f)\n",
                          i, _probes[i].lastReading, raw, _probes[i].calibOffset);
        }
    }
}

float sensor_getTemp(uint8_t index) {
    if (index >= _probeCount) return -127.0f;
    return _probes[index].lastReading;
}

float sensor_getCalibOffset(uint8_t index) {
    if (index >= _probeCount) return 0.0f;
    return _probes[index].calibOffset;
}

bool sensor_isValid(uint8_t index) {
    if (index >= _probeCount) return false;
    return _probes[index].valid;
}

const ProbeInfo* sensor_getProbe(uint8_t index) {
    if (index >= _probeCount) return nullptr;
    return &_probes[index];
}

void sensor_setCalibOffset(uint8_t index, float offset) {
    if (index >= _probeCount) return;
    _probes[index].calibOffset = offset;
    Serial.printf("[SENSOR] Probe %d calibration offset set to %.2f\n", index, offset);
}
