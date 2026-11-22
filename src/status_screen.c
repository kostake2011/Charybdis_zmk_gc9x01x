/*
 * Round status screen for the ZMK dongle (GC9A01, 240x240).
 *
 *   two quarter arcs  : battery of the left / right half (red below 20%)
 *   4 bars at the top : "link is up" indicator for each half, red X when the
 *                       half is disconnected (no signal-strength information)
 *   centre            : highest active layer (display-name from the keymap),
 *                       replaced by a red "LINK LOST: L/R/L+R" on disconnect
 *   bottom            : Caps Lock "A"  >  trackball badge on mouse layers  >  chip logo
 *
 * Accent colour follows the active layer, see layer_styles[] below.
 *
 * Works with LVGL 8 and LVGL 9 (flag helpers are chosen at compile time).
 * Build flag CONFIG_DONGLE_ROUND_SCREEN_SWAP_SIDES swaps L and R.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <ctype.h>
#include <stdio.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/hid_indicators.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/hid_indicators_changed.h>
/* The header that declares zmk_peripheral_battery_state_changed differs between
 * ZMK revisions, so include whichever of these exists. */
#include <zmk/events/battery_state_changed.h>
#if __has_include(<zmk/events/peripheral_battery_state_changed.h>)
#include <zmk/events/peripheral_battery_state_changed.h>
#endif
#if __has_include(<zmk/events/split_peripheral_status_changed.h>)
#include <zmk/events/split_peripheral_status_changed.h>
#define HAVE_SPLIT_STATUS 1
#else
#define HAVE_SPLIT_STATUS 0
#endif

/* ---------- LVGL 8 / 9 compatibility ---------- */
#if defined(LVGL_VERSION_MAJOR) && LVGL_VERSION_MAJOR >= 9
#define OBJ_FLAG_ON(o, f)  lv_obj_add_flag((o), (f))
#define OBJ_FLAG_OFF(o, f) lv_obj_remove_flag((o), (f))
#else
#define OBJ_FLAG_ON(o, f)  lv_obj_add_flag((o), (f))
#define OBJ_FLAG_OFF(o, f) lv_obj_clear_flag((o), (f))
#endif
#define OBJ_SHOW(o) OBJ_FLAG_OFF((o), LV_OBJ_FLAG_HIDDEN)
#define OBJ_HIDE(o) OBJ_FLAG_ON((o), LV_OBJ_FLAG_HIDDEN)

/* ---------- palette ---------- */
#define COL_BG    0x0B0E14
#define COL_DIM   0x232936
#define COL_LABEL 0x7A8499
#define COL_TEXT  0xFFFFFF
#define COL_RED   0xFF3B30
#define COL_CYAN  0x00F0FF
#define COL_MOUSE_BG 0x1E122B

#define LOW_LEVEL     20
#define LEVEL_UNKNOWN 255

enum { SIDE_L = 0, SIDE_R = 1 };

/* Accent colour per layer index. Keep in sync with charybdis_layers.h:
 * BASE POINTER LOWER RAISE SYMBOLS SCROLL SNIPING GAMING AUTOMOUSE.
 * "mouse" layers show the trackball badge instead of the chip logo. */
struct layer_style {
    uint32_t color;
    bool mouse;
};

static const struct layer_style layer_styles[] = {
    [0] = {0x00F0FF, false}, /* Base     */
    [1] = {0xB84DFF, true},  /* Pointer  */
    [2] = {0x39FF88, false}, /* Lower    */
    [3] = {0xFFB800, false}, /* Raise    */
    [4] = {0xFF4DA6, false}, /* Symbols  */
    [5] = {0xB84DFF, true},  /* Scroll   */
    [6] = {0xB84DFF, true},  /* Sniping  */
    [7] = {0xFF7A1A, false}, /* Gaming   */
    [8] = {0xB84DFF, true},  /* Mouse (automouse) */
};
static const struct layer_style default_style = {0x00F0FF, false};

static const struct layer_style *style_for(int layer) {
    if (layer >= 0 && layer < (int)ARRAY_SIZE(layer_styles)) {
        return &layer_styles[layer];
    }
    return &default_style;
}

/* ---------- shared state (indexed by DISPLAY side, i.e. after swap) ---------- */
static struct {
    uint8_t level[2];
    bool conn[2];
    int layer;
    const char *layer_label;
    bool caps;
} st = {
    .level = {LEVEL_UNKNOWN, LEVEL_UNKNOWN},
    .conn = {false, false},
    .layer = 0,
    .layer_label = NULL,
    .caps = false,
};

