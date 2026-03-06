# � DairyLedger — Goat Farm Temperature Monitoring System

## Project Plan — Cheese & Yogurt Production Fridge Monitoring

---

## 1. Overview

A distributed temperature monitoring system for a goat farm's cheese and yogurt
production facility. Each refrigeration unit gets a dedicated ESP32 node with
multiple high-accuracy temperature probes. Nodes communicate via ESP-NOW
(peer-to-peer, no WiFi infrastructure required) to a central gateway ESP32,
which also hosts an embedded web dashboard over WiFi (SoftAP). No Raspberry Pi
or external computer required — the gateway is the entire hub. Each node also
maintains local backup logs on removable SD cards for redundancy and easy
manual data collection.

### Why This Matters

FDA 21 CFR Part 117 (FSMA) and state dairy regulations typically require:

- Refrigerated storage of dairy products at **≤ 41°F (5°C)**
- **Temperature logs** with timestamps retained for inspection
- Ability to demonstrate corrective action if temps go out of range
- Records kept for **1–2 years** depending on jurisdiction

This system automates compliance and provides alerting when temps drift.

---

## 2. Requirements

### 2.1 Functional Requirements

| ID    | Requirement                                                                 | Priority |
|-------|-----------------------------------------------------------------------------|----------|
| FR-01 | Each fridge node reads multiple temperature probes on a configurable interval | Must     |
| FR-02 | Readings include real timestamp (NTP-synced or RTC-backed)                   | Must     |
| FR-03 | Nodes transmit readings to a central gateway via ESP-NOW                     | Must     |
| FR-04 | Each node logs readings locally to a microSD card as CSV                      | Must     |
| FR-05 | Gateway aggregates all node data on its SD card                              | Must     |
| FR-06 | Gateway provides alerting (buzzer + dashboard) when temp exceeds threshold    | Must     |
| FR-07 | SD card logs are human-readable (CSV) and easy to pull by farm staff          | Must     |
| FR-08 | System survives power blips (auto-recovers, no data loss)                     | Must     |
| FR-09 | Web dashboard showing current temps per fridge with visual status              | Must     |
| FR-10 | Historical data export for regulatory compliance                              | Should   |
| FR-11 | Each node and probe has a human-readable label (e.g. "Cheese Cave #2 — Top")  | Must     |
| FR-12 | Nodes and probes can be added/removed/renamed via dashboard UI (no code edits) | Must     |
| FR-13 | Dashboard self-hosted on gateway ESP32 WiFi AP — accessible from phone/tablet  | Must     |
| FR-14 | Dashboard shows connection status and last-seen time for each node             | Must     |

### 2.2 Accuracy & Compliance Requirements

| ID    | Requirement                                                                 | Priority |
|-------|-----------------------------------------------------------------------------|----------|
| AC-01 | Probe accuracy of **±0.5°C or better** across 0°C–10°C operating range      | Must     |
| AC-02 | Probes should be **food-safe / waterproof** (stainless steel tip)            | Must     |
| AC-03 | Timestamp accuracy within **±1 second** (synced from gateway via ESP-NOW)    | Must     |
| AC-04 | Readings logged at minimum every **15 minutes** (configurable down to 1 min) | Must     |
| AC-05 | Calibration offset per probe stored in config (field-calibratable)           | Should   |

### 2.3 Redundancy & Reliability

| ID    | Requirement                                                                 | Priority |
|-------|-----------------------------------------------------------------------------|----------|
| RR-01 | **Dual logging**: every reading goes to both node SD card AND gateway SD     | Must     |
| RR-02 | If ESP-NOW link is down, node continues logging to SD silently               | Must     |
| RR-03 | Gateway stores aggregated data on its own SD card (CSV files)                | Must     |
| RR-04 | SD card logs rotate daily (`2026-02-15.csv`) for easy retrieval              | Should   |
| RR-05 | Visual indicator (LED) on node: green = OK, red = temp alarm or comms fault  | Should   |
| RR-06 | Watchdog timer to auto-reboot node if firmware hangs                         | Must     |

---

## 3. System Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        GOAT FARM FACILITY                           │
│                                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐              │
│  │  FRIDGE #1    │  │  FRIDGE #2    │  │  FRIDGE #N    │              │
│  │              │  │              │  │              │              │
│  │  ┌────────┐  │  │  ┌────────┐  │  │  ┌────────┐  │              │
│  │  │ ESP32  │  │  │  │ ESP32  │  │  │  │ ESP32  │  │              │
│  │  │ Node   │  │  │  │ Node   │  │  │  │ Node   │  │              │
│  │  │        │  │  │  │        │  │  │  │        │  │              │
│  │  │ SD Card│  │  │  │ SD Card│  │  │  │ SD Card│  │              │
│  │  └───┬────┘  │  │  └───┬────┘  │  │  └───┬────┘  │              │
│  │      │       │  │      │       │  │      │       │              │
│  │  ┌───┴───┐   │  │  ┌───┴───┐   │  │  ┌───┴───┐   │              │
│  │  │DS18B20│   │  │  │DS18B20│   │  │  │DS18B20│   │              │
│  │  │ x2-3  │   │  │  │ x2-3  │   │  │  │ x2-3  │   │              │
│  │  └───────┘   │  │  └───────┘   │  │  └───────┘   │              │
│  └──────────────┘  └──────────────┘  └──────────────┘              │
│         │                 │                 │                       │
│         └────── ESP-NOW ──┴──── ESP-NOW ────┘                       │
│                           │                                         │
│                ┌──────────┴──────────┐                               │
│                │  GATEWAY ESP32       │                               │
│                │  + SD Card (hub log) │                               │
│                │  + RTC (master clock)│                               │
│                │  + Buzzer (alerts)   │                               │
│                │                     │                               │
│                │  WiFi AP: "DairyLedger"│  ← phone/tablet connects     │
│                │  Web Dashboard       │    to view temps, manage      │
│                │  (ESPAsyncWebServer) │    nodes, export data         │
│                └─────────────────────┘                               │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.1 Communication Flow

```
1. Node boots → reads node_id + label from EEPROM (or generates new ID on first boot)
   → sd_status initialized in RTC memory (survives deep sleep)
   → If cold boot: runs one-time scan to rebuild backfill_pos (see §3.6)
   → Determines ESP-NOW channel (see §3.3 channel note)
   → If cold boot AND no time yet: uses millis()-based relative timestamps
     until first gateway ACK provides real time (see §3.8)
2. Node wakes from deep sleep (or timer fires)
3. Reads all DS18B20 probes on 1-Wire bus
4. Timestamps reading (from internal RTC, synced via gateway — see §3.8)
5. Attempts to append CSV row to SD card (synced=N)
   → Records the byte offset of this row in memory
   → If write fails: sd_status set in memory (see §3.7), trigger buzzer + LED
   → Reading is NOT lost — still transmitted to gateway in step 6
6. Broadcasts current reading via ESP-NOW (includes sd_status)
   → broadcast to FF:FF:FF:FF:FF:FF — no pairing needed (see §3.3)
7. Gateway receives broadcast, ACKs via unicast (ACK includes current time),
   appends reading to its own SD log
   → Node receives ACK → updates internal clock if drift > 0.5s
   → pending_sync stays at 0 for this row
   No ACK → pending_sync++, backfill_pos set to this row's offset if first miss
8. If pending_sync > 0 → node reads up to 5 rows from backfill_pos
   and sends as MSG_BACKFILL (sequential reads only, no file scanning)
9. Checks for inbound config messages (label rename, threshold update)
10. Node sleeps until next interval
```

### 3.2 Basement Relay Node

Two fridges are in a **basement**. ESP-NOW signal may be degraded through
floor/ceiling between basement and main floor where the gateway lives.

**Strategy:**
1. **Phase 1 test**: temporarily place a node in the basement and check
   signal strength / packet loss to the main-floor gateway
2. **If signal is OK**: no relay needed, proceed normally
3. **If signal is weak**: deploy a **relay node** at the top of the
   basement stairs. This is just a bare ESP32 that:
   - Receives ESP-NOW packets from basement nodes
   - Re-broadcasts them to the gateway on the main floor
   - No probes, no SD card needed (though adding an SD is cheap insurance)

```
  Basement Fridges ──ESP-NOW──> Relay (top of stairs) ──ESP-NOW──> Gateway
```

Relay firmware is a simple pass-through — minimal code.

### 3.3 ESP-NOW Details

- **Protocol**: ESP-NOW (Espressif peer-to-peer, 2.4GHz)
- **Range**: ~200m line-of-sight, ~50–80m through walls
- **Payload**: 250 bytes max per message (plenty for temp readings)
- **Latency**: < 10ms
- **No WiFi router needed** — works even with zero infrastructure
- **Encryption**: ESP-NOW supports CCMP encryption (AES-128) for unicast only

