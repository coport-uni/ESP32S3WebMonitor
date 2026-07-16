#include "ui.h"

#include <stdint.h>

#include "esp_log.h"
#include "bsp/esp-box-3.h"
#include "lvgl.h"

#include "sdkconfig.h"

#include "hotplate_client.h"

static const char *TAG = "ui";

#define UI_LOCK_MS      50

#define COLOR_BG        lv_color_hex(0x0A0E27)
#define COLOR_MUTED     lv_color_hex(0x9CA3AF)
#define COLOR_OK        lv_color_hex(0x06D6A0)
#define COLOR_PINK      lv_color_hex(0xEF476F)
#define COLOR_ACCENT    lv_color_hex(0x00E5FF)
#define COLOR_PENDING   lv_color_hex(0xFFD166)  /* amber: command in flight */
#define COLOR_ON        lv_color_hex(0xE63946)  /* red: heater/motor active */

/* Identifies which control button fired the shared click handler. */
typedef enum {
    BTN_TEMP_DOWN,
    BTN_TEMP_UP,
    BTN_HEAT,
    BTN_SPEED_DOWN,
    BTN_SPEED_UP,
    BTN_MOTOR,
} btn_id_t;

static lv_obj_t *s_dot;
static lv_obj_t *s_lbl_state;
static lv_obj_t *s_lbl_age;
static lv_obj_t *s_val_plate;
static lv_obj_t *s_val_probe;
static lv_obj_t *s_val_speed;
static lv_obj_t *s_val_target;
static lv_obj_t *s_lbl_status;
static lv_obj_t *s_btn_heat;
static lv_obj_t *s_btn_motor;
static lv_obj_t *s_lbl_heat;
static lv_obj_t *s_lbl_motor;

/* Intended heater/motor state. The server exposes no run-state readback,
 * so the toggle buttons track what the user last asked for. */
static bool s_heat_on;
static bool s_motor_on;

#define UI_WITH_LOCK(BLOCK)                          \
    do {                                             \
        if (bsp_display_lock(UI_LOCK_MS)) {          \
            BLOCK;                                   \
            bsp_display_unlock();                    \
        }                                            \
    } while (0)

/* ---------------------- control buttons ---------------------- */

/* Paint a toggle button red while its function is on, or drop the local
 * override so it falls back to the theme default when off. */
static void set_toggle_color(lv_obj_t *btn, bool on)
{
    if (on) {
        lv_obj_set_style_bg_color(btn, COLOR_ON, 0);
    } else {
        lv_obj_remove_local_style_prop(btn, LV_STYLE_BG_COLOR, 0);
    }
}

/* Flag the connection indicator while a command is in flight. The next
 * status fetch (ui_set_status / ui_set_offline) overwrites this. Runs in
 * the LVGL task via the event callback, so it touches widgets directly. */
static void show_pending(void)
{
    lv_obj_set_style_bg_color(s_dot, COLOR_PENDING, 0);
    lv_label_set_text(s_lbl_state, "pending");
    lv_obj_set_style_text_color(s_lbl_state, COLOR_PENDING, 0);
}

static void on_button(lv_event_t *e)
{
    btn_id_t id = (btn_id_t)(intptr_t)lv_event_get_user_data(e);
    hp_command_t cmd = { 0 };

    switch (id) {
    case BTN_TEMP_DOWN:
        cmd.type = HP_CMD_TEMP_DELTA;
        cmd.arg = -(float)CONFIG_HOTPLATE_TEMP_STEP_C;
        hotplate_client_enqueue(&cmd);
        break;
    case BTN_TEMP_UP:
        cmd.type = HP_CMD_TEMP_DELTA;
        cmd.arg = (float)CONFIG_HOTPLATE_TEMP_STEP_C;
        hotplate_client_enqueue(&cmd);
        break;
    case BTN_SPEED_DOWN:
        cmd.type = HP_CMD_SPEED_DELTA;
        cmd.arg = -(float)CONFIG_HOTPLATE_SPEED_STEP_RPM;
        hotplate_client_enqueue(&cmd);
        break;
    case BTN_SPEED_UP:
        cmd.type = HP_CMD_SPEED_DELTA;
        cmd.arg = (float)CONFIG_HOTPLATE_SPEED_STEP_RPM;
        hotplate_client_enqueue(&cmd);
        break;
    case BTN_HEAT:
        s_heat_on = !s_heat_on;
        cmd.type = s_heat_on ? HP_CMD_HEATER_START : HP_CMD_HEATER_STOP;
        hotplate_client_enqueue(&cmd);
        lv_label_set_text_fmt(s_lbl_heat, "Heat\n%s",
                              s_heat_on ? "ON" : "OFF");
        set_toggle_color(s_btn_heat, s_heat_on);
        break;
    case BTN_MOTOR:
        s_motor_on = !s_motor_on;
        cmd.type = s_motor_on ? HP_CMD_MOTOR_START : HP_CMD_MOTOR_STOP;
        hotplate_client_enqueue(&cmd);
        lv_label_set_text_fmt(s_lbl_motor, "Stir\n%s",
                              s_motor_on ? "ON" : "OFF");
        set_toggle_color(s_btn_motor, s_motor_on);
        break;
    }

    /* Every button enqueues a command; show that one is in flight. */
    show_pending();
}

/* ---------------------- builders ---------------------- */

