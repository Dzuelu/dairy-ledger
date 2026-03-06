#pragma once
#include <cstdint>
#include "config.h"

// ─── Node → Gateway: Sensor Reading ─────────────────────────────────
// Size: 62 bytes (well within ESP-NOW 250-byte limit)
struct __attribute__((packed)) SensorReading {
    uint8_t  msg_type;                       // MSG_READING or MSG_BACKFILL
    char     node_id[NODE_ID_LEN + 1];       // Null-terminated 6-char ID
    uint32_t timestamp;                      // Unix epoch (0 = not synced)
    uint8_t  probe_count;                    // Number of valid readings
    float    temperatures[MAX_PROBES];       // °C readings
    float    calibration[MAX_PROBES];        // Applied offsets
    uint8_t  sd_status;                      // SDHealth enum value
    uint16_t pending_sync;                   // Unsynced row count
    uint8_t  checksum;                       // XOR checksum of payload
};

// ─── Gateway → Node: Acknowledgment ─────────────────────────────────
struct __attribute__((packed)) AckMessage {
    uint8_t  msg_type;                       // MSG_ACK
    char     node_id[NODE_ID_LEN + 1];       // Which node this is for
    uint32_t gateway_time;                   // Current epoch from DS3231
    uint8_t  time_quality;                   // 0=no_rtc, 1=rtc_only, 2=ntp_synced
};

// ─── Gateway → Node: Push Label ─────────────────────────────────────
struct __attribute__((packed)) LabelUpdate {
    uint8_t  msg_type;                       // MSG_SET_LABEL
    char     node_id[NODE_ID_LEN + 1];       // Target node
    char     label[NODE_LABEL_LEN + 1];      // New label to store in EEPROM
};

// ─── Gateway → Node: Push Config ────────────────────────────────────
struct __attribute__((packed)) ConfigUpdate {
    uint8_t  msg_type;                       // MSG_SET_CONFIG
    char     node_id[NODE_ID_LEN + 1];       // Target node
    uint16_t reading_interval_sec;           // New interval (0 = keep current)
    float    warn_high;
    float    crit_high;
    float    warn_low;
    float    crit_low;
};

// ─── Node → Gateway: Announce (first boot / heartbeat) ──────────────
struct __attribute__((packed)) AnnounceMessage {
    uint8_t  msg_type;                       // MSG_ANNOUNCE
    char     node_id[NODE_ID_LEN + 1];
    char     label[NODE_LABEL_LEN + 1];
    uint8_t  probe_count;
    uint8_t  sd_status;                      // SDHealth enum value
    uint32_t uptime_sec;                     // Seconds since last cold boot
};

// ─── Utility ─────────────────────────────────────────────────────────
inline uint8_t computeChecksum(const uint8_t* data, size_t len) {
    uint8_t cs = 0;
    for (size_t i = 0; i < len; i++) {
        cs ^= data[i];
    }
    return cs;
}
