#include "node_registry.h"
#include "config.h"
#include "rtc_time.h"
#include <ArduinoJson.h>
#include <SD.h>

static std::vector<NodeInfo> _nodes;
static uint32_t  _lastSave  = 0;

void registry_init() {
    _nodes.clear();
    _nodes.reserve(8);  // Pre-allocate for typical farm size

    if (registry_load()) {
        Serial.printf("[REGISTRY] Loaded %d node(s) from SD\n", (int)_nodes.size());
    } else {
        Serial.println("[REGISTRY] No saved registry (fresh start)");
    }
}

void registry_loop() {
    uint32_t now = rtc_time_getEpoch();

    // Mark nodes offline if not heard from
    for (size_t i = 0; i < _nodes.size(); i++) {
        if (_nodes[i].online && _nodes[i].lastSeen > 0) {
            if (now - _nodes[i].lastSeen > NODE_TIMEOUT_SEC) {
                _nodes[i].online = false;
                Serial.printf("[REGISTRY] Node %s went offline\n",
                              _nodes[i].node_id);
            }
        }
    }

    // Periodic save
    if (millis() - _lastSave > REGISTRY_SAVE_INTERVAL) {
        registry_save();
        _lastSave = millis();
    }
}

NodeInfo* registry_getNode(const char* nodeId) {
    for (size_t i = 0; i < _nodes.size(); i++) {
        if (strncmp(_nodes[i].node_id, nodeId, 6) == 0) {
            return &_nodes[i];
        }
    }
    return nullptr;
}

NodeInfo* registry_getOrCreate(const char* nodeId, const uint8_t* mac) {
    NodeInfo* existing = registry_getNode(nodeId);
    if (existing) {
        // Update MAC in case it changed (shouldn't, but be safe)
        memcpy(existing->mac, mac, 6);
        return existing;
    }

    // Safety check — don't exhaust heap
    if (ESP.getFreeHeap() < MIN_FREE_HEAP_BYTES) {
        Serial.printf("[REGISTRY] Low memory (%u bytes free) — cannot add node\n",
                      ESP.getFreeHeap());
        return nullptr;
    }

    // Create new entry
    _nodes.push_back(NodeInfo{});
    NodeInfo* node = &_nodes.back();
    memset(node, 0, sizeof(NodeInfo));
    strncpy(node->node_id, nodeId, 6);
    node->node_id[6] = '\0';
    strncpy(node->label, "New Node", sizeof(node->label) - 1);
    memcpy(node->mac, mac, 6);
    node->online = true;

    // Default probe labels
    for (uint8_t i = 0; i < MAX_PROBES_PER_NODE; i++) {
        snprintf(node->probe_labels[i], sizeof(node->probe_labels[i]),
                 "Probe %d", i + 1);
    }

    // Default thresholds
    node->warn_high = DEFAULT_WARN_HIGH_C;
    node->crit_high = DEFAULT_CRIT_HIGH_C;
    node->warn_low  = DEFAULT_WARN_LOW_C;
    node->crit_low  = DEFAULT_CRIT_LOW_C;

    Serial.printf("[REGISTRY] New node registered: %s (MAC=%02X:%02X:%02X:%02X:%02X:%02X) [%d total, %u heap free]\n",
                  nodeId, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  (int)_nodes.size(), ESP.getFreeHeap());

    registry_save();
    return node;
}

uint16_t registry_getNodeCount() {
    return (uint16_t)_nodes.size();
}

NodeInfo* registry_getNodeByIndex(uint16_t idx) {
    if (idx >= _nodes.size()) return nullptr;
    return &_nodes[idx];
}

void registry_updateFromReading(const SensorReading* msg, const uint8_t* mac) {
    NodeInfo* node = registry_getOrCreate(msg->node_id, mac);
    if (!node) return;

    node->probe_count  = msg->probe_count;
    node->sd_status    = msg->sd_status;
    node->pending_sync = msg->pending_sync;
    node->lastSeen     = rtc_time_getEpoch();
    node->lastReading  = msg->timestamp;
    node->online       = true;

    for (uint8_t i = 0; i < msg->probe_count && i < MAX_PROBES_PER_NODE; i++) {
        node->lastTemps[i]   = msg->temperatures[i];
        node->calibOffsets[i] = msg->calibration[i];
    }
}

