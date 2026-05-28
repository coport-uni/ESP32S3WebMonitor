# LearnedPatterns.md

Lessons distilled from completed (`[x]`) items in [ToDo.md](ToDo.md). Read the relevant section before drafting new tasks; append new findings here after each task. Each entry follows the Problem / Cause / Fix / Rule format, with `(from ToDo: ...)` traceability at the end.

Created: 2026-05-11 (bootstrap from BOX-3 firmware work)

---

## §1. Recurring Issues

(none yet — promote here once the same problem class appears twice or more)

---

## §2. Solved Gotchas

### 2.1 ESP-IDF `sdkconfig` overrides `sdkconfig.defaults` once it exists

- **Problem**: Adding `CONFIG_LV_USE_FLOAT=y` to `sdkconfig.defaults` had no effect on the next build; LVGL still printed only the literal `f`.
- **Cause**: `sdkconfig.defaults` is consulted only when a key is *absent* from `sdkconfig`. After the first build, `sdkconfig` already contained `# CONFIG_LV_USE_FLOAT is not set` (explicit `n`), which won over the new default.
- **Fix**: Edit both files — keep the value in `sdkconfig.defaults` for reproducibility, AND replace the existing line in `sdkconfig`. Alternatively delete `sdkconfig` and let `idf.py reconfigure` regenerate it.
- **Rule**: When changing an existing Kconfig value, never assume `sdkconfig.defaults` alone is enough — patch `sdkconfig` too, or remove it. (from ToDo: 2026-05-11 Accel/Gyro "f" 표시 + AHT30 미동작 진단)

### 2.2 Boot log capture must cover the first sensor polling cycle

- **Problem**: Initial monitor log was truncated at `main_task: Returned from app_main()` (~t=1558ms), making it impossible to tell whether AHT30 polling was succeeding or timing out.
- **Cause**: First sensor_hub polling fires at `iot_sensor_start + min_delay` (= ~t=2418ms for 1 s period), one second *after* `app_main()` returns.
- **Fix**: Always capture at least 5–10 s of serial output for sensor diagnostics, covering multiple polling cycles.
- **Rule**: For sensor_hub / sensor-polling debugging, capture monitor output for `min_delay × 3` minimum, never just the boot banner. (from ToDo: 2026-05-11 마이크/AHT30 추가 진단)

---

## §3. Library Quirks

### 3.1 LVGL built-in `sprintf` drops `%f` unless `LV_USE_FLOAT=y`

- **Problem**: `lv_label_set_text_fmt(lbl, "X: %+.2f", ax)` produced `X: f` instead of the numeric value.
- **Cause**: LVGL's builtin printf defines `PRINTF_DISABLE_SUPPORT_FLOAT = !LV_USE_FLOAT`. When `LV_USE_FLOAT=n` (default), the entire `case 'f':` branch is `#if`-compiled out (managed_components/lvgl__lvgl/src/stdlib/builtin/lv_sprintf_builtin.c:42, 776-794). The format scanner consumes flags/precision then emits the unrecognized specifier literally.
- **Fix**: Enable `CONFIG_LV_USE_FLOAT=y` in `sdkconfig.defaults`. Alternative: use stdio `snprintf` into a buffer then `lv_label_set_text`.
- **Rule**: Any `lv_label_set_text_fmt` call with `%f`/`%e`/`%g` on this project assumes `LV_USE_FLOAT=y`. If float printing breaks in the future, check Kconfig first. (from ToDo: 2026-05-11 Accel/Gyro "f" 표시 + AHT30 미동작 진단)

### 3.2 BOX-3 BSP routes `HUMITURE_ID` to the dock I2C bus, not main

- **Problem**: AHT30 driver was created and `iot_sensor_start` returned OK, but no temperature/humidity events ever fired.
- **Cause**: `bsp_sensor_init(HUMITURE_ID, ...)` internally uses `i2c_dock_handle` (managed_components/espressif__esp-box-3/esp-box-3.c:890-893), which lives on GPIO 40/41 — wired to the PMOD1 connector, not the main I2C bus. With nothing on PMOD1, the sensor cannot respond no matter how correct the code is.
- **Fix**: Plug in the official ESP32-S3-BOX-3-SENSOR extension board (or any board with AHT30 + pull-ups on PMOD1).
- **Rule**: BOX-3 humiture / radar / IR sensors all live on PMOD1 (dock I2C), never on the main I2C bus. Verify physical extension presence before debugging sensor_hub events. (from ToDo: 2026-05-11 Accel/Gyro "f" 표시 + AHT30 미동작 진단)

### 3.3 BOX-3 BSP audio: gain must be set before `esp_codec_dev_open`

