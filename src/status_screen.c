/*
 * Round status screen for a ZMK dongle (GC9A01, 240x240).
 *   left/right arc : battery of the two halves (fetched from peripherals)
 *   centre         : highest active layer (display-name from the keymap)
 *   CAPS pill      : Caps Lock state reported by the host
 *
 * Written against LVGL 9 names (lv_obj_remove_flag). On LVGL 8 use
 * lv_obj_clear_flag instead.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/hid_indicators.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/hid_indicators_changed.h>
/* Event with battery levels of the peripherals: verify the header and
 * struct name against your ZMK revision. */
#include <zmk/events/battery_state_changed.h>

#define COL_BG      0x0B0D10
#define COL_TRACK   0x232A31
#define COL_OK      0x3DDC97
#define COL_LOW     0xFF6B6B
#define COL_TEXT    0xE8ECEF
#define COL_DIM     0x9AA4AD
#define COL_CAPS    0xFFB454
#define LOW_LEVEL   20
#define LEVEL_UNKNOWN 255

enum { SIDE_L = 0, SIDE_R = 1 };

static struct {
    lv_obj_t *arc[2];
    lv_obj_t *batt[2];
    lv_obj_t *caps;
    lv_obj_t *layer;
    lv_obj_t *layer_idx;
} ui;

/* ---------- widgets: battery ---------- */

struct batt_state {
    uint8_t source;
    uint8_t level;
};

static void batt_update_cb(struct batt_state s) {
    int side = s.source;
#ifdef CONFIG_DONGLE_ROUND_SCREEN_SWAP_SIDES
    side = 1 - side;
#endif
    if (side < 0 || side > 1 || ui.arc[side] == NULL) {
        return;
    }
    const char *name = side == SIDE_L ? "L" : "R";

    if (s.level == LEVEL_UNKNOWN) {
        lv_arc_set_value(ui.arc[side], 0);
        lv_label_set_text_fmt(ui.batt[side], "%s --", name);
        return;
    }
    lv_arc_set_value(ui.arc[side], s.level);
    lv_label_set_text_fmt(ui.batt[side], "%s %d%%", name, s.level);
    lv_obj_set_style_arc_color(ui.arc[side],
                               lv_color_hex(s.level < LOW_LEVEL ? COL_LOW : COL_OK),
                               LV_PART_INDICATOR);
}

static struct batt_state batt_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev == NULL) {
        return (struct batt_state){.source = 0, .level = LEVEL_UNKNOWN};
    }
    return (struct batt_state){.source = ev->source, .level = ev->state_of_charge};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_batt, struct batt_state, batt_update_cb, batt_get_state)
ZMK_SUBSCRIPTION(widget_batt, zmk_peripheral_battery_state_changed);

/* ---------- widgets: layer ---------- */

struct layer_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

static void layer_update_cb(struct layer_state s) {
    if (ui.layer == NULL) {
        return;
    }
    if (s.label != NULL && s.label[0] != '\0') {
        lv_label_set_text(ui.layer, s.label);
    } else {
        lv_label_set_text_fmt(ui.layer, "%d", s.index);
    }
    lv_label_set_text_fmt(ui.layer_idx, "LAYER %d", s.index);
}

static struct layer_state layer_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct layer_state){
        .index = index,
        .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index)),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer, struct layer_state, layer_update_cb, layer_get_state)
ZMK_SUBSCRIPTION(widget_layer, zmk_layer_state_changed);

/* ---------- widgets: caps lock ---------- */

struct caps_state {
    bool on;
};

static void caps_update_cb(struct caps_state s) {
    if (ui.caps == NULL) {
        return;
    }
    if (s.on) {
        lv_obj_remove_flag(ui.caps, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.caps, LV_OBJ_FLAG_HIDDEN);
    }
}

static struct caps_state caps_get_state(const zmk_event_t *eh) {
    zmk_hid_indicators_t ind = zmk_hid_indicators_get_current_profile();
    /* HID LED report: bit0 Num, bit1 Caps, bit2 Scroll */
    return (struct caps_state){.on = (ind & BIT(1)) != 0};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_caps, struct caps_state, caps_update_cb, caps_get_state)
ZMK_SUBSCRIPTION(widget_caps, zmk_hid_indicators_changed);

/* ---------- layout ---------- */

static lv_obj_t *make_arc(lv_obj_t *parent, bool left) {
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, 232, 232);
    lv_obj_center(a);
    lv_arc_set_rotation(a, 0);
    if (left) {
        lv_arc_set_bg_angles(a, 95, 265);   /* bottom -> left -> top */
        lv_arc_set_mode(a, LV_ARC_MODE_NORMAL);
    } else {
        lv_arc_set_bg_angles(a, 275, 85);   /* top -> right -> bottom */
        lv_arc_set_mode(a, LV_ARC_MODE_REVERSE); /* fills from the bottom */
    }
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
    lv_obj_set_style_arc_width(a, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 9, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(COL_OK), LV_PART_INDICATOR);
    return a;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            lv_align_t align, int x, int y) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_align(l, align, x, y);
    return l;
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    ui.arc[SIDE_L] = make_arc(scr, true);
    ui.arc[SIDE_R] = make_arc(scr, false);

    /* CAPS pill, hidden until Caps Lock is on */
    ui.caps = make_label(scr, &lv_font_montserrat_12, COL_BG, LV_ALIGN_CENTER, 0, -62);
    lv_label_set_text(ui.caps, "CAPS");
    lv_obj_set_style_text_letter_space(ui.caps, 1, 0);
    lv_obj_set_style_bg_color(ui.caps, lv_color_hex(COL_CAPS), 0);
    lv_obj_set_style_bg_opa(ui.caps, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.caps, 10, 0);
    lv_obj_set_style_pad_hor(ui.caps, 12, 0);
    lv_obj_set_style_pad_ver(ui.caps, 3, 0);
    lv_obj_add_flag(ui.caps, LV_OBJ_FLAG_HIDDEN);

    /* layer name + index */
    ui.layer = make_label(scr, &lv_font_montserrat_40, COL_TEXT, LV_ALIGN_CENTER, 0, -8);
    lv_obj_set_width(ui.layer, 190);
    lv_label_set_long_mode(ui.layer, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(ui.layer, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ui.layer, "-");

    ui.layer_idx = make_label(scr, &lv_font_montserrat_12, COL_DIM, LV_ALIGN_CENTER, 0, 26);
    lv_obj_set_style_text_letter_space(ui.layer_idx, 2, 0);
    lv_label_set_text(ui.layer_idx, "LAYER");

    /* battery percentages */
    ui.batt[SIDE_L] = make_label(scr, &lv_font_montserrat_14, COL_TEXT, LV_ALIGN_CENTER, -38, 62);
    ui.batt[SIDE_R] = make_label(scr, &lv_font_montserrat_14, COL_TEXT, LV_ALIGN_CENTER, 38, 62);
    lv_label_set_text(ui.batt[SIDE_L], "L --");
    lv_label_set_text(ui.batt[SIDE_R], "R --");

    widget_batt_init();
    widget_layer_init();
    widget_caps_init();

    return scr;
}