#### Auto-Pairing — How Plug-and-Play Works

ESP-NOW requires calling `esp_now_add_peer()` before you can **send** to a
specific device. However, **receiving** works without any pairing — the recv
callback fires for any ESP-NOW frame on the same channel. This means:

1. **Nodes broadcast** their readings to `FF:FF:FF:FF:FF:FF` (broadcast MAC).
   Broadcasting requires no pre-registered peers — any node can start
   sending immediately on first boot with zero configuration.
2. **Gateway receives all broadcasts** — the recv callback captures every
   ESP-NOW packet, regardless of sender. No pairing needed on the rx side.
3. **Gateway auto-registers the sender** — when it sees a new MAC address
   for the first time, it calls `esp_now_add_peer()` with that MAC so it
   can send ACKs, label pushes, and config updates back to the node.
4. **Peer list is ephemeral** — stored in RAM, rebuilt automatically as
   nodes check in. If the gateway reboots, nodes re-appear on their next
   reading cycle (≤ 15 minutes).

```
Node (first boot)                    Gateway
─────────────────                    ───────
  Generate ID "KF7B2X"                  │
  Store in EEPROM                       │
  │                                     │
  ├── ESP-NOW broadcast ───────────────>│  recv_cb fires
  │   (MSG_READING, src=AA:BB:CC:01)    │  "New MAC! Adding peer..."
  │                                     │  esp_now_add_peer(AA:BB:CC:01)
  │                                     │  Register node KF7B2X in config
  │<── ESP-NOW unicast (MSG_ACK) ───────┤  Now can send to this node
  │                                     │
  │   ... 15 min later ...              │
  │                                     │
  ├── ESP-NOW broadcast ───────────────>│  Known node, just log + ACK
  │<── ESP-NOW unicast (MSG_ACK) ───────┤
```

**Why broadcast instead of unicast from nodes?**
- Nodes don't need to know the gateway's MAC address at compile time
- A new node works out of the box — flash firmware, power on, done
- Relay nodes can also hear and re-broadcast without pairing
- Slight downside: broadcast doesn't support ESP-NOW encryption, but
  for temp readings on a private farm this is not a security concern

**Peer limit:** ESP32 supports up to 20 registered peers (10 encrypted +
10 unencrypted). With 5 nodes + 1 relay = 6 peers, we're well within limits.
Scalable up to ~17 nodes before needing peer management.

### 3.4 Node Identity System

Each node auto-generates a **short unique ID** on first boot — a 6-character
alphanumeric string that's easy for humans to read and say aloud (e.g.
`KF7B2X`, `W3MN9A`). This avoids needing to manually set IDs in firmware.

**How it works:**
1. First boot → node checks EEPROM for an existing ID
2. If empty → generates a random 6-char ID from `A-Z, 0-9` (no ambiguous
   chars like `0/O`, `1/I/L` — uses charset `ABCDEFGHJKMNPQRSTUVWXYZ23456789`)
3. Stores ID + label in EEPROM → survives power outages and reboots
4. Node broadcasts this ID in every ESP-NOW message
5. Hub sees a new ID → auto-registers it as an "Unconfigured Node"
6. Farm staff names it via the Admin page → hub pushes the label back to
   the node via ESP-NOW → node saves the label in EEPROM

**Why 6 characters?** `30^6 = 729 million` possible IDs — collision is
effectively impossible for a system with 5–50 nodes. Easy to read off a
sticker, say over the phone, or spot in a log file.

**EEPROM layout** (64 bytes reserved):
```c
struct NodeIdentity {
    char     node_id[7];       // 6-char ID + null terminator
    char     label[33];        // Human label: "Cheese Fridge" + null
    uint8_t  magic;            // 0xA5 = valid data present
    uint8_t  reserved[23];     // Future use
};
// Stored at EEPROM address 0x00
```

### 3.5 Message Format (ESP-NOW Payload)

```c
// Message type identifiers
#define MSG_READING     0x01   // Node → Gateway: sensor reading
#define MSG_BACKFILL    0x02   // Node → Gateway: historical unsynced reading
#define MSG_ACK         0x10   // Gateway → Node: acknowledge receipt
#define MSG_SET_LABEL   0x20   // Gateway → Node: push new label to EEPROM
#define MSG_SET_CONFIG  0x21   // Gateway → Node: push threshold/interval changes
#define MSG_ANNOUNCE    0x30   // Node → Gateway: "I exist" (first boot / periodic)

// Sensor reading payload (MSG_READING / MSG_BACKFILL)
typedef struct {
    uint8_t  msg_type;          // MSG_READING or MSG_BACKFILL
    char     node_id[7];        // 6-char unique ID (e.g. "KF7B2X")
    uint32_t timestamp;         // Unix epoch (synced from gateway, see §3.8)
    uint8_t  probe_count;       // Number of probes on this node
    float    temperatures[6];   // Up to 6 probe readings (°C)
    float    calibration[6];    // Applied calibration offsets
    uint8_t  sd_status;         // 0=OK, 1=MISSING, 2=READONLY, 3=WRITE_ERR, 4=FS_ERR
    uint16_t pending_sync;      // Count of unsynced readings on SD
    uint8_t  checksum;          // CRC8 for data integrity
} __attribute__((packed)) SensorReading;
// Total: ~62 bytes — well within 250-byte ESP-NOW limit

// ACK payload (MSG_ACK)  Gateway → Node
typedef struct {
    uint8_t  msg_type;          // MSG_ACK
    char     node_id[7];        // Target node
    uint32_t gateway_time;      // Current gateway Unix epoch — node syncs clock
    uint8_t  time_quality;      // 0=NTP-synced, 1=RTC-backed, 2=boot-estimated
} __attribute__((packed)) AckMessage;
// Sent every cycle — nodes stay within ±1s of gateway time

// Label push payload (MSG_SET_LABEL)  Gateway → Node (triggered from dashboard)
typedef struct {
    uint8_t  msg_type;          // MSG_SET_LABEL
    char     node_id[7];        // Target node
    char     label[33];         // New human-readable label
} __attribute__((packed)) LabelUpdate;
// Node saves to EEPROM on receipt, ACKs back to gateway
```

### 3.6 Sync Retry & Backfill Mechanism

When the ESP-NOW link is down (out of range, gateway offline, interference),
readings accumulate on the SD card. The node tracks exactly where in the file
the unsynced readings start using an **in-memory pointer** — no scanning
required.

**RTC memory state for backfill:**

```c
// Persists across deep sleep, lost on hard power-off
RTC_DATA_ATTR uint16_t pending_sync   = 0;     // count of unsynced rows
RTC_DATA_ATTR uint32_t backfill_pos   = 0;     // byte offset of oldest unsynced row
RTC_DATA_ATTR uint16_t backfill_day   = 0;     // day-of-year of the file backfill_pos points to
```

**How it works — no file scanning needed:**

