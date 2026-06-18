# smart_plug — Tapo plug controller (ESP32-S3-BOX-3)

A standalone ESP-IDF app that turns the BOX-3 into a touch controller for
TP-Link Tapo smart plugs, talking to the LAN **FastAPI** smart-plug server
(see [`../../esp32-integration.md`](../../esp32-integration.md)). No TP-Link
account or library runs on the device — just plain HTTP/JSON.

This is a separate program from the `server_monitor` (Beszel) firmware; it
does not share its tabview. The whole screen is the plug control panel.

## What it does

- On boot it connects to Wi-Fi, then calls `GET /plugs` to **discover** the
  plugs exposed by the server (no hard-coded names).
- Each plug is shown as a card: a status dot, its name, `ON`/`OFF`/`OFFLINE`,
  the instantaneous power in watts (or `retrieving...` while a fresh reading
  settles after a switch), and two touch buttons — **ON / OFF**.
- It refreshes every plug's state + power every
  `CONFIG_SMART_PLUG_POLL_INTERVAL_S` seconds (`GET /plugs/{name}` +
  `/energy`).
- Pressing a button performs `POST /plugs/{name}/{on|off}` **off the
  LVGL thread** (each call contacts the physical plug and takes ~1-3 s, so it
  must never block the UI). The touch callback only enqueues the request; a
  background task does the HTTP and pushes the new state back to the screen.

## Configure & build

```powershell
idf.py set-target esp32s3
idf.py menuconfig    # Smart plug client -> WiFi SSID/password, API base URL
idf.py build
idf.py -p COM<N> flash monitor
```

Key options under **Smart plug client** (`menuconfig`):

| Option                          | Default                        | Notes                                  |
|---------------------------------|--------------------------------|----------------------------------------|
| `SMART_PLUG_WIFI_SSID`          | (empty)                        | WPA2-Personal SSID                     |
| `SMART_PLUG_WIFI_PASSWORD`      | (empty)                        | WPA2-Personal passphrase               |
| `SMART_PLUG_SERVER_URL`         | `http://192.168.1.129:17046`   | FastAPI base URL (scheme + port)       |
| `SMART_PLUG_POLL_INTERVAL_S`    | `20`                           | State/power refresh period (5-600 s)   |
| `SMART_PLUG_MAX_PLUGS`          | `8`                            | Cap on discovered plugs shown          |

The ESP32 and the server must be on the same LAN subnet, and TCP `17046`
must be open between them.

## Layout

```
examples/smart_plug/
├── CMakeLists.txt
├── sdkconfig.defaults        # BOX-3: 16MB flash, Octal PSRAM 80M, JTAG console
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml      # espressif/esp-box-3 + espressif/cjson
    ├── Kconfig.projbuild      # Smart plug client menu
    ├── main.c                 # init order: I2C -> display -> UI -> wifi -> task
    ├── network.c/.h           # Wi-Fi STA + reconnect
    ├── smart_plug.c/.h        # discovery + command queue + polling (HTTP/cJSON)
    └── ui.c/.h                # full-screen card list + touch buttons
```
