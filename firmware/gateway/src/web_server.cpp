#include "web_server.h"
#include "config.h"
#include "node_registry.h"
#include "rtc_time.h"
#include "sd_storage.h"
#include "espnow_recv.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

static AsyncWebServer _server(WEB_PORT);

// ─── API: GET /api/nodes ─────────────────────────────────────────────
// Returns all registered nodes with latest readings

static void handleGetNodes(AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (uint16_t i = 0; i < registry_getNodeCount(); i++) {
        NodeInfo* n = registry_getNodeByIndex(i);
        if (!n) continue;

        JsonObject obj = arr.add<JsonObject>();
        obj["id"]           = n->node_id;
        obj["label"]        = n->label;
        obj["online"]       = n->online;
        obj["probe_count"]  = n->probe_count;
        obj["sd_status"]    = n->sd_status;
        obj["pending_sync"] = n->pending_sync;
        obj["last_seen"]    = n->lastSeen;
        obj["last_reading"] = n->lastReading;

        JsonArray temps = obj["temperatures"].to<JsonArray>();
        for (uint8_t p = 0; p < n->probe_count; p++) {
            temps.add(serialized(String(n->lastTemps[p], 2)));
        }

        JsonArray plabels = obj["probe_labels"].to<JsonArray>();
        for (uint8_t p = 0; p < n->probe_count; p++) {
            plabels.add(n->probe_labels[p]);
        }

        JsonObject thresholds = obj["thresholds"].to<JsonObject>();
        thresholds["warn_high"] = n->warn_high;
        thresholds["crit_high"] = n->crit_high;
        thresholds["warn_low"]  = n->warn_low;
        thresholds["crit_low"]  = n->crit_low;
    }

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
}

// ─── API: GET /api/node/<id> ─────────────────────────────────────────

static void handleGetNode(AsyncWebServerRequest* req) {
    String nodeId = req->pathArg(0);

    NodeInfo* n = registry_getNode(nodeId.c_str());
    if (!n) {
        req->send(404, "application/json", "{\"error\":\"Node not found\"}");
        return;
    }

    JsonDocument doc;
    doc["id"]           = n->node_id;
    doc["label"]        = n->label;
    doc["online"]       = n->online;
    doc["probe_count"]  = n->probe_count;
    doc["sd_status"]    = n->sd_status;
    doc["pending_sync"] = n->pending_sync;
    doc["last_seen"]    = n->lastSeen;
    doc["last_reading"] = n->lastReading;

    // MAC address
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             n->mac[0], n->mac[1], n->mac[2],
             n->mac[3], n->mac[4], n->mac[5]);
    doc["mac"] = mac;

    JsonArray temps = doc["temperatures"].to<JsonArray>();
    JsonArray calibs = doc["calibration"].to<JsonArray>();
    JsonArray plabels = doc["probe_labels"].to<JsonArray>();
    for (uint8_t p = 0; p < n->probe_count; p++) {
        temps.add(serialized(String(n->lastTemps[p], 2)));
        calibs.add(serialized(String(n->calibOffsets[p], 2)));
        plabels.add(n->probe_labels[p]);
    }

    doc["reading_interval_sec"] = n->reading_interval_sec;

    JsonObject thresholds = doc["thresholds"].to<JsonObject>();
    thresholds["warn_high"] = n->warn_high;
    thresholds["crit_high"] = n->crit_high;
    thresholds["warn_low"]  = n->warn_low;
    thresholds["crit_low"]  = n->crit_low;

    // Available data dates
    String dates = sd_storage_listDates(n->node_id);
    doc["dates"] = serialized(dates);

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
}

// ─── API: POST /api/node/<id>/label ──────────────────────────────────

static void handleSetLabel(AsyncWebServerRequest* req, uint8_t* data,
                           size_t len, size_t index, size_t total) {
    // Body is handled in onBody callback
}

static void handleSetLabelBody(AsyncWebServerRequest* req) {
    String nodeId = req->pathArg(0);
    NodeInfo* n = registry_getNode(nodeId.c_str());
    if (!n) {
        req->send(404, "application/json", "{\"error\":\"Node not found\"}");
        return;
    }

    if (!req->hasParam("label", true)) {
        req->send(400, "application/json", "{\"error\":\"Missing 'label' param\"}");
        return;
    }

    String newLabel = req->getParam("label", true)->value();
    strncpy(n->label, newLabel.c_str(), sizeof(n->label) - 1);

    // Push to node via ESP-NOW (will take effect next time node wakes)
    espnow_sendLabel(n->mac, n->node_id, newLabel.c_str());

    registry_save();
    req->send(200, "application/json", "{\"ok\":true}");
}

// ─── API: POST /api/node/<id>/config ─────────────────────────────────