```
┌─────────────────────────────────────────────────────────────────┐
│  On every write cycle:                                          │
│                                                                 │
│    1. Write new CSV row to SD, record its byte offset           │
│    2. Send current reading via ESP-NOW                          │
│    3. Got ACK?                                                  │
│       → YES: mark synced=Y on SD (seek to offset, overwrite)   │
│       → NO:  pending_sync++                                     │
│              if backfill_pos == 0, save this row's offset       │
│              as the start of the unsynced region                │
│                                                                 │
│  Backfill (runs after current reading, only if pending > 0):   │
│    4. Seek directly to backfill_pos in the right day's CSV      │
│    5. Read up to 5 rows sequentially from that position         │
│    6. Send each as MSG_BACKFILL via ESP-NOW                     │
│    7. Got ACK? → mark synced=Y, advance backfill_pos,          │
│                  pending_sync--                                  │
│    8. No ACK?  → stop, try again next cycle                     │
│    9. If pending_sync hits 0 → reset backfill_pos to 0          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Why this is better than scanning files:**
- **Zero extra reads** — the node knows exactly where unsynced data starts
  because it tracked the byte offset when the write happened
- **O(1) seek** — `fseek(file, backfill_pos, SEEK_SET)` jumps straight
  to the right spot, no iterating through rows looking for `synced=N`
- **Survives deep sleep** — `RTC_DATA_ATTR` persists across sleep cycles
- **Sequential reads only** — once positioned, rows are read forward in
  order. This is the most SD-friendly access pattern (no random reads)

**What happens on hard power-off (RTC memory lost)?**

If the node fully loses power, `backfill_pos` resets to 0 and
`pending_sync` resets to 0. This means any unsynced rows from before the
power loss won't be automatically backfilled. This is acceptable because:

1. The rows are still safely on the SD card — nothing is lost
2. The gateway likely received most readings in real-time anyway
3. If the data is needed, farm staff can pull the SD card
4. A full power-off is rare (plugged into wall power at each fridge)

To recover gracefully, the node can optionally do a **one-time file scan**
on cold boot only (not every cycle) to rebuild `backfill_pos`:

```c
void coldBootSyncRecovery() {
    // Only runs once after a full power-off — not on deep sleep wake
    // Scans today's CSV for the first synced=N row to set backfill_pos
    // This is one read on boot, not every 15 minutes
    File f = SD.open(todayFilename(), FILE_READ);
    if (!f) return;

    while (f.available()) {
        long pos = f.position();
        String line = f.readStringUntil('\n');
        if (line.endsWith(",N")) {
            backfill_pos = pos;
            pending_sync++;
        }
    }
    f.close();
}
```

This single boot-time scan is fine — it's one read operation after an
uncommon event, not repeated wear on every cycle.

**Design decisions:**
- **Oldest-first**: backfills in chronological order so the hub gets a
  complete timeline without gaps
- **Batch of 5 per cycle**: at 15-min intervals, a 2-hour outage (8 missed
  readings) catches up in 2 cycles (~30 min). A 24-hour outage (96 readings)
  catches up in ~5 hours — all while still logging new readings on time
- **Cross-day**: `backfill_day` tracks which day file to read from. When it
  doesn't match today, the node opens yesterday's file for the remaining
  old rows, then advances to today's
- **Gateway deduplication**: the gateway checks `(node_id, probe_addr, recorded_at)`
  before writing — if a backfilled reading was somehow already received,
  it's silently skipped
- **No in-place SD marking needed**: since `backfill_pos` in memory tracks
  progress, we don't strictly need to overwrite `synced=N` → `Y` on the SD.
  The CSV can keep `N` for rows that were eventually backfilled — the source
  of truth for sync status is the gateway's aggregated log, not the node's SD card. This
  **eliminates all SD write-back overhead** from the backfill path entirely.
  (The `synced` column in the CSV becomes informational — it shows whether
  the row was delivered on the first attempt, useful for diagnosing comms
  reliability later.)

**Worst case — gateway down for days:**
The node just keeps logging to SD. All readings are safe. When the gateway
comes back, backfill drains the queue over hours/days. If you need the data
sooner, pull the node's SD card — everything is there regardless of sync status.

### 3.7 SD Card Health Monitoring

SD cards commonly fail by going **read-only** — the card's internal
controller locks out writes to protect data when it detects flash wear or
corruption. This is a silent failure that could mean a node *thinks* it's
logging but isn't.

**Strategy: detect on failure, not with proactive checks.**

Rather than writing canary files or running tests every cycle (which adds
unnecessary wear to the card), the node detects failures **during the
actual CSV write** and tracks the status in memory.

```c
// SD health status — held in RTC memory (survives deep sleep, lost on hard reset)
enum SDHealth {
    SD_OK         = 0,   // Last write succeeded
    SD_MISSING    = 1,   // Card not detected / failed to mount
    SD_READONLY   = 2,   // Card mounted but file open/write returned failure
    SD_WRITE_ERR  = 3,   // Write returned short (partial write / mid-write failure)
    SD_FS_ERR     = 4,   // Can't create directories or open log files
};

// Stored in RTC_DATA_ATTR so it persists across deep sleep cycles
RTC_DATA_ATTR SDHealth sd_status = SD_OK;
RTC_DATA_ATTR uint16_t sd_fail_count = 0;   // consecutive failures
```

**Detection happens naturally during the real write path:**

```c
SDHealth writeCSVRow(const char* csvRow) {
    // 1. Can we mount the card?
    if (!SD.begin(CS_PIN)) {
        return SD_MISSING;
    }

    // 2. Can we open/create the log directory?
    if (!SD.exists("/logs") && !SD.mkdir("/logs")) {
        return SD_FS_ERR;
    }

    // 3. Can we open the file for appending?
    File logFile = SD.open(todayFilename(), FILE_APPEND);
    if (!logFile) {
        return SD_READONLY;  // most likely: card went read-only
    }

    // 4. Did the write complete fully?
    size_t expected = strlen(csvRow);
    size_t written  = logFile.print(csvRow);
    logFile.flush();
    logFile.close();

    if (written != expected) {
        return SD_WRITE_ERR;  // partial write — card failing
    }

    // 5. Success — clear any previous error state
    return SD_OK;
}
```

**The main loop uses it like this:**

```c
void loop() {
    SensorReading reading = readProbes();

    // Attempt SD write
    SDHealth result = writeCSVRow(formatCSV(reading));
    sd_status = result;

    if (result == SD_OK) {
        sd_fail_count = 0;           // reset streak
    } else {
        sd_fail_count++;             // track consecutive failures
        triggerSDAlarm(result);      // buzzer + LED
    }

    // Always transmit to gateway — sd_status tells the hub what's going on
    reading.sd_status = sd_status;
    sendToGateway(reading);

    // ... backfill, config check, sleep
}
```

**Why `RTC_DATA_ATTR`?**
ESP32's RTC memory survives deep sleep (but not hard power-off). This means
the SD failure status persists across sleep cycles without needing EEPROM
writes. If the node power-cycles fully, `sd_status` resets to `SD_OK` and
the next write attempt will re-detect the failure immediately — no data lost.

**What happens on each status:**

| Status | LED | Buzzer | ESP-NOW | Node behavior |
|--------|-----|--------|---------|---------------|
| `SD_OK` | Green | Silent | `sd_status=0` | Normal operation |
| `SD_MISSING` | Blue solid | 3 beeps/min | `sd_status=1` | Keeps sending to gateway, **no local backup** |
| `SD_READONLY` | Magenta flash | **Continuous** | `sd_status=2` | Keeps sending to gateway, **card needs replacement** |
| `SD_WRITE_ERR` | Magenta flash | **Continuous** | `sd_status=3` | Same as READONLY — likely early-stage card failure |
| `SD_FS_ERR` | Blue flash | 3 beeps/min | `sd_status=4` | May self-recover after card reformat or re-seat |

**Key design principles:**
- **No extra writes** — failures are detected from the real CSV write, not
  canary files or health checks. Zero additional wear on the card.
- **An SD failure never stops the node** from reading probes and transmitting
  to the gateway. Local backup is lost, but the central log continues.
- **Consecutive failure tracking** — `sd_fail_count` lets the gateway distinguish
  a one-off glitch (count=1, maybe static) from a dead card (count=96 = 24h).
  Dashboard can show "SD failing for 6 hours" instead of just "error".
- **Self-clearing** — if someone swaps the card, the next successful write
  resets `sd_status` to `SD_OK` and silences the alarm automatically.

**Dashboard shows SD status per node:**
```
┌─────────────────────┐
│ 🟢 Cheese Fridge    │
│    Node KF7B2X      │
│                     │
│  Top:    3.2°C      │
│  Bottom: 2.8°C      │
│  Door:   3.5°C      │
│                     │
│  SD: ✅ OK           │
│  Last: 2 min ago    │
└─────────────────────┘

┌─────────────────────┐
│ 🟡 Basement #2      │
│    Node J2NK5F      │
│                     │
│  Top:    3.9°C      │
│  Bottom: 3.6°C      │
│                     │
│  SD: ⚠️ READ-ONLY   │  ← clearly visible, needs card swap
│  Last: 2 min ago    │
└─────────────────────┘
```

### 3.8 Time Sync — Gateway-to-Node

Nodes have **no hardware RTC** (no DS3231). Instead, they rely on the
ESP32-C3's internal RTC oscillator for timekeeping between syncs, and
receive the current time from the gateway in every ACK message.

**Time hierarchy:**
```
Internet (NTP)  →  Gateway DS3231 RTC  →  Nodes (internal RTC)
    ±10ms             ±2ppm                ±150ppm
```

**How it works:**

1. **Gateway** gets time from NTP (when WiFi is connected) and keeps
   the DS3231 RTC as battery-backed backup. It always knows the real time.
2. **Every ACK message** from the gateway includes `gateway_time` (Unix epoch)
   and `time_quality` (NTP-synced, RTC-backed, or estimated).
3. **Node receives ACK** → compares `gateway_time` to its internal clock →
   if drift > 0.5s, corrects its internal clock via `struct timeval` / `settimeofday()`.
4. **Between ACKs** (15-min deep sleep), the internal RTC keeps time.
   At 150ppm worst-case drift: `15 min × 150ppm = 0.135 seconds` — well
   within the ±1s accuracy requirement (AC-03).

**Cold boot (first power-on or hard reset):**
The node has no idea what time it is. Strategy:

```c
RTC_DATA_ATTR bool     time_valid = false;  // do we know the real time?
RTC_DATA_ATTR uint32_t last_sync  = 0;      // last gateway time received

