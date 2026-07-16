# ESP32S3WebMonitor

A **Home Assistant control panel** for the [ESP32-S3-BOX-3](https://github.com/espressif/esp-box). Toggle your lights and switches from the 320×240 LCD, and watch your sensors, without HA's app.

**No entity IDs live in this firmware.** It knows one thing: a Home Assistant **label**. Every poll asks HA "give me everything tagged `box3`", so labelling one more entity in HA makes it appear on screen within one poll — no rebuild, no reflash. Unlabel it and it disappears.

This README walks an absolute beginner from a fresh Windows PC to flashing the firmware. If you have already used ESP-IDF, jump straight to [Build, Flash, Monitor](#6-build-flash-monitor).

> Other firmware in this repo lives under [`examples/`](examples/) — a Beszel server monitor, a Tapo plug controller, a hotplate controller, a syringe-pump client, and a BOX-3 peripheral self-test. Each is a standalone ESP-IDF project.

---

## Screen layout

One tab per domain, built from whatever came back labelled. A domain with nothing labelled gets no tab at all.

```
┌─────────────────────────────────────┐
│  Switch  │  Sensor                  │ ← one tab per domain present
├─────────────────────────────────────┤
│ UNO Q MCU UNO Q LED3_R      (   ●)  │
│ UNO Q MCU UNO Q LED3_G      (   ●)  │
│ UNO Q MCU UNO Q LED3_B      (   ●)  │
│ tapo_p1                     (   ●)  │
│ tapo_p2                     (●   )  │ ← green = on
├─────────────────────────────────────┤
│ updated                             │ ← status footer
└─────────────────────────────────────┘
```

The `Sensor` tab is read-only — name on the left, value and unit on the right:

```
┌─────────────────────────────────────┐
│ tapo_p1 Current consumption   0.0 W │
│ tapo_p2 Current consumption   7.7 W │
│ tapo_p3 Current consumption   0.0 W │
└─────────────────────────────────────┘
```

- **Tabs**: `Lights` / `Switch` / `Sensor`, created only for domains that have a labelled entity.
- **Touch a switch** → the command is queued and sent, then the state is re-read from HA. If HA refuses, the switch snaps back rather than lying to you.
- **`CONFIG` / `MUTE`** buttons cycle tabs (prev / next). Touch swipe works natively.
- **Status footer**: `WiFi connecting…` / `set HA token (menuconfig)` / `sending…` / `updated` / `HA unreachable`.
- **Unavailable** entities stay on screen, greyed out, rather than vanishing.
- **Names** come from HA, but a non-ASCII name falls back to the entity_id's object part (`light.living_room` → "living room") because the only font compiled in is `montserrat_14`.

---

## Hardware required

- **ESP32-S3-BOX-3** main board (any revision — both GT911 and TT21100 touch controllers are supported by the BSP)
- A **USB-C cable that carries data** (a charge-only cable is the #1 cause of "device not detected")
- A reachable **Beszel** server on the same WiFi network (HTTP / HTTPS both work; the project defaults to plain HTTP)
- For the Claude tab: a host PC on the same LAN that runs [`claude_usage_server.py`](claude_usage_server.py) and owns a `ClaudeUsage.csv` somewhere in the workspace. Optional — the Claude tab simply shows `server unreachable` if no server is running.
- *(Optional)* A **common-cathode RGB LED** (or three discrete LEDs sharing a ground rail) on PMOD1 — R to GPIO 21, G to GPIO 38, B to GPIO 39, common to GND, with appropriate current-limit resistors (typically 220 Ω–1 kΩ per channel against the 3.3 V drive). The firmware blinks it orange (R+G ON, B OFF) for ~300 ms on every successful Claude usage poll, giving a visible "data is fresh" heartbeat from across the room. Skip the LED entirely if you don't need it — `usage_led_init()` only configures GPIOs and never blocks.

The previous board self-test firmware (which also exercised the BOX-3-SENSOR extension's IMU, AHT30, AT581X radar, IR, and audio peripherals) is preserved as a frozen reference under [`examples/sensor_example/`](examples/sensor_example/) — see [Prior firmware: examples/sensor_example/](#prior-firmware-examplessensor_example) below.

---

## 1. Install VSCode

Download from <https://code.visualstudio.com/> and install with defaults. No special options needed.

## 2. Install the official ESP-IDF VSCode extension

In VSCode, open the Extensions side panel (`Ctrl+Shift+X`) and search for **"ESP-IDF"** — the publisher must be **Espressif Systems**. Install it.

The extension is what bridges VSCode and the ESP-IDF toolchain; it owns the build/flash/monitor commands and the Kconfig editor.

## 3. Configure ESP-IDF (one time, ~10–20 minutes)

Open the Command Palette (`Ctrl+Shift+P`) → **`ESP-IDF: Configure ESP-IDF Extension`** → choose **Express** install with:

- ESP-IDF version: **v6.0.1** (or any v5.3+)
- ESP-IDF Tools directory: `C:\Espressif\tools` (default)
- Python environment: let the extension create one

Wait for the green "Configuration complete" message. After this you never have to source `export.ps1` manually — the extension handles it.

## 4. Get the source and Windows USB drivers

```powershell
git clone https://github.com/coport-uni/ESP32S3WebMonitor.git
cd ESP32S3WebMonitor
code .
```

When VSCode prompts to trust the workspace, accept. The extension will detect the project type automatically.

### Windows USB driver setup (Zadig, one time per host)

The ESP32-S3 USB-C port exposes a **composite device** with two interfaces — one CDC (virtual COM for `monitor`), one JTAG (for `flash` / debugger). Windows does not always bind the correct driver to each.

1. Plug in the BOX-3 via USB-C.
2. Download [Zadig](https://zadig.akeo.ie/) and run it.
3. `Options` → **List All Devices**.
4. For the **CDC** interface, keep / install **USB Serial (CDC)**. A `COMxx` should appear in Device Manager.
5. For the **JTAG** interface, install **WinUSB v6.x**. Do **not** replace the CDC half with WinUSB.
6. Confirm in Device Manager: one entry under *Ports (COM & LPT)*, one entry under *Universal Serial Bus devices* with `WinUSB`.

Symptoms of getting Zadig wrong:
- No COM port appears → CDC interface mis-driven.
- `idf.py flash` complains about libusb / cannot open device → JTAG interface mis-driven.

## 5. Configure WiFi and Home Assistant

### 5a. Create a long-lived access token

**The HA REST API does not accept your username and password** — those only log into the web UI. It takes a long-lived token instead.

1. Log into Home Assistant in a browser.
2. Click your user name, bottom-left.
3. Open the **Security** tab and scroll to **Long-lived access tokens**.
4. **Create token**, name it `box3`, and copy the string. **It is shown once and cannot be retrieved afterwards.**

The default lifespan is 10 years, so the firmware carries no token-refresh logic.

### 5b. Label the entities you want on screen

The firmware shows exactly the entities carrying one label. In Home Assistant:

1. **Settings → Areas & labels → Labels → Create label**, named `box3`.
2. For each entity you want: open it → **Settings** (gear) → **Labels** → add `box3`.

Label whatever you like from the `light`, `switch` and `sensor` domains. Anything else is skipped with a log line.

> **Why a label instead of "just show every switch"?** Because Home Assistant gives you no way to tell a real device from an integration's own config entities. `switch.tapo_p1_led` and `switch.tapo_p1_auto_off_enabled` sit in the same domain and the same area as `switch.tapo_p1`, with no `device_class` and no `entity_category` visible to the template API — all five discriminators return identical values (measured; see [`claude_test/README.md`](claude_test/README.md)). On the test server a domain sweep put 18 switches on screen of which 3 were real plugs. The label is the only thing that separates them.

### 5c. Fill in Kconfig

```
Ctrl+E G                # or Command Palette → "ESP-IDF: SDK Configuration editor"
```

Navigate to `(Top) → Home Assistant client`:

| Option | Example value |
|---|---|
| WiFi SSID | `home-2.4g` |
| WiFi password | `…` |
| Home Assistant base URL | `http://192.168.1.232:8123` |
| Long-lived access token | `eyJhbGciOi…` (from 5a) |
| Home Assistant label to show | `box3` (default) |
| Poll interval (seconds) | `10` (default) |
| Max labelled entities | `16` (default) |

Save and close. Values land in the **local `sdkconfig`** file, which is git-ignored — the token and WiFi password never get committed. `sdkconfig.defaults` only carries non-secret hardware defaults (PSRAM, flash size, etc.) and stays tracked.

### 5d. Check HA is reachable before you flash

Confirm the token and the label from your PC first — debugging this over a serial log is far slower:

```powershell
curl -s -X POST "http://192.168.1.232:8123/api/template" `
  -H "Authorization: Bearer <TOKEN>" -H "Content-Type: application/json" `
  -d '{\"template\":\"{{ label_entities(''box3'') | join(''\n'') }}\"}'
```

It should list your labelled entity IDs. `401` means the token is wrong; an empty result means the label is empty or misspelled.

Plain HTTP only — HTTPS would need a cert bundle and is not configured.

## 6. Build, Flash, Monitor

The extension binds every common action to a chord shortcut starting with `Ctrl+E`:

| Shortcut | What it does |
|----------|--------------|
| `Ctrl+E T` | Set target → choose **esp32s3** (only required once) |
| `Ctrl+E P` | Select serial port → pick the COM number that appeared after Zadig |
| `Ctrl+E B` | Build only |
| `Ctrl+E F` | Flash only |
| `Ctrl+E M` | Open serial monitor (exit with `Ctrl+]`) |
| `Ctrl+E D` | **Build + Flash + Monitor in one shot** — the everyday command |
| `Ctrl+E G` | Open the graphical menuconfig (Kconfig) |
| `Ctrl+Shift+P` → `ESP-IDF: …` | Anything else (full clean, reconfigure, etc.) |

First-time sequence: `Ctrl+E T` → `Ctrl+E P` → `Ctrl+E G` (credentials) → `Ctrl+E D`. After that, only `Ctrl+E D` for every iteration.

Expected first-boot output (truncated):
```
I (xxx) main: Home Assistant client starting
I (xxx) ESP-BOX-3: Setting LCD backlight: 100%
I (xxx) ui: ui ready
I (xxx) buttons: registered 3 buttons
I (xxx) network: starting wifi, ssid="..."
I (xxx) network: got IP 192.168.x.x
I (xxx) ha_client: parsed 9 entities: 0 lights, 6 switches, 3 sensors
```

The LCD then shows one tab per domain that had a labelled entity. Touching a switch adds:

```
I (xxx) ha_client: /api/services/switch/turn_on ok
I (xxx) ha_client: parsed 9 entities: 0 lights, 6 switches, 3 sensors
```

`parsed 0 entities` means the label is empty or misspelled — re-run the curl check in [5d](#5d-check-ha-is-reachable-before-you-flash).

---

## Project layout

```
Espress_dev/
├── main/                   # ← the firmware this README builds
│   ├── main.c              # app_main: bsp → ui_create → buttons → network → ha_client
│   ├── ui.c, ui.h          # domain tabview, lv_switch rows, sensor rows, tab cycling
│   ├── network.c, .h       # non-blocking WiFi STA + auto-reconnect
│   ├── ha_client.c, .h     # HA REST client: template poll, TSV parse, command queue
│   ├── buttons_check.c, .h # CONFIG / MUTE physical buttons → ui_select_*_tab callbacks
│   ├── Kconfig.projbuild   # menuconfig: Home Assistant client
│   ├── CMakeLists.txt      # SRCS + REQUIRES (esp_wifi, esp_http_client, …)
│   └── idf_component.yml   # Managed components (esp-box-3 BSP, espressif/cjson)
├── examples/               # other standalone firmware, each its own idf project
│   ├── sensor_example/     # BOX-3 peripheral self-test (6-tab dashboard)
│   ├── server_monitor/     # Beszel host monitor + Claude usage tab + RGB LED
│   ├── smart_plug/         # Tapo plugs via a FastAPI bridge
│   ├── hotplate_controller/# hotplate temperature/stir control
│   └── sy01b_firmware/     # syringe pump client
├── claude_test/            # throwaway probes + what each one taught us
├── sdkconfig.defaults      # hardware Kconfig (16 MB flash, octal PSRAM, LVGL float, …)
├── managed_components/     # auto-pulled libraries — DO NOT EDIT BY HAND
├── CLAUDE.md               # coding rules + initialization order documentation
├── LearnedPatterns.md      # bugs we hit and how we found them (read when stuck)
├── ToDo.md                 # append-only project history
└── README.md               # this file
```

`app_main` initialization order is non-negotiable (see [CLAUDE.md](CLAUDE.md) "Initialization order"): I²C → display → backlight → UI under display lock → buttons → network → HA client. Touching this order risks LVGL panics or WiFi/HTTP failure modes that look like network bugs.

### How the client works

- **Discovery and polling are one request.** Every `CONFIG_HA_CLIENT_POLL_INTERVAL_S`, [`ha_client.c`](main/ha_client.c) renders one Jinja template server-side via `POST /api/template` and gets back ~500 bytes of tab-separated text. `GET /api/states` would drag back every entity in the installation — hundreds of KB — and truncate against the 8 KB buffer.
- **Touch never does HTTP.** An LVGL callback that blocks 1–3 s on a request trips the watchdog. The callback resolves the entity_id and posts it to a queue; the worker's `xQueueReceive(q, &cmd, period)` doubles as the poll timer — a command short-cuts the wait, a timeout means refresh.
- **The command carries an entity_id, not an index.** By the time the worker drains the queue it may have re-polled and reshuffled the cache, and a stale index would switch the wrong device.
- **`turn_on` / `turn_off`, never `toggle`.** A toggle issued against a stale screen lands on the opposite of what was asked.
- **Lock order is one-way.** `on_ui_action` runs with the LVGL lock held and takes the cache mutex; the worker must therefore never hold the cache mutex while calling into `ui_*`. `publish_to_ui()` copies under the lock, releases, *then* draws.

Tab cycling is owned by [`ui.c`](main/ui.c): `ui_select_prev_tab` / `ui_select_next_tab` walk `lv_tabview_get_tab_count()`, so the buttons work regardless of which domains ended up with tabs.

---

## Prior firmware: `examples/sensor_example/`

[`examples/sensor_example/`](examples/sensor_example/) holds a **frozen copy of the previous firmware** that the BOX-3 ran before this project pivoted to monitoring Beszel. It boots a six-tab LVGL dashboard that exercises every peripheral on the ESP32-S3-BOX-3 + BOX-3-SENSOR extension board:

| Tab | What it shows |
|-----|---------------|
| **IMU** | Live accel / gyro / tilt from the on-board ICM42670 |
| **Env** | Temperature / humidity from the AHT30 on the SENSOR extension |
| **Radar** | AT581X presence detection events + count |
| **Audio** | ES7210 mic RMS bar + "Beep" button (ES8311 speaker, 1 kHz tone) |
| **IR** | RX pulse count + "Send Test" NEC frame over the IR diodes |
| **Btn** | Short / long press counters for CONFIG, MUTE, MAIN buttons |

It existed to **verify each peripheral worked** before any application code was written. Those peripherals are confirmed, so the self-test now lives on as documentation and a re-validation tool.

It is a **standalone ESP-IDF project** — its own top-level `CMakeLists.txt`, `sdkconfig.defaults`, and `main/`. Every project under `examples/` builds the same way:

```powershell
cd examples/sensor_example
idf.py set-target esp32s3
idf.py -p COM<N> flash monitor
```

Code is deliberately **not** shared between them: `network.c` exists as several near-identical copies differing only in their `CONFIG_*` key names. Promoting it to a common `components/` was tried and rejected because the signatures had already drifted apart per project. Copy and rename the keys — that is the sanctioned workflow here. See [`examples/README.md`](examples/README.md) for the full inventory.

---

## Common pitfalls

These cost real time and are documented in detail with file/line references in [LearnedPatterns.md](LearnedPatterns.md):

- **The HA REST API rejects your username and password** — they only log into the web UI. It needs a long-lived access token; see [5a](#5a-create-a-long-lived-access-token). A `401` with credentials that "definitely work" is this, every time.
- **`GET /api/states` does not fit on the device** — it returns *every* entity, hundreds of KB on a real install, and truncates against the response buffer. Render a filtered Jinja template with `POST /api/template` instead and let HA do the work; the payload drops to ~500 bytes. This is LP §2.3's rule (bound the payload at the source, not the device buffer) in a second guise.
- **HA cannot tell a real device from an integration's config entities** — `switch.plug_led` and `switch.plug` share a domain and an area, both have `device_class: NONE`, and `entity_category` is not exposed to templates. There is no attribute to filter on; that is why this firmware uses a label. Measured on a live server — see [`claude_test/README.md`](claude_test/README.md).
- **Windows Firewall silently blocks ESP → Python `http.server`** — `curl` from the **same PC** as the server hits the loopback path and bypasses the firewall, so "curl works locally" is **not** a proof the ESP can reach it. Always test from a second machine. See LP §5.9. (Does not apply to Home Assistant itself, which is a separate host.)
- **`json` component is missing in ESP-IDF v6.x** — cJSON is now the standalone managed component `espressif/cjson`. The legacy `REQUIRES json` line fails to resolve. Declare `espressif/cjson` in [main/idf_component.yml](main/idf_component.yml).
- **`NAME_MAX` collides with picolibc's filesystem constant** (255). The xtensa-esp-elf toolchain pulls `<sys/syslimits.h>` transitively through BSP / FreeRTOS headers — never `#define NAME_MAX` in your own code. Rename to e.g. `HOST_NAME_MAX_LEN`.
- **`printf("%u", uint32_t)` is `-Werror=format=` under picolibc** — on the xtensa target, `uint32_t = unsigned long`, not `unsigned int`. Cast to `(unsigned)` or use `PRIu32` from `<inttypes.h>`.
- **`idf.py flash` cannot find the chip** — usually the USB-C cable is power-only, or Zadig drivers are swapped.
- **`sdkconfig` overrides `sdkconfig.defaults`** once it exists. If you flip a Kconfig value in `sdkconfig.defaults` but the build still uses the old value, the answer is in `sdkconfig` — either patch it there too, or delete it and `idf.py reconfigure`.
- **LVGL's default font has no Hangul glyphs** — only `lv_font_montserrat_14` is enabled. This firmware works around it by falling back to the entity_id's object part when a friendly name is not ASCII (`pick_display_name()` in [main/ha_client.c](main/ha_client.c)); HA always slugifies entity IDs to ASCII. Enabling `LV_FONT_SOURCE_HAN_SANS_SC_*_CJK` adds ~200 KB of flash and even then SC may not cover Hangul — keep the fallback on the ESP.
- **Not every GPIO is free for re-use, and not every pin can drive LEDC PWM** — on ESP32-S3 BOX-3, strapping pins (GPIO 0, 3, 45, 46), the flash/PSRAM data lanes (GPIO 26-37 with octal PSRAM), the USB-Serial-JTAG pair (GPIO 19/20), and pins claimed by the BSP for touch/audio/display/SD all have hard restrictions. Assigning `ledc_channel_config` to a pin that does not support LEDC routing on this part, or one already owned by the BSP, panics at boot — often *after* `gpio_config` returns OK, so the symptom is a reset loop rather than an `ESP_ERROR_CHECK` print. The Claude heartbeat LED uses PMOD1 (GPIO 21 / 38 / 39 = IO5/IO7/IO3) and drives it as plain digital ON/OFF for exactly this reason. See [LearnedPatterns §4.2](LearnedPatterns.md#42-verify-gpio-capability-and-current-usage-before-assigning-a-pin-datasheet--bsp--project) for the three-step pre-flight check (datasheet capability → BSP grep → project grep) before claiming any new GPIO.

---

## Extending it

**Adding a device needs no code at all** — label it `box3` in Home Assistant and it shows up on the next poll. That is the whole point of the label design.

Code changes are only for new *kinds* of thing:

1. Read [CLAUDE.md](CLAUDE.md) §2 (style) and §7 (research-before-coding).
2. **Prove it against the real server before writing firmware.** Copy [`claude_test/probe_ha_template.py`](claude_test/probe_ha_template.py) and render your candidate template over REST. This is what caught the original domain-sweep design being unworkable — before the first flash, at the cost of a few lines instead of a debugging session over a serial log.
3. **A new domain** (e.g. `climate`): add it to `domain_from_name()` and `kind_of()` in [main/ha_client.c](main/ha_client.c), add a `ui_ha_kind_t` in [main/ui.h](main/ui.h), and give it a row builder and a `TABS[]` entry in [main/ui.c](main/ui.c).
4. **A new attribute** (e.g. light brightness): add it to the template in `build_template()`, widen `MAX_TSV_FIELDS`, and carry it through `ha_entity_t` → `ui_ha_entity_t`. Every LVGL call from outside the LVGL task **must** be wrapped in `bsp_display_lock` / `bsp_display_unlock` — the `UI_WITH_LOCK` macro handles this.
5. **A new data source unrelated to Home Assistant** gets its own module — its own Kconfig menu, poll task, and `ui_*` API — or its own standalone project under `examples/`. Do not bolt unrelated data into `ha_client.c`.
6. Append a dated section to [ToDo.md](ToDo.md), then check items off as you go.
7. When the work is done, distill any new gotcha into [LearnedPatterns.md](LearnedPatterns.md).

---

## License

See per-component licenses under `managed_components/`. Application source under `main/` and `examples/` is unencumbered — use as a reference for your own BOX-3 projects.