- **Problem**: ES7210 mic RMS bar stayed near zero even after `bsp_audio_codec_microphone_init` succeeded.
- **Cause**: `esp_codec_dev_set_in_gain` was called *after* `esp_codec_dev_open`, which differs from the BSP `API.md` reference example (gain 42.0 set before open).
- **Fix**: Match the BSP convention — call `esp_codec_dev_set_in_gain(mic, 42.0f)` first, then `esp_codec_dev_open`.
- **Rule**: Follow the BSP API.md sample order verbatim for ES7210/ES8311 init — gain before open, codec_dev created before sample_info struct passed in. (from ToDo: 2026-05-11 마이크/AHT30 추가 진단)

### 3.4 `sensor_hub`: `iot_sensor_handler_register_with_type` posts events to a different base than the polling task

- **Problem**: AHT30 humiture display never updated. Boot log showed `SENSOR_HUB: Sensor created ...` and `SENSOR_LOOP: register a new handler to event loop succeed × 2`, no `AHT30:` errors. Callback `humiture_event_cb` fired zero times.
- **Cause**: `sensor_hub` exposes two registration APIs that compile and register cleanly but bind to **different `esp_event` bases**. The polling task posts to a per-instance dynamic base built at `iot_sensor_hub.c:359` as `sprintf(sensor->event_base, "%s_%x", sensor_name, addr)` (e.g. `"sensor_hub_aht30_38"`) — that's what `sensors_event_post` uses at line 251. `iot_sensor_handler_register(handle, cb, ctx)` registers on `sensor->event_base` (line 634) → matches polling output. `iot_sensor_handler_register_with_type(HUMITURE_ID, event_id, cb, ctx)` registers on the **fixed macro base** `SENSOR_HUMITURE_EVENTS` (line 658) → never receives polling output. esp_event silently drops the mismatch.
- **Fix**: Use the handle-based variant: `iot_sensor_handler_register(s_humiture, humiture_event_cb, NULL)`. Branch on `event_id` inside the callback if you need per-event-id behaviour — `ESP_EVENT_ANY_ID` delivers everything (TEMP / HUMI / STARTED / STOPPED).
- **Rule**: When sensor_hub returns a handle, register against the handle. Treat `_with_type` as broken-by-design for the polling path; reach for it only if you also forward events from `sensor->event_base` to the macro base yourself. (from ToDo: 2026-05-11 AHT30 silent event drop 수정)

### 3.5 `esp_codec_dev_read`/`esp_codec_dev_write` return 0 on success, not the byte count

- **Problem**: ES7210 mic RMS bar stayed at 0 even though boot log showed `ES7210: Unmuted` and `Adev_Codec: Open codec device OK`. Beep button always set status to `beep write fail` even when sound played correctly.
- **Cause**: These APIs do **not** follow the POSIX `read`/`write` convention. Return value is `ESP_CODEC_DEV_OK` (= 0) on success, negative `ESP_CODEC_DEV_*` codes on failure (managed_components/espressif__esp_codec_dev/platform/audio_codec_data_i2s.c:717). The internal `bytes_read` / `bytes_written` counters are discarded inside the wrapper. User code that checks `if (got > 0)` or `if (written <= 0)` treats every successful call as a failure, breaking the data path silently.
- **Fix**: Check `== ESP_CODEC_DEV_OK` (or `== 0`) for success, and trust that the buffer was filled with exactly the requested `len` bytes (the wrapper blocks on `i2s_channel_read` with `DEFAULT_WAIT_TIMEOUT` until full).
- **Rule**: For any `esp_codec_dev_*` API, look up the return convention in `esp_codec_dev_types.h` before writing the success check. Don't reuse POSIX `read`/`write` muscle memory. (from ToDo: 2026-05-11 마이크 RMS 0 + beep 항상 실패 수정)

### 3.7 ESP-IDF v6.x dropped the in-tree `json` component — use `espressif/cjson`

