#include "smart_plug.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"

#include "sdkconfig.h"

#include "network.h"
#include "ui.h"

static const char *TAG = "smart_plug";

#define BUF_INITIAL      1024
#define BUF_MAX          (16 * 1024)
/* The doc warns each /plugs/{name}* call opens a session to the physical
 * plug (~1-3 s) and recommends a generous timeout. */
#define HTTP_TIMEOUT_MS  10000
#define PLUG_NAME_MAX    24
#define CMD_QUEUE_LEN    8
#define STATUS_LINE_CAP  40
#define MAX_PLUGS        CONFIG_SMART_PLUG_MAX_PLUGS

/* 0xRRGGBB status colors handed to ui_plugs_set_status. */
#define COL_OK     0x06D6A0u
#define COL_WARN   0xFFD166u
#define COL_PINK   0xEF476Fu
#define COL_MUTED  0x9CA3AFu

typedef struct {
    char  name[PLUG_NAME_MAX];
    bool  online;
    bool  is_on;
    bool  has_power;
    bool  power_pending;   /* online, awaiting a power reading (e.g. just switched) */
    float power_w;
} plug_t;

typedef struct {
    int              idx;
    ui_plug_action_t action;
} plug_cmd_t;

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} resp_buf_t;

static plug_t            s_plugs[MAX_PLUGS];
static int               s_plug_count;
static SemaphoreHandle_t s_mtx;
static QueueHandle_t     s_cmd_q;

/* ---------------------- HTTP response buffer ---------------------- */

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

/* ---------------------- HTTP request ---------------------- */

static void make_url(char *out, size_t cap, const char *path)
{
    const char *base = CONFIG_SMART_PLUG_SERVER_URL;
    size_t bl = strlen(base);
    while (bl > 0 && base[bl - 1] == '/') {
        bl--;
    }
    snprintf(out, cap, "%.*s%s", (int)bl, base, path);
}

/* Perform one request to `path`, collecting the body into `resp`. POST
 * sends an empty body (correct for /on, /off, /toggle). Returns the
 * transport esp_err_t and writes the HTTP status into *out_status. The
 * caller owns resp->buf and must free it. */
static esp_err_t http_request(esp_http_client_method_t method,
                              const char *path, resp_buf_t *resp,
                              int *out_status)
{
    char url[256];
    make_url(url, sizeof(url), path);

    esp_http_client_config_t cfg = {
        .url           = url,
        .method        = method,
        .timeout_ms    = HTTP_TIMEOUT_MS,
        .event_handler = http_event,
        .user_data     = resp,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        return ESP_FAIL;
    }
    if (method == HTTP_METHOD_POST) {
        esp_http_client_set_post_field(cli, "", 0);
    }
    esp_err_t err = esp_http_client_perform(cli);
    if (out_status) {
        *out_status = esp_http_client_get_status_code(cli);
    }
    esp_http_client_cleanup(cli);
    return err;
}

/* ---------------------- API calls ---------------------- */

/* GET /plugs — populate s_plugs[] with the discovered names. Cheap; does
 * not contact the physical plugs. */
static esp_err_t discover_plugs(void)
{
    resp_buf_t resp = { 0 };
    int status = 0;
    esp_err_t err = http_request(HTTP_METHOD_GET, "/plugs", &resp, &status);
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "discover: err=%s status=%d",
                 esp_err_to_name(err), status);
        free(resp.buf);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    cJSON *json = cJSON_ParseWithLength(resp.buf, resp.len);
    free(resp.buf);
    if (!cJSON_IsArray(json)) {
        ESP_LOGE(TAG, "discover: response is not an array");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    plug_t tmp[MAX_PLUGS];
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, json) {
        if (n >= MAX_PLUGS) {
            ESP_LOGW(TAG, "discover: more than %d plugs, ignoring rest",
                     MAX_PLUGS);
            break;
        }
        cJSON *name = cJSON_GetObjectItemCaseSensitive(it, "name");
        if (!cJSON_IsString(name) || !name->valuestring) {
            continue;
        }
        memset(&tmp[n], 0, sizeof(tmp[n]));
        strncpy(tmp[n].name, name->valuestring, PLUG_NAME_MAX - 1);
        n++;
    }
    cJSON_Delete(json);

    if (n == 0) {
        ESP_LOGW(TAG, "discover: no plugs in list");
        return ESP_FAIL;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    memcpy(s_plugs, tmp, sizeof(plug_t) * n);
    s_plug_count = n;
    xSemaphoreGive(s_mtx);
    ESP_LOGI(TAG, "discovered %d plug(s)", n);
    return ESP_OK;
}

