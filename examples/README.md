# examples/

Standalone ESP-IDF reference projects for the **ESP32-S3-BOX-3**. Each subfolder is a self-contained project that can be built with `idf.py build` from inside it — none of them are pulled into the top-level `Espress_dev/` build.

| Example | Purpose | Dependencies |
|---------|---------|--------------|
| [`sensor_example/`](sensor_example/) | Six-tab self-test of every BOX-3 + BOX-3-SENSOR peripheral (IMU, AHT30, AT581X radar, ES7210 mic, IR, buttons). The original firmware before the Beszel pivot. | `espressif/esp-box-3`, `espressif/icm42670` |
| [`server_monitor/`](server_monitor/) | First Beszel-only build before the `Claude` tab was added. Polls a self-hosted Beszel/PocketBase instance and shows CPU/MEM/GPU bars per host. | `espressif/esp-box-3`, `espressif/cjson` |
| [`hotplate_controller/`](hotplate_controller/) | Touch controller for a lab hotplate stirrer via the HotplateController FastAPI server. Plate/probe temperature, stir speed, setpoint +/- buttons. | `espressif/esp-box-3`, `espressif/cjson` |

Each example owns its `sdkconfig`, dependencies, and partition table; new features land in the folder of the firmware they belong to. Historically the active firmware lived at the repository root in `main/`, with these folders kept as snapshots of it — that root project no longer exists, and every app is now a peer under `examples/`.

## Building any example

Each example is its own ESP-IDF project. From the example folder:

```powershell
cd examples/sensor_example     # or examples/server_monitor
idf.py set-target esp32s3       # once, only if build/ is missing
idf.py build
idf.py -p COM<N> flash monitor   # Ctrl+] to exit
```

The hardware config (`sdkconfig.defaults`) is identical across all examples: 16 MB flash, Octal PSRAM at 80 MHz, USB-Serial JTAG console, LVGL with float-print enabled.

## `server_monitor/` Kconfig

Before flashing `server_monitor/`, set WiFi and Beszel credentials via `idf.py menuconfig` → `Beszel monitor`. Each example keeps its own `sdkconfig`, so credentials must be set per example folder.

## Why each example is in its own folder

ESP-IDF's project model expects exactly one `main/` component per build. Combining multiple "apps" into the root project is possible (multi-app partitions, `EXTRA_COMPONENT_DIRS` gymnastics) but messy. The standard `esp-idf/examples/`, `esp-bsp/examples/` layout — one standalone project per folder — keeps each example's `sdkconfig`, dependencies, and partition table independent and lets `idf.py` operate normally from inside any example folder.

## Adding a new example

1. `mkdir examples/<name> examples/<name>/main`
2. Move sources into `examples/<name>/main/` with a component-level `CMakeLists.txt` (`idf_component_register(SRCS ... INCLUDE_DIRS "." REQUIRES ...)`).
3. Create `examples/<name>/CMakeLists.txt`:
   ```cmake
   cmake_minimum_required(VERSION 3.16)
   include($ENV{IDF_PATH}/tools/cmake/project.cmake)
   project(<name>)
   ```
4. Copy `sdkconfig.defaults` from a sibling example and adjust if the hardware target differs. Check it for dead Kconfig symbols after copying — a clean build means zero `unknown kconfig symbol` warnings (see `LearnedPatterns.md` §3.11).
5. Add a row to the table at the top of this file and to the root [`README.md`](../README.md) if the example is user-facing.
