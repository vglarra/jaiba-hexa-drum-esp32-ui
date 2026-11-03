#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include "bridge_transport.h"
#include "sensor_link.h"

namespace ui {
namespace {

// ============================================================
// Palette — same flat, dark style as the Phase 0 bring-up screen.
// ============================================================
const lv_color_t kBackground = lv_color_hex(0x080808);
const lv_color_t kPanel = lv_color_hex(0x121212);
const lv_color_t kTile = lv_color_hex(0x1B1B1B);
const lv_color_t kTileBorder = lv_color_hex(0x2A2A2A);
const lv_color_t kAccent = lv_color_hex(0x1F6FEB);
const lv_color_t kGreen = lv_color_hex(0x2ECC71);
const lv_color_t kRed = lv_color_hex(0xE74C3C);
const lv_color_t kText = lv_color_hex(0xF5F5F5);
const lv_color_t kMuted = lv_color_hex(0x8A8A8A);

const lv_font_t* kFontSmall = &lv_font_montserrat_14;
const lv_font_t* kFontMedium = &lv_font_montserrat_20;
const lv_font_t* kFontLarge = &lv_font_montserrat_40;

// ============================================================
// Fixed sampler control table.
// The hub stores the selected control's CC/range/flags; the sampler will map
// these CCs to its parameters. Add rows here and in the sampler map together.
// ============================================================
struct ControlDef {
    const char* label;
    uint16_t id;
    uint8_t cc;
    uint8_t bits;
    uint16_t minValue;
    uint16_t maxValue;
};

const ControlDef kControls[] = {
    {"Selected Pad Pitch", 0, 20, 14, 0, 16383},
    {"Master Volume", 1, 7, 7, 0, 127},
    {"Selected Pad Volume", 2, 22, 7, 0, 127},
    {"Filter Cutoff", 3, 23, 7, 0, 127},
    {"Resonance", 4, 24, 7, 0, 127},
    {"Attack", 5, 25, 7, 0, 127},
    {"Release", 6, 26, 7, 0, 127},
    {"Panic", 7, 27, 7, 0, 127},
};
constexpr int kControlCount = sizeof(kControls) / sizeof(kControls[0]);

struct PadWidgets {
    lv_obj_t* tile = nullptr;
    lv_obj_t* index = nullptr;
    lv_obj_t* note = nullptr;
    lv_obj_t* velocity = nullptr;
};

struct KnobRow {
    lv_obj_t* dropdown = nullptr;
    lv_obj_t* bar = nullptr;
    lv_obj_t* value = nullptr;
    lv_obj_t* ccLabel = nullptr;
    lv_obj_t* enableLabel = nullptr;
};

lv_obj_t* screen_ = nullptr;
lv_obj_t* monitor_ = nullptr;
lv_obj_t* knobs_ = nullptr;
lv_obj_t* tuning_ = nullptr;
lv_obj_t* calibration_ = nullptr;

lv_obj_t* tabs_[4] = {};
lv_obj_t* tabLabels_[4] = {};
lv_obj_t* topR_ = nullptr;
lv_obj_t* topL_ = nullptr;
lv_obj_t* topHub_ = nullptr;
lv_obj_t* testLabel_ = nullptr;
lv_obj_t* lastHitLabel_ = nullptr;
lv_obj_t* saveStatus_ = nullptr;

PadWidgets rightPads_[9];
PadWidgets leftPads_[7];

KnobRow knobRows_[bridge::kKnobCount];
bridge::KnobConfig knobConfig_[bridge::kKnobCount];
uint16_t knobValues_[bridge::kKnobCount] = {};
bool configLoaded_ = false;

uint32_t lastUiMs_ = 0;
uint32_t lastKnobMs_ = 0;
int activeScreen_ = 0;

// ============================================================
// Small style helpers
// ============================================================
lv_obj_t* makePanel(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t color,
                    bool border = true) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_border_width(obj, border ? 1 : 0, 0);
    lv_obj_set_style_border_color(obj, kTileBorder, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    return obj;
}

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, int x, int y,
                    const lv_font_t* font, lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    if (font != nullptr) lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, int x, int y, int w, int h,
                     lv_event_cb_t cb, void* userData) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, kAccent, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, userData);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, kFontMedium, 0);
    lv_obj_set_style_text_color(label, kText, 0);
    return button;
}