/* GET /plugs/{name} — read the relay on/off state. */
static esp_err_t read_state(const char *name, bool *is_on)
{
    char path[PLUG_NAME_MAX + 16];
    snprintf(path, sizeof(path), "/plugs/%s", name);

    resp_buf_t resp = { 0 };
    int status = 0;
    esp_err_t err = http_request(HTTP_METHOD_GET, path, &resp, &status);
    if (err != ESP_OK || status != 200) {
        free(resp.buf);
        return err != ESP_OK ? err : ESP_FAIL;
    }
    cJSON *json = cJSON_ParseWithLength(resp.buf, resp.len);
    free(resp.buf);
    cJSON *on = cJSON_GetObjectItemCaseSensitive(json, "is_on");
    bool ok = cJSON_IsBool(on);
    if (ok) {
        *is_on = cJSON_IsTrue(on);
    }
    cJSON_Delete(json);
    return ok ? ESP_OK : ESP_FAIL;
}

/* GET /plugs/{name}/energy — read instantaneous power. A 422 means the
 * plug has no energy meter, which is a clean "no power" rather than a
 * failure. power_w may be JSON null. */
static esp_err_t read_power(const char *name, bool *has_power, float *power_w)
{
    char path[PLUG_NAME_MAX + 24];
    snprintf(path, sizeof(path), "/plugs/%s/energy", name);

    resp_buf_t resp = { 0 };
    int status = 0;
    esp_err_t err = http_request(HTTP_METHOD_GET, path, &resp, &status);
    if (err != ESP_OK) {
        free(resp.buf);
        return err;
    }
    if (status == 422) {
        *has_power = false;
        free(resp.buf);
        return ESP_OK;
    }
    if (status != 200) {
        free(resp.buf);
        return ESP_FAIL;
    }
    cJSON *json = cJSON_ParseWithLength(resp.buf, resp.len);
    free(resp.buf);
    cJSON *p = cJSON_GetObjectItemCaseSensitive(json, "power_w");
    if (cJSON_IsNumber(p)) {
        *has_power = true;
        *power_w = (float)p->valuedouble;
    } else {
        *has_power = false;
    }
    cJSON_Delete(json);
    return ESP_OK;
}

/* POST /plugs/{name}/{on|off}. The response reports is_on after the
 * action, which is the authoritative new state. */
static esp_err_t do_action(const char *name, ui_plug_action_t action,
                           bool *is_on_after)
{
    const char *verb = (action == UI_PLUG_ACTION_ON) ? "on" : "off";
    char path[PLUG_NAME_MAX + 24];
    snprintf(path, sizeof(path), "/plugs/%s/%s", name, verb);

    resp_buf_t resp = { 0 };
    int status = 0;
    esp_err_t err = http_request(HTTP_METHOD_POST, path, &resp, &status);
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "action %s/%s: err=%s status=%d",
                 name, verb, esp_err_to_name(err), status);
        free(resp.buf);
        return err != ESP_OK ? err : ESP_FAIL;
    }
    cJSON *json = cJSON_ParseWithLength(resp.buf, resp.len);
    free(resp.buf);
    cJSON *on = cJSON_GetObjectItemCaseSensitive(json, "is_on");
    bool ok = cJSON_IsBool(on);
    if (ok) {
        *is_on_after = cJSON_IsTrue(on);
    }
    cJSON_Delete(json);
    ESP_LOGI(TAG, "action %s/%s -> %s", name, verb,
             ok ? (*is_on_after ? "ON" : "OFF") : "?");
    return ok ? ESP_OK : ESP_FAIL;
}

