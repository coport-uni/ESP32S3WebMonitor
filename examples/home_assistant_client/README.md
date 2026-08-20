# home_assistant_client — Home Assistant control panel (ESP32-S3-BOX-3)

Toggle your lights and switches from the 320×240 LCD, and watch your sensors,
without opening Home Assistant's app.

**No entity IDs live in this firmware.** It knows one thing: a Home Assistant
**label**. Every poll asks HA "give me everything tagged `box3`", so labelling
one more entity in HA makes it appear on screen within one poll — no rebuild,
no reflash. Unlabel it and it disappears.

One-time host setup (VSCode, the ESP-IDF extension, Zadig USB drivers) is in
the [repository README](../../README.md); this file covers only what is
specific to this firmware.

## Screen layout

One tab per domain, built from whatever came back labelled. A domain with
nothing labelled gets no tab at all.

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

## Hardware required

- **ESP32-S3-BOX-3** main board (any revision — both GT911 and TT21100 touch controllers are supported by the BSP)
- A **USB-C cable that carries data** (a charge-only cable is the #1 cause of "device not detected")
- A reachable **Home Assistant** server on the same LAN (plain HTTP; HTTPS would need a cert bundle and is not configured)

No extension board or extra wiring is needed — this firmware uses only the LCD,
the touch panel, and the two side buttons.

## 1. Create a long-lived access token

**The HA REST API does not accept your username and password** — those only log
into the web UI. It takes a long-lived token instead.

1. Log into Home Assistant in a browser.
2. Click your user name, bottom-left.
3. Open the **Security** tab and scroll to **Long-lived access tokens**.
4. **Create token**, name it `box3`, and copy the string. **It is shown once and cannot be retrieved afterwards.**

The default lifespan is 10 years, so the firmware carries no token-refresh logic.

## 2. Label the entities you want on screen

The firmware shows exactly the entities carrying one label. In Home Assistant:

1. **Settings → Areas & labels → Labels → Create label**, named `box3`.
2. For each entity you want: open it → **Settings** (gear) → **Labels** → add `box3`.

Label whatever you like from the `light`, `switch` and `sensor` domains.
Anything else is skipped with a log line.

> **Why a label instead of "just show every switch"?** Because Home Assistant gives you no way to tell a real device from an integration's own config entities. `switch.tapo_p1_led` and `switch.tapo_p1_auto_off_enabled` sit in the same domain and the same area as `switch.tapo_p1`, with no `device_class` and no `entity_category` visible to the template API — all five discriminators return identical values (measured; see [`claude_test/README.md`](../../claude_test/README.md)). On the test server a domain sweep put 18 switches on screen of which 3 were real plugs. The label is the only thing that separates them.

## 3. Fill in Kconfig

```powershell
cd examples/home_assistant_client
idf.py menuconfig          # or Ctrl+E G in VSCode
```

Navigate to `(Top) → Home Assistant client`:

| Option | Example value |
|---|---|
| WiFi SSID | `home-2.4g` |
| WiFi password | `…` |
| Home Assistant base URL | `http://192.168.1.232:8123` |
| Long-lived access token | `eyJhbGciOi…` (from step 1) |
| Home Assistant label to show | `box3` (default) |
| Poll interval (seconds) | `10` (default) |
| Max labelled entities | `16` (default) |

Save and close. Values land in this folder's **local `sdkconfig`**, which is
git-ignored — the token and WiFi password never get committed.
`sdkconfig.defaults` only carries non-secret hardware defaults (PSRAM, flash
size, etc.) and stays tracked. Each example keeps its own `sdkconfig`, so
credentials must be set per example folder.

## 4. Check HA is reachable before you flash

Confirm the token and the label from your PC first — debugging this over a
serial log is far slower. From PowerShell:

```powershell
$body = '{"template":"{{ label_entities(''box3'') | join(''\n'') }}"}'
curl.exe -s -X POST "http://192.168.1.232:8123/api/template" `
  -H "Authorization: Bearer <TOKEN>" -H "Content-Type: application/json" `
  -d $body
```

It should list your labelled entity IDs. `401` means the token is wrong; an
empty result means the label is empty or misspelled.

## 5. Build, Flash, Monitor

```powershell
cd examples/home_assistant_client
idf.py set-target esp32s3        # once, only if build/ is missing
idf.py build
idf.py -p COM<N> flash monitor   # Ctrl+] to exit
```

In VSCode the same actions are chord shortcuts — `Ctrl+E T` (set target),
`Ctrl+E P` (port), `Ctrl+E G` (menuconfig), `Ctrl+E D` (build + flash +
monitor). See the [repository README](../../README.md).

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

The LCD then shows one tab per domain that had a labelled entity. Touching a
switch adds:

```
I (xxx) ha_client: /api/services/switch/turn_on ok
I (xxx) ha_client: parsed 9 entities: 0 lights, 6 switches, 3 sensors
```

`parsed 0 entities` means the label is empty or misspelled — re-run the curl
check in step 4.

## Layout

```
examples/home_assistant_client/
├── CMakeLists.txt
├── sdkconfig.defaults        # BOX-3: 16MB flash, Octal PSRAM 80M, JTAG console
└── main/
    ├── CMakeLists.txt        # SRCS + REQUIRES (esp_wifi, esp_http_client, …)
    ├── idf_component.yml     # espressif/esp-box-3 + espressif/cjson
    ├── Kconfig.projbuild     # menuconfig: Home Assistant client
    ├── main.c                # app_main: bsp → ui_create → buttons → network → ha_client
    ├── network.c/.h          # non-blocking WiFi STA + auto-reconnect
    ├── buttons_check.c/.h    # CONFIG / MUTE physical buttons → ui_select_*_tab
    ├── ha_client.c/.h        # HA REST client: template poll, TSV parse, command queue
    └── ui.c/.h               # domain tabview, lv_switch rows, sensor rows, tab cycling
```

`app_main` initialization order is non-negotiable (see
[CLAUDE.md](../../CLAUDE.md) "Initialization order"): I²C → display →
backlight → UI under display lock → buttons → network → HA client. Touching
this order risks LVGL panics or WiFi/HTTP failure modes that look like network
bugs.

## How the client works

- **Discovery and polling are one request.** Every `CONFIG_HA_CLIENT_POLL_INTERVAL_S`, [`main/ha_client.c`](main/ha_client.c) renders one Jinja template server-side via `POST /api/template` and gets back ~500 bytes of tab-separated text. `GET /api/states` would drag back every entity in the installation — hundreds of KB — and truncate against the 8 KB buffer.
- **Touch never does HTTP.** An LVGL callback that blocks 1–3 s on a request trips the watchdog. The callback resolves the entity_id and posts it to a queue; the worker's `xQueueReceive(q, &cmd, period)` doubles as the poll timer — a command short-cuts the wait, a timeout means refresh.
- **The command carries an entity_id, not an index.** By the time the worker drains the queue it may have re-polled and reshuffled the cache, and a stale index would switch the wrong device.
- **`turn_on` / `turn_off`, never `toggle`.** A toggle issued against a stale screen lands on the opposite of what was asked.
- **Lock order is one-way.** `on_ui_action` runs with the LVGL lock held and takes the cache mutex; the worker must therefore never hold the cache mutex while calling into `ui_*`. `publish_to_ui()` copies under the lock, releases, *then* draws.

Tab cycling is owned by [`main/ui.c`](main/ui.c): `ui_select_prev_tab` /
`ui_select_next_tab` walk `lv_tabview_get_tab_count()`, so the buttons work
regardless of which domains ended up with tabs.

## Home Assistant pitfalls

Documented in detail with file/line references in
[LearnedPatterns.md](../../LearnedPatterns.md):

- **The HA REST API rejects your username and password** — they only log into the web UI. It needs a long-lived access token; see step 1. A `401` with credentials that "definitely work" is this, every time.
- **`GET /api/states` does not fit on the device** — it returns *every* entity, hundreds of KB on a real install, and truncates against the response buffer. Render a filtered Jinja template with `POST /api/template` instead and let HA do the work; the payload drops to ~500 bytes. This is LP §2.3's rule (bound the payload at the source, not the device buffer) in a second guise.
- **HA cannot tell a real device from an integration's config entities** — `switch.plug_led` and `switch.plug` share a domain and an area, both have `device_class: NONE`, and `entity_category` is not exposed to templates. There is no attribute to filter on; that is why this firmware uses a label. Measured on a live server — see [`claude_test/README.md`](../../claude_test/README.md).
- **LVGL's default font has no Hangul glyphs** — only `lv_font_montserrat_14` is enabled. This firmware works around it by falling back to the entity_id's object part when a friendly name is not ASCII (`pick_display_name()` in [`main/ha_client.c`](main/ha_client.c)); HA always slugifies entity IDs to ASCII. Enabling `LV_FONT_SOURCE_HAN_SANS_SC_*_CJK` adds ~200 KB of flash and even then SC may not cover Hangul — keep the fallback on the ESP.

Toolchain and Windows-level pitfalls shared by every example in this repo are
listed in the [repository README](../../README.md).

## Extending it

**Adding a device needs no code at all** — label it `box3` in Home Assistant
and it shows up on the next poll. That is the whole point of the label design.

Code changes are only for new *kinds* of thing:

1. Read [CLAUDE.md](../../CLAUDE.md) §2 (style) and §7 (research-before-coding).
2. **Prove it against the real server before writing firmware.** Copy [`claude_test/probe_ha_template.py`](../../claude_test/probe_ha_template.py) and render your candidate template over REST. This is what caught the original domain-sweep design being unworkable — before the first flash, at the cost of a few lines instead of a debugging session over a serial log.
3. **A new domain** (e.g. `climate`): add it to `domain_from_name()` and `kind_of()` in [`main/ha_client.c`](main/ha_client.c), add a `ui_ha_kind_t` in [`main/ui.h`](main/ui.h), and give it a row builder and a `TABS[]` entry in [`main/ui.c`](main/ui.c).
4. **A new attribute** (e.g. light brightness): add it to the template in `build_template()`, widen `MAX_TSV_FIELDS`, and carry it through `ha_entity_t` → `ui_ha_entity_t`. Every LVGL call from outside the LVGL task **must** be wrapped in `bsp_display_lock` / `bsp_display_unlock` — the `UI_WITH_LOCK` macro handles this.
5. **A new data source unrelated to Home Assistant** gets its own module — its own Kconfig menu, poll task, and `ui_*` API — or its own standalone project under [`examples/`](../). Do not bolt unrelated data into `ha_client.c`.
6. Append a dated section to [ToDo.md](../../ToDo.md), then check items off as you go.
7. When the work is done, distill any new gotcha into [LearnedPatterns.md](../../LearnedPatterns.md).