- **Problem**: `idf.py reconfigure` failed with `CMake Error ... Failed to resolve component 'json' required by component 'main': unknown name.` on a project that builds fine under ESP-IDF v5.x.
- **Cause**: In ESP-IDF v6.0+, cJSON was moved out of the IDF tree and is now distributed exclusively through the [ESP Component Registry](https://components.espressif.com/) as `espressif/cjson`. No `components/json/` exists under the v6.0.1 install.
- **Fix**: Declare the dependency in `main/idf_component.yml` (`espressif/cjson: "^1.7.18"`) and remove `json` from `REQUIRES`. The header is still `cJSON.h` (capital J).
- **Rule**: When porting a project to ESP-IDF v6.x, audit `REQUIRES` against the in-tree component list — anything historical (`json`, possibly others) needs a managed-component replacement declared in `idf_component.yml`. (from ToDo: 2026-05-11 Beszel monitor)

### 3.8 Beszel `info.g` (GPU usage) is `omitempty` — single snapshot cannot tell "no GPU" from "0 %"

- **Problem**: H200Server and 3090Server (both confirmed to have NVIDIA GPUs) showed `GPU N/A` in the Beszel tab, even though the Beszel web UI displayed GPU graphs for them.
- **Cause**: In Beszel v0.18.x the `Info` struct serializes GPU usage as `GpuPct float64 \`json:"g,omitempty"\``. The `omitempty` tag means when current GPU usage is exactly `0.0`, the `g` key is dropped from the JSON of `/api/collections/systems/records`. Idle GPUs (CPU also reported ~0.05–0.16%) leave the field absent entirely; a single response cannot distinguish "GPU absent" from "GPU idle".
- **Fix**: Treat the GPU row as always present in the UI; render `info.g` value if the key exists, default to `0%` otherwise. The Beszel web UI achieves the same illusion by also reading the time-series `system_stats` records and picking up past non-zero samples.
- **Rule**: When parsing Beszel (or any Go-serialised) snapshot JSON, never rely on field *presence* to decide capability — `omitempty` hides zero values. If you need a definitive "feature exists?" signal, query the time-series `system_stats` for a non-zero sample, or accept that you cannot disambiguate from a single snapshot. (from ToDo: 2026-05-11 Beszel monitor)

### 3.9 `NAME_MAX` is a picolibc filesystem constant (255) — do not redefine

- **Problem**: `main/beszel.c:29: error: 'NAME_MAX' redefined [-Werror]` after introducing a `#define NAME_MAX 32` for the host-name buffer length.
- **Cause**: The xtensa-esp-elf toolchain ships **picolibc**, whose `<sys/syslimits.h>` defines `NAME_MAX 255` (max file name length). The BSP / FreeRTOS header chain pulls `<limits.h>` → `<syslimits.h>` long before user code is parsed. Any later `#define NAME_MAX <n>` collides under `-Werror`.
- **Fix**: Rename the project-local constant (e.g. `HOST_NAME_MAX_LEN`). Same advice applies to any other POSIX-flavoured `*_MAX` name — `PATH_MAX`, `LINE_MAX`, etc.
- **Rule**: Prefix project constants that *could* clash with POSIX with a module name (`BESZEL_NAME_MAX`, `UI_NAME_MAX`). picolibc is strict and shows up early. (from ToDo: 2026-05-11 Beszel monitor)

### 3.10 picolibc `uint32_t` is `unsigned long`, not `unsigned int` — `%u` is wrong

- **Problem**: `printf("Up %ud %uh", d, h)` with `uint32_t d, h` raised `-Werror=format=`: *expects 'unsigned int', argument has type 'uint32_t' {aka 'long unsigned int'}*.
- **Cause**: On the xtensa target with picolibc, `uint_least32_t` / `uint32_t` are typedefed to `unsigned long` (not `unsigned int`), making them mismatch `%u`. Glibc commonly types them as `unsigned int`, hiding this elsewhere.
- **Fix**: Use `PRIu32` / `PRId32` from `<inttypes.h>` (recommended for portability), or cast at the call site: `printf("%u", (unsigned)x)`. Format specifier `%lu` also works but is wrong on platforms where `uint32_t` is `unsigned int`.
- **Rule**: Never use `%u` / `%d` / `%x` with `uint32_t` directly on ESP-IDF — always cast or use `PRIu32`. Same goes for `uint64_t` (use `PRIu64`, not `%llu`). (from ToDo: 2026-05-11 Beszel monitor)

### 3.6 `bsp_i2c_get_handle()` returns the handle; does not take `&out`

- **Problem**: Earlier draft tried `bsp_i2c_get_handle(&bus)` and `icm42670_create(&cfg, &handle)`, both of which fail to compile or link.
- **Cause**: The ESP-BOX-3 BSP exposes the I2C bus via a value-returning getter, and `icm42670_create` takes three positional args `(bus, addr, &handle_out)` — there is no config-struct overload. This is documented in the project CLAUDE.md but easy to forget.
- **Fix**: Use `i2c_master_bus_handle_t bus = bsp_i2c_get_handle();` then `icm42670_create(bus, ICM42670_I2C_ADDRESS, &handle)`.
- **Rule**: Before calling any `managed_components/` function, open its `.h` and confirm the real signature (project CLAUDE.md §7 Research Before Coding). Memory of similar APIs is unreliable. (from CLAUDE.md prior incident; reinforced 2026-05-11)

---

## §4. Workflow Lessons

### 4.1 Diagnose float / printf bugs at the formatter level, not the data source

- **Problem**: Initially suspected the IMU driver was returning wrong values when Accel/Gyro showed only `f`.
- **Cause**: Skipped checking how the format spec was rendered. The IMU was fine; LVGL's sprintf was the actual failure point.
- **Fix**: When the suspicious value matches a literal piece of the format string ("f", "d", "%"), check the formatter implementation before the data path.
- **Rule**: A printed character that looks like a format specifier means the formatter dropped the conversion. Audit the printf implementation before the data source. (from ToDo: 2026-05-11 Accel/Gyro "f" 표시 + AHT30 미동작 진단)

### 4.2 Verify GPIO capability and current usage before assigning a pin (datasheet → BSP → project)

- **Problem**: First attempt at the Claude-usage heartbeat LED (2026-05-15 ToDo entry) picked R=GPIO 10, G=GPIO 11, B=GPIO 12 and drove them via LEDC PWM. The justification recorded in ToDo.md was only "the project's `main/` doesn't initialize SD or PMOD2, so the pins are free for LEDC output". Flashing the resulting firmware boot-looped — not the kind of error `ESP_ERROR_CHECK` would catch, but a reset before any task logged. Re-picking R/G/B as GPIO 21 / 38 / 39 (PMOD1 IO5/IO7/IO3) and dropping PWM in favour of digital R+G ON worked first try (commit `ef5eddf`).
- **Cause**: Two distinct hazards conflated into the single check "is `main/` calling something on this pin?":
  - **Peripheral capability** — not every ESP32-S3 GPIO can drive every peripheral. Strapping pins (GPIO 0, 3, 45, 46), flash/PSRAM data lanes (GPIO 26-37 on BOX-3 octal PSRAM), and the USB-Serial-JTAG pair (GPIO 19/20) have hard restrictions. LEDC routing in particular goes through the GPIO matrix but can fail at boot if the pin is already driven by another committed peripheral. The TRM § "IO MUX function list" is the source of truth, not memory.
  - **Latent BSP / hardware ownership** — "the project's `main/` doesn't touch the pin" is not the same as "no code touches the pin". `bsp_display_start()`, `bsp_i2c_init()`, `bsp_iot_button_create()`, and `bsp_audio_codec_*` each silently claim a handful of pins as part of their initialization. The BOX-3 BSP header `managed_components/espressif__esp-box-3/include/bsp/esp-box-3.h` documents the full ownership map, but only if you grep it. The 2026-05-15 ToDo note acknowledged GPIO 10/11/12 are mapped to SDMMC / PMOD2 SPI in the BSP and then dismissed the conflict because the project didn't init SD — which turned out to be the wrong call.
- **Fix**: Before assigning any GPIO in `gpio_config` / `ledc_channel_config` / `rmt_*_config` / similar, run a three-step pre-flight check **and write the result of each step into ToDo.md** so the reasoning is auditable:
  1. **Capability** — verify in the [ESP32-S3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf) §5 (IO MUX) that the pin can be routed to the intended peripheral. Cross-check against the strapping / flash / PSRAM reserved ranges for **this board** (BOX-3 = octal PSRAM, so GPIO 26-37 are off-limits even though the TRM lists them as general-purpose on bare S3).
  2. **BSP ownership** — grep the BSP header and source for the pin number, including pins reached transitively through `bsp_display_start` / `bsp_i2c_init`:
     ```powershell
     Select-String -Path managed_components/espressif__esp-box-3/include/bsp/esp-box-3.h -Pattern "GPIO_NUM_<n>\b"
     Select-String -Path managed_components/espressif__esp-box-3/esp-box-3.c          -Pattern "GPIO_NUM_<n>\b"
     ```
     Any pin appearing in a `BSP_*` macro is reserved once the matching `bsp_*_init` runs, even if your code never names the macro.
  3. **Project ownership** — grep `main/` for the pin number across all source, headers, and Kconfig files, including currently-disabled modules and commented stubs that may be reactivated later:
     ```powershell
     Select-String -Path main\*.c, main\*.h, main\Kconfig* -Pattern "GPIO_NUM_<n>\b"
     ```
  Prefer pins that are explicitly exposed for external use — on BOX-3 that's PMOD1 (`BSP_PMOD1_IO3 / IO5 / IO7 / IO8` = GPIO 39 / 21 / 38 / 40), which were chosen for the new heartbeat LED for exactly this reason.
- **Rule**: Pin numbers are never picked from memory or from "the project doesn't import that module". Run capability → BSP → project before writing the configuration, and record the three findings in the ToDo entry so a reviewer can see the work. If any step is uncertain, choose a different pin or test on a breadboard before soldering. (from ToDo: 2026-05-28 Claude 사용량 갱신 시 RGB LED 주황 깜빡 (핀 재지정))

---

## §5. Environment Specifics

### 5.1 BOX-3 PMOD1 (GPIO 40/41) has no on-board I²C pull-ups

- **Problem**: The dock I²C bus relies on external pull-ups, but the BOX-3 main board does not populate them.
- **Cause**: BSP header explicitly states "Intended for I2C SCL/SDA (pull-up NOT populated)" (managed_components/espressif__esp-box-3/include/bsp/esp-box-3.h:167,171). Espressif assumed users would plug a board (BOX-3-SENSOR or custom) that supplies the pull-ups.
- **Fix**: Use the official ESP32-S3-BOX-3-SENSOR extension, or add external ~4.7 kΩ pull-ups to 3V3 on SDA/SCL when prototyping with a bare PMOD board.
- **Rule**: Before debugging dock-I²C devices (AHT30, AT581X, …) in software, confirm a pull-up source is physically present. (from ToDo: 2026-05-11 ESP32-S3-BOX-3 + SENSOR 액세서리 전체 기능 점검)

### 5.2 `idf.py` is not on the Claude shell PATH (mitigated — see §5.7)

- **Problem**: Project hook `.claude/hooks/post-write-build-check.ps1` cannot run `idf.py build` after edits because `idf.py` is not on the shell's PATH.
- **Cause**: ESP-IDF is installed at `C:\Espressif\tools\python\v6.0.1\venv\` and exposed only via Espressif's `Initialize-Idf.ps1` / `export.ps1`, neither of which is sourced into the Claude session.
- **Fix**: Earlier sessions punted the build/flash to the user. As of 2026-05-11 there is a working in-shell recipe: see **§5.7** — set `IDF_PATH` / `IDF_TOOLS_PATH` / `IDF_PYTHON_ENV_PATH`, then `idf_tools.py export --format key-value` to dump the rest of the env, then call `idf.py` via the venv's `python.exe`. Run builds in the background (90 s+) and the hook in `.claude/hooks/post-write-build-check.ps1` can stay an indicative timeout.
- **Rule**: Claude *can* drive build/flash directly now. The previous "ask the user to run idf.py manually" workaround is the fallback when the §5.7 recipe fails (e.g. fresh IDF install without constraints copy). (from ToDo: 2026-05-11 UI 흰색 → 검정, 2026-05-11 Accel/Gyro 진단, 2026-05-11 마이크/AHT30 진단, 2026-05-11 Beszel monitor flash)

### 5.3 BOX-3 USB-C data port + USB-Serial JTAG console required for flash

- **Problem**: If `idf.py flash` cannot find the chip, the most common cause is the cable or console assignment.
- **Cause**: BOX-3 console is on the USB-C built-in JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`), not UART0. A power-only USB-C cable will not enumerate.
- **Fix**: Use a data-capable USB-C cable; keep `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` in `sdkconfig.defaults`.
- **Rule**: Document this in `sdkconfig.defaults` comments and check it first when flash fails. (from CLAUDE.md "Hardware-specific sdkconfig"; reinforced by 2026-05-11 monitor session at COM13)