void handleAck(AckMessage* ack) {
    struct timeval tv = { .tv_sec = ack->gateway_time, .tv_usec = 0 };
    settimeofday(&tv, NULL);     // set internal clock
    time_valid = true;
    last_sync  = ack->gateway_time;
}

uint32_t getTimestamp() {
    if (time_valid) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (uint32_t)tv.tv_sec;
    }
    // No time yet — use millis-based relative offset
    // Gateway will re-timestamp when it receives this reading
    return 0;
}
```

- Readings sent with `timestamp=0` tell the gateway "I don't know the time yet"
- Gateway uses its own clock to timestamp these readings
- Once the first ACK arrives (≤ 15 seconds after boot), the node has real time
- All subsequent readings (including backfill) have correct timestamps

**What if the gateway is down when a node cold-boots?**
The node logs to SD with `timestamp=0` and best-effort relative time.
When the gateway comes back and backfill syncs these rows, the gateway
can estimate timestamps based on the reading interval. For regulatory
purposes, this is a rare edge case (hard power-off + gateway down) and
the approximate time is still useful.

**Why this works reliably:**
- Nodes are plugged into wall power → hard resets are rare
- Deep sleep preserves the internal RTC → `time_valid` persists
- Gateway ACKs happen every 15 min → drift never exceeds ~0.2s
- Even worst case (30 min without ACK), drift is only ~0.27s

---

## 4. Hardware Selection

### 4.1 Per-Fridge Node

| Component | Part | Qty/Node | ~Cost | Notes |
|-----------|------|----------|-------|-------|
| Microcontroller | **ESP32-C3-DevKitM-01** | 1 | $4 | WiFi+BLE+ESP-NOW, deep sleep, RISC-V, 4MB flash |
| Temp Probes | **DS18B20 waterproof** (stainless steel, 1m cable) | 2–3 | $3 ea | ±0.5°C, 1-Wire bus, food-safe tip |
| Pull-up Resistor | 4.7kΩ | 1 | $0.10 | Required for 1-Wire data line |
| SD Card Module | MicroSD SPI breakout | 1 | $2 | FAT32, standard microSD cards |
| MicroSD Card | 8GB+ Class 10 | 1 | $4 | Years of CSV data at 15-min intervals |
| Power Supply | 5V USB adapter (fridge-external) | 1 | $3 | Micro-USB cable routed through fridge seal |
| Enclosure | IP65 junction box | 1 | $4 | Mount outside fridge, probes routed in |
| Status LED | *Built-in* addressable RGB (GPIO8) | 0 | $0 | On-board WS2812 — no extra wiring needed |
| Alert Buzzer | Active piezo buzzer (3.3V/5V) | 1 | $1 | Audible alarm on temp exceedance |
| **Node Total** | | | **~$15** | |

> **No DS3231 RTC on nodes.** Nodes sync time from the gateway via ESP-NOW
> ACK messages (see §3.8). The ESP32-C3's internal RTC maintains time
> during deep sleep with drift < 0.2s per 15-min cycle — well within our
> ±1s accuracy requirement. This saves ~$11 per node.

### 4.2 Gateway (Hub + Dashboard + Alerting)

| Component | Part | Qty | ~Cost | Notes |
|-----------|------|-----|-------|-------|
| Microcontroller | **ESP32-C3-DevKitM-01** | 1 | $4 | ESP-NOW rx + WiFi STA+AP + web server |
| Real-Time Clock | **DS3231 module** | 1 | $11 | NTP backup — keeps time if WiFi goes down |
| SD Card Module | MicroSD SPI breakout | 1 | $2 | Aggregated logs + config.json |
| MicroSD Card | 32GB Class 10 | 1 | $6 | Aggregated data from all nodes |
| Alert Buzzer | Active piezo buzzer (3.3V/5V) | 1 | $1 | Audible alarm — place gateway in farmhouse |
| Enclosure | Project box | 1 | $5 | Central location — ideally in farmhouse |
| **Gateway Total** | | | **~$29** | |

> **Only the gateway has a DS3231 RTC** ($11 × 1 instead of $11 × 7 = $77).
> The gateway is the system's time authority. It gets time from NTP when
> WiFi is available, with the DS3231 as battery-backed backup for outages.
> All nodes sync from the gateway via ESP-NOW — see §3.8.

> **Placement tip**: Put the gateway in the farmhouse (near the kitchen or
> main living area) so you can hear the buzzer. ESP-NOW range from fridges
> to farmhouse should be fine (~50-80m through walls). If range is an issue,
> the relay node at the stairwell helps. |

### 4.3 Probe Accuracy Considerations

For **cheese and yogurt production**, the standard DS18B20 at ±0.5°C is
generally acceptable for fridge monitoring (regulatory threshold is 41°F / 5°C,
and you're watching for drift, not precision lab work).

**However, to improve accuracy:**

1. **Use 12-bit resolution** (default) — gives 0.0625°C resolution
2. **Calibrate each probe** against a known reference thermometer:
   - Measure ice-water bath (0°C) and record offset
   - Store per-probe calibration offset in EEPROM/config
   - Apply offset in firmware before logging
3. **Use "parasite power" cautiously** — external VCC (3.3V) is more reliable
4. **Consider DS18B20 "±0.1°C" selected bins** if higher accuracy is needed
   (available from some suppliers as hand-selected units, ~$8 each)

### 4.4 ESP32-C3 Notes

We're using the **ESP32-C3-DevKitM-01** (RISC-V, 4MB flash, no PSRAM)
instead of the classic ESP32 DevKit V1. Key differences:

| Feature | Classic ESP32 | ESP32-C3 | Impact |
|---------|--------------|----------|--------|
| CPU | Dual-core Xtensa @ 240MHz | Single-core RISC-V @ 160MHz | Fine for our workload (read sensors every 15 min) |
| RAM | 520KB SRAM | 400KB SRAM | Sufficient — ESPAsyncWebServer + 5 nodes fits easily |
| Flash | 4MB (typical) | 4MB (embedded in chip) | Same — LittleFS + firmware fit comfortably |
| WiFi | 802.11 b/g/n | 802.11 b/g/n | Same |
| ESP-NOW | ✔ | ✔ | Same — broadcast + unicast both work |
| Deep sleep | ~10µA, RTC memory | ~5µA, RTC memory | C3 is *more* efficient, `RTC_DATA_ATTR` works |
| Bluetooth | Classic + BLE | BLE only | We don't use Bluetooth — no impact |
| GPIO | ~34 pins | ~15 usable pins | Enough (we need ~9: SPI, I2C, 1-Wire, buzzer) |
| On-board LED | None (external needed) | **WS2812 RGB on GPIO8** | Bonus — saves a component per node |
| Cost | ~$5 | ~$4 | Slightly cheaper |
| Arduino/PlatformIO | Mature | Mature (as of 2024+) | All our libraries supported |

**GPIO allocation (nodes — no RTC, time synced from gateway):**

| Function | GPIO | Notes |
|----------|------|-------|
| 1-Wire (DS18B20) | GPIO4 | Data line, 4.7kΩ pull-up to 3.3V |
| SPI CLK (SD card) | GPIO6 | Default SPI |
| SPI MISO | GPIO5 | Default SPI |
| SPI MOSI | GPIO7 | Default SPI |
| SPI CS (SD card) | GPIO10 | Chip select |
| Buzzer | GPIO1 | PWM-capable |
| RGB LED | GPIO8 | Built-in WS2812 (on-board) |
| **Total** | **7 GPIOs** | Leaves GPIO0, GPIO2, GPIO3, GPIO9, GPIO18-21 free |

**GPIO allocation (gateway — has DS3231 RTC):**

| Function | GPIO | Notes |
|----------|------|-------|
| SPI CLK (SD card) | GPIO6 | Default SPI |
| SPI MISO | GPIO5 | Default SPI |
| SPI MOSI | GPIO7 | Default SPI |
| SPI CS (SD card) | GPIO10 | Chip select |
| I2C SDA (DS3231) | GPIO3 | RTC — gateway only |
| I2C SCL (DS3231) | GPIO2 | RTC — gateway only |
| Buzzer | GPIO1 | PWM-capable |
| RGB LED | GPIO8 | Built-in WS2812 (on-board) |
| **Total** | **8 GPIOs** | Leaves GPIO0, GPIO4, GPIO9, GPIO18-21 free |

---

## 5. SD Card Log Format

### 5.1 File Naming Convention

```
/logs/
  2026-02-15.csv
  2026-02-16.csv
  ...
