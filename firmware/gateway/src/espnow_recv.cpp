#include "espnow_recv.h"
#include "config.h"
#include "messages.h"
#include "node_registry.h"
#include "rtc_time.h"
#include "sd_storage.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Simple queue for ISR-safe message passing
#define MSG_QUEUE_SIZE 8

struct QueuedMsg {
    uint8_t  mac[6];
    uint8_t  data[250];
    int      len;
    bool     ready;
};

static volatile QueuedMsg _queue[MSG_QUEUE_SIZE];
static volatile uint8_t   _queueHead = 0;

// ─── ISR Callback ───────────────────────────────────────────────────

static void onRecvCb(const esp_now_recv_info_t* info,
                     const uint8_t* data, int len) {
    uint8_t slot = _queueHead;
    uint8_t next = (slot + 1) % MSG_QUEUE_SIZE;

    if (_queue[slot].ready) {
        // Queue full — drop message
        return;
    }

    memcpy((void*)_queue[slot].mac, info->src_addr, 6);
    memcpy((void*)_queue[slot].data, data, min(len, 250));
    _queue[slot].len = len;
    _queue[slot].ready = true;
    _queueHead = next;
}

// ─── Send ACK back to node (unicast) ────────────────────────────────

static void sendAck(const uint8_t* mac, const char* nodeId) {
    // Ensure peer is registered for unicast
    if (!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = ESPNOW_CHANNEL;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    AckMessage ack = {};
    ack.msg_type     = MSG_ACK;
    strncpy(ack.node_id, nodeId, 6);
    ack.node_id[6]   = '\0';
    ack.gateway_time  = rtc_time_getEpoch();
    ack.time_quality  = rtc_time_getQuality();

    esp_now_send(mac, (uint8_t*)&ack, sizeof(ack));
}

// ─── Process queued messages ─────────────────────────────────────────

static void processMessage(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < 1) return;

    uint8_t msgType = data[0];

    switch (msgType) {
        case MSG_READING:
        case MSG_BACKFILL: {
            if ((size_t)len < sizeof(SensorReading)) break;
            const SensorReading* msg = (const SensorReading*)data;

            // Verify checksum
            uint8_t cs = computeChecksum(data, sizeof(SensorReading) - 1);
            if (cs != msg->checksum) {
                Serial.printf("[ESPNOW] Checksum mismatch from %s\n", msg->node_id);
                break;
            }

            // Update registry
            registry_updateFromReading(msg, mac);

            // Determine timestamp — use node's if valid, else gateway's
            uint32_t ts = msg->timestamp;
            if (ts == 0) {
                ts = rtc_time_getEpoch();
            }

            // Log to SD
            sd_storage_logReading(msg->node_id, ts,
                                  msg->temperatures, msg->probe_count);

            // Send ACK (with time sync)
            sendAck(mac, msg->node_id);

            Serial.printf("[ESPNOW] %s from %s: %d probes, ts=%lu\n",
                          (msgType == MSG_READING) ? "Reading" : "Backfill",
                          msg->node_id, msg->probe_count, ts);
            break;
        }

        case MSG_ANNOUNCE: {
            if ((size_t)len < sizeof(AnnounceMessage)) break;
            const AnnounceMessage* msg = (const AnnounceMessage*)data;

            registry_updateFromAnnounce(msg, mac);
            sendAck(mac, msg->node_id);

            Serial.printf("[ESPNOW] Announce from %s (%s), %d probes\n",
                          msg->node_id, msg->label, msg->probe_count);
            break;
        }

        default:
            Serial.printf("[ESPNOW] Unknown message type: 0x%02X\n", msgType);
            break;
    }
}

// ─── Public API ──────────────────────────────────────────────────────

void espnow_recv_init() {
    // Clear queue
    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        _queue[i].ready = false;
    }
    _queueHead = 0;

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] Init FAILED");
        return;
    }

    esp_now_register_recv_cb(onRecvCb);
    Serial.printf("[ESPNOW] Gateway listening on channel %d\n", ESPNOW_CHANNEL);
}

void espnow_recv_loop() {
    // Process all queued messages (called from main loop)
    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        if (_queue[i].ready) {
            processMessage(
                (const uint8_t*)_queue[i].mac,
                (const uint8_t*)_queue[i].data,
                _queue[i].len
            );
            _queue[i].ready = false;
        }
    }
}

void espnow_sendLabel(const uint8_t* mac, const char* nodeId, const char* label) {
    if (!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = ESPNOW_CHANNEL;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    LabelUpdate msg = {};
    msg.msg_type = MSG_SET_LABEL;
    strncpy(msg.node_id, nodeId, 6);
    msg.node_id[6] = '\0';
    strncpy(msg.label, label, 32);
    msg.label[32] = '\0';

    esp_now_send(mac, (uint8_t*)&msg, sizeof(msg));
    Serial.printf("[ESPNOW] Sent label '%s' to %s\n", label, nodeId);
}

void espnow_sendConfig(const uint8_t* mac, const char* nodeId,
                       uint16_t interval, float wh, float ch, float wl, float cl) {
    if (!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = ESPNOW_CHANNEL;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    ConfigUpdate msg = {};
    msg.msg_type = MSG_SET_CONFIG;
    strncpy(msg.node_id, nodeId, 6);
    msg.node_id[6] = '\0';
    msg.reading_interval_sec = interval;
    msg.warn_high = wh;
    msg.crit_high = ch;
    msg.warn_low  = wl;
    msg.crit_low  = cl;

    esp_now_send(mac, (uint8_t*)&msg, sizeof(msg));
    Serial.printf("[ESPNOW] Sent config to %s\n", nodeId);
}
