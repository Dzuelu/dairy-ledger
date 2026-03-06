#include "identity.h"
#include "config.h"
#include <EEPROM.h>

static NodeIdentity _identity;

// Unambiguous charset: no 0/O, 1/I/L
static const char CHARSET[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
static const int CHARSET_LEN = sizeof(CHARSET) - 1; // 29 chars

static void generateId() {
    // Use ESP32 hardware RNG for true randomness
    for (int i = 0; i < NODE_ID_LEN; i++) {
        _identity.node_id[i] = CHARSET[esp_random() % CHARSET_LEN];
    }
    _identity.node_id[NODE_ID_LEN] = '\0';
}

void identity_init() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, _identity);

    if (_identity.magic != EEPROM_MAGIC) {
        // First boot — generate new identity
        memset(&_identity, 0, sizeof(_identity));
        generateId();
        strncpy(_identity.label, "New Node", sizeof(_identity.label) - 1);
        _identity.magic = EEPROM_MAGIC;
        EEPROM.put(0, _identity);
        EEPROM.commit();

        Serial.printf("[IDENTITY] Generated new ID: %s\n", _identity.node_id);
    } else {
        Serial.printf("[IDENTITY] Loaded ID: %s  Label: %s\n",
                       _identity.node_id, _identity.label);
    }
}

const char* identity_getId() {
    return _identity.node_id;
}

const char* identity_getLabel() {
    return _identity.label;
}

void identity_setLabel(const char* newLabel) {
    strncpy(_identity.label, newLabel, sizeof(_identity.label) - 1);
    _identity.label[sizeof(_identity.label) - 1] = '\0';
    EEPROM.put(0, _identity);
    EEPROM.commit();

    Serial.printf("[IDENTITY] Label updated: %s\n", _identity.label);
}
