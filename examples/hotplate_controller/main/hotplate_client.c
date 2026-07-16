#include "hotplate_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"

#include "sdkconfig.h"

#include "network.h"
#include "ui.h"

static const char *TAG = "hotplate";

#define BUF_INITIAL       1024
#define BUF_MAX           (16 * 1024)
#define HTTP_TIMEOUT_MS   6000
#define CMD_QUEUE_LEN     8

/* Setpoint limits, mirroring the server's hotplate_controller.limits. */
#define TEMP_MIN_C        0.0f
#define TEMP_MAX_C        310.0f
#define SPEED_MIN_RPM     0.0f
#define SPEED_MAX_RPM     1500.0f

static QueueHandle_t s_cmd_queue;

/* Last setpoints the server reported, used to turn UI deltas into the
 * absolute values the control endpoints expect. Touched only by the
 * client task. */
static float s_last_target_c = 25.0f;
static float s_last_target_rpm = 0.0f;

/* ---------------------- HTTP response buffer ---------------------- */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} resp_buf_t;

static esp_err_t buf_ensure(resp_buf_t *r, size_t need)
{
    if (r->cap >= need) {
        return ESP_OK;
    }
    size_t new_cap = r->cap ? r->cap : BUF_INITIAL;
    while (new_cap < need) {
        new_cap *= 2;
        if (new_cap > BUF_MAX) {
            new_cap = BUF_MAX;
            break;
        }
    }
    if (new_cap < need) {
        return ESP_ERR_NO_MEM;
    }
    char *n = realloc(r->buf, new_cap);
    if (!n) {
        return ESP_ERR_NO_MEM;
    }
    r->buf = n;
    r->cap = new_cap;
    return ESP_OK;
}

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data_len <= 0) {
        return ESP_OK;
    }
    resp_buf_t *r = (resp_buf_t *)evt->user_data;
    if (!r) {
        return ESP_OK;
    }
    if (buf_ensure(r, r->len + (size_t)evt->data_len + 1) != ESP_OK) {
        ESP_LOGW(TAG, "response buffer hit cap %d", BUF_MAX);
        return ESP_OK;
    }
    memcpy(r->buf + r->len, evt->data, evt->data_len);
    r->len += (size_t)evt->data_len;
    r->buf[r->len] = '\0';
    return ESP_OK;
}

static void buf_free(resp_buf_t *r)
{
    free(r->buf);
    r->buf = NULL;
    r->len = 0;
    r->cap = 0;
}

/* ---------------------- URL + requests ---------------------- */

static void make_url(char *out, size_t out_len, const char *path)
{
    const char *base = CONFIG_HOTPLATE_SERVER_URL;
    size_t bl = strlen(base);
    while (bl > 0 && base[bl - 1] == '/') {  /* trim trailing slash */
        bl--;
    }
    snprintf(out, out_len, "%.*s%s", (int)bl, base, path);
}

static esp_err_t http_get(const char *path, resp_buf_t *resp, int *status_out)
{
    char url[256];
    make_url(url, sizeof(url), path);
    esp_http_client_config_t cfg = {
        .url           = url,
        .method        = HTTP_METHOD_GET,
        .timeout_ms    = HTTP_TIMEOUT_MS,
        .event_handler = http_event,
        .user_data     = resp,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_perform(cli);
    *status_out = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    return err;
}

static esp_err_t http_post(const char *path, const char *body,
                           int *status_out)
{
    char url[256];
    make_url(url, sizeof(url), path);
    esp_http_client_config_t cfg = {
        .url        = url,
        .method     = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        return ESP_FAIL;
    }
    if (body) {
        esp_http_client_set_header(cli, "Content-Type", "application/json");
        esp_http_client_set_post_field(cli, body, strlen(body));
    }
    esp_err_t err = esp_http_client_perform(cli);
    *status_out = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    return err;
}

/* ---------------------- Status polling ---------------------- */

/* Read a JSON number into *out; returns false (and leaves *out alone) if
 * the key is missing or null — matches the server returning null fields
 * while the device is disconnected. */
static bool json_number(cJSON *root, const char *key, float *out)
{
    cJSON *n = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(n)) {
        *out = (float)n->valuedouble;
        return true;
    }
    return false;
}