PadWidgets makePad(lv_obj_t* parent, int x, int y, int w, int h, int index,
                   const char* side, int note) {
    PadWidgets pad;
    pad.tile = makePanel(parent, x, y, w, h, kTile);
    pad.index = makeLabel(pad.tile, "", 8, 4, kFontSmall, kMuted);
    pad.note = makeLabel(pad.tile, "", 0, 0, kFontMedium, kText);
    pad.velocity = makeLabel(pad.tile, "—", 0, 0, kFontSmall, kMuted);

    lv_label_set_text_fmt(pad.index, "%s%d", side, index);
    lv_label_set_text_fmt(pad.note, "%d", note);
    lv_obj_center(pad.note);
    lv_obj_align(pad.velocity, LV_ALIGN_BOTTOM_MID, 0, -4);
    return pad;
}

void updatePad(PadWidgets& widget, sensors::PadState& pad) {
    lv_label_set_text_fmt(widget.note, "%d", pad.note);

    bool hit = pad.lastHitMs != 0 && (millis() - pad.lastHitMs) < 120;
    lv_obj_set_style_bg_color(widget.tile, hit ? kAccent : kTile, 0);
    if (hit) {
        lv_label_set_text_fmt(widget.velocity, "%u", pad.lastVelocity);
    } else {
        lv_label_set_text(widget.velocity, "—");
    }
}

// ============================================================
// Knob config
// ============================================================
int findControl(uint16_t id) {
    for (int i = 0; i < kControlCount; i++) {
        if (kControls[i].id == id) return i;
    }
    return 0;
}

void defaultKnobConfig() {
    for (int i = 0; i < bridge::kKnobCount; i++) {
        knobConfig_[i] = bridge::KnobConfig{};
        knobConfig_[i].controlId = (uint16_t)i;
        knobConfig_[i].cc = (i == 1) ? 7 : (22 + i);
        knobConfig_[i].channel = 1;
        knobConfig_[i].minValue = 0;
        knobConfig_[i].maxValue = (i == 0) ? 16383 : 127;
        knobConfig_[i].flags = (i == 0) ? 0x03 : 0x00;
    }
    // Keep knob 0 aligned with the table's 14-bit selected-pitch control.
    knobConfig_[0].cc = 20;
}

void applyConfigToRows() {
    for (int i = 0; i < bridge::kKnobCount; i++) {
        int selected = findControl(knobConfig_[i].controlId);
        lv_dropdown_set_selected(knobRows_[i].dropdown, selected);

        lv_bar_set_range(knobRows_[i].bar, 0, knobConfig_[i].maxValue);
        lv_label_set_text_fmt(knobRows_[i].ccLabel, "CC %u · %s",
                              knobConfig_[i].cc,
                              (knobConfig_[i].flags & 0x02) ? "14-bit" : "7-bit");
        lv_label_set_text(knobRows_[i].enableLabel,
                          (knobConfig_[i].flags & 0x01) ? "ON" : "OFF");
        lv_obj_set_style_text_color(knobRows_[i].enableLabel,
                                    (knobConfig_[i].flags & 0x01) ? kGreen : kMuted, 0);
    }
}

void loadConfigFromHub() {
    if (!bridge::bridge.hubOnline()) return;
    if (!bridge::bridge.readKnobConfig(knobConfig_)) return;
    configLoaded_ = true;
    applyConfigToRows();
}