static int side_of(uint8_t source) {
    int side = source;
#ifdef CONFIG_DONGLE_ROUND_SCREEN_SWAP_SIDES
    side = 1 - side;
#endif
    return (side == SIDE_L || side == SIDE_R) ? side : -1;
}

/* ---------- UI objects ---------- */
static struct {
    lv_obj_t *scr;
    lv_obj_t *arc[2];
    lv_obj_t *bat[2];
    lv_obj_t *bar[2][4];
    lv_obj_t *lost_x[2];
    lv_obj_t *layer_cap;
    lv_obj_t *layer;
    lv_obj_t *caps;
    lv_obj_t *logo;
    lv_obj_t *badge;
} ui;

/* ---------- rendering ---------- */

static void update_side(int side, lv_color_t theme) {
    bool conn = st.conn[side];
    uint8_t lvl = st.level[side];

    if (!conn) {
        lv_arc_set_value(ui.arc[side], 0);
        lv_obj_set_style_arc_opa(ui.arc[side], LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_label_set_text(ui.bat[side], "--%");
        lv_obj_set_style_text_color(ui.bat[side], lv_color_hex(COL_DIM), 0);
        for (int i = 0; i < 4; i++) {
            OBJ_HIDE(ui.bar[side][i]);
        }
        OBJ_SHOW(ui.lost_x[side]);
        return;
    }

    OBJ_HIDE(ui.lost_x[side]);
    for (int i = 0; i < 4; i++) {
        OBJ_SHOW(ui.bar[side][i]);
        lv_obj_set_style_bg_color(ui.bar[side][i], theme, 0);
    }

    if (lvl == LEVEL_UNKNOWN) {
        lv_arc_set_value(ui.arc[side], 0);
        lv_obj_set_style_arc_opa(ui.arc[side], LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_label_set_text(ui.bat[side], "--%");
        lv_obj_set_style_text_color(ui.bat[side], lv_color_hex(COL_TEXT), 0);
        return;
    }

    bool low = lvl < LOW_LEVEL;
    lv_arc_set_value(ui.arc[side], lvl);
    lv_obj_set_style_arc_opa(ui.arc[side], LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ui.arc[side], low ? lv_color_hex(COL_RED) : theme,
                               LV_PART_INDICATOR);
    lv_label_set_text_fmt(ui.bat[side], "%d%%", lvl);
    lv_obj_set_style_text_color(ui.bat[side], lv_color_hex(low ? COL_RED : COL_TEXT), 0);
}

static void render(void) {
    if (ui.scr == NULL) {
        return;
    }
    const struct layer_style *ls = style_for(st.layer);
    lv_color_t theme = lv_color_hex(ls->color);

    /* centre: link lost message or layer name */
    bool lost_l = !st.conn[SIDE_L];
    bool lost_r = !st.conn[SIDE_R];
    if (lost_l || lost_r) {
        lv_label_set_text(ui.layer, lost_l && lost_r ? "LINK LOST L+R"
                                  : lost_l           ? "LINK LOST: L"
                                                     : "LINK LOST: R");
        lv_obj_set_style_text_color(ui.layer, lv_color_hex(COL_RED), 0);
        OBJ_HIDE(ui.layer_cap);
    } else {
        char name[20];
        const char *label = st.layer_label;
        if (label != NULL && label[0] != '\0') {
            size_t i = 0;
            for (; label[i] != '\0' && i < sizeof(name) - 1; i++) {
                name[i] = (char)toupper((unsigned char)label[i]);
            }
            name[i] = '\0';
        } else {
            snprintf(name, sizeof(name), "%d", st.layer);
        }
        lv_label_set_text(ui.layer, name);
        lv_obj_set_style_text_color(ui.layer, lv_color_hex(COL_TEXT), 0);
        OBJ_SHOW(ui.layer_cap);
    }

    update_side(SIDE_L, theme);
    update_side(SIDE_R, theme);

    /* bottom block: Caps Lock > trackball badge > chip logo */
    if (st.caps) {
        OBJ_HIDE(ui.badge);
        OBJ_HIDE(ui.logo);
        OBJ_SHOW(ui.caps);
        lv_obj_set_style_text_color(ui.caps, theme, 0);
    } else if (ls->mouse) {
        OBJ_HIDE(ui.caps);
        OBJ_HIDE(ui.logo);
        OBJ_SHOW(ui.badge);
    } else {
        OBJ_HIDE(ui.caps);
        OBJ_HIDE(ui.badge);
        OBJ_SHOW(ui.logo);
    }
}

/* ---------- ZMK listeners (run on the display thread) ---------- */

struct upd_state {
    uint8_t unused;
};

/* battery of the peripherals */
static struct upd_state batt_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev != NULL) {
        int side = side_of(ev->source);
        if (side >= 0) {
            st.level[side] = ev->state_of_charge;
            /* a battery report can only come from a connected half */
            st.conn[side] = true;
        }
    }
    return (struct upd_state){0};
}
static void batt_cb(struct upd_state s) { render(); }
ZMK_DISPLAY_WIDGET_LISTENER(widget_batt, struct upd_state, batt_cb, batt_get_state)
ZMK_SUBSCRIPTION(widget_batt, zmk_peripheral_battery_state_changed);