```

Daily rotation. One file per day per node. Easy for farm staff to pull the card
and hand to an inspector or copy to a computer.

### 5.2 CSV Format

```csv
timestamp,datetime,node_id,node_label,probe_1_addr,probe_1_temp_c,probe_1_temp_f,probe_2_addr,probe_2_temp_c,probe_2_temp_f,probe_3_addr,probe_3_temp_c,probe_3_temp_f,alert,synced
1739635200,2026-02-15T12:00:00,KF7B2X,Cheese Fridge,28FF1A2B3C4D5E01,3.25,37.85,28FF6A7B8C9D0E02,3.50,38.30,,,,OK,Y
1739636100,2026-02-15T12:15:00,KF7B2X,Cheese Fridge,28FF1A2B3C4D5E01,3.31,37.96,28FF6A7B8C9D0E02,3.44,38.19,,,,OK,Y
1739637000,2026-02-15T12:30:00,KF7B2X,Cheese Fridge,28FF1A2B3C4D5E01,5.10,41.18,28FF6A7B8C9D0E02,5.25,41.45,,,,HIGH,N
```

- **timestamp**: Unix epoch (machine-readable)
- **datetime**: ISO 8601 (human-readable)
- **node_id**: 6-char auto-generated unique ID (from EEPROM)
- **node_label**: human-readable name (from EEPROM, set via hub admin)
- **probe addresses**: DS18B20 unique 64-bit address (identifies which probe)
- **temps in both °C and °F**: inspectors may want either
- **alert**: OK / HIGH / LOW / SENSOR_ERR
- **synced**: Y/N — written as `N` initially, flipped to `Y` in-place when
  gateway ACKs receipt. Rows with `N` are queued for backfill on next cycle
  (see §3.6 Sync Retry). The column is fixed-width so the node can overwrite
  it without rewriting the file.

### 5.3 Storage Capacity

At 15-minute intervals with 3 probes:
- ~150 bytes per row × 96 rows/day = **~14 KB/day**
- 8GB card = **~1,500 years** of data
- Even a 256MB card holds **~50 years**
- No practical concern about running out of space

---

## 6. Alert Thresholds

| Parameter | Warning | Critical | Action |
|-----------|---------|----------|--------|
| Fridge temp | > 3.3°C (38°F) | > 5.0°C (41°F) | LED red + **local buzzer** + **gateway buzzer** |
| Fridge temp | < -2.2°C (28°F) | < -3.9°C (25°F) | Freezing risk — LED red + **local buzzer** + **gateway buzzer** |
| Probe failure | — | Sensor not responding | LED blue, log SENSOR_ERR, gateway alert |
| SD card missing | — | Card not detected on mount | LED blue solid, 3 beeps/min, gateway alert |
| SD card read-only / write err | — | Write fails or returns short | LED magenta flash, **continuous buzzer**, gateway alert |
| SD filesystem | — | Can't open/create dirs or files | LED blue flash, gateway alert |
| Comms | 3 missed syncs | 10 missed syncs | LED yellow, queues for retry |
| Gateway alert | Any node warning | Any node critical | **Gateway buzzer** (place gateway in farmhouse for audibility) |

---

## 7. Software Components

### 7.1 Firmware — Fridge Node (`firmware/node/`)

```
firmware/node/
├── node.ino              # Main Arduino sketch
├── config.h              # Default thresholds, pin assignments, constants
├── identity.h/.cpp       # Auto-generate & persist node ID + label in EEPROM
├── espnow_comm.h/.cpp    # ESP-NOW send/receive + label push handler
├── sensor_mgr.h/.cpp     # DS18B20 1-Wire probe management
├── sd_logger.h/.cpp      # SD card CSV writing with daily rotation + sync marking
├── sd_health.h/.cpp      # SD card health detection during real writes
├── backfill.h/.cpp       # In-memory pointer backfill, re-send to gateway
├── time_sync.h/.cpp      # Internal RTC + time sync from gateway ACKs (no hardware RTC)
├── alert_mgr.h/.cpp      # Threshold checking, LED + buzzer control
└── watchdog.h/.cpp       # Hardware watchdog timer
```

**Libraries needed:**
- `OneWire` — 1-Wire protocol
- `DallasTemperature` — DS18B20 high-level driver
- `esp_now.h` — ESP-NOW (built into ESP-IDF)
- `SD` / `SdFat` — SD card access
- `ArduinoJson` — config file parsing (optional)
- *No RTClib* — nodes use internal RTC + time sync from gateway

### 7.2 Firmware — Relay Node (`firmware/relay/`) — if needed

```
firmware/relay/
├── relay.ino              # Receive ESP-NOW → re-broadcast to gateway
└── config.h               # Gateway MAC address, channel config
```

Minimal firmware — just a pass-through. Only needed if basement range test
fails in Phase 1.

### 7.3 Firmware — Gateway (`firmware/gateway/`)

The gateway is the **hub of the system** — it receives all node data via
ESP-NOW, logs to its own SD card, runs the web dashboard over WiFi AP,
and drives the buzzer for farmhouse alerting. All in one ESP32.

**How ESP-NOW + WiFi coexist:** ESP32 supports running ESP-NOW and WiFi
SoftAP simultaneously, as long as both use the same channel (default ch 1).
The ESP-NOW peer channel must match the AP channel. This is a well-supported
configuration in ESP-IDF.

```
firmware/gateway/
├── gateway.ino            # Main Arduino sketch — setup WiFi AP + ESP-NOW + web server
├── config.h               # Pin assignments, AP SSID/password, default thresholds
├── espnow_recv.h/.cpp     # ESP-NOW receive callbacks + ACK + label push to nodes
├── sd_logger.h/.cpp       # Aggregated CSV logging (one file per node per day)
├── node_registry.h/.cpp   # Node & probe config — stored as JSON on SD card
├── web_server.h/.cpp      # ESPAsyncWebServer: serves dashboard + REST API
├── api_handlers.h/.cpp    # REST endpoints: GET readings, POST labels, GET export
├── rtc_time.h/.cpp        # DS3231 master clock + NTP sync + time distribution to nodes
├── alert_mgr.h/.cpp       # Cross-node alerting + buzzer control
├── data/                  # LittleFS partition — uploaded to ESP32 flash
│   ├── index.html         # Dashboard SPA — all nodes at a glance
│   ├── node.html          # Node detail page (charts, probe list)
│   ├── admin.html         # Node/probe management page
│   ├── export.html        # CSV download page
│   ├── css/
│   │   └── style.css      # Minimal responsive CSS
│   └── js/
│       ├── dashboard.js   # Fetch JSON, render fridge cards, auto-refresh
│       ├── charts.js      # Lightweight chart rendering (no heavy libs)
│       └── admin.js       # Admin page logic
└── platformio.ini         # Or Arduino IDE board config
```

**Libraries needed (gateway-specific):**
- `ESPAsyncWebServer` — async HTTP server (no blocking)
- `AsyncTCP` — required by ESPAsyncWebServer
- `ArduinoJson` — JSON serialization for REST API responses
- `LittleFS` — serves static HTML/CSS/JS from flash
- `SD` / `SdFat` — reading/writing CSV data files
- `RTClib` — DS3231 RTC (gateway only — nodes don't need this library)
- `esp_now.h` — ESP-NOW (built into ESP-IDF)

**WiFi — Dual Mode (STA + AP fallback):**

The gateway runs WiFi in one of two modes depending on configuration:

| Mode | When | Dashboard Access | Extras |
|------|------|-----------------|--------|
| **AP-only** | No home WiFi configured (default) | Connect to `DairyLedger` → `http://192.168.4.1` | Works out of the box |
| **STA+AP** | Home WiFi SSID/password set via admin | Use farm network → `http://dairyledger.local` (mDNS) | NTP time sync, future cloud/email |

**How it works:**
1. On boot, gateway checks `config.json` for a `wifi_ssid` / `wifi_password`
2. If set → starts in **STA+AP mode**: connects to home WiFi *and* runs
   `DairyLedger` AP simultaneously. Dashboard reachable from either network.
3. If not set (or connection fails after 15s) → **AP-only mode**: just
   creates the `DairyLedger` network. Full functionality, just no internet.
4. WiFi credentials are configured via the Admin page — no code changes

**AP details:**
- SSID: `DairyLedger` (configurable)
- Password: `goatfarm` (configurable — or open for simplicity)
- IP: `192.168.4.1` (ESP32 SoftAP default)
- Always running, even in STA+AP mode (fallback if home WiFi goes down)
- Supports ~2-3 concurrent clients (sufficient for farm use)

**STA mode benefits (when home WiFi is available):**
- **NTP time sync** — gateway can sync RTC to internet time on boot
- **mDNS** — access dashboard at `http://dairyledger.local` instead of IP
- **No WiFi switching** — view dashboard from your normal network
- **Future**: email/SMS alerts, cloud backup (optional, not required)