void registry_updateFromAnnounce(const AnnounceMessage* msg, const uint8_t* mac) {
    NodeInfo* node = registry_getOrCreate(msg->node_id, mac);
    if (!node) return;

    strncpy(node->label, msg->label, sizeof(node->label) - 1);
    node->probe_count = msg->probe_count;
    node->sd_status   = msg->sd_status;
    node->lastSeen    = rtc_time_getEpoch();
    node->online      = true;
}

// ─── Persistence ─────────────────────────────────────────────────────

bool registry_save() {
    JsonDocument doc;
    JsonArray nodesArr = doc["nodes"].to<JsonArray>();

    for (size_t i = 0; i < _nodes.size(); i++) {
        JsonObject n = nodesArr.add<JsonObject>();
        n["id"]          = _nodes[i].node_id;
        n["label"]       = _nodes[i].label;
        n["probes"]      = _nodes[i].probe_count;
        n["warn_high"]   = _nodes[i].warn_high;
        n["crit_high"]   = _nodes[i].crit_high;
        n["warn_low"]    = _nodes[i].warn_low;
        n["crit_low"]    = _nodes[i].crit_low;
        n["interval"]    = _nodes[i].reading_interval_sec;

        // Probe labels
        JsonArray plabels = n["probe_labels"].to<JsonArray>();
        for (uint8_t p = 0; p < _nodes[i].probe_count; p++) {
            plabels.add(_nodes[i].probe_labels[p]);
        }

        // Store MAC as hex string
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 _nodes[i].mac[0], _nodes[i].mac[1], _nodes[i].mac[2],
                 _nodes[i].mac[3], _nodes[i].mac[4], _nodes[i].mac[5]);
        n["mac"] = macStr;
    }

    File f = SD.open(SD_CONFIG_FILE, FILE_WRITE);
    if (!f) {
        Serial.println("[REGISTRY] Failed to save config.json");
        return false;
    }

    serializeJsonPretty(doc, f);
    f.close();

    Serial.printf("[REGISTRY] Saved %d nodes to %s\n", (int)_nodes.size(), SD_CONFIG_FILE);
    return true;
}

bool registry_load() {
    if (!SD.exists(SD_CONFIG_FILE)) return false;

    File f = SD.open(SD_CONFIG_FILE, FILE_READ);
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[REGISTRY] JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonArray nodesArr = doc["nodes"].as<JsonArray>();
    _nodes.clear();

    for (JsonObject n : nodesArr) {
        _nodes.push_back(NodeInfo{});
        NodeInfo* node = &_nodes.back();
        memset(node, 0, sizeof(NodeInfo));

        strncpy(node->node_id, n["id"] | "??????", 6);
        node->node_id[6] = '\0';
        strncpy(node->label, n["label"] | "Unknown", sizeof(node->label) - 1);
        node->probe_count = n["probes"] | 0;
        node->warn_high   = n["warn_high"] | DEFAULT_WARN_HIGH_C;
        node->crit_high   = n["crit_high"] | DEFAULT_CRIT_HIGH_C;
        node->warn_low    = n["warn_low"]  | DEFAULT_WARN_LOW_C;
        node->crit_low    = n["crit_low"]  | DEFAULT_CRIT_LOW_C;
        node->reading_interval_sec = n["interval"] | 900;
        node->online      = false;  // Will go online when we hear from it

        // Load probe labels (default to "Probe N")
        JsonArray plabels = n["probe_labels"].as<JsonArray>();
        for (uint8_t p = 0; p < MAX_PROBES_PER_NODE; p++) {
            if (plabels && p < plabels.size()) {
                strncpy(node->probe_labels[p],
                        plabels[p].as<const char*>(),
                        sizeof(node->probe_labels[p]) - 1);
            } else {
                snprintf(node->probe_labels[p],
                         sizeof(node->probe_labels[p]),
                         "Probe %d", p + 1);
            }
        }

        // Parse MAC
        const char* macStr = n["mac"] | "00:00:00:00:00:00";
        sscanf(macStr, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX",
               &node->mac[0], &node->mac[1], &node->mac[2],
               &node->mac[3], &node->mac[4], &node->mac[5]);
    }

    return true;
}