static void handleSetConfigBody(AsyncWebServerRequest* req) {
    String nodeId = req->pathArg(0);
    NodeInfo* n = registry_getNode(nodeId.c_str());
    if (!n) {
        req->send(404, "application/json", "{\"error\":\"Node not found\"}");
        return;
    }

    // Update thresholds from params
    if (req->hasParam("warn_high", true))
        n->warn_high = req->getParam("warn_high", true)->value().toFloat();
    if (req->hasParam("crit_high", true))
        n->crit_high = req->getParam("crit_high", true)->value().toFloat();
    if (req->hasParam("warn_low", true))
        n->warn_low = req->getParam("warn_low", true)->value().toFloat();
    if (req->hasParam("crit_low", true))
        n->crit_low = req->getParam("crit_low", true)->value().toFloat();
    if (req->hasParam("interval", true))
        n->reading_interval_sec = req->getParam("interval", true)->value().toInt();

    // Push to node
    espnow_sendConfig(n->mac, n->node_id, n->reading_interval_sec,
                      n->warn_high, n->crit_high,
                      n->warn_low, n->crit_low);

    registry_save();
    req->send(200, "application/json", "{\"ok\":true}");
}

// ─── API: GET /api/node/<id>/data?date=YYYY-MM-DD ────────────────────

static void handleGetData(AsyncWebServerRequest* req) {
    String nodeId = req->pathArg(0);

    NodeInfo* n = registry_getNode(nodeId.c_str());
    if (!n) {
        req->send(404, "application/json", "{\"error\":\"Node not found\"}");
        return;
    }

    String date = req->getParam("date")->value();
    String csv = sd_storage_getCSV(nodeId.c_str(), date.c_str());

    if (csv.length() == 0) {
        req->send(404, "text/plain", "No data for this date");
        return;
    }

    req->send(200, "text/csv", csv);
}

// ─── API: GET /api/status ────────────────────────────────────────────

static void handleStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["uptime_sec"]  = millis() / 1000;
    doc["time"]        = rtc_time_getEpoch();
    doc["time_iso"]    = rtc_time_formatISO(rtc_time_getEpoch());
    doc["time_quality"] = rtc_time_getQuality();
    doc["has_rtc"]     = rtc_time_hasRTC();
    doc["has_ntp"]     = rtc_time_hasNTP();
    doc["node_count"]  = registry_getNodeCount();
    doc["free_heap"]   = ESP.getFreeHeap();

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
}

// ─── API: GET /api/export/<nodeId>?start=YYYY-MM-DD&end=YYYY-MM-DD ──

static void handleExport(AsyncWebServerRequest* req) {
    String nodeId = req->pathArg(0);

    NodeInfo* n = registry_getNode(nodeId.c_str());
    if (!n) {
        req->send(404, "text/plain", "Node not found");
        return;
    }

    // For now, single-date export; range export would concatenate
    if (req->hasParam("date")) {
        String date = req->getParam("date")->value();
        String csv = sd_storage_getCSV(nodeId.c_str(), date.c_str());

        AsyncWebServerResponse* resp = req->beginResponse(200, "text/csv", csv);
        resp->addHeader("Content-Disposition",
                        "attachment; filename=" + nodeId + "_" + date + ".csv");
        req->send(resp);
    } else {
        req->send(400, "text/plain", "Missing 'date' parameter");
    }
}

// ─── API: POST /api/node/<id>/probe-labels ───────────────────────────

static void handleSetProbeLabels(AsyncWebServerRequest* req) {
    String nodeId = req->pathArg(0);
    NodeInfo* n = registry_getNode(nodeId.c_str());
    if (!n) {
        req->send(404, "application/json", "{\"error\":\"Node not found\"}");
        return;
    }

    // Probe labels come as probe0, probe1, probe2, ...
    for (uint8_t p = 0; p < n->probe_count && p < MAX_PROBES_PER_NODE; p++) {
        char paramName[8];
        snprintf(paramName, sizeof(paramName), "probe%d", p);
        if (req->hasParam(paramName, true)) {
            String lbl = req->getParam(paramName, true)->value();
            strncpy(n->probe_labels[p], lbl.c_str(),
                    sizeof(n->probe_labels[p]) - 1);
        }
    }

    registry_save();
    req->send(200, "application/json", "{\"ok\":true}");
}

// ─── API: DELETE /api/node/<id>/data ─────────────────────────────────

static void handleDeleteData(AsyncWebServerRequest* req) {
    String nodeId = req->pathArg(0);
    NodeInfo* n = registry_getNode(nodeId.c_str());
    if (!n) {
        req->send(404, "application/json", "{\"error\":\"Node not found\"}");
        return;
    }

    bool ok = sd_storage_clearNodeData(nodeId.c_str());
    if (ok) {
        req->send(200, "application/json", "{\"ok\":true}");
    } else {
        req->send(500, "application/json", "{\"error\":\"Failed to clear data\"}");
    }
}