static lv_obj_t *make_reading(const char *name, int y)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_color(lbl, COLOR_MUTED, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 4, y);

    lv_obj_t *val = lv_label_create(scr);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, lv_color_white(), 0);
    lv_obj_align(val, LV_ALIGN_TOP_LEFT, 78, y);
    return val;
}

/* Returns the button object; its text label is the first child, reachable
 * with lv_obj_get_child(btn, 0). */
static lv_obj_t *make_button(const char *text, int x, int y, btn_id_t id)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 100, 46);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_add_event_cb(btn, on_button, LV_EVENT_CLICKED,
                        (void *)(intptr_t)id);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl);
    return btn;
}

/* ---------------------- public API ---------------------- */

void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);

    /* Top: connection dot + word + reading age. */
    s_dot = lv_obj_create(scr);
    lv_obj_set_size(s_dot, 12, 12);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_set_style_pad_all(s_dot, 0, 0);
    lv_obj_set_style_bg_color(s_dot, COLOR_MUTED, 0);
    lv_obj_align(s_dot, LV_ALIGN_TOP_LEFT, 4, 5);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_state = lv_label_create(scr);
    lv_label_set_text(s_lbl_state, "starting");
    lv_obj_set_style_text_color(s_lbl_state, COLOR_MUTED, 0);
    lv_obj_align(s_lbl_state, LV_ALIGN_TOP_LEFT, 24, 2);

    s_lbl_age = lv_label_create(scr);
    lv_label_set_text(s_lbl_age, "");
    lv_obj_set_style_text_color(s_lbl_age, COLOR_MUTED, 0);
    lv_obj_align(s_lbl_age, LV_ALIGN_TOP_RIGHT, -4, 2);

    /* Readings: plate and probe temperatures both get their own row. */
    s_val_plate  = make_reading("Plate",  26);
    s_val_probe  = make_reading("Probe",  48);
    s_val_speed  = make_reading("Speed",  70);
    s_val_target = make_reading("Target", 92);

    /* Control buttons: 2 rows x 3 columns. */
    make_button("Temp\n-",  6, 114, BTN_TEMP_DOWN);
    make_button("Temp\n+", 110, 114, BTN_TEMP_UP);
    s_btn_heat = make_button("Heat\nOFF", 214, 114, BTN_HEAT);
    s_lbl_heat = lv_obj_get_child(s_btn_heat, 0);

    make_button("Spd\n-",   6, 164, BTN_SPEED_DOWN);
    make_button("Spd\n+",  110, 164, BTN_SPEED_UP);
    s_btn_motor = make_button("Stir\nOFF", 214, 164, BTN_MOTOR);
    s_lbl_motor = lv_obj_get_child(s_btn_motor, 0);

    /* Footer status line. */
    s_lbl_status = lv_label_create(scr);
    lv_label_set_text(s_lbl_status, "starting...");
    lv_obj_set_style_text_color(s_lbl_status, COLOR_MUTED, 0);
    lv_obj_set_width(s_lbl_status, 320);
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_DOT);
    lv_obj_align(s_lbl_status, LV_ALIGN_BOTTOM_LEFT, 4, -2);

    ESP_LOGI(TAG, "ui ready");
}

void ui_set_status(const ui_status_t *st)
{
    if (!st) {
        return;
    }
    UI_WITH_LOCK({
        lv_color_t c = st->connected ? COLOR_OK : COLOR_PINK;
        lv_obj_set_style_bg_color(s_dot, c, 0);
        lv_label_set_text(s_lbl_state,
                          st->connected ? "online" : "device offline");
        lv_obj_set_style_text_color(s_lbl_state, c, 0);
        lv_label_set_text_fmt(s_lbl_age, "%ds", st->age_s);

        if (st->connected) {
            lv_label_set_text_fmt(s_val_plate, "%.1f C", st->plate_c);
            if (st->probe_valid) {
                lv_label_set_text_fmt(s_val_probe, "%.1f C", st->probe_c);
            } else {
                lv_label_set_text(s_val_probe, "--");
            }
            lv_label_set_text_fmt(s_val_speed, "%.0f rpm", st->speed_rpm);
            lv_label_set_text_fmt(s_val_target,
                                  "%.0f C / %.0f rpm   Safety %.0fC",
                                  st->target_c, st->target_rpm,
                                  st->safety_c);
            lv_label_set_text_fmt(s_lbl_status, "updated %ds ago",
                                  st->age_s);
        } else {
            lv_label_set_text(s_val_plate, "-- C");
            lv_label_set_text(s_val_probe, "-- C");
            lv_label_set_text(s_val_speed, "-- rpm");
            lv_label_set_text(s_val_target, "--");
            lv_label_set_text(s_lbl_status, "device offline");
        }
        lv_obj_set_style_text_color(s_lbl_status, COLOR_MUTED, 0);
    });
}

void ui_set_offline(const char *reason)
{
    UI_WITH_LOCK({
        lv_obj_set_style_bg_color(s_dot, COLOR_MUTED, 0);
        lv_label_set_text(s_lbl_state, "offline");
        lv_obj_set_style_text_color(s_lbl_state, COLOR_MUTED, 0);
        lv_label_set_text(s_lbl_age, "");
        lv_label_set_text(s_val_plate, "-- C");
        lv_label_set_text(s_val_probe, "-- C");
        lv_label_set_text(s_val_speed, "-- rpm");
        lv_label_set_text(s_val_target, "--");
        lv_label_set_text(s_lbl_status, reason ? reason : "offline");
        lv_obj_set_style_text_color(s_lbl_status, COLOR_ACCENT, 0);
    });
}