/* highest active layer */
static struct upd_state layer_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    st.layer = index;
    st.layer_label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index));
    return (struct upd_state){0};
}
static void layer_cb(struct upd_state s) { render(); }
ZMK_DISPLAY_WIDGET_LISTENER(widget_layer, struct upd_state, layer_cb, layer_get_state)
ZMK_SUBSCRIPTION(widget_layer, zmk_layer_state_changed);

/* Caps Lock reported by the host */
static struct upd_state caps_get_state(const zmk_event_t *eh) {
    zmk_hid_indicators_t ind = zmk_hid_indicators_get_current_profile();
    /* HID LED report: bit0 Num, bit1 Caps, bit2 Scroll */
    st.caps = (ind & BIT(1)) != 0;
    return (struct upd_state){0};
}
static void caps_cb(struct upd_state s) { render(); }
ZMK_DISPLAY_WIDGET_LISTENER(widget_caps, struct upd_state, caps_cb, caps_get_state)
ZMK_SUBSCRIPTION(widget_caps, zmk_hid_indicators_changed);

/* connection of the halves */
#if HAVE_SPLIT_STATUS
static struct upd_state conn_get_state(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *ev =
        as_zmk_split_peripheral_status_changed(eh);
    if (ev != NULL) {
        /* in ZMK this event identifies the half by `slot`, not `source` */
        int side = side_of(ev->slot);
        if (side >= 0) {
            st.conn[side] = ev->connected;
        }
    }
    return (struct upd_state){0};
}
static void conn_cb(struct upd_state s) { render(); }
ZMK_DISPLAY_WIDGET_LISTENER(widget_conn, struct upd_state, conn_cb, conn_get_state)
ZMK_SUBSCRIPTION(widget_conn, zmk_split_peripheral_status_changed);
#endif

/* ---------- layout helpers ---------- */

static lv_obj_t *make_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color,
                           int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            lv_align_t align, int x, int y) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_align(l, align, x, y);
    return l;
}

/* Quarter arc on the left (135..225 deg) or right (315..45 deg) edge.
 * The right arc is reversed so that both fill from the bottom upwards. */
static lv_obj_t *make_arc(lv_obj_t *parent, bool left) {
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, 224, 224);
    lv_obj_center(a);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(a, 0, 0);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_arc_set_rotation(a, left ? 135 : 315);
    lv_arc_set_bg_angles(a, 0, 90);
    if (!left) {
        lv_arc_set_mode(a, LV_ARC_MODE_REVERSE);
    }
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    OBJ_FLAG_OFF(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 7, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    return a;
}