static esp_err_t fetch_status(void)
{
    resp_buf_t resp = { 0 };
    int status = 0;
    esp_err_t err = http_get("/status", &resp, &status);
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "GET /status failed (err=%s, http=%d)",
                 esp_err_to_name(err), status);
        buf_free(&resp);
        ui_set_offline("server unreachable");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_ParseWithLength(resp.buf, resp.len);
    buf_free(&resp);
    if (!json) {
        ui_set_offline("bad JSON from server");
        return ESP_FAIL;
    }

    ui_status_t st = { 0 };
    cJSON *conn = cJSON_GetObjectItemCaseSensitive(json, "connected");
    st.connected = cJSON_IsTrue(conn);

    json_number(json, "plate_temperature_c", &st.plate_c);
    st.probe_valid = json_number(json, "probe_temperature_c", &st.probe_c);
    json_number(json, "speed_rpm", &st.speed_rpm);
    json_number(json, "safety_temperature_c", &st.safety_c);
    if (json_number(json, "target_temperature_c", &st.target_c)) {
        s_last_target_c = st.target_c;
    }
    if (json_number(json, "target_speed_rpm", &st.target_rpm)) {
        s_last_target_rpm = st.target_rpm;
    }
    float age = 0;
    if (json_number(json, "age_seconds", &age)) {
        st.age_s = (int)age;
    }
    cJSON_Delete(json);

    ui_set_status(&st);
    return ESP_OK;
}

/* ---------------------- Control commands ---------------------- */

static float clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void execute_command(const hp_command_t *cmd)
{
    int status = 0;
    char body[48];

    switch (cmd->type) {
    case HP_CMD_TEMP_DELTA: {
        float v = clampf(s_last_target_c + cmd->arg, TEMP_MIN_C, TEMP_MAX_C);
        snprintf(body, sizeof(body), "{\"value\":%.1f}", v);
        if (http_post("/control/target/temperature", body, &status)
                == ESP_OK && status == 200) {
            s_last_target_c = v;
        }
        ESP_LOGI(TAG, "set target temp %.1f C -> http %d", v, status);
        break;
    }
    case HP_CMD_SPEED_DELTA: {
        float v = clampf(s_last_target_rpm + cmd->arg,
                         SPEED_MIN_RPM, SPEED_MAX_RPM);
        snprintf(body, sizeof(body), "{\"value\":%.0f}", v);
        if (http_post("/control/target/speed", body, &status)
                == ESP_OK && status == 200) {
            s_last_target_rpm = v;
        }
        ESP_LOGI(TAG, "set target speed %.0f rpm -> http %d", v, status);
        break;
    }
    case HP_CMD_HEATER_START:
        http_post("/control/heater/start", NULL, &status);
        ESP_LOGI(TAG, "heater start -> http %d", status);
        break;
    case HP_CMD_HEATER_STOP:
        http_post("/control/heater/stop", NULL, &status);
        ESP_LOGI(TAG, "heater stop -> http %d", status);
        break;
    case HP_CMD_MOTOR_START:
        http_post("/control/motor/start", NULL, &status);
        ESP_LOGI(TAG, "motor start -> http %d", status);
        break;
    case HP_CMD_MOTOR_STOP:
        http_post("/control/motor/stop", NULL, &status);
        ESP_LOGI(TAG, "motor stop -> http %d", status);
        break;
    }
}

/* ---------------------- Task ---------------------- */

static void client_task(void *arg)
{
    (void)arg;

    if (strlen(CONFIG_HOTPLATE_WIFI_SSID) == 0) {
        ui_set_offline("configure WiFi (menuconfig)");
        vTaskDelete(NULL);
        return;
    }

    ui_set_offline("WiFi connecting...");
    while (!network_is_connected()) {
        network_wait_connected(2000);
    }

    const uint32_t period_ms = CONFIG_HOTPLATE_POLL_INTERVAL_S * 1000;

    while (1) {
        if (!network_is_connected()) {
            ui_set_offline("WiFi lost, reconnecting...");
            network_wait_connected(5000);
            continue;
        }

        hp_command_t cmd;
        if (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS(period_ms))
                == pdTRUE) {
            execute_command(&cmd);
            fetch_status();  /* reflect the change without waiting a cycle */
        } else {
            fetch_status();  /* periodic poll */
        }
    }
}

/* ---------------------- Public API ---------------------- */

esp_err_t hotplate_client_init(void)
{
    s_cmd_queue = xQueueCreate(CMD_QUEUE_LEN, sizeof(hp_command_t));
    if (!s_cmd_queue) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(
        client_task, "hotplate", 6144, NULL, 4, NULL, 0);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

bool hotplate_client_enqueue(const hp_command_t *cmd)
{
    if (!s_cmd_queue || !cmd) {
        return false;
    }
    return xQueueSend(s_cmd_queue, cmd, 0) == pdTRUE;
}
