# hotplate_controller — lab hotplate/stirrer controller (ESP32-S3-BOX-3)

A standalone ESP-IDF app that turns the BOX-3 into a touch controller for a
lab hotplate stirrer, talking to a LAN **HotplateController FastAPI** server
over plain HTTP/JSON. No serial link to the hotplate runs on the device — the
server owns the instrument.

This is a separate program from the `server_monitor` (Beszel) firmware; it
does not share its tabview. The whole screen is the hotplate control panel.

## What it does

- On boot it connects to Wi-Fi, then polls `GET /status` every
  `CONFIG_HOTPLATE_POLL_INTERVAL_S` seconds for probe/plate temperature,
  stir speed, setpoints, and heater/motor state.
- The screen shows **plate** and **probe** temperature as separate rows (both
  always visible, `--` when offline), plus speed, target, and safety limit.
- Touch buttons adjust the setpoints by `CONFIG_HOTPLATE_TEMP_STEP_C` /
  `CONFIG_HOTPLATE_SPEED_STEP_RPM`, and start/stop the heater and motor.
  Heat/Stir buttons turn **red** while ON.
- Deltas are applied to the server's last-reported target by the client task,
  so the UI never tracks absolute setpoints itself.
- Button presses only **enqueue** a command; a background task performs the
  `POST /control/...` off the LVGL thread, so HTTP never blocks the UI. The
  connection indicator goes amber **pending** until the next status poll
  confirms the change.
- The two physical buttons mirror the temperature +/- touch buttons, so the
  setpoint can be driven without the touchscreen.

## Configure & build

```powershell
idf.py set-target esp32s3
idf.py menuconfig    # Hotplate monitor -> WiFi SSID/password, server URL
idf.py build
idf.py -p COM<N> flash monitor
```

Key options under **Hotplate monitor** (`menuconfig`):

| Option                        | Default                      | Notes                                   |
|-------------------------------|------------------------------|-----------------------------------------|
| `HOTPLATE_WIFI_SSID`          | `TP-Link_0624`               | WPA2-Personal SSID; empty skips Wi-Fi   |
| `HOTPLATE_WIFI_PASSWORD`      | (see Kconfig)                | WPA2-Personal passphrase                |
| `HOTPLATE_SERVER_URL`         | `http://192.168.1.129:17048` | FastAPI base URL (scheme + port)        |
| `HOTPLATE_POLL_INTERVAL_S`    | `2`                          | `GET /status` period (1-60 s)           |
| `HOTPLATE_TEMP_STEP_C`        | `5`                          | Temperature +/- button step (1-50 C)    |
| `HOTPLATE_SPEED_STEP_RPM`     | `100`                        | Speed +/- button step (10-500 rpm)      |

The ESP32 and the server must be on the same LAN subnet, and the server's TCP
port must be open between them.

## Layout

```
examples/hotplate_controller/
├── CMakeLists.txt
├── sdkconfig.defaults        # BOX-3: 16MB flash, Octal PSRAM 80M, JTAG console
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml      # espressif/esp-box-3 + espressif/cjson
    ├── Kconfig.projbuild      # Hotplate monitor menu
    ├── main.c                 # init order: I2C -> display -> UI -> wifi -> task
    ├── network.c/.h           # Wi-Fi STA + reconnect
    ├── buttons_check.c/.h     # physical button handling
    ├── hotplate_client.c/.h   # status polling + command queue (HTTP/cJSON)
    └── ui.c/.h                # full-screen readings + touch control buttons
```
