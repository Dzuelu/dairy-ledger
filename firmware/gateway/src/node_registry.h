#pragma once
#include <Arduino.h>
#include <vector>
#include "messages.h"

// ─── Node Registry: track all discovered nodes ──────────────────────
// Nodes are stored in a dynamic std::vector — no hard cap.
// A new node is only rejected if free heap drops below MIN_FREE_HEAP_BYTES.

struct NodeInfo {
    char     node_id[7];
    char     label[33];
    char     probe_labels[MAX_PROBES_PER_NODE][17]; // Per-probe labels (e.g. "Top Shelf")
    uint8_t  mac[6];                    // Sender MAC (for unicast ACKs)
    uint8_t  probe_count;
    float    lastTemps[MAX_PROBES_PER_NODE];
    float    calibOffsets[MAX_PROBES_PER_NODE];
    uint32_t lastSeen;                  // Unix epoch
    uint32_t lastReading;               // Unix epoch of last data
    uint8_t  sd_status;
    uint16_t pending_sync;
    bool     online;                    // Updated by timeout check
    float    warn_high, crit_high;
    float    warn_low,  crit_low;
    uint16_t reading_interval_sec;       // Node sleep/read interval
};

void         registry_init();
void         registry_loop();          // Check for timeouts
NodeInfo*    registry_getNode(const char* nodeId);
NodeInfo*    registry_getOrCreate(const char* nodeId, const uint8_t* mac);
uint16_t     registry_getNodeCount();
NodeInfo*    registry_getNodeByIndex(uint16_t idx);
void         registry_updateFromReading(const SensorReading* msg, const uint8_t* mac);
void         registry_updateFromAnnounce(const AnnounceMessage* msg, const uint8_t* mac);
bool         registry_save();          // Write to SD config.json
bool         registry_load();          // Read from SD config.json
