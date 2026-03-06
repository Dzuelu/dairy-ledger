#pragma once
#include <Arduino.h>

// ─── Sensor Manager: DS18B20 probe discovery, reading, calibration ──

struct ProbeInfo {
    uint8_t address[8];      // 1-Wire 64-bit address
    float   lastReading;     // °C
    float   calibOffset;     // Applied calibration offset
    bool    valid;           // Last read succeeded
};

void     sensor_init();
uint8_t  sensor_getProbeCount();
void     sensor_readAll();
float    sensor_getTemp(uint8_t index);
float    sensor_getCalibOffset(uint8_t index);
bool     sensor_isValid(uint8_t index);
const ProbeInfo* sensor_getProbe(uint8_t index);
void     sensor_setCalibOffset(uint8_t index, float offset);