// ============================================================
// Events
// ============================================================
void showScreen(int index) {
    if (index < 0 || index > 3) return;
    activeScreen_ = index;

    lv_obj_add_flag(monitor_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(knobs_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tuning_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(calibration_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* selected = nullptr;
    switch (index) {
        case 0: selected = monitor_; break;
        case 1: selected = knobs_; break;
        case 2: selected = tuning_; break;
        default: selected = calibration_; break;
    }
    lv_obj_clear_flag(selected, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(tabs_[i], i == index ? kAccent : kPanel, 0);
        lv_obj_set_style_text_color(tabLabels_[i], i == index ? kText : kMuted, 0);
    }
}

void tabEvent(lv_event_t* event) {
    int index = (int)(uintptr_t)lv_event_get_user_data(event);
    showScreen(index);
}

void testEvent(lv_event_t* event) {
    (void)event;
    bridge::bridge.setTestMode(!bridge::bridge.testMode());
}

void saveEvent(lv_event_t* event) {
    (void)event;
    bool ok = bridge::bridge.writeKnobConfig(knobConfig_) && bridge::bridge.commit();
    if (ok) {
        lv_label_set_text(saveStatus_, "SAVED TO HUB EEPROM");
        lv_obj_set_style_text_color(saveStatus_, kGreen, 0);
    } else {
        lv_label_set_text(saveStatus_, "SAVE FAILED — HUB OFFLINE");
        lv_obj_set_style_text_color(saveStatus_, kRed, 0);
    }
}

void dropdownEvent(lv_event_t* event) {
    int row = (int)(uintptr_t)lv_event_get_user_data(event);
    if (row < 0 || row >= bridge::kKnobCount) return;

    lv_obj_t* dropdown = (lv_obj_t*)lv_event_get_target(event);
    int selected = lv_dropdown_get_selected(dropdown);
    if (selected < 0 || selected >= kControlCount) return;

    const ControlDef& control = kControls[selected];
    bridge::KnobConfig& config = knobConfig_[row];

    uint8_t enabled = config.flags & 0x01;
    config.controlId = control.id;
    config.cc = control.cc;
    config.channel = 1;
    config.minValue = control.minValue;
    config.maxValue = control.maxValue;
    config.flags = enabled | (control.bits == 14 ? 0x02 : 0x00);

    lv_bar_set_range(knobRows_[row].bar, 0, config.maxValue);
    lv_label_set_text_fmt(knobRows_[row].ccLabel, "CC %u · %s",
                          config.cc, (config.flags & 0x02) ? "14-bit" : "7-bit");
}

// ============================================================
// Screen construction
// ============================================================
void buildTopBar() {
    lv_obj_t* bar = makePanel(screen_, 0, 0, 800, 54, lv_color_hex(0x0D0D0D), false);
    lv_obj_set_style_radius(bar, 0, 0);

    makeLabel(bar, "JAIBA", 16, 6, kFontLarge, kText);

    topR_ = makeLabel(bar, "R", 500, 16, kFontMedium, kMuted);
    topL_ = makeLabel(bar, "L", 544, 16, kFontMedium, kMuted);
    topHub_ = makeLabel(bar, "HUB", 590, 16, kFontSmall, kMuted);
    testLabel_ = makeLabel(bar, "TEST LEFT", 650, 16, kFontSmall, kMuted);
    lv_obj_add_flag(testLabel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(testLabel_, testEvent, LV_EVENT_CLICKED, nullptr);
}

void buildFooter() {
    static const char* names[4] = {"MONITOR", "KNOBS", "TUNING", "CAL"};
    for (int i = 0; i < 4; i++) {
        tabs_[i] = makeButton(screen_, names[i], 8 + i * 196, 426, 190, 42,
                              tabEvent, (void*)(uintptr_t)i);
        tabLabels_[i] = lv_obj_get_child(tabs_[i], 0);
    }
}

void buildMonitor() {
    monitor_ = makePanel(screen_, 0, 54, 800, 370, kBackground, false);
    lv_obj_set_style_radius(monitor_, 0, 0);

    // Right hemisphere: 9 pads, 3x3.
    lv_obj_t* right = makePanel(monitor_, 12, 8, 382, 258, kPanel);
    makeLabel(right, "RIGHT · 9", 12, 6, kFontMedium, kText);
    for (int i = 0; i < 9; i++) {
        int col = i % 3;
        int rowI = i / 3;
        rightPads_[i] = makePad(right, 10 + col * 120, 40 + rowI * 72, 110, 64,
                                i, "R", 60 + 2 * i);
    }

    // Left hemisphere: 7 pads, 4 + 3.
    lv_obj_t* left = makePanel(monitor_, 406, 8, 382, 258, kPanel);
    makeLabel(left, "LEFT · 7", 12, 6, kFontMedium, kText);
    for (int i = 0; i < 7; i++) {
        int col = i % 4;
        int rowI = i / 4;
        leftPads_[i] = makePad(left, 10 + col * 91, 44 + rowI * 78, 84, 70,
                               i, "L", 78 + 2 * i);
    }

    // 5 loop-pad placeholders (not wired yet).
    lv_obj_t* loops = makePanel(monitor_, 12, 274, 776, 50, kPanel);
    makeLabel(loops, "LOOP PADS — PENDING", 12, 14, kFontSmall, kMuted);
    for (int i = 0; i < 5; i++) {
        lv_obj_t* tile = makePanel(loops, 210 + i * 110, 8, 100, 34, kTile);
        lv_obj_t* label = makeLabel(tile, "—", 0, 6, kFontSmall, kMuted);
        lv_obj_center(label);
        lv_label_set_text_fmt(label, "LOOP %d", i);
    }

    lastHitLabel_ = makeLabel(monitor_, "No hits yet", 16, 336, kFontSmall, kMuted);
}

void buildKnobs() {
    knobs_ = makePanel(screen_, 0, 54, 800, 370, kBackground, false);
    lv_obj_set_style_radius(knobs_, 0, 0);

    makeLabel(knobs_, "KNOBS · FIXED CONTROL MAP", 16, 8, kFontMedium, kText);
    makeLabel(knobs_, "KNOB 0 = A2 TEST POT", 540, 12, kFontSmall, kMuted);

    String options;
    for (int i = 0; i < kControlCount; i++) {
        if (i > 0) options += "\n";
        options += kControls[i].label;
    }

    for (int i = 0; i < bridge::kKnobCount; i++) {
        int y = 44 + i * 50;
        lv_obj_t* row = makePanel(knobs_, 12, y, 776, 44, kPanel);
        makeLabel(row, "K", 8, 10, kFontMedium, kAccent);
        lv_obj_t* indexLabel = makeLabel(row, "0", 30, 12, kFontSmall, kMuted);
        lv_label_set_text_fmt(indexLabel, "%d", i);

        lv_obj_t* dropdown = lv_dropdown_create(row);
        lv_dropdown_set_options(dropdown, options.c_str());
        lv_obj_set_pos(dropdown, 52, 4);
        lv_obj_set_size(dropdown, 250, 36);
        lv_obj_set_style_text_font(dropdown, kFontSmall, 0);
        lv_obj_set_style_bg_color(dropdown, kTile, 0);
        lv_obj_set_style_text_color(dropdown, kText, 0);
        lv_obj_set_style_border_width(dropdown, 1, 0);
        lv_obj_set_style_border_color(dropdown, kTileBorder, 0);
        lv_obj_add_event_cb(dropdown, dropdownEvent, LV_EVENT_VALUE_CHANGED,
                            (void*)(uintptr_t)i);

        lv_obj_t* bar = lv_bar_create(row);
        lv_obj_set_pos(bar, 318, 13);
        lv_obj_set_size(bar, 190, 18);
        lv_bar_set_range(bar, 0, 127);
        lv_obj_set_style_bg_color(bar, kTile, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, kAccent, LV_PART_INDICATOR);

        lv_obj_t* value = makeLabel(row, "0", 520, 10, kFontSmall, kText);
        lv_obj_t* cc = makeLabel(row, "", 570, 12, kFontSmall, kMuted);
        lv_obj_t* enable = makeLabel(row, "OFF", 720, 12, kFontSmall, kMuted);

        knobRows_[i].dropdown = dropdown;
        knobRows_[i].bar = bar;
        knobRows_[i].value = value;
        knobRows_[i].ccLabel = cc;
        knobRows_[i].enableLabel = enable;
    }

    makeButton(knobs_, "SAVE TO HUB", 16, 348, 180, 34, saveEvent, nullptr);
    saveStatus_ = makeLabel(knobs_, "HUB OFFLINE", 210, 358, kFontSmall, kRed);
}

void buildPlaceholder(lv_obj_t** container, const char* title, const char* text) {
    *container = makePanel(screen_, 0, 54, 800, 370, kBackground, false);
    lv_obj_set_style_radius(*container, 0, 0);
    makeLabel(*container, title, 16, 8, kFontMedium, kText);
    makeLabel(*container, text, 16, 70, kFontSmall, kMuted);
}

void updateTopStatus() {
    sensors::BoardState& right = sensors::sensorLink.board(sensors::kBoardRight);
    sensors::BoardState& left = sensors::sensorLink.board(sensors::kBoardLeft);

    lv_obj_set_style_text_color(topR_, right.online() ? kGreen : kMuted, 0);
    lv_obj_set_style_text_color(topL_, left.online() ? kGreen : kMuted, 0);
    lv_obj_set_style_text_color(topHub_, bridge::bridge.hubOnline() ? kGreen : kRed, 0);
    lv_label_set_text(testLabel_, bridge::bridge.testMode() ? "TEST LEFT ON" : "TEST LEFT");
    lv_obj_set_style_text_color(testLabel_, bridge::bridge.testMode() ? kAccent : kMuted, 0);
}

void updateMonitor() {
    sensors::BoardState& right = sensors::sensorLink.board(sensors::kBoardRight);
    sensors::BoardState& left = sensors::sensorLink.board(sensors::kBoardLeft);

    for (int i = 0; i < right.padCount && i < 9; i++) {
        updatePad(rightPads_[i], right.pads[i]);
    }
    for (int i = 0; i < left.padCount && i < 7; i++) {
        updatePad(leftPads_[i], left.pads[i]);
    }

    if (right.lastHitPad >= 0) {
        lv_label_set_text_fmt(lastHitLabel_, "R · P%d · %d   |   hits %u",
                              right.lastHitPad, right.lastHitVelocity,
                              (unsigned)right.hitCount);
    } else if (left.lastHitPad >= 0) {
        lv_label_set_text_fmt(lastHitLabel_, "L · P%d · %d   |   hits %u",
                              left.lastHitPad, left.lastHitVelocity,
                              (unsigned)left.hitCount);
    }
}

void updateKnobs() {
    if (bridge::bridge.hubOnline()) {
        if (!configLoaded_) {
            loadConfigFromHub();
        }

        if (bridge::bridge.readKnobs(knobValues_)) {
            for (int i = 0; i < bridge::kKnobCount; i++) {
                lv_bar_set_value(knobRows_[i].bar, knobValues_[i], LV_ANIM_OFF);
                lv_label_set_text_fmt(knobRows_[i].value, "%u", knobValues_[i]);
            }
        }

        if (configLoaded_) {
            lv_label_set_text(saveStatus_, "HUB 0x42 ONLINE");
            lv_obj_set_style_text_color(saveStatus_, kGreen, 0);
        }
    } else {
        lv_label_set_text(saveStatus_, "HUB OFFLINE — I2C 0x42");
        lv_obj_set_style_text_color(saveStatus_, kRed, 0);
    }
}

} // namespace

void init() {
    screen_ = lv_screen_active();
    lv_obj_set_style_bg_color(screen_, kBackground, 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);

    buildTopBar();
    buildFooter();
    buildMonitor();
    buildKnobs();
    buildPlaceholder(&tuning_, "TUNING", "Per-pad threshold, curve and crosstalk — next phase.");
    buildPlaceholder(&calibration_, "CALIBRATION", "PIEZO / VELOSTAT calibration flow — next phase.");

    defaultKnobConfig();
    applyConfigToRows();
    showScreen(0);
}

void update() {
    uint32_t now = millis();
    if (lastUiMs_ != 0 && now - lastUiMs_ < 30) return;
    lastUiMs_ = now;

    updateTopStatus();
    updateMonitor();

    if (lastKnobMs_ == 0 || now - lastKnobMs_ >= 50) {
        lastKnobMs_ = now;
        updateKnobs();
    }
}

} // namespace ui
