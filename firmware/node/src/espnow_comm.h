#pragma once
#include <Arduino.h>

// ─── ESP-NOW Communication: broadcast readings, receive ACKs ────────

typedef void (*OnAckCallback)(uint32_t gatewayTime, uint8_t timeQuality);
typedef void (*OnLabelCallback)(const char* newLabel);
typedef void (*OnConfigCallback)(uint16_t interval, float wh, float ch,
                                 float wl, float cl);

void espnow_init();
void espnow_send(const uint8_t* data, size_t len);
bool espnow_waitForAck(uint32_t timeoutMs);  // Blocks until ACK or timeout
void espnow_onAck(OnAckCallback cb);
void espnow_onLabel(OnLabelCallback cb);
void espnow_onConfig(OnConfigCallback cb);
bool espnow_lastSendOk();
