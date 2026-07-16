## 2026-05-11 | ESP32-S3-BOX-3 + SENSOR 액세서리 전체 기능 점검 코드

작업 경로: `container/Espress_dev/`. SENSOR 액세서리(레이더·온습도·IR)까지 포함한 LVGL 대시보드 펌웨어 작성.

### 사전 조사 결과 (Research Before Coding)

- 레이더 칩셋: **AT581X** (Airoha, 5.8 GHz). HLK-LD2410 아님. Espressif 공식 factory_demo와 일치.
- 레이더 인터페이스: dock I2C 버스 (GPIO 40 SCL / 41 SDA), I2C addr 0x28, INT 핀 **GPIO 21**.
- 온습도: **AHT30**. BSP `bsp_sensor_init(HUMITURE_ID, ...)`로 직접 지원 (managed_components에 이미 포함).
- IR 핀: BOX-3-SENSOR 공식 schematic 미공개. 잠정값 TX=GPIO 39 (PMOD1_IO3), RX=GPIO 38 (PMOD1_IO7) — `#define`으로 노출시켜 사용자 검증 후 조정 가능하게 한다.
- 추가 의존성: `espressif/at581x: ^0.1.0` 만 추가 (IR은 ESP-IDF 내장 RMT 드라이버로 처리).

### 작업 항목

- [ ] `main/idf_component.yml`에 `espressif/at581x` 의존성 추가
- [ ] LVGL UI를 tabview 구조로 재구성 (`main/ui.c/h`): IMU / 환경 / 레이더 / 오디오 / IR / 버튼 6개 탭
- [ ] IMU 탭: 기존 가속도·자이로·tilt 표시 유지
- [ ] 환경 탭(AHT30): `bsp_sensor_init(HUMITURE_ID)` 후 1초 주기 온도·습도 표시
- [ ] 레이더 탭(AT581X): dock I2C 버스에 디바이스 생성, INT GPIO 21 ISR 콜백에서 LVGL 락 잡고 상태 라벨 갱신, 마지막 감지 timestamp 표시
- [ ] 오디오 탭: ES7210 마이크에서 일정 샘플 받아 RMS 계산 → 바 표시, "Beep" 버튼 누르면 ES8311 스피커로 1 kHz 짧은 사인 톤 출력
- [ ] IR 탭: RX RMT 채널 + TX RMT 채널 초기화, 수신 시 펄스 개수 표시, "Send NEC" 버튼으로 임의 NEC 코드 송신
- [ ] 버튼 탭: `bsp_iot_button_create()`로 CONFIG/MUTE/MAIN 3개 버튼 핸들 받고 short/long press 카운터 표시
- [ ] 탭 전환은 화면 좌우 스와이프 + 상단 헤더 탭 클릭 둘 다 지원
- [ ] 초기화 순서 검증: I2C → display → backlight → 모든 센서/오디오 → LVGL UI(락) → 태스크 → ISR
- [ ] `idf.py build` 통과 (warning 0, [.claude/hooks/post-write-build-check.ps1](.claude/hooks/post-write-build-check.ps1) 사용)
- [ ] 실기기 flash 후 6개 탭 모두 동작 확인 (LP §3 기록용)
- [ ] LearnedPatterns.md에 BOX-3-SENSOR 핀 매핑 / AT581X 사용법 추가

### 불확실 항목 (사용자 결정 필요)

- IR 핀: BOX-3-SENSOR 모듈의 실제 schematic 확보 가능한지? 불가하면 GPIO 38/39 잠정값으로 시작하고 실기기 테스트로 보정
- AT581X INT의 active 레벨: 데이터시트 기준 active-high 가정 (코드에서 `interrupt_level=1`). 동작 안 하면 0으로 토글

## 2026-05-11 | UI 흰색 글씨 → 검정으로 변경 (Espress_dev)

- [x] `main/ui.c`의 `make_value_label()`에서 `lv_color_white()` → `lv_color_black()` 변경 (main/ui.c:55)
- [x] 색상 라벨 (COLOR_ACCENT/WARN/OK/MUTED/PINK)은 유지
- [x] 배경(COLOR_BG)도 유지 (사용자 결정)
- [ ] `idf.py build` 통과 확인 — 현재 셸에 idf.py 미등록, 사용자 IDF 환경에서 수동 빌드 필요

## 2026-05-11 | Accel/Gyro "f" 표시 + AHT30 미동작 진단 (Espress_dev)

증상: IMU 탭의 Accel/Gyro 값이 "X: f", "Y: f"처럼 부동소수 자리에 'f'만 출력. Env 탭의 AHT30(온/습도)는 갱신 자체가 없음. Dock은 연결돼 있음.

- [x] 원인 분석: LVGL 내장 sprintf가 `LV_USE_FLOAT=n`일 때 `%f` case 자체가 컴파일에서 빠짐 (managed_components/lvgl__lvgl/src/stdlib/builtin/lv_sprintf_builtin.c:42,776-794) → 플래그/정밀도만 소비되고 'f'가 literal로 출력됨
- [x] `sdkconfig.defaults`에 `CONFIG_LV_USE_FLOAT=y` 추가 + 기존 `sdkconfig`의 `# CONFIG_LV_USE_FLOAT is not set` 라인을 `CONFIG_LV_USE_FLOAT=y`로 교체 (sdkconfig 우선순위 때문)
- [ ] `idf.py build` (사용자 IDF 환경에서 수동) — idf.py가 호스트 PATH에 없어 클로드가 직접 못 돌림
- [x] flash & monitor로 Accel/Gyro가 `+0.12` 형식으로 정상 출력되는지 확인 (사용자: 자이로 정상)
- [x] AHT30 진단: 같은 모니터 로그에서 `sensors` TAG로 다음 패턴 확인 — `bsp_sensor_init(HUMITURE): ...`, `iot_sensor_start: ...`, `i2c_dock` 관련 에러. AHT30은 BOX-3 dock의 별도 I2C 버스(GPIO 40/41) 상에 있어 코드 자체는 맞음 (managed_components/espressif__esp-box-3/esp-box-3.c:884-893)
- [x] 사용자 로그 공유 (부트 ~1558ms까지)

## 2026-05-11 | 마이크/AHT30 추가 진단 + 마이크 게인 보정 (Espress_dev)

진단 정황: 부트 로그에서 IMU/AHT30 sensor_hub 생성+핸들러 등록 성공(`SENSOR_HUB`/`SENSOR_LOOP` 라인), ES7210 마이크 `Enable MIC1/MIC2`+`Unmuted`+`Adev_Codec: Open codec device OK`. PMOD1에는 공식 ESP32-S3-BOX-3-SENSOR 확장보드가 연결됨. 부트 로그가 t=1558ms에서 끊겨 첫 AHT30 polling 사이클 진입 전.

- [x] audio_check.c: `esp_codec_dev_set_in_gain(s_mic, 30.0f)` → `42.0f`로 올리고 `esp_codec_dev_open` **이전**으로 이동 (BSP API.md 예시와 동일 순서)
- [ ] `idf.py build && flash && monitor` (사용자 IDF 환경)
- [ ] **MUTE 슬라이드 스위치 확인** — BOX-3 본체 상단의 mic mute 슬라이드가 mute 위치면 ES7210 unmuted여도 logic gate가 GPIO_1을 통해 입력 차단. 우선 사용자 육안 확인 필요
- [ ] flash 이후 **최소 5초간** monitor 로그 캡처. `AHT30:` 태그의 `Failed to start AHT30 measurement` 또는 `timeout`이 찍히는지 확인 (sensor_hub polling 1초 주기 × 3-4회)
- [ ] 로그에 AHT30 에러 없는데 콜백 미진입이면 → `iot_sensor_handler_register_with_type` → `iot_sensor_handler_register` (handle 기반) 으로 전환, 공식 display_sensors 예제 패턴과 정렬

## 2026-05-11 | ToDo/LP repo-local 전환 + 환경 LP 추가 (Espress_dev)

- [x] CLAUDE.md §4 task management의 ToDo.md 경로를 `workspace root` → `project repo root (Espress_dev/ToDo.md)`로 변경
- [x] CLAUDE.md §9, §10의 LearnedPatterns.md 경로도 repo root로 동기화
- [x] LP §5.4 Zadig 드라이버 할당 (CDC + WinUSB v6) 항목 추가
- [x] LP §5.5 ESP-IDF VS Code 확장 + Ctrl+E 코드 단축키 항목 추가

## 2026-05-11 | AHT30 silent event drop 수정 (sensor_hub base 불일치)

가설/근거: `iot_sensor_handler_register_with_type(HUMITURE_ID, ...)`은 고정 매크로 base `SENSOR_HUMITURE_EVENTS`에 등록 (iot_sensor_hub.c:658), polling task는 동적 base `sensor->event_base` (sprintf "%s_%x" → "sensor_hub_aht30_38", iot_sensor_hub.c:251,359)로 post. esp_event base 불일치 → 콜백 0회 호출. 부트 로그는 `register succeed × 2`까지 정상 표시되어 silent drop. LV_USE_FLOAT과 같은 카테고리("라이브러리 silent data drop")지만 메커니즘은 API 변형 선택 문제.

- [x] `main/sensors.c:109-114`: `_with_type` × 2 호출과 `inst_t/inst_h` 로컬을 `iot_sensor_handler_register(s_humiture, humiture_event_cb, NULL)` 1줄로 교체
- [x] flash + monitor: 사용자 확인 — Temp/Hum 정상 갱신 ✅
- [x] LP §3.4에 `sensor_hub _with_type vs handle-based register 차이` 항목 영구 등록

## 2026-05-11 | 마이크 RMS 0 + beep 항상 실패 수정 (esp_codec_dev 반환 규약)

원인: `esp_codec_dev_read/write`는 POSIX read/write와 달리 **성공 시 0 (`ESP_CODEC_DEV_OK`)** 반환, 바이트 수 반환 아님 (audio_codec_data_i2s.c:717). 사용자 코드 `if (got > 0)` (audio_check.c:58)는 성공을 절대 인식 못 함 → mic_task가 RMS 업데이트 분기로 진입 못 함 → UI bar 0 고정. beep도 `if (written <= 0)`로 성공을 실패로 오인 (audio_check.c:94) → 상태 라벨 항상 "beep write fail". LV_USE_FLOAT, sensor_hub base 불일치에 이은 세 번째 silent data drop 패턴.

- [x] audio_check.c mic_task: `if (got > 0)` → `if (err == ESP_CODEC_DEV_OK)`로 교체, `frames = got/sizeof(int16_t)` → `MIC_CHUNK_FRAMES` 상수 사용
- [x] audio_check.c beep_task: `if (written <= 0)` → `if (err != ESP_CODEC_DEV_OK)`로 교체
- [x] LP §3.5에 `esp_codec_dev_read/write 반환 규약 (POSIX 아님)` 항목 등록
- [x] `idf.py build` 통과 확인 (사용자 IDF 환경 — 워닝 0)
- [x] flash + monitor: Audio 탭에서 마이크에 소리 입력 시 RMS bar가 즉시 반응 — 사용자 확인 ✅
- [x] Beep 버튼 / status 라벨 동작 확인 — 사용자 확인 ✅

## 2026-05-11 | Beszel(http://10.16.21.197:8090) 모니터링 탭 추가

계획 파일: `C:\Users\USER_55_DeepLearning\.claude\plans\beszel-http-10-16-21-197-8090-esp32-resilient-bee.md` (사용자 ExitPlanMode 승인 완료)