/* ---------------------- publish to UI ---------------------- */

static void publish_to_ui(void)
{
    /* Only ever called from the smart_plug task (never reentrant), so these
     * large buffers are file-static to keep them off the task stack. */
    static ui_plug_t local[MAX_PLUGS];
    static char      names[MAX_PLUGS][PLUG_NAME_MAX];
    int       count;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    count = s_plug_count;
    for (int i = 0; i < count; i++) {
        strncpy(names[i], s_plugs[i].name, PLUG_NAME_MAX - 1);
        names[i][PLUG_NAME_MAX - 1] = '\0';
        local[i].name          = names[i];
        local[i].online        = s_plugs[i].online;
        local[i].is_on         = s_plugs[i].is_on;
        local[i].has_power     = s_plugs[i].has_power;
        local[i].power_pending = s_plugs[i].power_pending;
        local[i].power_w       = s_plugs[i].power_w;
    }
    xSemaphoreGive(s_mtx);

    ui_plugs_replace(local, count);
}

/* ---------------------- work cycles ---------------------- */

static void poll_all(void)
{
    char names[MAX_PLUGS][PLUG_NAME_MAX];
    int  count;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    count = s_plug_count;
    for (int i = 0; i < count; i++) {
        strncpy(names[i], s_plugs[i].name, PLUG_NAME_MAX - 1);
        names[i][PLUG_NAME_MAX - 1] = '\0';
    }
    xSemaphoreGive(s_mtx);

    for (int i = 0; i < count; i++) {
        bool  is_on = false;
        bool  has_power = false;
        float power_w = 0.0f;
        esp_err_t se = read_state(names[i], &is_on);
        esp_err_t pe = (se == ESP_OK)
            ? read_power(names[i], &has_power, &power_w)
            : ESP_FAIL;

        xSemaphoreTake(s_mtx, portMAX_DELAY);
        /* Guard against a concurrent re-discovery shrinking/reordering the
         * list while we were off doing HTTP. */
        if (i < s_plug_count &&
            strncmp(names[i], s_plugs[i].name, PLUG_NAME_MAX) == 0) {
            if (se == ESP_OK) {
                s_plugs[i].online = true;
                s_plugs[i].is_on  = is_on;
                if (pe == ESP_OK) {
                    /* Definitive answer: a number (has_power) or a 422
                     * "no meter" (has_power false). Either way, stop waiting. */
                    s_plugs[i].has_power     = has_power;
                    s_plugs[i].power_w       = power_w;
                    s_plugs[i].power_pending = false;
                } else {
                    /* Transient energy-read failure: keep showing pending. */
                    s_plugs[i].has_power     = false;
                    s_plugs[i].power_pending = true;
                }
            } else {
                s_plugs[i].online        = false;
                s_plugs[i].has_power     = false;
                s_plugs[i].power_pending = false;
            }
        }
        xSemaphoreGive(s_mtx);
    }

    /* Publish once after the whole sweep rather than per plug: one LVGL lock
     * + one redraw instead of `count` of each. */
    publish_to_ui();
}

