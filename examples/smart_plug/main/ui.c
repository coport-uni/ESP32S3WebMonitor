#include "ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "bsp/esp-box-3.h"
#include "lvgl.h"

#include "sdkconfig.h"

static const char *TAG = "ui";

#define MAX_PLUGS       CONFIG_SMART_PLUG_MAX_PLUGS
#define PLUG_NAME_MAX   24
#define UI_LOCK_MS      50
#define STATUS_CAP      40

#define HEADER_H        30
#define CARD_H          82
#define DOT_SIZE        14
#define BTN_H           30

/* Card content (320 wide screen, list pad 6, card pad 8) leaves ~292 px.
 * Two buttons (ON / OFF) across the bottom of each card; power is
 * right-aligned on the top line so a long "retrieving..." cannot clip. */
#define ACTION_STRIDE   2   /* button user_data = idx * ACTION_STRIDE + action */
#define BTN_ON_X        0
#define BTN_OFF_X       150
#define BTN_W           138
#define BTN_ROW_Y       34

#define STATE_X         130

#define COLOR_BG        lv_color_hex(0x0A0E27)
#define COLOR_CARD      lv_color_hex(0x1F2937)
#define COLOR_ACCENT    lv_color_hex(0x00E5FF)
#define COLOR_OK        lv_color_hex(0x06D6A0)
#define COLOR_WARN      lv_color_hex(0xFFD166)
#define COLOR_PINK      lv_color_hex(0xEF476F)
#define COLOR_MUTED     lv_color_hex(0x9CA3AF)
#define COLOR_TEXT      lv_color_hex(0xE5E7EB)

typedef struct {
    lv_obj_t *card;
    lv_obj_t *dot;
    lv_obj_t *lbl_state;
    lv_obj_t *lbl_power;
} plug_row_t;

static lv_obj_t  *s_status_lbl;
static lv_obj_t  *s_list;
static plug_row_t s_rows[MAX_PLUGS];
static int        s_row_count;

/* Cached plug data, so an in-place update or a rebuild can re-render
 * without waiting for the next poll. Names own their storage; the cached
 * ui_plug_t.name pointers reference s_names. */
static ui_plug_t  s_data[MAX_PLUGS];
static char       s_names[MAX_PLUGS][PLUG_NAME_MAX];
static int        s_count;

static ui_plug_action_cb_t s_action_cb;

#define UI_WITH_LOCK(BLOCK)                          \
    do {                                             \
        if (bsp_display_lock(UI_LOCK_MS)) {          \
            BLOCK;                                   \
            bsp_display_unlock();                    \
        }                                            \
    } while (0)

/* ---------------------- touch buttons ---------------------- */

/* Each button carries (plug_idx, action) packed into its user_data:
 * code = idx * ACTION_STRIDE + action. Decoding is the inverse. */
static void plug_btn_event_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e);
    int idx = code / ACTION_STRIDE;
    ui_plug_action_t action = (ui_plug_action_t)(code % ACTION_STRIDE);
    if (s_action_cb) {
        s_action_cb(idx, action);
    }
}

static void make_btn(lv_obj_t *card, const char *txt, int x, int w,
                     int idx, ui_plug_action_t action)
{
    lv_obj_t *btn = lv_button_create(card);
    lv_obj_set_size(btn, w, BTN_H);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, BTN_ROW_Y);
    int code = idx * ACTION_STRIDE + (int)action;
    lv_obj_add_event_cb(btn, plug_btn_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)code);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_center(lbl);
}

/* ---------------------- card build / update ---------------------- */