**Important**: ESP-NOW and WiFi must share the same channel. When in STA
mode, the ESP32 uses whatever channel the router assigns. The AP and
ESP-NOW are forced to that same channel. Nodes auto-scan channels on boot
(try ch 1, then scan if no ACK) — this is handled transparently.

```c
// In gateway setup()
void setupWiFi() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid, ap_password);

    if (strlen(wifi_ssid) > 0) {
        WiFi.begin(wifi_ssid, wifi_password);
        uint8_t retries = 30;  // 15 seconds
        while (WiFi.status() != WL_CONNECTED && retries-- > 0) {
            delay(500);
        }
        if (WiFi.status() == WL_CONNECTED) {
            configTime(0, 0, "pool.ntp.org");  // NTP sync
            MDNS.begin("dairyledger");             // http://dairyledger.local
        }
        // If connection failed, AP is still running — no problem
    }
}
```

**REST API endpoints (served by ESPAsyncWebServer):**

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/nodes` | All nodes with current status, last reading, sd_status |
| GET | `/api/node/<id>` | Single node detail + all probe readings |
| GET | `/api/node/<id>/history?hours=24` | Historical readings for charts |
| POST | `/api/node/<id>/label` | Set node label → pushed to node EEPROM via ESP-NOW |
| POST | `/api/node/<id>/thresholds` | Update alert thresholds |
| GET | `/api/probes/<node_id>` | List probes for a node |
| POST | `/api/probe/<node_id>/<addr>/label` | Set probe label |
| POST | `/api/probe/<node_id>/<addr>/calibration` | Set calibration offset |
| GET | `/api/export?from=YYYY-MM-DD&to=YYYY-MM-DD&node=<id>` | Download CSV |
| GET | `/api/alerts?acked=false` | Unacked alerts |
| POST | `/api/alerts/<id>/ack` | Acknowledge an alert |
| POST | `/api/buzzer/silence?minutes=5` | Temporarily silence buzzer |
| GET | `/api/settings` | Current global settings (incl. WiFi config) |
| POST | `/api/settings` | Update settings (temp unit, intervals, WiFi creds, etc.) |

**Gateway SD card layout:**
```
/data/
  config.json            # Node registry, probe labels, thresholds, settings
  nodes/
    KF7B2X/
      2026-02-15.csv     # Aggregated readings for this node, this day
      2026-02-16.csv
    W3MN9A/
      2026-02-15.csv
  alerts/
    2026-02.csv          # Monthly alert log
```

**Memory considerations:**
- ESP32-C3 has ~400KB SRAM. ESPAsyncWebServer is efficient (~10KB per client)
- Node state for 5 nodes × 3 probes = ~1KB in-memory struct
- Historical queries read from SD on-demand (not cached in RAM)
- LittleFS partition: ~1.5MB for HTML/CSS/JS assets (plenty within 4MB flash)
- For chart data, limit API responses to last 24h by default
- Single-core RISC-V @ 160MHz handles the gateway workload easily —
  ESP-NOW callbacks, async web server, and SD writes are all non-blocking

### 7.4 No External Computer Required

The gateway ESP32 handles everything — ESP-NOW reception, data logging,
web dashboard, alerting, and data export. **No Raspberry Pi, laptop, or
server needed.**

**Accessing the dashboard:**
- **Without home WiFi**: connect phone/tablet to `DairyLedger` → `http://192.168.4.1`
- **With home WiFi configured**: just open `http://dairyledger.local` from any
  device on your network (phone, tablet, laptop — no WiFi switching needed)

> **Future option**: With home WiFi connected, the gateway could optionally
> push alerts via email/SMS or sync data to a cloud service. But this is
> entirely optional — the system is fully functional without internet.

### 7.5 Web Dashboard — Design

The dashboard is a **lightweight single-page app** served from the gateway
ESP32's LittleFS flash. Static HTML/CSS/JS files make requests to the REST
API (§7.3) which returns JSON. No server-side templating — all rendering
happens in the browser. This keeps the ESP32's workload minimal.

**Access**: Connect to `DairyLedger` WiFi → `http://192.168.4.1`, or if
gateway is on home WiFi → `http://dairyledger.local` from any device on
the same network.

#### Overview Page (`/`)

The main dashboard shows all nodes at a glance as visual "fridge cards":

```
┌─────────────────────────────────────────────────────────────────────┐
│  🐐 Goat Farm — Temperature Monitor           Last refresh: 12:15  │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐     │
│  │ 🟢 Cheese Fridge │  │ 🟢 Yogurt Aging  │  │ 🔴 Milk Cooler   │     │
│  │    Node 1        │  │    Node 2        │  │    Node 3        │     │
│  │                 │  │                 │  │                 │     │
│  │  Top:    3.2°C  │  │  Top:    4.1°C  │  │  Top:    5.3°C  │     │
│  │  Bottom: 2.8°C  │  │  Bottom: 3.8°C  │  │  Bottom: 5.1°C  │     │
│  │  Door:   3.5°C  │  │  Door:   4.4°C  │  │  Door:   5.6°C  │     │
│  │                 │  │                 │  │                 │     │
│  │  Last: 2 min ago│  │  Last: 2 min ago│  │  ⚠ TEMP HIGH    │     │
│  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘     │
│           │                    │                    │               │
│  ┌────────┴────────┐  ┌────────┴────────┐                          │
│  │ 🟢 Basement #1   │  │ 🟡 Basement #2   │                          │
│  │    Node 4        │  │    Node 5        │                          │
│  │                 │  │                 │                          │
│  │  Top:    3.0°C  │  │  Top:    3.9°C  │                          │
│  │  Bottom: 2.5°C  │  │  Bottom: 3.6°C  │                          │
│  │                 │  │                 │                          │
│  │  Last: 2 min ago│  │  Last: 17min ago│  ← stale = yellow       │
│  └─────────────────┘  └─────────────────┘                          │
│                                                                     │
│  [⚙ Admin]  [📊 Export Reports]  [🔕 Silence Gateway Buzzer (5m)]  │
└─────────────────────────────────────────────────────────────────────┘
```

**Card color logic:**
- 🟢 **Green** — all probes within range, node reporting on time
- 🟡 **Yellow** — warning threshold exceeded OR node hasn't reported in > 2× interval
- 🔴 **Red** — critical threshold exceeded OR node offline > 5× interval
- ⚫ **Gray** — node disabled / decommissioned

**Features:**
- Auto-refreshes every 30 seconds (configurable)
- Click a card → drill into that node's detail page with history charts
- Responsive layout — works on phone, tablet, or desktop browser
- "Silence Buzzer" button — temporarily mutes the RPi buzzer (5/15/60 min)

#### Node Detail Page (`/node/<id>`)

- **24-hour temperature chart** (line graph, one line per probe)
- **7-day trend** chart (min/max/avg bands)
- **Alert log** — table of all alerts for this node
- **Probe list** with labels, current value, calibration offset
- **Edit button** — rename node, rename probes, adjust thresholds

#### Admin Page (`/admin`)

This is where nodes and probes are managed — **no code changes needed** to
add or reconfigure the system.