UX: 한 호스트씩 CPU/Memory/GPU 바 그래프. CONFIG(=이전) / MUTE(=다음) 버튼으로 호스트 전환. WPA2-Personal, 폴링 5초. GPU 없으면 회색 + "N/A". 자격증명은 menuconfig(`sdkconfig` 로컬, .gitignore 확인) 한정.

관련 LP: §5.2(idf.py PATH 없음 → 빌드/플래시는 사용자 환경), §2.1(sdkconfig 우선순위), §3.6(헤더로 시그니처 검증).

### 작업 항목

- [x] `main/Kconfig.projbuild` 신규: BESZEL_WIFI_SSID/PASSWORD/SERVER_URL/USER/PASSWORD/POLL_INTERVAL_S/MAX_HOSTS
- [x] `.gitignore`에 `sdkconfig` 추가 + `git rm --cached sdkconfig` (사용자 결정: 자격증명 누출 방지)
- [x] `main/network.h/c` 신규: WiFi STA 비차단 초기화 + auto-reconnect (`esp_wifi_connect` on disconnect) + `network_wait_connected`
- [x] `main/beszel.h/c` 신규: PocketBase 인증, /api/collections/systems/records 폴링, cJSON 파싱, 토큰 5h30m 사전 갱신 + 401 재인증 1회 재시도, `s_systems[]` 캐시, `beszel_select_prev/next`
- [x] `main/beszel.c` 첫 응답 1회 raw JSON 로깅(`s_logged_raw_systems` 플래그, 256바이트 청크). GPU 필드는 `g`/`gpu`/`gp` 순서로 root 또는 `info.{...}` 양쪽 탐색 — 첫 부팅 후 raw 로그에서 실제 경로 확정 시 `parse_one_system`의 `gpu_keys[]` 1줄 수정
- [x] `main/ui.h` 확장: `ui_beszel_host_t` + `ui_beszel_set_host/set_status/set_unavailable`. (계획에 있던 `ui_callbacks_t.on_btn_config/on_btn_mute`는 실제 호출 경로가 ui→cb가 아닌 buttons_check→cb라 추가하지 않음 — 죽은 필드 회피)
- [x] `main/ui.c`: 7번째 탭 "Beszel" (호스트명 + 상태닷 + N/M 인덱스 + CPU/MEM/GPU 바 + 푸터). `make_metric_row` 헬퍼로 3개 바 행 공통화. GPU 없을 때 회색 인디케이터 + "N/A"
- [x] `main/buttons_check.h/c`: `buttons_callbacks_t` 추가, `buttons_check_init(const buttons_callbacks_t *)`. on_short에서 카운터 업데이트 후 인덱스별 콜백 분기 — Btn 탭 카운터 표시 유지
- [x] `main/main.c`: `on_config_pressed`=`beszel_select_prev`, `on_mute_pressed`=`beszel_select_next` 콜백 + `buttons_check_init(&btn_cbs)` + `network_init()` + `beszel_init()` 호출
- [x] `main/CMakeLists.txt`: SRCS `network.c`/`beszel.c` 추가, REQUIRES `nvs_flash esp_wifi esp_netif esp_event esp_http_client esp-tls json`
- [x] `idf.py menuconfig`로 WiFi SSID/PW + BESZEL_USER/PW 입력 → `idf.py build && flash && monitor` 완료 (사용자 확인)
- [x] 시리얼 로그: `network: got IP 192.168.1.206`, `beszel: auth OK (token len=224)`, raw systems response 815 bytes 1회 출력 확인. `info.cpu/mp/dp/g` 필드 경로 확정 (idle 0%일 때 `info.g`가 omitempty로 빠지는 v0.18.x 동작 확인)
- [x] Beszel 웹 UI(H200Server/3090Server)와 CPU/MEM 수치 일치 — 사용자 확인 ✅
- [x] CONFIG/MUTE 버튼으로 탭 전환 동작 — 사용자 확인 ✅
- [x] **계획 중간 변경**: 단일 Beszel 탭 + 다른 모듈 탭 → "탭 = 호스트" 구성으로 전면 재설계. sensors.c/h, audio_check.c/h, ir_check.c/h 삭제하고 sensor_example/에 보존된 이전 스냅샷을 README에서 설명. ui.c는 동적 tabview rebuild 패턴으로 재작성
- [x] GPU 표시 결정: `info.g` 누락 시 N/A 대신 0% 표시 (omitempty 모호성 해소) — 사용자 결정
- [x] `LearnedPatterns.md` §3.7–3.10, §5.6에 신규 함정 5건 등록 (json 컴포넌트 v6.x 변경, NAME_MAX picolibc 충돌, uint32_t printf, info.g omitempty, gh CLI 부재시 portable 설치)
- [x] README.md 전면 재작성: Beszel monitor를 메인으로, sensor_example/를 이전 자체 진단 펌웨어 스냅샷으로 설명
- [x] GitHub Issue 생성: https://github.com/coport-uni/Esp32S3-CrawlerDisplay/issues/2

## 2026-05-11 | Claude 셸에서 idf.py 직접 구동 + COM 포트 좀비 monitor 정리

빌드를 Claude 셸에서 직접 돌릴 수 있게 환경 구성 절차를 확정하고, 그 과정에서 발견한 좀비 `idf_monitor.py` → COM 포트 점유 이슈를 LP에 영구 기록한다.

- [x] LP §5.2 갱신: "Claude shell에선 빌드 불가"는 stale → §5.7로 cross-ref
- [x] LP §5.7 신설: `Initialize-Idf.ps1` 우회 + `idf_tools.py export`로 env dump → `idf.py` 직접 구동 레시피 (PowerShell 스니펫 그대로 기록)
- [x] LP §5.7 단점 1회성 픽스 기록: 설치 시 `espidf.constraints.v6.0.txt`이 `C:\Espressif\tools\`에 깔리는데 idf.py는 `C:\Espressif\`에서 찾음 → 1회 복사
- [x] LP §5.8 신설: VS Code monitor 터미널을 `Ctrl+]` 대신 X 버튼으로 닫으면 `idf_monitor.py` python 좀비가 누적되어 COM 포트 점유 → `Win32_Process` 진단법 + `Stop-Process` 정리법
- [x] Beszel monitor 펌웨어 빌드 + COM13 flash 완료 (Claude 셸에서 직접) ✅
- [x] GitHub Issue 생성: https://github.com/coport-uni/Esp32S3-CrawlerDisplay/issues/3 + commit + push

## 2026-05-12 | Beszel 호스트 탭에 DISK 용량 사용률(%) 추가

목적: 각 호스트 탭에 현재 표시 중인 CPU/MEM/GPU 외에 디스크 용량 사용률(%)도 보이도록 한다. 사용자 명령: "지금 UI에서 DISK 사용량도 볼 수 있음 좋겠어 → 의미는 디스크 용량 사용량".

가설/근거: Beszel `Info` 구조체는 `DiskPct float64 \`json:"dp"\``로 직렬화. 이전 작업(2026-05-11 Beszel monitor)의 raw 응답에서 `info.dp` 경로 이미 확정됨. GPU와 달리 `omitempty`가 없어 항상 존재 — 누락 시에만 0% 기본 (방어적). (see LP §3.8)

레이아웃: 현재 호스트 탭은 status 행(y=0), CPU(y=30), MEM(y=60), GPU(y=90). 220px tabview - 30px tab bar = 190px 콘텐츠 영역. DISK를 y=120에 추가하면 바 하단이 y=136이라 여유 충분.

스타일 결정(사용자): DISK 행은 y=120 추가. 추가로 CPU/MEM/GPU/DISK 모든 바가 사용량 임계값에 따라 색이 바뀌어야 함 — 0–69% cyan, 70–89% 노랑(`COLOR_WARN`), 90–100% 핑크(`COLOR_PINK`). GPU N/A(`gpu_present=false`)는 기존처럼 회색 유지.

### 작업 항목

- [x] `main/ui.h`: `ui_beszel_host_t`에 `int disk_pct;` 필드 추가
- [x] `main/beszel.c`: `beszel_system_t`에 `float disk;` 추가, `parse_one_system`에서 `dp`/`disk`/`diskPercent` 키로 파싱(누락 시 0), `publish_all_to_ui`에서 `local_hosts[i].disk_pct` 채우기
- [x] `main/ui.c`: `host_ui_t`에 `bar_disk`/`lbl_disk_val` 추가, `build_host_tab`에서 `build_metric_row(tab, "DISK", 120, ...)` 호출, `apply_host_data`에서 디스크 바/라벨 갱신, 임계값 색상 함수 `bar_color_for_pct(int)` 도입 후 CPU/MEM/GPU/DISK 모두에 적용
- [x] `idf.py build` 워닝 0으로 통과 — bin 0x13afb0, 16% 여유
- [x] COM13 flash 완료 — hash verified, hard reset OK (`.claude/last-flash.log`)
- [x] 화면 시각 확인: 디스크 % 값 Beszel 웹 UI와 일치 + 임계값별 색 전환 — 사용자 확인 ✅
- [x] GitHub Issue 생성: https://github.com/coport-uni/Esp32S3-CrawlerDisplay/issues/4
- [x] 커밋 + push: `2537e06 Add disk-usage row and severity-coloured metric bars`

## 2026-05-12 | Claude 사용량 탭 추가 (CSV → PC HTTP → ESP 폴링)

목적: `C:\Users\USER_55_DeepLearning\Desktop\workspace\ClaudeUsage.csv`의 최신 행을 LVGL 탭으로 표시. CSV가 갱신되면 자동으로 ESP 화면에도 반영. Beszel과는 별도 경로 — 새 `claude_usage` 모듈 + PC 측 자체 Python HTTP 서버.

### 전달 방식

PC 측 `claude_usage_server.py`(워크스페이스 경로의 CSV를 read-only로 서빙) ← ESP 30초 폴링 `GET /ClaudeUsage.csv` → 마지막 행 파싱 → UI 갱신.

### CSV 포맷 (현재)

```
측정시간,현재 세션 사용량,재설정까지 남은 시간,주간한도(모든 모델),주간한도(Sonnet만),주간한도(Claude Design)
2026-05-12 8:34,5%,4시간 46분,31%,2%,10%
```

필수 표시: ① 현재 세션 사용량 ② 재설정까지 남은 시간 ③ 주간한도(모든 모델). Sonnet/Design은 동일 CSV에 있으나 이번 탭에서는 생략.

### UI 결정

- 호스트 탭 뒤에 항상 표시되는 "Claude" 탭 1개 — 호스트 토폴로지 rebuild 시에도 항상 마지막 탭으로 추가.
- 탭 순환은 호스트 + Claude 모두 포함 — 기존 `beszel_select_prev/next`의 host-only modulo 한계 때문에 cycling 로직을 `ui.c`로 이동 (`ui_select_prev_tab`/`ui_select_next_tab`, `lv_tabview_get_tab_count` 기반).
- 한국어 글리프 미내장(`lv_font_montserrat_14`) → CSV의 "4시간 46분"은 ESP에서 파싱 후 "4h 46m" 형태로 표시. 측정시간(ASCII)은 그대로 표시.
- 레이아웃: 상단 "Updated YYYY-MM-DD HH:MM" 회색 라벨 → SESSION 바 행 → WEEK 바 행 → 큰 글씨 "Reset in 4h 46m" 중앙.

### 작업 항목

