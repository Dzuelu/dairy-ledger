#pragma once
#include <Arduino.h>

// ─── ESP-NOW Receive: handle incoming node messages, send ACKs ──────

void espnow_recv_init();
void espnow_recv_loop();    // Process any queued messages
void espnow_sendLabel(const uint8_t* mac, const char* nodeId, const char* label);
void espnow_sendConfig(const uint8_t* mac, const char* nodeId,
                       uint16_t interval, float wh, float ch, float wl, float cl);
