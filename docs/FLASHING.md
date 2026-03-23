# Flashing Guide — DairyLedger ESP32-C3 Firmware

How to install, build, and flash the firmware onto your ESP32-C3-DevKitM-01 boards.

---

## Prerequisites

### 1. Install PlatformIO

PlatformIO is the build system for all three firmware targets. Choose **one**:

**Option A — VS Code Extension (recommended)**
1. Open VS Code
2. Go to Extensions (⌘⇧X)
3. Search for **"PlatformIO IDE"** and click Install
4. Wait for PlatformIO Core to finish downloading (~1 min on first install)
5. Restart VS Code when prompted

**Option B — CLI only**
```bash
# Install via pip (Python 3.6+ required)
pip install platformio

# Verify
pio --version
```

### 2. USB Driver

The ESP32-C3-DevKitM-01 uses a built-in **USB-JTAG/Serial** bridge — no
external driver needed on macOS or Linux. On Windows, the driver should
auto-install via Windows Update.

If the board isn't detected, check:
```bash
# macOS / Linux — look for /dev/cu.usbmodem* or /dev/ttyACM*
ls /dev/cu.usb*
ls /dev/ttyACM*

# Or use PlatformIO's device list
pio device list
```

---

## Connecting the Board

1. Plug the ESP32-C3-DevKitM-01 into your Mac via USB-C
2. The on-board RGB LED may flash briefly — that's normal
3. Verify detection:
   ```bash
   pio device list
   ```
   You should see something like:
   ```
   /dev/cu.usbmodem14101
   ————————————————
   Hardware ID: USB VID:PID=303A:1001
   Description: USB JTAG/serial debug unit
   ```

> **Tip:** If you have multiple boards plugged in simultaneously, note which
> port belongs to which board. You can specify the port explicitly with
> `--upload-port /dev/cu.usbmodemXXXXX`.

---

## Flashing the Node Firmware

The node firmware is the most common flash — one per fridge.

```bash
cd firmware/node

# Build (downloads dependencies on first run — takes ~2 min)
pio run

# Upload to the connected board
pio run --target upload

# Open serial monitor to see boot output
pio device monitor
```

**What you'll see on first boot:**
```
[NODE] Boot #1 (reason: 0)
[IDENTITY] Generated new ID: K3VP8N
[TIME] No valid time (cold boot or never synced)
[SD_HEALTH] Status: 0, fail count: 0
[ALERT] Thresholds: warn [-2.2, 3.3] crit [-3.9, 5.0]
[ESPNOW] Initialized on channel 1
[SENSOR] Found 0 probe(s) on GPIO4
[SD] Card mount failed
[NODE] Sleeping for 900 seconds...
```

> This is expected with no hardware attached. The node generates its unique
> ID, initializes all subsystems, and goes to deep sleep. When you connect
> DS18B20 probes and an SD card, you'll see actual readings.

### Node Build Flags

Defined in [firmware/node/platformio.ini](../firmware/node/platformio.ini):

| Flag | Purpose |
|------|---------|
| `CORE_DEBUG_LEVEL=3` | Show info/warning/error logs over serial |
| `ARDUINO_USB_CDC_ON_BOOT=1` | Enable USB serial output (required for C3) |

---

## Flashing the Gateway Firmware

The gateway has **two** flash steps: firmware + filesystem (dashboard files).

### Step 1: Flash the firmware

```bash
cd firmware/gateway

# Build
pio run

# Upload firmware
pio run --target upload
```

### Step 2: Upload the dashboard filesystem (LittleFS)

The dashboard HTML/CSS/JS files live in `firmware/gateway/data/` and need
to be uploaded to the ESP32's flash as a LittleFS partition:

```bash
# Build and upload the filesystem image
pio run --target uploadfs
```

> **Important:** You must run `uploadfs` after every change to the files in
> `data/`. The firmware itself doesn't need re-flashing for dashboard changes.

### Step 3: Verify

```bash
pio device monitor
```

**Expected output:**
```
[GATEWAY] Boot — DairyLedger Gateway v1.0
[RTC] DS3231 not found — using internal clock
[SD] Card mounted, type=2, size=14920MB
[ESPNOW] Initialized on channel 1
[WEB] Starting server...
[WIFI] AP mode: DairyLedger (192.168.4.1)
[WEB] Dashboard available at http://192.168.4.1
```

Once running, connect to the `DairyLedger` WiFi network from your phone or
laptop, then open **http://192.168.4.1** in a browser.

---

## Flashing the Relay Firmware

Relays are optional range extenders. Same process as the node:

```bash
cd firmware/relay

pio run
pio run --target upload
pio device monitor
```

**Expected output:**
```
[RELAY] Booting — DairyLedger Range Extender
[RELAY] MAC: XX:XX:XX:XX:XX:XX
[ESPNOW] Initialized on channel 1
[RELAY] Ready — listening for packets to re-broadcast
```

---

## Flashing Multiple Boards

When flashing several boards in a row:

1. **Flash one at a time** — unplug, swap, replug
2. Each node auto-generates a unique ID on first boot, so there's no
   configuration needed per-board
3. Label each board after flashing (a piece of tape with the 6-char ID
   from the serial monitor works great)

If you have multiple boards connected simultaneously:
```bash
# List all connected devices
pio device list

# Flash a specific port
pio run --target upload --upload-port /dev/cu.usbmodem14101

# Monitor a specific port
pio device monitor --port /dev/cu.usbmodem14101
```

---

## Troubleshooting

### Board not detected
- Try a different USB-C cable (some are charge-only, no data)
- Hold the **BOOT** button while plugging in to force download mode
- Check `pio device list` output

### Upload fails with "No serial data received"
- Hold **BOOT**, press **RESET**, release **BOOT**, then try upload again
- This forces the ESP32-C3 into download mode

### Upload succeeds but no serial output
- Make sure `ARDUINO_USB_CDC_ON_BOOT=1` is in `build_flags` (it is by default)
- Try `pio device monitor --baud 115200`
- Press the **RESET** button on the board after opening the monitor

### "LittleFS upload failed" on gateway
- Make sure no serial monitor is open (it locks the port)
- Close the monitor, run `uploadfs`, then reopen the monitor

### Build errors about missing libraries
```bash
# Force re-download all dependencies
pio pkg install

# Or clean everything and rebuild
pio run --target clean
pio run
```

### Node keeps rebooting (watchdog reset)
- The watchdog timeout is 30 seconds — if sensor reads or SD writes hang,
  the board reboots automatically
- Check wiring if this happens consistently

---

## Quick Reference

| Role    | Directory              | Flash Command             | Extra Step          |
|---------|------------------------|---------------------------|---------------------|
| Node    | `firmware/node/`       | `pio run -t upload`       | —                   |
| Gateway | `firmware/gateway/`    | `pio run -t upload`       | `pio run -t uploadfs` |
| Relay   | `firmware/relay/`      | `pio run -t upload`       | —                   |

| Useful Commands | |
|---|---|
| `pio device list` | List connected boards |
| `pio device monitor` | Open serial monitor (115200 baud) |
| `pio run` | Build without uploading |
| `pio run -t clean` | Clean build artifacts |
| `pio pkg install` | Install/update libraries |
| `pio pkg list` | List installed libraries |

---

## What's Next?

After flashing:
1. **Wire up the hardware** — see [docs/WIRING.md](WIRING.md)
2. **Power on the gateway first** — it creates the WiFi AP
3. **Power on nodes one at a time** — watch the serial monitor to note their IDs
4. **Connect to `DairyLedger` WiFi** — open http://192.168.4.1 to see the dashboard
5. **Label your nodes** from the Admin page