```
┌─────────────────────────────────────────────────────────────────────┐
│  ⚙ Admin — Node & Probe Management                                 │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  Nodes                                              [+ Add Node]   │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │ ID     │ Label            │ MAC Address       │ Probes │ Status    │ │
│  │ KF7B2X │ Cheese Fridge    │ AA:BB:CC:DD:EE:01 │ 3      │ 🟢 Online │ │
│  │ W3MN9A │ Yogurt Aging     │ AA:BB:CC:DD:EE:02 │ 3      │ 🟢 Online │ │
│  │ P4TX6D │ Milk Cooler      │ AA:BB:CC:DD:EE:03 │ 3      │ 🔴 Alert  │ │
│  │ R8CV3H │ Basement #1      │ AA:BB:CC:DD:EE:04 │ 2      │ 🟢 Online │ │
│  │ J2NK5F │ Basement #2      │ AA:BB:CC:DD:EE:05 │ 2      │ 🟡 Stale  │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  Click a node row to edit:                                          │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │  Node ID:     KF7B2X  (auto-generated, read-only)              │ │
│  │  Node Label:  [Cheese Fridge          ]  → pushed to node EEPROM│ │
│  │  Location:    [Main floor, north wall ]  (free text note)      │ │
│  │  Warn °C:     [3.3]    Crit °C:  [5.0]                        │ │
│  │  Low Warn °C: [-2.2]   Low Crit: [-3.9]                       │ │
│  │                                                                │ │
│  │  Probes:                                     [+ Add Probe]    │ │
│  │  ┌──────────────────────────────────────────────────────────┐  │ │
│  │  │ Address          │ Label       │ Offset │ Current       │  │ │
│  │  │ 28FF1A2B3C4D5E01 │ Top Shelf   │ -0.12  │ 3.2°C        │  │ │
│  │  │ 28FF6A7B8C9D0E02 │ Bottom Rear │ +0.05  │ 2.8°C        │  │ │
│  │  │ 28FF9C8D7E6F5A03 │ Door Area   │  0.00  │ 3.5°C        │  │ │
│  │  └──────────────────────────────────────────────────────────┘  │ │
│  │                                                                │ │
│  │  [💾 Save]  [🗑 Delete Node]                                   │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  Settings                                                           │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │  Temp display unit:   (●) °C   ( ) °F   ( ) Both              │ │
│  │  Reading interval:    [15] minutes                             │ │
│  │  Stale threshold:     [2x] intervals (node → yellow)           │ │
│  │  Offline threshold:   [5x] intervals (node → red/gray)         │ │
│  │  Buzzer enabled:      [✓]                                      │ │
│  │  Data retention:      [365] days (auto-prune older records)     │ │
│  │                                                                │ │
│  │  WiFi Connection                                               │ │
│  │  Home WiFi SSID:      [FarmNetwork       ]                     │ │
│  │  Home WiFi Password:  [••••••••          ]                     │ │
│  │  Status:              🟢 Connected (192.168.1.47)               │ │
│  │                       mDNS: http://dairyledger.local              │ │
│  │  [📡 Test Connection]  [🗑 Clear WiFi (AP-only mode)]          │ │
│  └────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

**How adding a new node works:**
1. Flash an ESP32 with the **generic** node firmware (no config edits needed)
2. Power it on → it auto-generates a 6-char ID (e.g. `KF7B2X`), saves to
   EEPROM, and starts broadcasting via ESP-NOW
3. The node appears on the Admin page as **"Unconfigured Node (KF7B2X)"**
4. Click it → give it a friendly label (e.g. "Cheese Fridge"), set location
   and thresholds
5. Click **Save** → the gateway pushes the label to the node via ESP-NOW →
   the node saves it to EEPROM (survives power outages & reboots)
6. Probes auto-discovered on first data receive — just label each one
7. Done — **zero code changes, zero config files to edit**

> **Tip**: the node ID is printed in its boot log over serial, and also
> appears on the Admin page as soon as the gateway sees it. You can also
> write the ID on a sticker and attach it to the enclosure.

**How adding a probe works:**
1. Physically wire a new DS18B20 onto the node's 1-Wire bus
2. Reboot the node — it auto-detects the new probe address
3. On the Admin page, the new probe appears as "Unlabeled Probe"
   with its 1-Wire address
4. Give it a friendly name (e.g. "Middle Shelf") and set calibration
   offset if known
5. Done

#### Export Page (`/export`)

- Download CSV or PDF reports for a date range
- Filter by node / probe / alert status
- Formatted for inspector handoff
- Optionally auto-generated on a cron (daily/weekly)

### 7.6 Data Storage (SD Card — JSON + CSV)

No database engine runs on ESP32. Instead, the gateway uses:
- **JSON** for configuration (node registry, probe labels, settings)
- **CSV** for time-series data (same format as node SD cards)

#### `config.json` — Node & Probe Registry

```json
{
  "settings": {
    "temp_unit": "C",
    "reading_interval_min": 15,
    "stale_multiplier": 2,
    "offline_multiplier": 5,
    "buzzer_enabled": true,
    "retention_days": 365,
    "ap_ssid": "DairyLedger",
    "ap_password": "goatfarm",
    "wifi_ssid": "",
    "wifi_password": ""
  },
  "nodes": {
    "KF7B2X": {
      "label": "Cheese Fridge",
      "location": "Main floor, north wall",
      "mac": "AA:BB:CC:DD:EE:01",
      "warn_high": 3.3,
      "crit_high": 5.0,
      "warn_low": -2.2,
      "crit_low": -3.9,
      "label_synced": true,
      "enabled": true,
      "probes": {
        "28FF1A2B3C4D5E01": { "label": "Top Shelf", "cal_offset": -0.12 },
        "28FF6A7B8C9D0E02": { "label": "Bottom Rear", "cal_offset": 0.05 },
        "28FF9C8D7E6F5A03": { "label": "Door Area", "cal_offset": 0.00 }
      }
    }
  }
}
```

Loaded into RAM on boot (~2KB for 5 nodes). Written back to SD only when
admin makes a change (rare). ArduinoJson handles serialization.

#### Aggregated CSV — Per Node Per Day

```
/data/nodes/KF7B2X/2026-02-15.csv
```

Same CSV format as node SD cards (§5.2). Gateway appends rows as they
arrive via ESP-NOW. For chart data, the API reads the relevant day file(s)
and returns JSON. For export, the raw CSV is streamed directly.

#### Alert Log — Monthly CSV

```
/data/alerts/2026-02.csv
```

```csv
timestamp,node_id,probe_addr,alert_type,temp_c,message,acked
2026-02-15T12:30:00,KF7B2X,28FF1A2B3C4D5E01,HIGH,5.10,Top shelf exceeded 5.0°C,Y
```

#### Deduplication

When the gateway receives a reading (live or backfill), it checks if a row
with the same `node_id + timestamp` already exists in today's CSV. Since
readings arrive in chronological order, this is a simple check against the
last-written timestamp per node (held in memory). Backfill rows are appended
with the correct timestamp — they sort naturally when viewing.

---

## 8. Probe Placement (Per Fridge)

For regulatory compliance and practical monitoring:

| Probe | Location | Purpose |
|-------|----------|---------|
| **Probe 1** | Top shelf, center | Warmest spot (heat rises, door openings) |
| **Probe 2** | Bottom shelf, rear | Coldest spot (near compressor) |
| **Probe 3** (optional) | Door area / middle | Most variable (door openings) |

This gives a complete thermal picture of each unit. Log all readings; report
the **max temperature** as the regulatory data point.

---

## 9. Bill of Materials

### 9a. Phase 1 — Trial (2 Fridges + Gateway)

| Item | Qty | Unit Cost | Total | Url
|------|-----|-----------|-------| ----
| ESP32-C3-DevKitM-01 | 3 (2 nodes + 1 gateway) | $4 | $12 | https://www.jameco.com/webapp/wcs/stores/servlet/ProductDisplay?langId=-1&storeId=10001&catalogId=10001&productId=2524151
| DS18B20 waterproof probes (1m) | 6 (3 per fridge) | $3 | $18 | https://www.jameco.com/webapp/wcs/stores/servlet/ProductDisplay?langId=-1&storeId=10001&catalogId=10001&productId=2501151
| 4.7kΩ resistors | 3 | $0.10 | $0.30 | included above
| DS3231 RTC module | 1 (gateway only) | $11 | $11 | https://www.jameco.com/webapp/wcs/stores/servlet/ProductDisplay?langId=-1&storeId=10001&catalogId=10001&productId=2217625
| MicroSD SPI modules | 3 | $2 | $6 | https://www.jameco.com/webapp/wcs/stores/servlet/ProductDisplay?langId=-1&storeId=10001&catalogId=10001&productId=2520111
| MicroSD cards (8GB nodes, 32GB gateway) | 3 | $4–6 | $14 |
| 5V USB power adapters | 3 | $3 | $9 |
| IP65 enclosures | 3 | $4 | $12 |
| Piezo buzzers (active, 3.3V) | 3 | $1 | $3 | https://www.jameco.com/webapp/wcs/stores/servlet/ProductDisplay?langId=-1&storeId=10001&catalogId=10001&productId=2098523
| Breadboards + jumper wires | 1 kit | $10 | $10 |
| Micro-USB cables | 3 | $2 | $6 | https://www.jameco.com/webapp/wcs/stores/servlet/ProductDisplay?langId=-1&storeId=10001&catalogId=10001&productId=2333239
| **Phase 1 Total** | | | **~$101** |

> **Note**: No separate RGB LEDs needed — ESP32-C3-DevKitM-01 has a
> built-in WS2812 addressable RGB LED on GPIO8.
>
> **No RTC on nodes** — nodes sync time from gateway via ESP-NOW (see §3.8).
> Only the gateway has a DS3231 ($11 × 1 vs $11 × 3 = saves $22 in Phase 1).

### 9b. Full Deployment (5 Fridges + Gateway + Relay)

| Item | Qty | Unit Cost | Total |
|------|-----|-----------|-------|
| ESP32-C3-DevKitM-01 | 7 (5 nodes + 1 gateway + 1 relay) | $4 | $28 |
| DS18B20 waterproof probes (1m) | 15 (3 per fridge) | $3 | $45 |
| 4.7kΩ resistors | 6 | $0.10 | $0.60 |
| DS3231 RTC module | 1 (gateway only) | $11 | $11 |
| MicroSD SPI modules | 6 (5 nodes + 1 gateway, relay doesn't need) | $2 | $12 |
| MicroSD cards (8GB nodes, 32GB gateway) | 6 | $4–6 | $26 |
| 5V USB power adapters | 7 | $3 | $21 |
| IP65 enclosures | 7 | $4 | $28 |
| Piezo buzzers (active, 3.3V) | 6 (5 nodes + 1 gateway) | $1 | $6 |
| Breadboards / PCB + jumper wires | 1 kit | $10 | $10 |
| Micro-USB cables | 7 | $2 | $14 |
| **Full Deploy Total** | | | **~$202** |

> **Note**: Relay node is budgeted for the basement-to-main-floor link.
> It's just an ESP32-C3 + enclosure + power — no probes or SD needed unless
> we want logging redundancy there too.
>
> **No Raspberry Pi needed.** The gateway ESP32-C3 handles everything:
> data aggregation, web dashboard, alerting, and data export.
>
> **No separate LEDs needed.** The C3 DevKit's built-in RGB LED on GPIO8
> replaces the external RGB LED + resistors from the original design.
>
> **Only 1 RTC.** Nodes sync time from the gateway via ESP-NOW ACKs.
> Saves $66 vs putting a DS3231 on every board.

---

## 10. Project Phases

### Phase 1 — Prototype & Trial (2 fridges + gateway) ✏️
- [ ] Order Phase 1 parts (~$101 — see BOM 9a)
- [ ] Wire up first ESP32 node: DS18B20 probes + SD card + RTC + buzzer
- [ ] Write node firmware: read probes → log to SD → send via ESP-NOW
- [ ] Wire up gateway ESP32: ESP-NOW receive → log to SD → buzzer alerts
- [ ] Write gateway firmware: WiFi AP + ESPAsyncWebServer + REST API
- [ ] Build dashboard HTML/CSS/JS, upload to gateway LittleFS
- [ ] Calibrate probes with farm's reference thermometer (ice-water bath)
- [ ] Deploy 2 nodes in main-floor fridges, gateway in farmhouse
- [ ] Test ESP-NOW range and reliability over 48 hours
- [ ] **Test basement range**: temporarily move a node to basement fridge
  to determine if relay node is needed
- [ ] Validate SD card CSV logs — confirm format is inspector-friendly
- [ ] Test dashboard from phone — verify cards, charts, admin page
- [ ] Verify buzzer fires on simulated temp exceedance
- [ ] Run 2-fridge trial for **1 week**, review data quality with farm owner

### Phase 2 — Full 5-Fridge Deployment
- [ ] Order remaining parts for 3 more nodes + relay node (~$139 delta)
- [ ] Build and calibrate 3 additional fridge nodes
- [ ] If basement range test failed: deploy relay node at stairwell
- [ ] Assign node IDs and configure thresholds per fridge
- [ ] Deploy nodes in all 5 fridges (including 2 basement units)
- [ ] Verify all 5 nodes reporting to gateway
- [ ] Run for 1 week, review data quality and alert behavior
- [ ] Train farm staff on SD card retrieval process

### Phase 3 — Dashboard Polish & Compliance
- [ ] Refine dashboard UI based on Phase 1-2 feedback
- [ ] Add 7-day trend charts to node detail page
- [ ] Build export page — CSV download filtered by date range and node
- [ ] Configure node labels and probe names for all 5 fridges via admin page
- [ ] Test adding a 6th node via admin page (scalability check)
- [ ] Review export format with inspector — confirm it meets requirements
- [ ] Configure home WiFi credentials via admin page (STA+AP mode)
- [ ] Verify NTP time sync and mDNS (`http://dairyledger.local`) work
- [ ] Test dashboard access from home network (no WiFi switching needed)

