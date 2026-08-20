# ESP32S3WebMonitor

A collection of **standalone ESP-IDF firmware projects for the
[ESP32-S3-BOX-3](https://github.com/espressif/esp-box)** — each one turns the
board's 320×240 touch LCD into a control panel or dashboard for something on
your LAN.

There is **no firmware at the repository root**. Every app is a peer under
[`examples/`](examples/), with its own `sdkconfig`, dependencies, and partition
table, built by running `idf.py` from inside its folder.

## Pick a project

| Project | What it does | Needs |
|---------|--------------|-------|
| [`home_assistant_client/`](examples/home_assistant_client/) | **Home Assistant control panel.** Toggle labelled lights and switches, watch labelled sensors. No entity IDs in the firmware — label an entity in HA and it appears within one poll. | A Home Assistant server + long-lived token |
| [`server_monitor/`](examples/server_monitor/) | **Beszel host monitor.** CPU / MEM / GPU bars per host from a self-hosted Beszel/PocketBase instance, plus a Claude usage tab and an RGB heartbeat LED. | A Beszel server; optionally [`claude_usage_server.py`](claude_usage_server.py) on a host PC |
| [`smart_plug/`](examples/smart_plug/) | **TP-Link Tapo plug controller** via a LAN FastAPI bridge — no Tapo library on the device. | The FastAPI bridge in [`esp32-integration.md`](esp32-integration.md) |
| [`hotplate_controller/`](examples/hotplate_controller/) | **Lab hotplate/stirrer controller.** Plate and probe temperature, stir speed, setpoint +/- buttons. | The HotplateController FastAPI server |
| [`sy01b_firmware/`](examples/sy01b_firmware/) | **SY-01B syringe pump client** over a FastAPI bridge. | The SY-01B server |
| [`sensor_example/`](examples/sensor_example/) | **BOX-3 peripheral self-test.** Six-tab dashboard exercising the IMU, AHT30, AT581X radar, ES7210 mic, IR, and buttons. The original firmware, kept as a re-validation tool. | BOX-3-SENSOR extension board for the full set |

Each project's own README covers its wiring, Kconfig keys, and screen layout.
[`examples/README.md`](examples/README.md) explains why each one is a separate
project rather than a shared build.

---

## One-time host setup

Needed once per PC, no matter which project you build.

### 1. Install VSCode

Download from <https://code.visualstudio.com/> and install with defaults. No special options needed.

### 2. Install the official ESP-IDF VSCode extension

In VSCode, open the Extensions side panel (`Ctrl+Shift+X`) and search for **"ESP-IDF"** — the publisher must be **Espressif Systems**. Install it.

The extension is what bridges VSCode and the ESP-IDF toolchain; it owns the build/flash/monitor commands and the Kconfig editor.

### 3. Configure ESP-IDF (~10–20 minutes)

Open the Command Palette (`Ctrl+Shift+P`) → **`ESP-IDF: Configure ESP-IDF Extension`** → choose **Express** install with:

- ESP-IDF version: **v6.0.1** (or any v5.3+)
- ESP-IDF Tools directory: `C:\Espressif\tools` (default)
- Python environment: let the extension create one

Wait for the green "Configuration complete" message. After this you never have to source `export.ps1` manually — the extension handles it.

### 4. Get the source

```powershell
git clone https://github.com/coport-uni/ESP32S3WebMonitor.git
cd ESP32S3WebMonitor
code .
```

When VSCode prompts to trust the workspace, accept.

> **Open the project folder, not the repo root.** The extension expects a
> top-level `CMakeLists.txt`. Point it at `examples/<project>/` — for example
> `code examples/home_assistant_client`.

### 5. Windows USB drivers (Zadig)

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

---

## Everyday build, flash, monitor

From a terminal, inside the project folder:

```powershell
cd examples/home_assistant_client   # or any other project folder
idf.py set-target esp32s3           # once, only if build/ is missing
idf.py menuconfig                   # WiFi + server credentials, per project
idf.py build
idf.py -p COM<N> flash monitor      # Ctrl+] to exit
```

In VSCode the extension binds the same actions to chord shortcuts starting with `Ctrl+E`:

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

Every project shares the same hardware config in its `sdkconfig.defaults`:
16 MB flash, Octal PSRAM at 80 MHz, USB-Serial-JTAG console, LVGL with
float-print enabled. Credentials go in the **local `sdkconfig`**, which is
git-ignored, so they are never committed — and they are **per project folder**.

---

## Repository layout

```
Espress_dev/
├── examples/               # every firmware project — each its own idf project
│   ├── home_assistant_client/  # HA lights/switches/sensors control panel
│   ├── server_monitor/         # Beszel host monitor + Claude usage tab + RGB LED
│   ├── smart_plug/             # Tapo plugs via a FastAPI bridge
│   ├── hotplate_controller/    # hotplate temperature/stir control
│   ├── sy01b_firmware/         # syringe pump client
│   └── sensor_example/         # BOX-3 peripheral self-test (6-tab dashboard)
├── claude_test/            # throwaway probes + what each one taught us
├── managed_components/     # auto-pulled libraries — DO NOT EDIT BY HAND
├── media/                  # screenshots and photos
├── claude_usage_server.py  # host-side CSV server for server_monitor's Claude tab
├── esp32-integration.md    # FastAPI bridge notes for the Tapo smart-plug server
├── DeviceChange.ps1        # Windows USB device-swap helper
├── CLAUDE.md               # coding rules + initialization order documentation
├── LearnedPatterns.md      # bugs we hit and how we found them (read when stuck)
├── ToDo.md                 # append-only project history
└── README.md               # this file
```

Code is deliberately **not** shared between projects: `network.c` exists as
several near-identical copies differing only in their `CONFIG_*` key names.
Promoting it to a common `components/` was tried and rejected because the
signatures had already drifted apart per project. Copy and rename the keys —
that is the sanctioned workflow here.

`app_main` initialization order is non-negotiable in every project (see
[CLAUDE.md](CLAUDE.md) "Initialization order"): I²C → display → backlight → UI
under the display lock → everything else. Touching this order risks LVGL panics
or failure modes that look like network bugs.

---

## Common pitfalls

These cost real time and are documented in detail, with file/line references,
in [LearnedPatterns.md](LearnedPatterns.md). Project-specific traps live in each
project's own README.

- **`idf.py flash` cannot find the chip** — usually the USB-C cable is power-only, or the Zadig drivers are swapped. A stray `openocd.exe` left over from an earlier session also holds the WinUSB interface and produces `got response: '-1', expecting: '0'`.
- **`sdkconfig` overrides `sdkconfig.defaults`** once it exists. If you flip a Kconfig value in `sdkconfig.defaults` but the build still uses the old value, the answer is in `sdkconfig` — either patch it there too, or delete it and `idf.py reconfigure`.
- **`json` component is missing in ESP-IDF v6.x** — cJSON is now the standalone managed component `espressif/cjson`. The legacy `REQUIRES json` line fails to resolve; declare `espressif/cjson` in the project's `idf_component.yml`.
- **LVGL 9 dropped `LV_MEM_CUSTOM` / `LV_MEMCPY_MEMSET_STD`** — copying them into a new project's `sdkconfig.defaults` emits `unknown kconfig symbol` warnings. They are dead no-ops; delete them.
- **`NAME_MAX` collides with picolibc's filesystem constant** (255). The xtensa-esp-elf toolchain pulls `<sys/syslimits.h>` transitively through BSP / FreeRTOS headers — never `#define NAME_MAX` in your own code. Rename to e.g. `HOST_NAME_MAX_LEN`.
- **`printf("%u", uint32_t)` is `-Werror=format=` under picolibc** — on the xtensa target, `uint32_t = unsigned long`, not `unsigned int`. Cast to `(unsigned)` or use `PRIu32` from `<inttypes.h>`.
- **Windows Firewall silently blocks ESP → Python `http.server`** — `curl` from the **same PC** as the server hits the loopback path and bypasses the firewall, so "curl works locally" is **not** proof the ESP can reach it. Always test from a second machine. See LP §5.9.
- **Only `lv_font_montserrat_14` is compiled in** — the default LVGL font has no Hangul or CJK glyphs, and enabling a CJK font costs ~200 KB of flash. Keep non-ASCII text off the screen, or fall back to an ASCII form on the device.
- **Not every GPIO is free for re-use, and not every pin can drive LEDC PWM** — on ESP32-S3 BOX-3, strapping pins (GPIO 0, 3, 45, 46), the flash/PSRAM data lanes (GPIO 26-37 with octal PSRAM), the USB-Serial-JTAG pair (GPIO 19/20), and pins claimed by the BSP for touch/audio/display/SD all have hard restrictions. Assigning `ledc_channel_config` to a pin that does not support LEDC routing on this part, or one already owned by the BSP, panics at boot — often *after* `gpio_config` returns OK, so the symptom is a reset loop rather than an `ESP_ERROR_CHECK` print. See [LearnedPatterns §4.2](LearnedPatterns.md) for the three-step pre-flight check (datasheet capability → BSP grep → project grep) before claiming any new GPIO.
- **A background `idf.py build` reported as "stopped" keeps running** on Windows and holds `build/` — a second build on the same tree then dies with `File can't be removed and still exist`. Kill the first one before retrying.

---

## Starting a new project

1. Read [CLAUDE.md](CLAUDE.md) §2 (style) and §7 (research-before-coding).
2. Follow **Adding a new example** in [`examples/README.md`](examples/README.md) — folder skeleton, `CMakeLists.txt`, and which files to copy from a sibling.
3. **Prove the server side before writing firmware.** Host-side probes go in [`claude_test/`](claude_test/); the README there records what each one taught us. This has repeatedly killed an unworkable design before the first flash.
4. A new data source gets its **own module or its own project** — its own Kconfig menu, poll task, and `ui_*` API. Do not bolt unrelated data into an existing client.
5. Append a dated section to [ToDo.md](ToDo.md), then check items off as you go.
6. When the work is done, distill any new gotcha into [LearnedPatterns.md](LearnedPatterns.md).

---

## License

See per-component licenses under `managed_components/`. Application source
under `examples/` is unencumbered — use as a reference for your own BOX-3
projects.