static void handle_command(const plug_cmd_t *cmd)
{
    char name[PLUG_NAME_MAX] = "?";
    bool valid = false;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (cmd->idx >= 0 && cmd->idx < s_plug_count) {
        strncpy(name, s_plugs[cmd->idx].name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        valid = true;
    }
    xSemaphoreGive(s_mtx);

    if (!valid) {
        return;
    }

    char sb[STATUS_LINE_CAP];
    snprintf(sb, sizeof(sb), "switching %s...", name);
    ui_plugs_set_status(sb, COL_WARN);

    bool is_on = false;
    if (do_action(name, cmd->action, &is_on) == ESP_OK) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        if (cmd->idx < s_plug_count &&
            strncmp(name, s_plugs[cmd->idx].name, PLUG_NAME_MAX) == 0) {
            s_plugs[cmd->idx].online = true;
            s_plugs[cmd->idx].is_on  = is_on;
            /* The P110M power meter lags a relay change by ~4-6 s; show
             * "retrieving..." until the next poll picks up the new value. */
            s_plugs[cmd->idx].has_power     = false;
            s_plugs[cmd->idx].power_pending = true;
        }
        xSemaphoreGive(s_mtx);
        publish_to_ui();
        ui_plugs_set_status("updated", COL_MUTED);
    } else {
        ui_plugs_set_status("switch failed", COL_PINK);
    }
}

/* ---------------------- task ---------------------- */

static void smart_plug_task(void *arg)
{
    (void)arg;

    if (strlen(CONFIG_SMART_PLUG_SERVER_URL) == 0) {
        ui_plugs_set_status("set server URL (menuconfig)", COL_PINK);
        vTaskDelete(NULL);
        return;
    }

    ui_plugs_set_status("WiFi connecting...", COL_WARN);
    network_wait_connected(portMAX_DELAY);

    ui_plugs_set_status("discovering plugs...", COL_WARN);
    while (discover_plugs() != ESP_OK) {
        ui_plugs_set_status("server unreachable", COL_PINK);
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (!network_is_connected()) {
            network_wait_connected(portMAX_DELAY);
        }
        ui_plugs_set_status("discovering plugs...", COL_WARN);
    }
    publish_to_ui();   /* draw the cards before the first (slow) poll */

    ui_plugs_set_status("refreshing...", COL_WARN);
    poll_all();
    ui_plugs_set_status("updated", COL_MUTED);

    const TickType_t period =
        pdMS_TO_TICKS(CONFIG_SMART_PLUG_POLL_INTERVAL_S * 1000);

    while (1) {
        plug_cmd_t cmd;
        if (xQueueReceive(s_cmd_q, &cmd, period) == pdTRUE) {
            handle_command(&cmd);
        } else {
            if (!network_is_connected()) {
                ui_plugs_set_status("WiFi lost...", COL_WARN);
                network_wait_connected(portMAX_DELAY);
                continue;
            }
            ui_plugs_set_status("refreshing...", COL_WARN);
            poll_all();
            ui_plugs_set_status("updated", COL_MUTED);
        }
    }
}

/* ---------------------- UI action sink ---------------------- */

/* Runs on the LVGL task from a button click. Must not block: just hand the
 * request to the worker task. */
static void on_ui_action(int idx, ui_plug_action_t action)
{
    if (!s_cmd_q) {
        return;
    }
    plug_cmd_t cmd = { .idx = idx, .action = action };
    if (xQueueSend(s_cmd_q, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "command queue full, dropping action");
    }
}

/* ---------------------- public API ---------------------- */

esp_err_t smart_plug_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) {
        return ESP_ERR_NO_MEM;
    }
    s_cmd_q = xQueueCreate(CMD_QUEUE_LEN, sizeof(plug_cmd_t));
    if (!s_cmd_q) {
        return ESP_ERR_NO_MEM;
    }
    ui_plugs_set_action_cb(on_ui_action);

    /* 8 KB: the task nests HTTP + cJSON under poll_all()/handle_command();
     * leaves margin even at CONFIG_SMART_PLUG_MAX_PLUGS = 16. */
    BaseType_t ok = xTaskCreatePinnedToCore(
        smart_plug_task, "smart_plug", 8192, NULL, 4, NULL, 0);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