- [x] `claude_usage_server.py` 신규 (`container/Espress_dev/`) — `ThreadingTCPServer`로 `ClaudeUsage.csv` 1개만 서빙. 기본 포트 8765, `--port`/`--bind`/`--csv` CLI, UTF-8 + no-cache, 404 처리. stdlib only.
- [x] `main/Kconfig.projbuild`: `menu "Claude usage tab"` 추가 — `CLAUDE_USAGE_SERVER_URL` 기본값 `http://192.168.1.16:8765/ClaudeUsage.csv` (사용자 확인 IP), `CLAUDE_USAGE_POLL_INTERVAL_S` 기본 30 (range 5~600).
- [x] `main/claude_usage.h/c` 신규: WiFi 연결 대기 후 30초 주기 폴링. `parse_csv_latest`(마지막 non-empty 행), `parse_pct`(`"5%"` → 5), `parse_kr_time`(UTF-8 마커 0xEC8B9C=시 / 0xEAB084=간 / 0xEBB684=분 직접 검사로 "4시간 46분" → h=4,m=46). 헤더 행 자동 skip (col1이 숫자가 아니면 reject).
- [x] `main/ui.h`: `ui_claude_data_t {timestamp, session_pct, week_all_pct, reset_h, reset_m, valid}` + `ui_claude_set_data`, `ui_claude_set_unavailable`, `ui_select_prev_tab`, `ui_select_next_tab` 선언. `ui_beszel_select_tab` 제거.
- [x] `main/ui.c`: Claude 탭 widget set (`claude_ui_t`, 캐시된 데이터로 rebuild 시 재적용). `build_claude_tab`: timestamp/SESSION 바/WEEK 바/"Reset in Xh YYm" 큰 글씨(centered, accent). `append_claude_tab`로 호스트 탭 뒤에 상시 append.
- [x] `main/ui.c`: `ui_select_prev_tab`/`ui_select_next_tab` — `lv_tabview_get_tab_count` + `lv_tabview_get_tab_active`로 전체 탭 순환.
- [x] `main/ui.c`: `ui_beszel_replace_hosts(..., active_idx)` — `active_idx == -1`이면 활성 탭 유지(폴링이 사용자 선택 덮어쓰지 않음).
- [x] `main/beszel.c`: `s_selected_idx` static + `beszel_select_prev`/`beszel_select_next` 함수 제거. `publish_all_to_ui`에서 active_idx에 -1 전달.
- [x] `main/beszel.h`: `beszel_select_prev/next` 선언 제거.
- [x] `main/main.c`: 버튼 콜백을 `ui_select_prev_tab`/`ui_select_next_tab`로 변경, `claude_usage_init()` 호출 추가.
- [x] `main/CMakeLists.txt`: `claude_usage.c` SRCS 추가.
- [x] `idf.py build` warning 0 통과 — bin 0x13afb0 bytes, 16% 여유. `.claude/hooks/post-write-build-check.ps1` 자동 실행 통과.
- [ ] COM13 flash → 시리얼 로그에서 `claude_usage: session=X%% week=Y%% reset=Hh MMm ts=...` 확인.
- [ ] 실기기 화면: Claude 탭 표시 + CONFIG/MUTE로 [host0 → host1 → Claude → host0] 순환 확인.
- [ ] PC에서 `python claude_usage_server.py` 실행 → curl로 응답 확인 → CSV 수동 수정 후 30초 이내 화면 반영 확인.
- [x] LearnedPatterns.md §5.9 Windows Firewall 함정 기록 (PC↔ESP HTTP).
- [x] GitHub Issue 생성: https://github.com/coport-uni/ESP32S3WebMonitor/issues/5 (repo rename 후 자동 redirect)
- [x] README.md에 실기기 사진 2장(Beszel 호스트 / Claude 탭) 임베드 + Claude 사용량 설정 섹션 + Windows Firewall 안내 추가.
- [x] `gh repo rename`으로 GitHub 레포 이름을 `Esp32S3-CrawlerDisplay` → `ESP32S3WebMonitor`로 변경, 로컬 origin URL도 동기화.
- [x] 커밋 + push: `bb3eb0b Add Claude usage CSV tab + host-side HTTP server`.

## 2026-05-14 | CLAUDE.md에 CommonClaude README 비-Python 잔여 항목 반영

source: https://github.com/coport-uni/CommonClaude (README.md + CLAUDE.md)

현재 프로젝트 `CLAUDE.md`의 §1~§10 CommonClaude Conventions 본문은 이미 C/ESP-IDF에 맞게 적응됨 (MIT → Google C, Ruff → idf.py build). README/CLAUDE.md에서 비-Python인데 누락된 보조 항목들만 추가한다.

사용자 결정:
- 부록 위치: CommonClaude 섹션 뒤에 §11+로 이어붙임
- §4 처리: 소스의 MANDATORY 인용구 + 7단계 워크플로우 그대로 채택

### 작업 항목

