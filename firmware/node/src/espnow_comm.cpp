#include "espnow_comm.h"
#include "config.h"
#include "messages.h"
#include "identity.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

static volatile bool _ackReceived = false;
static volatile bool _sendOk      = false;

static OnAckCallback    _onAckCb    = nullptr;
static OnLabelCallback  _onLabelCb  = nullptr;
static OnConfigCallback _onConfigCb = nullptr;

// Broadcast address — all nodes send here, gateway listens
static const uint8_t BROADCAST_ADDR[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─── Callbacks ───────────────────────────────────────────────────────

static void onSendCb(const uint8_t* mac, esp_now_send_status_t status) {
    _sendOk = (status == ESP_NOW_SEND_SUCCESS);
    if (!_sendOk) {
        Serial.println("[ESPNOW] Send FAILED (no ACK from peer)");
    }
}

static void onRecvCb(const esp_now_recv_info_t* info,
                     const uint8_t* data, int len) {
    if (len < 1) return;

    uint8_t msgType = data[0];
    const char* myId = identity_getId();

    switch (msgType) {
        case MSG_ACK: {
            if ((size_t)len < sizeof(AckMessage)) break;
            const AckMessage* ack = (const AckMessage*)data;

            // Ignore ACKs meant for other nodes
            if (strncmp(ack->node_id, myId, NODE_ID_LEN) != 0) break;

            _ackReceived = true;
            if (_onAckCb) {
                _onAckCb(ack->gateway_time, ack->time_quality);
            }

            Serial.printf("[ESPNOW] ACK received (time=%lu, quality=%d)\n",
                          ack->gateway_time, ack->time_quality);
            break;
        }

        case MSG_SET_LABEL: {
            if ((size_t)len < sizeof(LabelUpdate)) break;
            const LabelUpdate* upd = (const LabelUpdate*)data;

            if (strncmp(upd->node_id, myId, NODE_ID_LEN) != 0) break;

            if (_onLabelCb) {
                _onLabelCb(upd->label);
            }

            Serial.printf("[ESPNOW] Label update: %s\n", upd->label);
            break;
        }

        case MSG_SET_CONFIG: {
            if ((size_t)len < sizeof(ConfigUpdate)) break;
            const ConfigUpdate* cfg = (const ConfigUpdate*)data;

            if (strncmp(cfg->node_id, myId, NODE_ID_LEN) != 0) break;

            if (_onConfigCb) {
                _onConfigCb(cfg->reading_interval_sec,
                            cfg->warn_high, cfg->crit_high,
                            cfg->warn_low,  cfg->crit_low);
            }

            Serial.printf("[ESPNOW] Config update: interval=%d, thresholds=[%.1f,%.1f,%.1f,%.1f]\n",
                          cfg->reading_interval_sec,
                          cfg->warn_low, cfg->warn_high,
                          cfg->crit_low, cfg->crit_high);
            break;
        }

        default:
            Serial.printf("[ESPNOW] Unknown message type: 0x%02X\n", msgType);
            break;
    }
}

// ─── Public API ──────────────────────────────────────────────────────

void espnow_init() {
    // WiFi must be initialized for ESP-NOW (STA mode, no connection needed)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Lock to configured channel
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] Init FAILED");
        return;
    }

    esp_now_register_send_cb(onSendCb);
    esp_now_register_recv_cb(onRecvCb);

    // Add broadcast peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_ADDR, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;

    if (!esp_now_is_peer_exist(BROADCAST_ADDR)) {
        esp_now_add_peer(&peer);
    }

    Serial.printf("[ESPNOW] Initialized on channel %d\n", ESPNOW_CHANNEL);
}

void espnow_send(const uint8_t* data, size_t len) {
    _ackReceived = false;
    _sendOk = false;

    esp_err_t result = esp_now_send(BROADCAST_ADDR, data, len);
    if (result != ESP_OK) {
        Serial.printf("[ESPNOW] esp_now_send error: %d\n", result);
    }
}

bool espnow_waitForAck(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (!_ackReceived && (millis() - start) < timeoutMs) {
        delay(1);  // Yield to WiFi task
    }
    return _ackReceived;
}

void espnow_onAck(OnAckCallback cb)       { _onAckCb = cb; }
void espnow_onLabel(OnLabelCallback cb)    { _onLabelCb = cb; }
void espnow_onConfig(OnConfigCallback cb)  { _onConfigCb = cb; }

bool espnow_lastSendOk() { return _sendOk; }