// ─── API: POST /api/wifi ──────────────────────────────────────────────

static void handleSetWifi(AsyncWebServerRequest* req) {
    if (!req->hasParam("ssid", true)) {
        req->send(400, "application/json", "{\"error\":\"Missing 'ssid'\"}");
        return;
    }

    String ssid = req->getParam("ssid", true)->value();
    String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : "";

    Preferences prefs;
    prefs.begin("dairyledger", false);
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", pass);
    prefs.end();

    req->send(200, "application/json", "{\"ok\":true,\"msg\":\"Restarting...\"}");

    // Restart after response is sent
    delay(500);
    ESP.restart();
}

// ─── API: GET /api/gateway/config ────────────────────────────────────

static void handleGetGatewayConfig(AsyncWebServerRequest* req) {
    Preferences prefs;
    prefs.begin("dairyledger", true);

    JsonDocument doc;
    doc["default_warn_high"] = prefs.getFloat("def_warn_hi", DEFAULT_WARN_HIGH_C);
    doc["default_crit_high"] = prefs.getFloat("def_crit_hi", DEFAULT_CRIT_HIGH_C);
    doc["default_warn_low"]  = prefs.getFloat("def_warn_lo", DEFAULT_WARN_LOW_C);
    doc["default_crit_low"]  = prefs.getFloat("def_crit_lo", DEFAULT_CRIT_LOW_C);
    doc["node_timeout_sec"]  = prefs.getUInt("node_timeout", NODE_TIMEOUT_SEC);
    doc["reading_interval_sec"] = prefs.getUInt("def_interval", 900);
    doc["ntp_sync_interval_min"] = prefs.getUInt("ntp_interval", 60);

    prefs.end();

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
}

// ─── API: POST /api/gateway/config ───────────────────────────────────

static void handleSetGatewayConfig(AsyncWebServerRequest* req) {
    Preferences prefs;
    prefs.begin("dairyledger", false);

    if (req->hasParam("default_warn_high", true))
        prefs.putFloat("def_warn_hi", req->getParam("default_warn_high", true)->value().toFloat());
    if (req->hasParam("default_crit_high", true))
        prefs.putFloat("def_crit_hi", req->getParam("default_crit_high", true)->value().toFloat());
    if (req->hasParam("default_warn_low", true))
        prefs.putFloat("def_warn_lo", req->getParam("default_warn_low", true)->value().toFloat());
    if (req->hasParam("default_crit_low", true))
        prefs.putFloat("def_crit_lo", req->getParam("default_crit_low", true)->value().toFloat());
    if (req->hasParam("node_timeout_sec", true))
        prefs.putUInt("node_timeout", req->getParam("node_timeout_sec", true)->value().toInt());
    if (req->hasParam("reading_interval_sec", true))
        prefs.putUInt("def_interval", req->getParam("reading_interval_sec", true)->value().toInt());
    if (req->hasParam("ntp_sync_interval_min", true))
        prefs.putUInt("ntp_interval", req->getParam("ntp_sync_interval_min", true)->value().toInt());

    prefs.end();

    req->send(200, "application/json", "{\"ok\":true}");
    Serial.println("[WEB] Gateway config updated");
}

// ─── Init ────────────────────────────────────────────────────────────

void web_server_init() {
    // Serve static files from LittleFS (dashboard HTML/CSS/JS)
    if (!LittleFS.begin(true)) {
        Serial.println("[WEB] LittleFS mount failed!");
    }

    // Static files
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // REST API routes
    _server.on("/api/nodes", HTTP_GET, handleGetNodes);
    _server.on("/api/node/%", HTTP_GET, handleGetNode);
    _server.on("/api/node/%/label", HTTP_POST, handleSetLabelBody);
    _server.on("/api/node/%/config", HTTP_POST, handleSetConfigBody);
    _server.on("/api/node/%/probe-labels", HTTP_POST, handleSetProbeLabels);
    _server.on("/api/node/%/data", HTTP_GET, handleGetData);
    _server.on("/api/node/%/data", HTTP_DELETE, handleDeleteData);
    _server.on("/api/status", HTTP_GET, handleStatus);
    _server.on("/api/export/%", HTTP_GET, handleExport);
    _server.on("/api/gateway/config", HTTP_GET, handleGetGatewayConfig);
    _server.on("/api/gateway/config", HTTP_POST, handleSetGatewayConfig);
    _server.on("/api/wifi", HTTP_POST, handleSetWifi);

    // 404 handler
    _server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not Found");
    });

    _server.begin();
    Serial.printf("[WEB] Server started on port %d\n", WEB_PORT);
}