- [x] §4 Task Management: 6단계 → 7단계로 재작성, "MANDATORY ... every task without exception" 인용구 + "non-negotiable" reminder 추가 (CLAUDE.md §4)
- [x] §11 `ultrathink` 사용 규칙 신설 (plan mode/복잡 작업 시 명령 끝에 부착) (CLAUDE.md §11)
- [x] §12 Claude Code IDE Commands 표 (`/clear`, `/rewind`, `/memory`, `/permission`, `/review`, `/output-style`) (CLAUDE.md §12)
- [x] §13 Claude Code VS Code Shortcuts 표 (`Shift+Tab`, `Ctrl+Shift+E`, `Ctrl+Shift+X`, `Alt+K`) (CLAUDE.md §13)
- [x] §14 References (소스 README의 책/링크 출처) (CLAUDE.md §14)
- [x] `idf.py build` 영향 없음 확인 — CLAUDE.md만 수정, 빌드 훅 트리거 패턴(`main/**`, `CMakeLists.txt`, `sdkconfig.defaults`, `idf_component.yml`)에 해당 없음
- [x] GitHub Issue 생성: https://github.com/coport-uni/ESP32S3WebMonitor/issues/6
- [x] Issue #1 (BOX-3 self-test bring-up) close — Beszel 피벗으로 사실상 완료, 본문에 4건 fix 정리 완료 상태로 close
- [x] 커밋 + push: `fee1a55 Adopt CommonClaude task-management and IDE reference docs in CLAUDE.md` (Closes #6)

## 2026-05-20 | CommonClaude `feat/c-language-support` 브랜치 반영 (Git 규칙 + MIT C 스타일)

source: https://github.com/coport-uni/CommonClaude/tree/feat/c-language-support (CLAUDE.md size=19591, sha 7337e2e)

사용자 결정:
- 범위: Git 규칙 전체 (§11~§17 of branch) + **`.clang-format` 추가 채택** (사용자 후속 결정: "적용해줘").
- §2 C 스타일 가이드: Google → MIT로 교체. ESP-IDF의 `snake_case_t` typedef 관례는 유지 (브랜치 표에도 `_t` 선택지 명시됨).
- 신규 섹션은 기존 §11~§14 뒤에 §15~§21로 append (renumbering으로 인한 ToDo.md cross-ref 깨짐 방지). Git References는 새 §21로 분리.
- `.clang-format` 빌드 충돌 방지: `managed_components/.clang-format`에 `DisableFormat: true` 가드 파일을 둬서 제3자 코드 자동 정렬 차단.

### 작업 항목

- [ ] `CLAUDE.md §2`: Google C++ Style Guide 기반 본문을 MIT CommLab 스타일로 교체 — 연속행 좌측 연산자, 시각적 정렬, `/* TODO */` 블록, 공개 함수 Doxygen 의무화 등. Naming 표는 ESP-IDF `snake_case_t` 유지
- [ ] `CLAUDE.md §15` Commit Messages 신설 — Conventional Commits 표 + 규칙 + 예시 + Breaking Changes
- [ ] `CLAUDE.md §16` Branching Strategy 신설 — GitHub Flow, `<type>/<short-description>` 네이밍, 표준 워크플로우
- [ ] `CLAUDE.md §17` .gitignore Base Template 신설 — C 빌드 산출물 + 에디터/OS + secrets
- [ ] `CLAUDE.md §18` Versioning 신설 — SemVer, Conventional Commits 매핑, `git tag -a` 태깅
- [ ] `CLAUDE.md §19` Pull Request Guidelines 신설 — Conventional Commits 제목 + Changes/Why/Testing/Related Issues 템플릿 + 400줄 권장
- [ ] `CLAUDE.md §20` Git Automation (Optional) 신설 — pre-commit + `.pre-commit-config.yaml` 예시
- [x] `CLAUDE.md §21` Git Convention References 신설 — Conventional Commits / GitHub Flow / SemVer / pre-commit / clang-format / Pro Git 등 외부 링크
- [x] `.clang-format` (project root) 생성 — LLVM 베이스, 80-col / 4-space / 연속행 좌측 연산자 (브랜치와 동일)
- [~] `managed_components/.clang-format` 생성 — **변경**: pre-write 훅이 차단 + `idf.py reconfigure` 시 wipe 위험. 대신 §6/§20에 "도구 레벨 제외" 방식(manual glob 제한, pre-commit `exclude: ^managed_components/`, VS Code `[c]` scope) 문서화
- [x] `CLAUDE.md §6 Build & Static Checks`에 `.clang-format` 사용법 + `managed_components/` 제외 방침 한 단락 추가
- [x] `CLAUDE.md §2` MIT 스타일로 교체 — 연속행 좌측 연산자, 시각적 정렬, `/* TODO */`, 공개 함수 Doxygen 의무화
- [x] `CLAUDE.md §15~§21` 신설 — Conventional Commits / GitHub Flow / .gitignore / SemVer / PR / pre-commit / Git References
- [x] `idf.py build` 영향 없음 확인 — CLAUDE.md/.clang-format만 수정, 빌드 훅 트리거 패턴(`main/**`, `CMakeLists.txt`, `sdkconfig.defaults`, `idf_component.yml`) 미해당
- [x] GitHub Issue 생성: https://github.com/coport-uni/ESP32S3WebMonitor/issues/7
- [x] `.claude/branch_CLAUDE.md`, `.claude/branch_clang_format.txt` 임시 파일 삭제
- [x] 커밋 + push: `e960e5f chore: add .clang-format with LLVM base + 80-col / 4-space rules` (Closes #7). CLAUDE.md §2 MIT 본문 교체 및 §15~§21 신설은 실제 파일 반영 안 됨 — 후속 작업으로 분리 필요

## 2026-05-20 | VSCode ESP-IDF 확장의 stale OpenOCD 시리얼 캐시 정리

증상: VSCode 내장 `ESP-IDF: OpenOCD`(`Ctrl+E O`) 또는 JTAG flash 실행 시 항상 다음 두 줄로 실패 — `Info : No device matches the serial string` → `Error: esp_usb_jtag: could not find or open device!`. PowerShell에서 직접 `openocd -f board/esp32s3-builtin.cfg`를 띄우면 정상 동작. 즉 VSCode 확장만의 문제로 좁혀짐.

원인 확정: workspaceStorage SQLite 메멘토에 옛 보드의 USB iSerial이 캐싱돼 있고 확장이 그걸 `adapter serial …`로 OpenOCD에 넘기고 있었음 — `%APPDATA%\Code\User\workspaceStorage\<hash>\state.vscdb`의 `ItemTable.espressif.esp-idf-extension` JSON 값 안 `openocd.usbAdapterSerial = "90:E5:B1:D6:50:D4"` (옛 보드). 현재 보드 MAC은 `90:E5:B1:D6:5A:48`이라 진짜로 시리얼 불일치. `settings.json`도 Settings UI도 노출 안 함 → 일반 검색으로는 발견 불가.

- [x] Windows PnP enumeration으로 현재/유령 보드 MAC 식별 (`Get-PnpDevice ... VID_303A&PID_1001`) — 옛=`50:D4`, 현재=`5A:48`
- [x] MI_02 인터페이스 드라이버 서비스 `WinUSB` 확인 (driver binding은 정상, Zadig 문제 아님 확정)
- [x] PowerShell에서 OpenOCD 직접 실행 성공 (VSCode 확장만의 문제로 좁힘)
- [x] workspaceStorage `state.vscdb` 조회로 캐시된 시리얼 위치 특정 (Python `sqlite3`)
- [x] VSCode 종료 → DB 백업 → `openocd.usbAdapterSerial` 키 제거 → VSCode 재시작
- [x] 실기기 확인: VSCode에서 OpenOCD 정상 동작 — 사용자 확인 ✅
- [x] LearnedPatterns.md §5.10에 진단/픽스 영구 등록

## 2026-05-21 | claude_usage_server.py 부팅 시 자동 실행 (Windows Task Scheduler)

목적: PC 부팅(로그온) 시 `claude_usage_server.py`가 자동으로 떠 있어, ESP32가 항상 최신 `ClaudeUsage.csv`를 받을 수 있게 한다. 매번 수동으로 터미널을 띄울 필요 없음.

사용자 결정:
- 트리거: **로그온 시(At log on)**. 관리자 권한 불필요, 사용자 폴더 경로(ClaudeUsage.csv)에 안전하게 접근 가능.
- 실행기: `pythonw.exe` — 콘솔 창 숨김.
- 재시작 정책: 실패 시 1분 후 재시도, 최대 3회.

### 기술 메모

- 작업 이름: `ClaudeUsageServer`
- 스크립트 경로: `C:\Users\USER_55_DeepLearning\Desktop\workspace\container\Espress_dev\claude_usage_server.py`
- CSV 경로: 스크립트 기본값(`../../ClaudeUsage.csv` → `C:\Users\USER_55_DeepLearning\Desktop\workspace\ClaudeUsage.csv`) 그대로 사용
- 포트: 8765 (Kconfig `CLAUDE_USAGE_SERVER_URL` 기본값과 일치, 변경 불필요)
- `pythonw.exe` 위치: `(Get-Command pythonw).Source`로 동적 해결
- 작업 폴더(Start in): 스크립트 디렉터리로 지정 — `DEFAULT_CSV` 상대 경로 기준이 올바르게 잡힘

### 작업 항목

- [x] 현재 `pythonw.exe` 경로 확인 — `C:\Users\USER_55_DeepLearning\anaconda3\pythonw.exe`
- [x] 기존 동명 작업 존재 여부 확인 — 없음 (clean slate)
- [x] PowerShell `Register-ScheduledTask`로 로그온 트리거 작업 등록 (Hidden + 1분 간격 3회 재시도 + Interactive/LIMITED principal까지 한 번에 설정)
- [x] **함정 발견 및 수정**: `pythonw.exe`에선 `sys.stdout=None`이라 `log_message`의 `sys.stdout.write()`가 AttributeError → 응답 전 연결 끊김 ("empty reply from server"). `claude_usage_server.py` 상단에 stdout/stderr 가드 추가, `None`이면 `claude_usage_server.log`로 리다이렉트
- [x] `.gitignore`에 `claude_usage_server.log` 추가
- [x] 작업 즉시 실행으로 동작 검증 — pythonw PID 24348, localhost:8765 + LAN IP 192.168.1.16:8765 둘 다 `200 OK` 4626 bytes, 로그 파일에 두 요청 모두 기록
- [x] LearnedPatterns.md §5.11에 `pythonw.exe + sys.stdout=None` 함정 영구 등록
- [x] GitHub Issue 생성: https://github.com/coport-uni/ESP32S3WebMonitor/issues/9
- [x] 커밋 + push: `26014bd chore(host): auto-start claude_usage_server.py at Windows logon`

## 2026-05-21 | CLAUDE.md §2 컨벤션 감사 — HIGH + MEDIUM 위반 수정

source: 2026-05-21 컨벤션 감사 결과. 사용자 결정: HIGH(brace-less if) + MEDIUM(public API Doxygen 누락 3건) 함께 진행. LOW(beszel.c 80-col 2건)는 범위 외.

CLAUDE.md §2 위반:
- Spacing & braces: "Always brace single-statement bodies — no brace-less `if (x) do_y();`"
- Documentation: "All public functions and types must have Doxygen blocks" with `@brief`, `@param`, `@return`

### 작업 항목

- [x] HIGH: `main/ui.c:82-83` — `clamp_pct()`의 brace-less `if` 두 줄에 `{ }` 추가
- [x] MED:  `main/buttons_check.h:14` — `buttons_check_init()`에 Doxygen 블록 추가
- [x] MED:  `main/network.h:33` — `network_get_state()`에 Doxygen 블록 추가
- [x] MED:  `main/network.h:34` — `network_is_connected()`에 Doxygen 블록 추가
- [x] `idf.py build` 워닝 0 확인 — bin 0x13bbf0 bytes, 16% 여유. 사전 Kconfig 워닝(LV_MEM_CUSTOM/LV_MEMCPY_MEMSET_STD) 2건은 이번 편집과 무관
- [x] GitHub Issue 생성: https://github.com/coport-uni/ESP32S3WebMonitor/issues/8
- [x] 커밋 + push — `cadc208 style(main): brace single-statement bodies and add Doxygen to public API` (Closes #8)

## 2026-05-21 | examples/ 폴더 ESP-IDF 공식 스타일(#1)로 재구성 (standalone 프로젝트화)

목적: `examples/sensor_example/`, `examples/server_monitor_examples/` 두 폴더는 현재 component-level `CMakeLists.txt`만 있어 standalone 빌드 불가. ESP-IDF 공식 `esp-idf/examples/`, `esp-bsp/examples/` 패턴(각 example = 독립 ESP-IDF 프로젝트)으로 재구성해 폴더당 `idf.py build`/`flash`/`monitor` 직접 실행 가능하게 한다. 루트 `main/`(Beszel + Claude usage 활성 펌웨어)는 그대로 유지.

### 사용자 결정

- 폴더명: `server_monitor_examples/` → **`server_monitor/`** (ESP-IDF 공식 examples 단수형 컨벤션). `sensor_example/`은 이미 단수형이라 유지.
- 루트 `main/` 처리: 그대로 두고 examples/ 두 개만 standalone화. README는 이미 Beszel을 메인으로, sensor_example을 진단용 스냅샷으로 설명함.
- 공통 코드(`buttons_check.*`, `ui.*`) → 시그니처가 두 example 간 불일치(`buttons_check_init(void)` vs `buttons_check_init(const buttons_callbacks_t *)`). 무리하게 `components/`로 승격하지 않고 각 example 안에 격리 유지. (공통화는 진짜 같은 코드만 모이면 후속 작업으로 분리)

### 변경 후 구조

```
Espress_dev/
├── CMakeLists.txt              # 루트 = 메인 펌웨어 (변경 없음)
├── main/                       # Beszel + Claude usage (변경 없음)
├── sdkconfig.defaults
├── examples/
│   ├── sensor_example/
│   │   ├── CMakeLists.txt              # 신규: project(sensor_example)
│   │   ├── sdkconfig.defaults          # 신규: 루트 sdkconfig.defaults 복사
│   │   └── main/
│   │       ├── CMakeLists.txt          # idf_component_register (이동)
│   │       ├── idf_component.yml       # (이동)
│   │       ├── main.c                  # (이동)
│   │       ├── ui.c/h
│   │       ├── sensors.c/h
│   │       ├── audio_check.c/h
│   │       ├── ir_check.c/h
│   │       └── buttons_check.c/h
│   ├── server_monitor/                  # 폴더 rename
│   │   ├── CMakeLists.txt              # 신규: project(server_monitor)
│   │   ├── sdkconfig.defaults          # 신규: 루트 + Kconfig.projbuild 호환
│   │   └── main/
│   │       ├── CMakeLists.txt          # (이동)
│   │       ├── idf_component.yml       # (이동)
│   │       ├── Kconfig.projbuild       # (이동, BESZEL_* 메뉴)
│   │       ├── main.c
│   │       ├── ui.c/h
│   │       ├── buttons_check.c/h
│   │       ├── network.c/h
│   │       └── beszel.c/h
│   └── README.md                       # 신규: 두 example 비교/빌드법
└── README.md                            # 기존, examples/ 섹션만 폴더 rename 반영
```

### 작업 항목

- [x] `git mv examples/server_monitor_examples examples/server_monitor` (12개 파일 rename, 히스토리 보존)
- [x] 각 example에서 `main.c`/`*.c`/`*.h`/`CMakeLists.txt`/`idf_component.yml`/`Kconfig.projbuild`를 폴더 안 `main/` 서브디렉토리로 `git mv` (sensor 13개, server_monitor 12개)
- [x] `examples/sensor_example/CMakeLists.txt` 신규 — `project(sensor_example)`
- [x] `examples/server_monitor/CMakeLists.txt` 신규 — `project(server_monitor)`
- [x] `examples/sensor_example/sdkconfig.defaults` — 루트 sdkconfig.defaults 복사
- [x] `examples/server_monitor/sdkconfig.defaults` — 루트 sdkconfig.defaults 복사 (Kconfig.projbuild BESZEL_* 호환)
- [x] `examples/README.md` 신규 — 두 example 목적/빌드 명령/관계
- [x] 루트 `README.md` 갱신 — `examples/sensor_example/` 경로 및 `examples/server_monitor/` 추가, "frozen reference" 설명을 standalone project로 보강
- [x] `.gitignore`에 `examples/*/managed_components/` + `.claude/*-build.log` 추가 (사용자 결정: dependencies.lock만 트래킹, ESP-IDF 표준)
- [x] `examples/sensor_example/`에서 `idf.py build` 워닝 0 통과 — bin 0xa88a0 (55% free)
- [x] `examples/server_monitor/`에서 `idf.py build` 워닝 0 통과 — bin 0x8f970 (62% free)
- [x] GitHub Issue 생성: https://github.com/coport-uni/ESP32S3WebMonitor/issues/11
- [x] 커밋 + push: `031b442 refactor(examples): convert to standalone ESP-IDF projects` (Closes #11)

### 검증

- 각 example 폴더에서 `idf.py build` 단독 통과 ✅
- 루트 `idf.py build`는 여전히 메인 펌웨어(Beszel + Claude usage)를 빌드 — 회귀 없음 (재빌드 불필요, 루트 main/은 변경 없음)
- `git log --follow examples/sensor_example/main/main.c` 히스토리 끊김 없음 — rename 100% 매치로 인식됨 (`renamed:  examples/sensor_example/{ => main}/main.c (100%)`)

### 위험/주의

- `.claude/hooks/post-write-build-check.ps1`은 `main/**`만 모니터링 — examples/는 자동 빌드 안 됨. 수동 빌드로 검증.
- `idf.py reconfigure` 캐시: 폴더 이동 후 각 example의 `build/`가 없을 테니 그냥 새로 빌드 → 충돌 없음.
- root `main/`의 `buttons_check.*` / `ui.*` / `network.*` / `beszel.*` / `claude_usage.*`는 server_monitor example의 후속 버전 — examples/server_monitor/는 **스냅샷**으로 남기고 후속 작업은 root에서만 진행.

## 2026-05-21 | examples/sy01b_firmware standalone 화 + build & flash (COM6)

목적: 사용자 요청 — `examples/sy01b_firmware` (Syringe Pump Client) build & flash. 현재 폴더는 `CMakeLists.txt`(component-level `idf_component_register`)와 `main/` 서브디렉토리만 있어 standalone 빌드 불가. 2026-05-21 이전 작업(`031b442 refactor(examples): convert to standalone ESP-IDF projects`)의 패턴(examples/server_monitor/와 동일)으로 정리한 뒤 COM6으로 flash.

사용자 결정 (AskUserQuestion):
- 구조: standalone 프로젝트로 정리 (server_monitor와 동일 패턴)
- COM 포트: COM6

### 변경 후 구조

```
examples/sy01b_firmware/
├── CMakeLists.txt              # 신규: project(sy01b_firmware)
├── sdkconfig.defaults          # 신규: 루트 sdkconfig.defaults 복사
└── main/
    ├── CMakeLists.txt          # 신규(=현재 루트 CMakeLists.txt 내용 이동)
    ├── Kconfig.projbuild       # 기존
    ├── idf_component.yml       # 기존
    ├── main.c / config_store.* / pump_client.* / state.* / ui.* / wifi.*  # 기존
```

### 작업 항목

- [x] `examples/sy01b_firmware/main/CMakeLists.txt` 신규 — 루트 CMakeLists.txt의 `idf_component_register(...)` 내용 이동
- [x] `examples/sy01b_firmware/CMakeLists.txt` 교체 — `project(sy01b_client)` (사용자가 project name을 `sy01b_client`로 후속 조정)
- [x] `examples/sy01b_firmware/sdkconfig.defaults` — 사용자가 직접 sy01b 전용으로 작성(custom partitions.csv, FreeRTOS_HZ=1000, ESP_TLS_INSECURE, WiFi 튜닝, LVGL buf PSRAM). `sdkconfig.defaults.esp32s3`로 PSRAM oct 80M / flash QIO 80M 16MB / CPU 240MHz 분리. README.md / partitions.csv 추가
- [x] **의존성 호환 픽스**: `main/idf_component.yml`의 `espressif/esp-box-3: "^4.0"` → `"^3.0.1"`로 다운그레이드 (components.espressif.com 최신 버전이 3.2.0, v4.x 미공개) + `espressif/cjson: "^1.7.18"` 추가
- [x] **IDF v6.0 빌드 픽스**: `main/CMakeLists.txt`의 `REQUIRES json` 제거 (IDF v6.0에서 `json` 컴포넌트 삭제 → `espressif/cjson` managed component로 대체, 자동 include) — see LP §3.7 동일 패턴
- [x] `idf.py build` 워닝 0 통과 — `sy01b_client.bin` 0x130fb0 bytes, 60% 여유 (`.claude/last-sy01b-build.log`)
- [x] `idf.py -p COM6 flash` 성공 — Hash verified, hard reset OK (`.claude/last-sy01b-flash.log`)
- [x] GitHub Issue 생성: https://github.com/coport-uni/ESP32S3WebMonitor/issues/12
- [x] 커밋 + push — `d7ca736 feat(sy01b_firmware): scaffold standalone ESP-IDF project (Syringe Pump Client)` (Closes #12)

## 2026-05-21 | sy01b_firmware 재빌드 (LVGL v9 포팅 + format-truncation 픽스)

목적: 사용자가 `examples/sy01b_firmware/main/idf_component.yml`와 `main/CMakeLists.txt`를 첫 작업 이전 상태(`esp-box-3: "^4.0"` + `REQUIRES json`)로 되돌리고 `main/main.c`도 후속 편집 (APP_STATE_READY/BUSY/ERROR_FATAL 추가, snprintf banner 코드 추가). 재빌드 요청.

### 빌드 실패 3단 (모두 픽스 완료)

1. **dependency**: `esp-box-3 ^4.0`는 registry에 미공개 (latest 3.2.0) + `json` 컴포넌트는 IDF v6.0에서 삭제 → `^3.0.1` + `espressif/cjson ^1.7.18` 추가 + `REQUIRES json` 제거 (동일 픽스 d7ca736에서 적용됐던 것, 사용자 revert 이후 재적용)
2. **format-truncation**: `main/main.c:87` `snprintf(banner[64], "%s", error_msg[128])` → GCC 15 `-Werror=format-truncation`로 차단. precision specifier `%.*s` + `(int)(sizeof(banner)-1)`로 명시적 truncate
3. **LVGL v8→v9 API**: `main/ui.c`가 v8 API 사용 (`lv_msgbox_create(parent, title, txt, btns, close)`, `lv_msgbox_get_active_btn_text`, `lv_spinner_create(parent, time, arc)`) — esp-box-3 v3.x가 LVGL v9 가져옴. msgbox/spinner 모두 v9 API(`lv_msgbox_create(parent)` + `add_title/add_text/add_footer_button`, `lv_spinner_set_anim_params`)로 포팅, per-footer-button `LV_EVENT_CLICKED` 콜백으로 v8의 `LV_EVENT_VALUE_CHANGED` + `get_active_btn_text` 패턴 대체

### 작업 항목

- [x] `main/idf_component.yml`: `esp-box-3 ^4.0` → `^3.0.1` + `espressif/cjson ^1.7.18` 추가
- [x] `main/CMakeLists.txt`: `REQUIRES json` 제거 (cjson은 managed component, 자동 include)
- [x] `main/main.c:87`: `snprintf(banner, ..., "%s", error_msg)` → `"%.*s"` + precision으로 64바이트 truncate 명시
- [x] `main/ui.c` Prime modal (197-223): v8 `lv_msgbox_create(NULL, title, msg, btns, false)` + `LV_EVENT_VALUE_CHANGED` 단일 콜백 → v9 `lv_msgbox_create(NULL)` + `add_title/add_text/add_footer_button` + per-button `LV_EVENT_CLICKED` 콜백 2개 (`prime_start_cb`/`prime_cancel_cb`). `prime_confirm_cb` 삭제
- [x] `main/ui.c` Spinner (238): v8 `lv_spinner_create(parent, 1000, 60)` → v9 `lv_spinner_create(parent)` + `lv_spinner_set_anim_params(sp, 1000, 60)`
- [x] `main/ui.c` Error modal (436-488): `modal_event_cb` 삭제, `modal_close()` 헬퍼 + `modal_retry_cb`/`modal_reinit_cb`/`modal_dismiss_cb` 3개로 분리. `ui_show_error_modal`에서 fatal/recover에 따라 다른 footer button 조합으로 build
- [x] `idf.py build` warning 0 — `sy01b_client.bin` 0x134d50 bytes, 3 MB factory partition에서 60% 여유
- [x] `idf.py -p COM6 flash` — Hash verified, hard reset OK
- [x] 커밋 + push: `3ffa5a2 fix(sy01b_firmware): port UI to LVGL v9 + format-truncation fix` (Refs #12)

## 2026-05-28 | DeviceChange.ps1 — VSCode ESP-IDF 캐시 시리얼 정리 스크립트화 (see LP §5.10)

목적: LP §5.10에 기록된 "보드 교체 시 VSCode ESP-IDF 확장이 `openocd.usbAdapterSerial`을 캐싱해 OpenOCD가 실패" 픽스 절차를 매번 손으로 따라가지 않도록 재사용 가능한 PS1 스크립트로 굳힌다.

기존 초안(`DeviceChange.ps1`)은 LP에서 그대로 복사한 7줄짜리로 `<workspaceHash>` placeholder를 매번 수동 치환해야 했음.

### 개선 사항

- workspaceHash 자동 탐지: `%APPDATA%\Code\User\workspaceStorage\*\workspace.json`의 `folder` URI를 `-WorkspacePath`(기본값: `$PSScriptRoot`)와 매칭
- VSCode 종료 안전화: `Get-Process Code` 있을 때만 종료 + 1초 대기, `-NoStopCode`로 옵트아웃
- `state.vscdb.bak-yyyyMMdd-HHmmss` 백업
- Python 인라인 `-c` 따옴표 지옥 제거 → temp `.py` 파일에 here-string으로 작성 후 실행, `finally`로 정리
- 멱등성: 키 없거나 JSON 항목 없으면 `[skip]` 후 종료
- `-AllWorkspaces`로 모든 워크스페이스 일괄 처리
- `[before]` 로그로 제거 직전 캐싱된 MAC 출력

### 작업 항목

- [x] `DeviceChange.ps1` 재작성 — workspaceHash 자동 탐지, 백업/로그/멱등성 추가
- [x] PowerShell 파서로 syntax 검증 (no parse errors)
- [x] 사용자 실기기 검증 — VSCode에서 OpenOCD 정상 동작 ✅
- [x] GitHub Issue 생성: https://github.com/coport-uni/ESP32S3WebMonitor/issues/13
- [x] 커밋 + push: `462589e tools: make DeviceChange.ps1 reusable (auto workspaceHash, backup, idempotent)` (Closes #13)


## 2026-05-28 | Claude 사용량 갱신 시 RGB LED 주황 점등 (G10/G11/G12)

목적: 서버에서 새 Claude 사용량 CSV가 성공적으로 수신·파싱되어 UI에 반영되는 순간, 외부 RGB LED(R=GPIO10, G=GPIO11, B=GPIO12, common cathode)에 주황색이 약 250 ms 동안 들어왔다가 꺼지는 시각적 인디케이터를 추가한다.

결정 사항 (사전 확인):
- 타이밍: 갱신 성공 시 짧게(약 250 ms) 깜빡 (사용자 확정)
- 색상 구현: LEDC PWM 3채널, R=full / G≈40

## 2026-05-28 | Claude 사용량 갱신 시 RGB LED 주황 점등 (G10/G11/G12)

목적: 서버에서 새 Claude 사용량 CSV가 성공적으로 수신·파싱되어 UI에 반영되는 순간, 외부 RGB LED(R=GPIO10, G=GPIO11, B=GPIO12, common cathode)에 주황색이 약 250 ms 동안 들어왔다가 꺼지는 시각적 인디케이터를 추가한다.

결정 사항 (사전 확인):
- 타이밍: 갱신 성공 시 짧게(약 250 ms) 깜빡 (사용자 확정)
- 색상 구현: LEDC PWM 3채널, R=full / G≈40% / B=0 (실제 주황색)
- LED 배선: common cathode — high duty가 점등

GPIO 충돌 확인:
- BOX-3 BSP에서 GPIO 10/11/12는 SDMMC(SD)와 PMOD2 SPI에 매핑되어 있으나, 본 프로젝트는 SD/PMOD2를 초기화하지 않으므로 LEDC 출력으로 재할당 가능.

### 작업 항목

- [ ] 신규 모듈 `main/usage_led.c` / `main/usage_led.h` 생성: `usage_led_init()`(LEDC 타이머/3채널 설정), `usage_led_pulse_orange(uint32_t ms)`(주황 점등 → 지정 ms 후 소등). 핀과 PWM 듀티는 `#define`/`static const`로 명명 상수화.
- [ ] `main/CMakeLists.txt`에 `usage_led.c` 추가, `REQUIRES`에 `driver` 누락 시 추가.
- [ ] `main/main.c` `app_main()`에서 `claude_usage_init()` 이전에 `usage_led_init()` 호출.
- [ ] `main/claude_usage.c` `poll_once()`에서 `ui_claude_set_data(&d);` 직후 `usage_led_pulse_orange(250);` 호출하여 갱신 성공 시에만 점등 (`ui_claude_set_unavailable` 경로에서는 호출하지 않음).
- [ ] `idf.py build` warning 0.
- [ ] 실기기 flash 후 서버 폴 주기마다 LED가 주황색으로 짧게 깜빡이는지 시각 확인.
- [ ] GitHub Issue 생성 + 커밋/푸시.

## 2026-05-28 | Claude 사용량 갱신 시 RGB LED 주황 깜빡 (핀 재지정)

목적: 위 2026-05-15 항목의 후속. 사용자 결정에 따라 RGB LED 핀과 색상 구현 방식을 변경한다. 서버에서 새 Claude 사용량 데이터가 성공적으로 파싱·UI 반영될 때마다 외부 RGB LED에 주황불이 약 300 ms 동안 들어왔다가 꺼지는 시각적 인디케이터를 제공한다.

결정 사항 (사전 확인 답변):
- 결선: **Common cathode (active-high)** — 각 색상 핀에 HIGH 출력 시 점등
- 동작: **약 300 ms 짧은 깜빡** (1회)
- 색 표현: **디지털 R+G ON (PWM 없음, 노랑-주황 톤)** — B는 항상 LOW
- 핀 매핑: **R=GPIO 21, G=GPIO 38, B=GPIO 39** (BOX-3 BSP 상 PMOD1_IO5 / PMOD1_IO7 / PMOD1_IO3)

GPIO 충돌 확인:
- 현재 `main/main.c`는 PMOD1을 사용하지 않음 (radar/IR 모듈 미통합). GPIO 21/38/39 자유.
- 이전 2026-05-15 항목(R=10/11/12, PWM)은 미구현 상태로 남기되 본 항목으로 대체한다.

### 작업 항목

- [x] 신규 모듈 `main/usage_led.c` / `main/usage_led.h` 생성. → [main/usage_led.h](main/usage_led.h), [main/usage_led.c](main/usage_led.c) — gpio_config + esp_timer one-shot 사용, 호출 측 블로킹 없음.
- [x] 핀 번호·펄스 길이를 `#define USAGE_LED_{R,G,B}_GPIO`, `USAGE_LED_PULSE_US`로 명명 상수화 ([main/usage_led.c:9-15](main/usage_led.c#L9-L15)).
- [x] `main/CMakeLists.txt`에 `usage_led.c` + `REQUIRES driver`, `esp_timer` 추가 ([main/CMakeLists.txt:9,18,19](main/CMakeLists.txt)).
- [x] `app_main()`에서 `claude_usage_init()` 직전에 `usage_led_init()` 호출 ([main/main.c:46](main/main.c#L46)).
- [x] `poll_once()`에서 `ui_claude_set_data(&d);` 직후에만 `usage_led_pulse_orange();` 호출 — 실패 경로(`ui_claude_set_unavailable(...)`)는 그대로 통과 ([main/claude_usage.c:269](main/claude_usage.c#L269)).
- [x] `idf.py build` 통과, 내 모듈 경고 0 (기존 LV_MEM_CUSTOM kconfig 경고만 잔존, 무관). 빌드 산출물: `build/my_box3_sensor.bin` (0x13bdf0 B).
- [ ] 실기기 flash 후 폴 주기마다(`CONFIG_CLAUDE_USAGE_POLL_INTERVAL_S`) LED가 짧게 주황색으로 깜빡이는지 시각 확인. — 사용자 확인 대기
- [x] GitHub Issue 생성 + 커밋/푸시. → Issue [#15](https://github.com/coport-uni/ESP32S3WebMonitor/issues/15), commit `ef5eddf`, pushed to `origin/main`.

## 2026-05-28 | README + LearnedPatterns 업데이트 (LED 작업 결과 + 핀 선정 절차)

목적: 오늘 추가된 RGB LED 인디케이터(R=GPIO 21, G=GPIO 38, B=GPIO 39)를 README에 반영하고, 같은 작업의 첫 시도(R=10/11/12, LEDC PWM)에서 관찰된 부팅 오류를 공식 경고로 남긴다. 같은 함정을 다시 밟지 않도록 LearnedPatterns에 "핀 할당 전 capability + 점유 여부 3단계 체크" 절차를 등록한다.

### 작업 항목

- [x] `README.md` 인트로에 LED 하트비트 1문단 추가 ([README.md:7](README.md#L7)).
- [x] `README.md` Hardware required에 옵션 common-cathode RGB LED 항목 추가 ([README.md:62](README.md#L62)).
- [x] `README.md` Project layout `main/` 리스팅에 `usage_led.c, .h` 추가 + init 순서 설명에 `usage_led_init()` 위치 명시 ([README.md:212](README.md#L212), [README.md:230](README.md#L230)).
- [x] `README.md` Common pitfalls에 "GPIO/LEDC 할당 시 boot panic" 경고 추가, LP §4.2 링크 ([README.md:272](README.md#L272)).
- [x] `LearnedPatterns.md` §4.2 신규: 핀 할당 전 datasheet → BSP grep → project grep 3단계 체크, 결과를 ToDo에 기록할 것.
- [x] 커밋 + 푸시. → commit `c8960ea`, pushed to `origin/main`.

## 2026-06-04 | 탭 라벨을 PC 이름 첫 4글자로 축약

명령 검증 — What: tabview의 호스트 탭 라벨(탭 바에 보이는 텍스트). How: 호스트 이름 전체 대신 첫 4글자만 표시. Why: Beszel에 PC 2대를 추가 연결해 호스트 수가 늘면서 320px 탭 바가 좁아짐. 참고 자료: 기존 [main/ui.c](main/ui.c) `rebuild_tabview()` (라벨 생성), [main/ui.h](main/ui.h) 호스트 구조체.

- [x] [main/ui.c](main/ui.c) `rebuild_tabview()`에서 `lv_tabview_add_tab`에 넘기는 라벨을 첫 4글자로 축약. 명명 상수 `TAB_LABEL_LEN` 사용 (no magic number). → [main/ui.c:15](main/ui.c#L15), [main/ui.c:375-386](main/ui.c#L375-L386) snprintf `%.*s`.
- [x] 토폴로지 비교/탭 콘텐츠용 `host_name`은 전체 이름 그대로 유지 (첫 4글자가 같은 서로 다른 PC가 같은 탭으로 오인되지 않도록). → `strncpy(host_name, name, ...)` 전체 이름 유지, `topology_changed()` 비교도 전체 이름 기준.
- [x] `idf.py build` 통과 + 내 파일 경고 0 확인. → `build/my_box3_sensor.bin` 0x13be20 B, 16% free.
- [x] GitHub Issue 생성 + 커밋/푸시. → Issue [#16](https://github.com/coport-uni/ESP32S3WebMonitor/issues/16) (`Closes #16`으로 자동 종료), commit `5df017f`, pushed to `origin/main`.

## 2026-06-04 | Claude 사용량 CSV 파싱 실패 수정 (파일이 8KB 초과)

원인: 서버가 내보내는 워크스페이스 루트 `ClaudeUsage.csv`가 9288 B로 증가, 펌웨어 `BUF_MAX`(8192 B)를 초과. 응답이 ~8128 B(청크 경계)에서 잘려 마지막 줄이 행 중간에서 끊김 → `parse_csv_latest()`가 col_count<4로 false → `W claude_usage: CSV parse failed (header only?) len=8128`. 장치는 마지막 1행만 필요하므로 전체 파일 전송이 근본 문제. 결정: 둘 다 적용.

- [x] 서버 [claude_usage_server.py](claude_usage_server.py) `do_GET`: 전체 파일 대신 헤더+마지막 비어있지 않은 행만 전송 (응답 ~350 B 고정, 파일 크기 무관). BOM은 utf-8-sig로 제거. → 실제 CSV로 검증 시 196 B, BOM 제거 확인.
- [x] 펌웨어 [main/claude_usage.c](main/claude_usage.c) `BUF_MAX` 8192 → 32KB 상향 (방어용 여유). → [claude_usage.c:22-26](main/claude_usage.c#L22-L26).
- [x] `idf.py build` 통과 + 내 파일 경고 0. → `build/my_box3_sensor.bin` 0x13be20 B, 16% free.
- [ ] 사용자: pythonw Task Scheduler 서버 재시작 필요(스크립트 변경 반영). 별도 호스트에서 `curl`로 헤더+1행만 오는지 확인 (LP §5.9). — 사용자 확인 대기
- [x] LearnedPatterns에 "성장하는 CSV가 BUF_MAX 초과 → 서버에서 tail만 전송" 항목 추가. → LP §2.3.
- [x] GitHub Issue 생성 + 커밋/푸시. → Issue [#17](https://github.com/coport-uni/ESP32S3WebMonitor/issues/17), 커밋/푸시 진행.

## 2026-06-18 | 스마트 플러그 제어 탭 추가 (FastAPI 192.168.1.129:17046)

명령 검증 — What: ESP32-S3-BOX-3에서 LAN의 FastAPI 스마트플러그 서버(`http://192.168.1.129:17046`)에 접근해 각 플러그의 on/off 상태·소비전력(W)을 보고, 화면 터치 버튼으로 on/off/toggle 제어. How: [main/beszel.c](main/beszel.c)와 동일한 `esp_http_client` + cJSON 폴링 패턴으로 신규 `smart_plug` 모듈 작성 + 전용 LVGL "Plugs" 탭. Why: 사용자 요청 — 플러그 상태 조회 + 스위치 제어. 참고: [esp32-integration.md](esp32-integration.md)(API 레퍼런스), 기존 [main/beszel.c](main/beszel.c)·[main/ui.c](main/ui.c) 패턴.

사용자 결정 (AskUserQuestion):
- 제어 UX: **플러그별 터치 버튼**(ON / OFF / TOGGLE) — 전용 Plugs 탭
- 플러그 목록: **부팅 시 `GET /plugs` 자동 디스커버리**(하드코딩 안 함)
- 위치: **root `main/`**(활성 펌웨어)

설계 메모:
- 제어 POST는 실물 플러그 접속으로 ~1–3s 소요 → LVGL 터치 콜백에서 직접 HTTP 금지. 콜백은 FreeRTOS 큐로 `{plug_idx, action}` 명령만 전달하고, `smart_plug` 태스크가 UI 스레드 밖에서 POST 수행(UI/WDT 블로킹 방지).
- 폴링: 명령 큐 수신 타임아웃 = poll interval. 명령 도착 시 즉시 처리(POST + 해당 플러그 상태 갱신), 타임아웃 시 전체 플러그 `GET /plugs/{name}`(is_on) + `/energy`(power_w). doc의 "serialize per plug, ~1–3s/call, 10s timeout" 준수.
- cJSON은 `espressif/cjson` managed component로 자동 포함(REQUIRES 추가 불필요, beszel.c와 동일). (see LP §3.7)
- 전력값은 LVGL sprintf `%f` 의존 회피 위해 stdio `snprintf("%.1f W")`로 포맷. (see LP §3.1)
- Plugs 탭은 Claude 탭처럼 tabview rebuild 시 항상 재생성(`append_plugs_tab`) + 캐시 데이터 재적용.
- 신규 Kconfig 키는 기존 `sdkconfig`에 없으므로 빌드 시 default(`192.168.1.129:17046`)가 자동 반영됨(신규 키는 default 적용; LP §2.1은 *기존* 키 한정).

### 작업 항목

- [ ] `main/Kconfig.projbuild`: `menu "Smart plug tab"` 추가 — `SMART_PLUG_SERVER_URL`(기본 `http://192.168.1.129:17046`), `SMART_PLUG_POLL_INTERVAL_S`(기본 20, range 5~600), `SMART_PLUG_MAX_PLUGS`(기본 8, range 1~16)
- [ ] `main/smart_plug.h/.c` 신규: 네트워크 대기 → `GET /plugs` 디스커버리 → 명령 큐 기반 제어 + 주기 폴링 태스크. esp_http_client + cJSON, mutex 보호 캐시.
- [ ] `main/ui.h/.c`: `ui_plug_t`, `ui_plug_action_t`, `ui_plugs_replace/set_state/set_status/set_unavailable`, `ui_plugs_set_action_cb`. Plugs 탭 빌드(플러그별 상태닷+이름+ON/OFF+전력 + ON/OFF/TOGGLE 터치 버튼). 버튼 이벤트는 인덱스·액션을 user_data로 인코딩 후 등록된 action cb 호출(큐 post만).
- [ ] `main/main.c`: `smart_plug_init()` 호출 추가(`claude_usage_init` 뒤).
- [ ] `main/CMakeLists.txt`: SRCS에 `smart_plug.c` 추가.
- [ ] `idf.py build` warning 0 통과 (LP §5.7 in-shell recipe, background).
- [ ] 실기기 flash 후: Plugs 탭에서 plug1/plug2 상태·전력 표시 + 터치 버튼으로 on/off/toggle 동작 확인. — 사용자 확인 대기
- [ ] GitHub Issue 생성 + 커밋/푸시.

## 2026-06-18 | (방향 변경) 스마트 플러그를 탭이 아니라 별개 standalone 프로그램으로

위 "스마트 플러그 제어 탭 추가" 항목 **대체**. 사용자 추가 지시: "탭이 아니라 별개의 프로그램으로 작업해줘". 기존 Beszel 펌웨어(root `main/`)의 탭으로 통합하지 않고, examples/의 standalone ESP-IDF 프로젝트 컨벤션(server_monitor / sensor_example / sy01b_firmware, 2026-05-21)에 따라 신규 프로젝트 `examples/smart_plug/`로 작성한다. UI는 탭이 아니라 전체 화면 플러그 제어 패널.

사용자 결정 유지: 터치 버튼(ON/OFF/TOGGLE) 제어 + `GET /plugs` 자동 디스커버리. **변경**: 위치 = `examples/smart_plug/`(root main/ 아님).

### 작업 항목

- [x] `examples/smart_plug/CMakeLists.txt` + `sdkconfig.defaults` + `README.md`. → LVGL9에서 제거된 `LV_MEM_CUSTOM`/`LV_MEMCPY_MEMSET_STD` 2줄은 빼서 빌드 워닝 0.
- [x] `examples/smart_plug/main/CMakeLists.txt` + `idf_component.yml`(esp-box-3 ^3.0.1 + cjson ^1.7.18).
- [x] `main/Kconfig.projbuild`: SMART_PLUG_WIFI_SSID/PASSWORD, SMART_PLUG_SERVER_URL(기본 `http://192.168.1.129:17046`), SMART_PLUG_POLL_INTERVAL_S(20), SMART_PLUG_MAX_PLUGS(8).
- [x] `main/network.c/.h`: server_monitor에서 복사, config 키를 `CONFIG_SMART_PLUG_WIFI_*`로 변경.
- [x] `main/smart_plug.c/.h`: `GET /plugs` 디스커버리 + 명령 큐 + 주기 폴링(state+energy) + POST on/off/toggle. esp_http_client + cJSON.
- [x] `main/ui.c/.h`: 전체 화면 플러그 제어 UI(헤더 status + 스크롤 카드 리스트, 카드별 상태닷/이름/ON·OFF·전력 + ON/OFF/TOGGLE 터치 버튼). 버튼 이벤트→등록된 action cb→큐 post(HTTP는 UI 스레드 밖).
- [x] `main/main.c`: bsp_i2c_init → display_start → backlight → ui_create(lock) → network_init → smart_plug_init.
- [x] `idf.py build` warning 0 통과 — `smart_plug.bin` 0xdc910 B, 41% free (`.claude/last-smartplug-build.log`).
- [x] 멀티에이전트 adversarial review(4 dimension): 5건 제기 → 2건 confirmed(둘 다 poll_all의 per-plug publish + 6KB 스택 압박). 적용: publish_to_ui를 폴 루프 밖에서 1회 호출, publish 버퍼 static화, task 스택 6144→8192. cJSON NULL-deref 주장 3건은 dismissed(cJSON 접근자가 NULL-safe, beszel.c와 동일 관용).
- [x] 후속 요청 반영: (1) **TOGGLE 버튼 제거** → ON/OFF만 (`ui_plug_action_t`에서 TOGGLE 삭제, do_action verb on/off, 버튼 2개 레이아웃, user_data stride 2). (2) **전력 대기 표시** `power_pending` 추가 — 스위치 직후/에너지 일시 실패 시 `retrieving...`(노랑), 값 도착 시 `X.X W`, 오프라인/미터없음 `--`.
- [x] WiFi 자격증명: root Beszel `sdkconfig`의 SSID/PW를 `examples/smart_plug/sdkconfig`(gitignored)에 복제(같은 보드·망). server URL은 Kconfig 기본값 `http://192.168.1.129:17046`.
- [x] 실기기 flash(COM14) + 시리얼 검증: `got IP 192.168.1.103`, `discovered 2 plug(s)`, 터치 `action plug1/on -> ON` 확인. (최초엔 서버 미기동으로 `ECONNREFUSED`였고 PC에서도 동일 확인 → 사용자 서버 기동 후 정상.) 화면 `retrieving...` 표기는 사용자 육안 확인 대기.
- [x] GitHub Issue #18 업데이트(제목/코멘트) + 커밋/푸시 → `e84a60b feat(smart_plug): standalone Tapo plug-control app (examples/smart_plug)`, pushed to `origin/main`. Issue #18 close.

## 2026-06-18 | main/ 활성 펌웨어를 examples/smartplugcontroller/로 백업 (standalone, 빌드 가능)

명령: "지금 main 폴더 프로젝트를 examples 폴더로 백업해줘. 나중에 빌드할 수 있도록 필요한 자료도 넣어줘." 사용자 결정(AskUserQuestion): 대상 = `examples/smartplugcontroller/`.

대상: root `main/` 활성 펌웨어(Beszel + Claude usage + usage_led RGB, project `my_box3_sensor`)를 standalone ESP-IDF 프로젝트로 examples/에 충실히 복제. 나중에 단독 `idf.py build` 가능하도록 필요한 파일 포함. (examples standalone 컨벤션: 2026-05-21)

설계 메모:
- 구조: `examples/smartplugcontroller/{CMakeLists.txt, sdkconfig.defaults, dependencies.lock, README.md, main/(전체 소스)}`.
- `main/`은 root와 byte-faithful 복사. `CMakeLists.txt`만 `project(smartplugcontroller)`로 신규.
- `managed_components/`는 미포함(gitignored) — `idf.py build`가 `dependencies.lock` 기준 자동 다운로드. 그래서 lock 파일 포함이 "필요한 자료".
- `sdkconfig` 미포함(gitignored, WiFi/Beszel 자격증명). 빌드 시 menuconfig로 설정 — README에 명시.
- `sdkconfig.defaults`는 충실 복사. 빌드 시 LVGL8 잔재 심볼 `LV_MEM_CUSTOM`/`LV_MEMCPY_MEMSET_STD` 2건 "unknown kconfig symbol" 워닝이 뜨지만 root main/과 동일한 무해 노이즈(LP §3.11). 백업 충실성 우선.

### 작업 항목

- [ ] `examples/smartplugcontroller/main/` ← root `main/` 전체 복사(main.c, ui.c/h, beszel.c/h, network.c/h, claude_usage.c/h, usage_led.c/h, buttons_check.c/h, CMakeLists.txt, idf_component.yml, Kconfig.projbuild)
- [ ] `examples/smartplugcontroller/CMakeLists.txt` 신규 — `project(smartplugcontroller)`
- [ ] `examples/smartplugcontroller/sdkconfig.defaults` + `dependencies.lock` ← root 복사
- [ ] `examples/smartplugcontroller/README.md` — 백업 설명 + 빌드법 + menuconfig 자격증명 안내
- [ ] `idf.py build`로 빌드 가능 확인(LP §5.7)
- [ ] GitHub Issue 생성 + 커밋/푸시

## 2026-06-23 | JTAG 플래시 '-1' 오류 진단 (좀비 openocd)

명령: "UART로 하니까 잘 되는데 예전엔 JTAG이 잘 됬는데 왜 지금은 안될까?" → 원인 진단 및 복구. (see LP §5.12, §5.8, §5.10)

- [x] 증상 확인: VSCode JTAG 플래시 `got response: '-1', expecting: '0'`, UART(esptool COM14)는 정상
- [x] 드라이버 바인딩 점검 — MI_02→WinUSB 정상, 워크스페이스 시리얼 캐시 무관(plain shell openocd 연결 성공) → §5.4/§5.10 배제
- [x] 근본 원인: 좀비 `openocd.exe`(PID 34896, 19:09 기동)가 MI_02(WinUSB JTAG)를 libusb로 독점 점유 → 플래시가 띄운 2번째 openocd가 인터페이스를 못 열어 nonzero(-1) 리턴. UART은 MI_00(COM)을 써서 무관.
- [x] 복구: `Stop-Process -Id 34896 -Force` → 잔여 openocd 0개 확인
- [x] 검증: `openocd -s <scripts> -f board/esp32s3-builtin.cfg -c "init; exit"` → `Examination succeed` ×2, EXITCODE 0 → JTAG 정상 복구
- [x] 메모리 노트 `esp-box3-openocd-jtag` 갱신 + LearnedPatterns §5.12 추가
- [ ] (선택) GitHub Issue — 진단성 작업이라 미생성. 필요 시 사용자 요청 시 생성

## 2026-06-23 | Hotplate UI 개선 (pending 상태 · ON 버튼 빨간색 · probe/plate 동시 표시)

명령: "UI 개선 — (1) 버튼 조작 후 딜레이 동안 connection 상태가 'pending'으로 바뀌게, (2) Heat/Stir가 ON이면 버튼 색이 빨간색으로, (3) UI에 probe 온도와 plate 온도를 모두 표시." 대상: root `main/ui.c` (hotplate controller 펌웨어, `my_box3_sensor`). (see LP §3 LVGL quirks)

- [x] 버튼 클릭(enqueue) 직후 connection 인디케이터를 amber "pending"으로 전환 — 다음 status fetch(ui_set_status/offline)가 자연히 덮어씀 (show_pending(), COLOR_PENDING 0xFFD166)
- [x] Heat/Stir 토글: ON이면 버튼 bg 빨간색(`lv_obj_set_style_bg_color`), OFF이면 `lv_obj_remove_local_style_prop(LV_STYLE_BG_COLOR)`로 테마 기본색 복귀 (set_toggle_color(), COLOR_ON 0xE63946)
- [x] make_button이 버튼 객체를 반환하도록 리팩터(라벨은 `lv_obj_get_child(btn,0)`로 취득), s_btn_heat/s_btn_motor 보관
- [x] Probe 온도 전용 reading row 추가, Plate와 함께 항상 표시 (offline 시 "--")
- [x] readings 레이아웃 재배치(Plate y26/Probe y48/Speed y70/Target y92) + safety를 target 줄에 통합, aux 줄 제거
- [x] `idf.py build` 무경고 빌드 확인 — ui.c 단독 재컴파일 exit=0, warning 0건, bin 0x138e80 (17% free)
- [x] GitHub Issue 생성 (#20) + 커밋/푸시

## 2026-07-16 | Hotplate 펌웨어를 examples/hotplate_controller 로 이동

명령: "지금 main에 있는 프로젝트를 example 폴더의 hotplate control로 옮겨줘". 대상: 루트 `main/` 의 hotplate controller 펌웨어(54a0d12). 방식: 사용자 확인 결과 **진짜 이동(git mv)** + 폴더명 `hotplate_controller`. 목적: examples/README.md 의 "standalone 프로젝트 1개 = 1폴더" 규약에 맞춰 hotplate 펌웨어를 독립 예제로 승격. (see LP §3.11, §5.7)

- [x] `examples/hotplate_controller/main/` 생성 후 루트 `main/` 추적 파일 전체를 `git mv` — 실제 **12개**(ToDo 초안의 11개는 오기): main.c, ui.c/h, buttons_check.c/h, network.c/h, hotplate_client.c/h, CMakeLists.txt, Kconfig.projbuild, idf_component.yml. 빈 `main/` 디렉터리는 rmdir
- [x] `examples/hotplate_controller/CMakeLists.txt` 작성 — `project(hotplate_controller)` (examples/README.md "Adding a new example" 3단계)
- [x] 루트 `sdkconfig.defaults` 복사 — LP §3.11 에 따라 죽은 LVGL 8 심볼 `CONFIG_LV_MEM_CUSTOM` / `CONFIG_LV_MEMCPY_MEMSET_STD` + 이를 설명하던 주석까지 3줄 제거 → 빌드 로그 `unknown kconfig symbol` 0건으로 검증됨
- [x] 루트 `dependencies.lock` 복사 (examples 는 lock 추적 / managed_components 는 gitignore) — manifest_hash e7d13ec…, target esp32s3
- [x] `examples/hotplate_controller/README.md` 작성 (smart_plug 예제와 동일 수준의 빌드·Kconfig 안내) — Kconfig 6개 옵션 표 + 레이아웃 트리
- [x] `examples/README.md` 표에 hotplate_controller 행 추가 + stale 문구 4곳 수정(L3 "snapshots for ../main/", L10 "active firmware lives in ../main/", L24/L28 "matches the root project") + "Adding a new example" 4단계에 LP §3.11 경고 추가
- [x] 새 위치에서 무경고 빌드 검증 (LP §5.7 env-dump 레시피) — clean 빌드 exit=0, `Project build complete`, 컴파일 경고 0건, `unknown kconfig symbol` 0건. `hotplate_controller.bin` 0x138e80 (17% free) → **이동 전 루트 빌드(2026-06-23 항목)의 0x138e80·17% free 와 정확히 일치**, 이동이 펌웨어를 바꾸지 않았음을 확인
- [x] 빌드 실패 1건 진단 — 원인은 코드가 아니라 Windows 파일 잠금(`liblwip.a`): harness가 "stopped"로 보고한 1차 빌드(PID 31048)가 실제로는 생존 + `post-write-build-check.ps1` 훅이 띄운 3번째 빌드(PID 41940)까지 같은 `build/` 경합. 전량 kill(MCP 서버 python 보존) → `build/` 삭제 → 단일 빌드로 해결
- [x] LearnedPatterns §5.13 추가 — "stopped 보고된 백그라운드 빌드는 살아있다" (§5.8/§5.12 와 동일 계열: 스테일 프로세스가 리소스 점유)
- [x] GitHub Issue 생성 (gh, origin=ESP32S3WebMonitor) + 커밋/푸시 — Issue [#22](https://github.com/coport-uni/ESP32S3WebMonitor/issues/22), 커밋 `4a2d802` (12개 파일 전부 git rename 100% = 소스 변경 0줄), push `6aa0f49..4a2d802` 확인

추가 메모: 작업 중 루트 `CMakeLists.txt`(`project(my_box3_sensor)` → `project(home_assistant_client)`)와 루트 `sdkconfig.defaults`(LVGL 죽은 심볼 제거)에 **이 작업과 무관한 외부 변경**이 발견됨. 사용자가 루트에 새 펌웨어(home_assistant_client)를 준비 중인 것으로 보여 커밋에서 제외하고 워킹트리에 그대로 둠. `examples/server_monitor/` 미커밋 WIP도 계획대로 미포함.

메모: 사용자 선택에 따라 이동 후 루트 `main/` 은 사라지며 루트 프로젝트(`project(my_box3_sensor)`)는 빌드 불가 상태가 된다(의도된 결과). 루트 `README.md` 는 54a0d12 시점부터 이미 Beszel 기준으로 stale 하므로 이번 범위에서 제외. `examples/server_monitor/` 의 미커밋 변경(beszel/claude_usage/usage_led)은 이번 작업과 무관하므로 건드리지 않는다.

## 2026-07-16 | Home Assistant 제어 클라이언트 (루트 main/)

명령: "ESP32S3를 통해 HomeAssistant 서버에 연결되어있는 사물인터넷 장치들을 다루고 싶어" → "/main 폴더에서 진행해줘". 대상: 루트 `main/` (hotplate 이동으로 비워진 자리). HA 서버 `http://192.168.1.232:8123`. 설계 결정(사용자 확인): REST 폴링 / 조명·스위치 제어 + 센서 표시 / 엔티티 자동 발견 / device_class 필터 / 도메인별 탭뷰. (see LP §2.1, §2.3, §3.1, §3.7, §3.11, §5.7, §5.9)

- [ ] 루트 `CMakeLists.txt` 프로젝트명 `my_box3_sensor` → `home_assistant_client` (IMU 데모 시절 이름으로 이미 stale)
- [ ] 루트 `sdkconfig.defaults` 에서 죽은 LVGL 8 심볼 `CONFIG_LV_MEM_CUSTOM` / `CONFIG_LV_MEMCPY_MEMSET_STD` 2줄 제거 (LP §3.11) — `CONFIG_LV_USE_FLOAT=y` 는 센서 `%f` 출력에 필요하므로 유지 (LP §3.1)
- [ ] 낡은 루트 `sdkconfig` 제거 — hotplate 키만 남아있어 무의미. 재생성 후 HA_CLIENT_ 신규 키에 default 반영 (LP §2.1 corollary: 신규 키는 default 자동 적용). sdkconfig 는 gitignore 대상이라 WiFi/토큰을 여기 넣어도 유출 없음
- [ ] `main/Kconfig.projbuild` — 메뉴 "Home Assistant client", 접두사 `HA_CLIENT_` (`HA_` 는 짧아 충돌 위험). 키: WIFI_SSID / WIFI_PASSWORD / SERVER_URL / TOKEN / POLL_INTERVAL_S / MAX_PER_DOMAIN / SENSOR_CLASSES
- [ ] `main/CMakeLists.txt` + `main/idf_component.yml` — `json` 을 REQUIRES 에 넣지 않고 `espressif/cjson` managed component 사용 (LP §3.7)
- [ ] `main/network.c/.h` — `examples/smart_plug/` 에서 복사 후 `CONFIG_SMART_PLUG_WIFI_*` → `CONFIG_HA_CLIENT_WIFI_*` 개명 (공용 components/ 승격 안 함 — ToDo 2026-05-21 결정)
- [ ] `main/buttons_check.c/.h` — `examples/server_monitor/` 에서 복사 (CONFIG→prev tab, MUTE→next tab)
- [ ] `main/ha_client.c/.h` — `POST /api/template` 폴링. `/api/states` 는 전체 엔티티라 수십~수백 KB → 버퍼 초과로 잘림, LP §2.3 규칙대로 소스(HA)에서 Jinja 로 필터해 ~2KB 로 축소. 요청 본문은 cJSON 으로 조립(탭/개행/따옴표 escape). 응답은 평문 TSV → 줄 단위 파싱
- [ ] `main/ha_client.c` — 명령 큐 + 워커 태스크 (`smart_plug.c:431-512` 이식). LVGL 콜백에서 HTTP 금지(WDT), `xQueueReceive(q,&cmd,period)` 가 폴 타이머 겸 명령 대기. 스택 8192 (HTTP+cJSON 중첩)
- [ ] `main/ha_client.c` — `toggle` 대신 명시적 `turn_on`/`turn_off` (폴링 지연 중 2회 탭 시 toggle 은 의도와 반대로 끝남). 상한 초과 시 조용히 버리지 않고 ESP_LOGW
- [ ] `main/ui.c/.h` — lv_tabview 320×220 + 하단 상태 라벨 (`server_monitor/ui.c` 구조). Lights/Switch/Sensor 탭, 0개면 탭 미생성. UI_WITH_LOCK 매크로, topology_changed → rebuild (전체 이름으로 비교)
- [ ] `main/ui.c` — 스위치 재진입 가드: 폴링 반영 시 `lv_obj_add_state` 가 VALUE_CHANGED 를 되쏘는지 하드웨어 검증. `s_applying` 플래그로 방어 후 불필요하면 제거
- [ ] `main/main.c` — init 순서 고정: bsp_i2c_init → bsp_display_start → backlight_on → lock/ui_create/unlock → buttons_check_init → network_init → ha_client_init
- [ ] **선행(사용자)**: HA 장기 액세스 토큰 발급 — HA REST API 는 id/pw 를 받지 않음. `arduino`/`peal2024` 로 로그인 → 프로필 → 보안 → 장기 액세스 토큰 → 생성. beszel 식 토큰 갱신 로직 불필요(기본 10년)
- [ ] 플래시 전 curl 로 `/api/template` 도달 확인 — HA 는 별도 호스트(.232)라 loopback 아님, LP §5.9 함정에 안 걸리는 유효 테스트. 조명 목록·응답 크기 확인 후 device_class 필터 조정
- [ ] 무경고 빌드 (LP §5.7 레시피, `run_in_background`) — 컴파일 경고 0건 + unknown kconfig symbol 0건
- [ ] 하드웨어 검증: 읽기(화면 vs HA 웹UI) / 쓰기(BOX-3 터치 → 실제 조명) / 역방향(HA 웹UI 변경 → 폴 주기 내 반영) / 토폴로지 변경
- [ ] 루트 `README.md` 갱신 (54a0d12 시점부터 Beszel 기준으로 stale) + `LearnedPatterns.md` 에 새 gotcha append
- [ ] GitHub Issue 생성 + 커밋/푸시

메모: 범위 제외 — WebSocket 푸시(1단계는 REST 폴링, 상태 변경 최대 10초 지연 감수하고 재연결·핑퐁·메시지ID 관리 회피), 밝기/색온도 슬라이더, climate 도메인, HTTPS(LAN 평문, 기존 앱 전부 동일). 루트 `CLAUDE.md` 의 "Project" 절은 IMU 데모(`my_box3_sensor`) 기준으로 이미 stale — 프로젝트명 변경과 함께 손볼지는 별건.
