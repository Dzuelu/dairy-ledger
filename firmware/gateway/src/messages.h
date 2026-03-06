#pragma once
#include <cstdint>
#include "config.h"

// ─── Same message structs as node firmware ───────────────────────────
// Kept in sync — consider extracting to shared lib in future

struct __attribute__((packed)) SensorReading {
    uint8_t  msg_type;
    char     node_id[7];
    uint32_t timestamp;
    uint8_t  probe_count;
    float    temperatures[MAX_PROBES_PER_NODE];
    float    calibration[MAX_PROBES_PER_NODE];
    uint8_t  sd_status;
    uint16_t pending_sync;
    uint8_t  checksum;
};

struct __attribute__((packed)) AckMessage {
    uint8_t  msg_type;
    char     node_id[7];
    uint32_t gateway_time;
    uint8_t  time_quality;
};

struct __attribute__((packed)) LabelUpdate {
    uint8_t  msg_type;
    char     node_id[7];
    char     label[33];
};

struct __attribute__((packed)) ConfigUpdate {
    uint8_t  msg_type;
    char     node_id[7];
    uint16_t reading_interval_sec;
    float    warn_high;
    float    crit_high;
    float    warn_low;
    float    crit_low;
};

struct __attribute__((packed)) AnnounceMessage {
    uint8_t  msg_type;
    char     node_id[7];
    char     label[33];
    uint8_t  probe_count;
    uint8_t  sd_status;
    uint32_t uptime_sec;
};

inline uint8_t computeChecksum(const uint8_t* data, size_t len) {
    uint8_t cs = 0;
    for (size_t i = 0; i < len; i++) cs ^= data[i];
    return cs;
}