static void build_card(int i)
{
    plug_row_t *r = &s_rows[i];

    lv_obj_t *card = lv_obj_create(s_list);
    lv_obj_set_size(card, lv_pct(100), CARD_H);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    r->card = card;

    r->dot = lv_obj_create(card);
    lv_obj_set_size(r->dot, DOT_SIZE, DOT_SIZE);
    lv_obj_set_style_radius(r->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(r->dot, 0, 0);
    lv_obj_set_style_pad_all(r->dot, 0, 0);
    lv_obj_set_style_bg_color(r->dot, COLOR_MUTED, 0);
    lv_obj_align(r->dot, LV_ALIGN_TOP_LEFT, 0, 2);
    lv_obj_clear_flag(r->dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, s_names[i]);
    lv_obj_set_style_text_color(name, COLOR_TEXT, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 22, 0);

    r->lbl_state = lv_label_create(card);
    lv_label_set_text(r->lbl_state, "...");
    lv_obj_set_style_text_color(r->lbl_state, COLOR_MUTED, 0);
    lv_obj_align(r->lbl_state, LV_ALIGN_TOP_LEFT, STATE_X, 0);

    r->lbl_power = lv_label_create(card);
    lv_label_set_text(r->lbl_power, "--");
    lv_obj_set_style_text_color(r->lbl_power, COLOR_MUTED, 0);
    lv_obj_align(r->lbl_power, LV_ALIGN_TOP_RIGHT, 0, 0);

    make_btn(card, "ON",  BTN_ON_X,  BTN_W, i, UI_PLUG_ACTION_ON);
    make_btn(card, "OFF", BTN_OFF_X, BTN_W, i, UI_PLUG_ACTION_OFF);
}

static void apply_row(int i)
{
    plug_row_t *r = &s_rows[i];
    const ui_plug_t *p = &s_data[i];
    if (!r->card) {
        return;
    }

    lv_color_t color;
    const char *state;
    if (!p->online) {
        color = COLOR_PINK;
        state = "OFFLINE";
    } else if (p->is_on) {
        color = COLOR_OK;
        state = "ON";
    } else {
        color = COLOR_MUTED;
        state = "OFF";
    }
    lv_obj_set_style_bg_color(r->dot, color, 0);
    lv_label_set_text(r->lbl_state, state);
    lv_obj_set_style_text_color(r->lbl_state, color, 0);

    char pb[20];
    lv_color_t pcolor;
    if (!p->online) {
        snprintf(pb, sizeof(pb), "--");
        pcolor = COLOR_MUTED;
    } else if (p->has_power) {
        snprintf(pb, sizeof(pb), "%.1f W", p->power_w);
        pcolor = COLOR_TEXT;
    } else if (p->power_pending) {
        snprintf(pb, sizeof(pb), "retrieving...");
        pcolor = COLOR_WARN;
    } else {
        snprintf(pb, sizeof(pb), "--");
        pcolor = COLOR_MUTED;
    }
    lv_label_set_text(r->lbl_power, pb);
    lv_obj_set_style_text_color(r->lbl_power, pcolor, 0);
}

static void rebuild_list(void)
{
    lv_obj_clean(s_list);
    memset(s_rows, 0, sizeof(s_rows));

    int n = s_count;
    if (n > MAX_PLUGS) {
        n = MAX_PLUGS;
    }
    for (int i = 0; i < n; i++) {
        build_card(i);
    }
    s_row_count = n;
    for (int i = 0; i < n; i++) {
        apply_row(i);
    }
}

static bool topology_changed(const ui_plug_t *plugs, int count)
{
    if (count != s_count) {
        return true;
    }
    for (int i = 0; i < count; i++) {
        const char *nm = plugs[i].name ? plugs[i].name : "";
        if (strncmp(nm, s_names[i], PLUG_NAME_MAX) != 0) {
            return true;
        }
    }
    return false;
}

/* ---------------------- public API ---------------------- */

void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "SMART PLUGS");
    lv_obj_set_style_text_color(title, COLOR_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 7);

    s_status_lbl = lv_label_create(scr);
    lv_label_set_text(s_status_lbl, "starting...");
    lv_obj_set_style_text_color(s_status_lbl, COLOR_MUTED, 0);
    lv_obj_set_width(s_status_lbl, 180);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_status_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_RIGHT, -8, 9);

    s_list = lv_obj_create(scr);
    lv_obj_set_size(s_list, 320, 240 - HEADER_H);
    lv_obj_align(s_list, LV_ALIGN_TOP_LEFT, 0, HEADER_H);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 6, 0);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    ESP_LOGI(TAG, "ui ready");
}

void ui_plugs_set_action_cb(ui_plug_action_cb_t cb)
{
    s_action_cb = cb;
}

void ui_plugs_replace(const ui_plug_t *plugs, int count)
{
    if (!plugs || count < 0) {
        return;
    }
    UI_WITH_LOCK({
        bool changed = topology_changed(plugs, count);
        int n = count;
        if (n > MAX_PLUGS) {
            n = MAX_PLUGS;
        }
        for (int i = 0; i < n; i++) {
            const char *nm = plugs[i].name ? plugs[i].name : "?";
            strncpy(s_names[i], nm, PLUG_NAME_MAX - 1);
            s_names[i][PLUG_NAME_MAX - 1] = '\0';
            s_data[i] = plugs[i];
            s_data[i].name = s_names[i];
        }
        s_count = n;
        if (changed) {
            rebuild_list();
        } else {
            for (int i = 0; i < n && i < s_row_count; i++) {
                apply_row(i);
            }
        }
    });
}

void ui_plugs_set_status(const char *msg, uint32_t color_hex)
{
    UI_WITH_LOCK({
        if (s_status_lbl) {
            lv_label_set_text(s_status_lbl, msg ? msg : "");
            lv_obj_set_style_text_color(s_status_lbl,
                                        lv_color_hex(color_hex), 0);
        }
    });
}