### 5.4 Windows Zadig: assign one interface CDC, the other WinUSB v6

- **Problem**: On a fresh Windows host, the ESP32-S3 USB-Serial-JTAG enumerates as two interfaces but only one of `idf.py monitor` (serial) and `idf.py openocd`/JTAG flash works at a time — or neither, depending on which driver Windows auto-binds.
- **Cause**: The S3 exposes a composite USB device — interface 0 is CDC ACM (virtual COM port for `idf.py monitor`) and interface 1 is the JTAG endpoint (libusb-style, needs WinUSB). Windows does not always assign the right driver to each interface, and forcing WinUSB on the CDC half kills the COM port (or vice versa).
- **Fix**: Run [Zadig](https://zadig.akeo.ie/) → `Options → List All Devices` → for the **CDC interface** keep/select the USB Serial (CDC) driver, for the **JTAG interface** install **WinUSB v6.x**. Replace, never globally swap.
- **Rule**: After plugging a new BOX-3 (or a new Windows host), verify in Device Manager that the CDC half shows a COM port and the JTAG half shows `WinUSB` — do not run Zadig "Replace Driver" against the wrong interface. (from ToDo: 2026-05-11 ToDo/LP repo-local 전환 + 환경 LP 추가)

### 5.7 In-shell `idf.py` recipe: bypass `Initialize-Idf.ps1`, drive env via `idf_tools.py export`

- **Problem**: `Initialize-Idf.ps1` fails on this host (`idf-env config get` returns empty because `C:\Espressif\esp_idf.json` has `idfInstalled: {}` and the VS Code extension manages installs separately). `export.ps1` then falls back to a hard-coded `C:\Espressif\python_env\idf6.0_py3.12_env\` path that does not exist. Both errors leave `idf.py` undefined. Result: every prior session believed the build was impossible (see old §5.2).
- **Cause**: This box has a hybrid setup — IDF tree at `C:\esp\.espressif\v6.0.1\esp-idf`, tools at `C:\Espressif\tools\`, Python venv at `C:\Espressif\tools\python\v6.0.1\venv\`. The Espressif installer-managed initialisation scripts assume a single canonical layout and break when it's split this way.
- **Fix**: Bypass the init scripts. From PowerShell (works inside the Claude shell — tested with `run_in_background=true` for the actual build):
   ```powershell
   $env:IDF_PATH         = "C:\esp\.espressif\v6.0.1\esp-idf"
   $env:IDF_TOOLS_PATH   = "C:\Espressif"
   $env:IDF_PYTHON_ENV_PATH = "C:\Espressif\tools\python\v6.0.1\venv"
   $py = "C:\Espressif\tools\python\v6.0.1\venv\Scripts\python.exe"
   # idf_tools.py export dumps PATH + ESP_IDF_VERSION + a dozen other vars
   foreach ($line in (& $py "$env:IDF_PATH\tools\idf_tools.py" export --format key-value)) {
       if ($line -match '^([A-Za-z_]\w*)=(.*)$') {
           $v = $Matches[2] -replace '%PATH%', $env:PATH
           Set-Item "env:$($Matches[1])" $v
       }
   }
   & $py "$env:IDF_PATH\tools\idf.py" build
   ```
   One-time prerequisite (the installer left a stray file): `Copy-Item C:\Espressif\tools\espidf.constraints.v6.0.txt C:\Espressif\espidf.constraints.v6.0.txt` — `idf.py` looks for the constraints at `$IDF_TOOLS_PATH/` but this install left them in `$IDF_TOOLS_PATH/tools/`.
- **Rule**: For ESP-IDF on this host, never source `Initialize-Idf.ps1` or `export.ps1` from a Claude shell. Use the env-dump recipe above. The full build is ~1 minute on warm cache, ~3 minutes from clean — run with `run_in_background=true` so the cache stays warm during the wait. (from ToDo: 2026-05-11 Beszel monitor flash)

### 5.8 Stale `idf_monitor.py` processes hold the COM port → flash fails with `PermissionError(13)`

- **Problem**: `idf.py -p COM13 flash` failed with `Could not open COM13, the port is busy or doesn't exist. (PermissionError(13))`, but no visible monitor terminal was open in VS Code.
- **Cause**: VS Code's `Ctrl+E D` (Build + Flash + Monitor) spawns `idf_monitor.py` as a child Python process. Closing the monitor terminal with its X button — instead of `Ctrl+]` — leaves the Python child orphaned. After several iterations, multiple stale monitors accumulate. On this box we found four: two against the IDF venv Python, two against Anaconda Python. Any one of them is enough to hold `COM13` exclusively.
- **Fix**:
   ```powershell
   # Diagnose:
   Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'idf_monitor|esp_idf_monitor' } |
       Select-Object ProcessId, Name
   # Kill:
   <pids> | ForEach-Object { Stop-Process -Id $_ -Force }
   ```
   The Esp32-S3 USB-Serial-JTAG composite device exposes only one CDC interface, so a single live monitor is a hard lock on flash. There's no way to "share" the port.
- **Rule**: Always exit ESP-IDF monitor with `Ctrl+]`, never with the terminal's X. If `flash` fails with `PermissionError(13)` and you "know" the monitor is closed, grep `Win32_Process` for `idf_monitor` before doing anything else. (from ToDo: 2026-05-11 Beszel monitor flash)

### 5.6 `gh` CLI missing from PATH; bring-up via portable zip into `%LOCALAPPDATA%\Programs\gh`

- **Problem**: CLAUDE.md §4 mandates `gh issue create` per task, but `gh: command not found` from every shell. No `winget`, `scoop`, or `choco` available — fresh Windows install with package managers absent.
- **Cause**: The Windows 11 Education image on this host did not ship with App Installer / winget. ESP-IDF tooling does not pull `gh` transitively. The Espressif Python venv path also has no `gh` shim.
- **Fix**: Download the latest GitHub CLI Windows zip directly via the API (`https://api.github.com/repos/cli/cli/releases/latest`), extract to `%LOCALAPPDATA%\Programs\gh`, and prepend `\bin` to **User-scoped** PATH using `[Environment]::SetEnvironmentVariable("PATH", ..., "User")`. No admin rights needed. `gh` was already authenticated via keyring from a previous login (scope `repo`), so `gh issue create --repo …` worked immediately.
- **Rule**: On a Windows host where `gh` is missing **and** no package manager is available, use the portable zip release flow above — single PowerShell command, persistent across sessions, fully user-scoped. Avoids touching system PATH or asking for admin. (from ToDo: 2026-05-11 Beszel monitor)

### 5.5 Use the ESP-IDF VS Code extension instead of a separate IDF shell

- **Problem**: `idf.py` is not on the Claude session's `PATH` (LP §5.2) and opening a separate IDF PowerShell window for every build/flash/monitor is friction-heavy.
- **Cause**: The export script bound to `C:\Espressif\tools\python\v6.0.1\venv\` is what `idf.py` depends on; only the Espressif IDF VS Code extension (`espressif.esp-idf-extension`) integrates that setup transparently.
- **Fix**: Install the **ESP-IDF VS Code extension** (already configured in [.vscode/settings.json](.vscode/settings.json) with `idf.currentSetup`). Drive build/flash/monitor with its chord shortcuts:
  - `Ctrl+E B` — Build
  - `Ctrl+E F` — Flash
  - `Ctrl+E M` — Monitor (Ctrl+] to exit)
  - `Ctrl+E D` — Build + Flash + Monitor in one shot
  - `Ctrl+E P` — Select serial port (also persisted as `idf.portWin` in settings.json)
  - `Ctrl+E T` — Set target
  - `Ctrl+E G` — GUI menuconfig
  - `Ctrl+Shift+P` → `ESP-IDF: …` — full command palette
- **Rule**: Prefer the VS Code extension chord shortcuts over a separate `idf.py` shell for routine build/flash/monitor. Drop to a terminal only for commands the extension does not surface (e.g. `idf.py fullclean`). (from ToDo: 2026-05-11 ToDo/LP repo-local 전환 + 환경 LP 추가)

### 5.10 VS Code ESP-IDF extension caches `openocd.usbAdapterSerial` in workspaceStorage SQLite — stale value blocks OpenOCD when boards swap

- **Problem**: VS Code-launched OpenOCD (`Ctrl+E O` and JTAG flash) failed every time with `Info : No device matches the serial string` followed by `Error: esp_usb_jtag: could not find or open device!`. The same `openocd -f board/esp32s3-builtin.cfg` invoked from a plain PowerShell session worked fine against the currently connected ESP32-S3-BOX-3. Windows showed MI_02 bound to `WinUSB` and the device "OK", so driver binding and Zadig were not at fault. The error message reads like a generic "device not found", which throws diagnosis off the actual cause.
- **Cause**: The ESP-IDF VS Code extension (`espressif.esp-idf-extension` v2.1.0) persists the last-seen board's USB iSerial in the workspace SQLite memento — `%APPDATA%\Code\User\workspaceStorage\<workspaceHash>\state.vscdb`, table `ItemTable`, key `espressif.esp-idf-extension`, JSON field `openocd.usbAdapterSerial`. The cached value (`90:E5:B1:D6:50:D4`) belonged to a different BOX-3 previously plugged into this workspace. When a different board is connected (`90:E5:B1:D6:5A:48`), the extension still passes the old serial as `adapter serial …` to OpenOCD; the "No device matches the serial string" message is then literally accurate. The setting is not exposed in `settings.json` or the Settings UI, so `Select-String` / config inspection cannot find it.
- **Fix**: Close VS Code, back up `state.vscdb`, then patch the JSON value to drop the cached key. The ESP32-S3 USB-JTAG iSerial is the chip MAC, so each board is distinct — locate the workspaceHash by matching the `workspace.json`'s `folder` URI.
   ```powershell
   Get-Process -Name "Code" -ErrorAction SilentlyContinue | Stop-Process
   $db = "$env:APPDATA\Code\User\workspaceStorage\<workspaceHash>\state.vscdb"
   Copy-Item $db "$db.bak-$(Get-Date -Format yyyyMMdd-HHmmss)"
   python -c @"
   import sqlite3, json
   c = sqlite3.connect(r'$db').cursor()
   v = c.execute(\"SELECT value FROM ItemTable WHERE key='espressif.esp-idf-extension'\").fetchone()[0]
   if isinstance(v, bytes): v = v.decode('utf-8')
   j = json.loads(v); j.pop('openocd.usbAdapterSerial', None)
   c.execute(\"UPDATE ItemTable SET value=? WHERE key='espressif.esp-idf-extension'\", (json.dumps(j),))
   c.connection.commit()
   "@
   ```
   After removal, the extension launches OpenOCD without a serial filter on next use and auto-detects the connected board. Future board swaps do not regress as long as the user has not re-triggered the extension's serial-pick UI.
- **Rule**: If `openocd` works from a plain shell but fails from the ESP-IDF VS Code extension with `No device matches the serial string`, suspect the workspaceStorage memento — the JTAG serial filter lives in `state.vscdb`, not `settings.json`. After replacing a physical board (or switching between multiple boards on the same workspace), clear `openocd.usbAdapterSerial` via the SQLite patch above. Treat the OpenOCD message as "extension's cached filter mismatched the present board's iSerial", not as a generic enumeration failure. (from ToDo: 2026-05-20 VSCode ESP-IDF 확장의 stale OpenOCD 시리얼 캐시 정리)

### 5.9 Windows Firewall silently blocks LAN connections to Python `http.server` from ESP

- **Problem**: ESP HTTP polling failed with `server unreachable` even though `curl http://<PC-IP>:<port>/...` from the same network returned the file fine. The Python `http.server` process was alive and listening on `0.0.0.0`.
- **Cause**: Windows Defender Firewall blocks inbound connections to user-launched listeners by default — but only for hosts on a *different* IP than the loopback / local interface. `curl` from the **same PC** that runs the server hits the loopback path and bypasses the firewall, so local `curl http://<own-LAN-IP>:port/...` falsely "proves" the server works. The ESP, coming from a different LAN IP, hits the firewall and the SYN is dropped silently — ESP sees a connect timeout / refused.
- **Fix**: Allow inbound traffic on the chosen TCP port (e.g. 8765) for the Private profile:
   ```powershell
   New-NetFirewallRule -DisplayName "ClaudeUsage CSV server" -Direction Inbound `
       -Protocol TCP -LocalPort 8765 -Action Allow -Profile Private
   ```
   Or accept the Windows Firewall popup if it appears when Python first binds the port. Verify from a **second** machine: `curl http://<PC-IP>:8765/ClaudeUsage.csv` must work — not from the host running the server.
- **Rule**: When testing PC-side HTTP services for ESP consumption, never trust `curl` from the same PC running the server — it tests the loopback path only. Always test from a separate host (or the ESP itself) before debugging the ESP HTTP client. If "server works locally but ESP fails", suspect the firewall first. (from ToDo: 2026-05-12 Claude 사용량 탭 추가)

### 5.11 `pythonw.exe` sets `sys.stdout`/`sys.stderr` to `None` — any `stdout.write()` crashes mid-request

- **Problem**: `claude_usage_server.py` registered as a Task Scheduler job via `pythonw.exe` accepted TCP connections on port 8765 but every HTTP request closed before sending headers (`curl: (52) Empty reply from server`). The exact same script under `python.exe` returned the CSV correctly.
- **Cause**: `pythonw.exe` runs detached from a console, so `sys.stdout` and `sys.stderr` are both `None`. `BaseHTTPRequestHandler.send_response()` internally calls `log_request()` → `log_message()`. The script's `log_message` override wrote to `sys.stdout.write(...)`, which raised `AttributeError: 'NoneType' object has no attribute 'write'`. The exception propagated up, the request thread died, and the kernel-buffered response was discarded before any bytes hit the wire.
- **Fix**: At the top of any Python script that may be launched via `pythonw.exe` (Task Scheduler, Startup folder, NSSM service), guard against `None` streams and redirect to a log file before any handler runs:
   ```python
   if sys.stdout is None or sys.stderr is None:
       _fp = open(LOG_PATH, "a", buffering=1, encoding="utf-8")
       sys.stdout = _fp
       sys.stderr = _fp
   ```
   This also gives you a real log to diagnose future failures of a "silent" pythonw-launched service.
- **Rule**: Any script invoked by `pythonw.exe` must either avoid `print` / `sys.stdout.write` entirely or redirect `sys.stdout`/`sys.stderr` to a file at startup. Test the script under `pythonw.exe` from a second host before declaring it deployable — a foreground `python.exe` smoke test does not exercise the failure mode. (from ToDo: 2026-05-21 claude_usage_server.py 부팅 시 자동 실행)

---

## §99. Uncategorized

(empty — temporary holding spot for findings that do not fit §1-§5)
