#pragma once
#include <Arduino.h>

// ─── Identity: auto-generated 6-char node ID, EEPROM-persisted ──────
// Charset avoids ambiguous characters (0/O, 1/I/L)

struct NodeIdentity {
    char    node_id[7];    // 6 chars + null
    char    label[33];     // 32 chars + null
    uint8_t magic;         // 0xA5 = valid
    uint8_t reserved[23];  // Pad to 64 bytes
};

void     identity_init();
const char* identity_getId();
const char* identity_getLabel();
void     identity_setLabel(const char* newLabel);