### Phase 4 — Hardening & Long-Term
- [ ] Design and order custom PCBs (replace breadboards)
- [ ] 3D print or source permanent enclosures
- [ ] Add battery backup (UPS) for power outage logging
- [ ] Document calibration procedure for farm staff
- [ ] Create "how to pull SD card data" guide for staff
- [ ] Regulatory review of logging format with inspector
- [ ] Plan for additional fridges as farm grows (just add nodes)

---

## 11. Open Questions

### Answered

- [x] **How many fridges total need monitoring?**
  **5 currently**, but starting with a 2-fridge trial (Phase 1–2). More fridges
  may be added over time — system is designed to scale by just adding nodes.

- [x] **Are any fridges very far apart (>100m)?**
  Fridges are generally **bunched together**, but **2 are in a basement**.
  Basement nodes may have reduced ESP-NOW range due to floor/ceiling between
  them and the gateway. **Plan**: place the gateway on the main floor near the
  stairwell. If signal is weak, add a **relay node** at the top of the stairs
  (just an ESP32 that re-broadcasts ESP-NOW packets). Test range in Phase 1.

- [x] **Is there any WiFi at all?**
  **Yes** — home WiFi is available in the farmhouse. Gateway will connect in
  **STA+AP mode** (joins home WiFi + runs its own `DairyLedger` AP as fallback).
  This enables NTP time sync, mDNS (`http://dairyledger.local`), and potential
  future cloud/email features. If home WiFi is down, AP-only mode still works.

- [x] **Power availability at each fridge location?**
  **Yes** — users can find a USB power outlet near each fridge. Each node needs
  a standard 5V USB adapter. No battery operation required (simplifies design).

- [x] **Does the farm already have a reference thermometer for calibration?**
  **Yes**. Will use it during Phase 1 to calibrate each DS18B20 probe with an
  ice-water bath and store per-probe offsets in firmware config.

- [x] **Any existing compliance software or paper logs to integrate with?**
  **TBD** — not answered yet. Currently assumed to be paper logs that this
  system will replace. Follow up with farm owner.

- [x] **Preferred alert method?**
  - **Buzzer on each fridge node** — immediate local alarm for anyone nearby
  - **Buzzer on the gateway** — placed in the farmhouse so the
    owners hear it and can go check the fridge
  - Email/SMS alerts are a future "nice to have" if WiFi becomes available

- [x] **Do inspectors want °F, °C, or both on reports?**
  **Configurable**, defaulting to **°C**. CSV logs will always contain both
  units. Display/report output will respect the configured preference.

### Remaining Questions

- [ ] What compliance software or paper logs exist today? (need to confirm)
- [ ] Exact location of the 2 basement fridges relative to main floor — for
      ESP-NOW range testing and possible relay node placement
- [ ] Is there a preferred spot in the farmhouse for the gateway + buzzer?
      (needs USB power, should be audible, near WiFi router for best STA signal)
- [ ] How long should data be retained? (State regs may specify 1–2 years)
- [ ] Any specific cheese aging fridges with different temp requirements?

---

## 12. File Structure (This Repo)

```
goats/
├── PROJECT_PLAN.md          ← You are here
├── firmware/
│   ├── node/                # ESP32 fridge node firmware (x5 fridges)
│   │   ├── node.ino
│   │   ├── config.h
│   │   ├── identity.h/.cpp
│   │   ├── espnow_comm.h/.cpp
│   │   ├── sensor_mgr.h/.cpp
│   │   ├── sd_logger.h/.cpp
│   │   ├── sd_health.h/.cpp
│   │   ├── backfill.h/.cpp
│   │   ├── time_sync.h/.cpp   # No hardware RTC — syncs from gateway
│   │   ├── alert_mgr.h/.cpp
│   │   └── watchdog.h/.cpp
│   ├── relay/               # ESP32 relay node (basement, if needed)
│   │   ├── relay.ino
│   │   └── config.h
│   └── gateway/             # ESP32 gateway — hub + dashboard + alerting
│       ├── gateway.ino
│       ├── config.h
│       ├── espnow_recv.h/.cpp
│       ├── sd_logger.h/.cpp
│       ├── node_registry.h/.cpp
│       ├── web_server.h/.cpp
│       ├── api_handlers.h/.cpp
│       ├── rtc_time.h/.cpp
│       ├── alert_mgr.h/.cpp
│       └── data/            # LittleFS web assets (uploaded to ESP32 flash)
│           ├── index.html
│           ├── node.html
│           ├── admin.html
│           ├── export.html
│           ├── css/style.css
│           └── js/
│               ├── dashboard.js
│               ├── charts.js
│               └── admin.js
├── docs/
│   ├── wiring_diagram.md
│   ├── calibration_guide.md
│   └── sd_card_howto.md     # Guide for farm staff
├── pcb/                     # KiCad files (Phase 4)
└── enclosures/              # 3D print STL files (Phase 4)
```

---

*Last updated: 2026-02-15 — DS3231 RTC on gateway only, nodes sync time via ESP-NOW*