/* Little microchip drawn from rectangles; container is 70x60 at (85,156). */
static lv_obj_t *make_chip_logo(lv_obj_t *parent) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 70, 60);
    lv_obj_set_pos(c, 85, 156);
    OBJ_FLAG_OFF(c, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *core = make_rect(c, 23, 18, 24, 24, COL_BG, 4);
    lv_obj_set_style_border_color(core, lv_color_hex(COL_CYAN), 0);
    lv_obj_set_style_border_width(core, 2, 0);
    lv_obj_set_style_border_opa(core, LV_OPA_COVER, 0);

    lv_obj_t *inner = make_rect(c, 30, 25, 10, 10, COL_CYAN, 2);
    lv_obj_set_style_bg_opa(inner, LV_OPA_40, 0);

    static const int pin_off[3] = {-7, 0, 7};
    for (int i = 0; i < 3; i++) {
        int o = pin_off[i];
        make_rect(c, 9, 29 + o, 14, 2, COL_CYAN, 0);   /* left   */
        make_rect(c, 47, 29 + o, 14, 2, COL_CYAN, 0);  /* right  */
        make_rect(c, 34 + o, 6, 2, 12, COL_CYAN, 0);   /* top    */
        make_rect(c, 34 + o, 42, 2, 12, COL_CYAN, 0);  /* bottom */
    }
    return c;
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    OBJ_FLAG_OFF(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* battery arcs */
    ui.arc[SIDE_L] = make_arc(scr, true);
    ui.arc[SIDE_R] = make_arc(scr, false);

    /* link bars at the top: left group x=86.., right group x=132.. */
    for (int i = 0; i < 4; i++) {
        int h = 6 + i * 3;
        ui.bar[SIDE_L][i] = make_rect(scr, 86 + i * 6, 42 - h, 4, h, COL_CYAN, 0);
        ui.bar[SIDE_R][i] = make_rect(scr, 132 + i * 6, 42 - h, 4, h, COL_CYAN, 0);
    }
    ui.lost_x[SIDE_L] = make_label(scr, &lv_font_montserrat_18, COL_RED, LV_ALIGN_TOP_MID, -22, 26);
    ui.lost_x[SIDE_R] = make_label(scr, &lv_font_montserrat_18, COL_RED, LV_ALIGN_TOP_MID, 22, 26);
    lv_label_set_text(ui.lost_x[SIDE_L], "X");
    lv_label_set_text(ui.lost_x[SIDE_R], "X");
    OBJ_HIDE(ui.lost_x[SIDE_L]);
    OBJ_HIDE(ui.lost_x[SIDE_R]);

    /* battery percentages: small L / R caption + value */
    lv_obj_t *tl = make_label(scr, &lv_font_montserrat_12, COL_LABEL, LV_ALIGN_LEFT_MID, 28, -14);
    lv_label_set_text(tl, "L");
    ui.bat[SIDE_L] = make_label(scr, &lv_font_montserrat_14, COL_TEXT, LV_ALIGN_LEFT_MID, 24, 4);
    lv_label_set_text(ui.bat[SIDE_L], "--%");

    lv_obj_t *tr = make_label(scr, &lv_font_montserrat_12, COL_LABEL, LV_ALIGN_RIGHT_MID, -28, -14);
    lv_label_set_text(tr, "R");
    ui.bat[SIDE_R] = make_label(scr, &lv_font_montserrat_14, COL_TEXT, LV_ALIGN_RIGHT_MID, -24, 4);
    lv_label_set_text(ui.bat[SIDE_R], "--%");

    /* centre: small caption + layer name / link lost message */
    ui.layer_cap = make_label(scr, &lv_font_montserrat_12, COL_LABEL, LV_ALIGN_CENTER, 0, -26);
    lv_obj_set_style_text_letter_space(ui.layer_cap, 2, 0);
    lv_label_set_text(ui.layer_cap, "LAYER");

    ui.layer = make_label(scr, &lv_font_montserrat_18, COL_TEXT, LV_ALIGN_CENTER, 0, -4);
    lv_obj_set_width(ui.layer, 150);
    lv_obj_set_style_text_align(ui.layer, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ui.layer, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui.layer, "-");

    /* bottom: Caps Lock neon "A" */
    ui.caps = make_label(scr, &lv_font_montserrat_36, COL_CYAN, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_label_set_text(ui.caps, "A");
    OBJ_HIDE(ui.caps);

    /* bottom: idle chip logo */
    ui.logo = make_chip_logo(scr);

    /* bottom: trackball badge on mouse layers */
    ui.badge = make_rect(scr, 53, 178, 134, 30, COL_MOUSE_BG, 8);
    lv_obj_set_style_border_color(ui.badge, lv_color_hex(0xB84DFF), 0);
    lv_obj_set_style_border_width(ui.badge, 1, 0);
    lv_obj_set_style_border_opa(ui.badge, LV_OPA_COVER, 0);
    lv_obj_t *bl = lv_label_create(ui.badge);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(COL_TEXT), 0);
    lv_label_set_text(bl, "TRACKBALL");
    lv_obj_center(bl);
    OBJ_HIDE(ui.badge);

    ui.scr = scr;

    widget_batt_init();
    widget_layer_init();
    widget_caps_init();
#if HAVE_SPLIT_STATUS
    widget_conn_init();
#endif

    render();
    return scr;
}
