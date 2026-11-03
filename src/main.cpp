// Jaiba Hexa Drum — ESP32-S3 CrowPanel 7.0" UI
//
// Display: 800x480 RGB (EK9716BD3 + EK73002ACGB), LovyanGFX RGB bus.
// Touch:   GT911 on Wire1 (SDA 19, SCL 20), PCA9557 reset at 0x18.
// UI:      LVGL 9.1, two full-frame buffers in PSRAM.
//
// Phase 1: Monitor + Knobs screens fed by the Teensy control hub over I2C.

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lvgl.h>

#include "bridge_transport.h"
#include "sensor_link.h"
#include "ui.h"

namespace {

constexpr uint32_t kScreenW = 800;
constexpr uint32_t kScreenH = 480;
constexpr uint8_t kBacklightPin = 2;

class LGFX : public lgfx::LGFX_Device {
public:
    lgfx::Bus_RGB bus;
    lgfx::Panel_RGB panel;

    LGFX() {
        auto busConfig = bus.config();
        busConfig.panel = &panel;

        busConfig.pin_d0  = GPIO_NUM_15; // B0
        busConfig.pin_d1  = GPIO_NUM_7;  // B1
        busConfig.pin_d2  = GPIO_NUM_6;  // B2
        busConfig.pin_d3  = GPIO_NUM_5;  // B3
        busConfig.pin_d4  = GPIO_NUM_4;  // B4
        busConfig.pin_d5  = GPIO_NUM_9;  // G0
        busConfig.pin_d6  = GPIO_NUM_46; // G1
        busConfig.pin_d7  = GPIO_NUM_3;  // G2
        busConfig.pin_d8  = GPIO_NUM_8;  // G3
        busConfig.pin_d9  = GPIO_NUM_16; // G4
        busConfig.pin_d10 = GPIO_NUM_1;  // G5
        busConfig.pin_d11 = GPIO_NUM_14; // R0
        busConfig.pin_d12 = GPIO_NUM_21; // R1
        busConfig.pin_d13 = GPIO_NUM_47; // R2
        busConfig.pin_d14 = GPIO_NUM_48; // R3
        busConfig.pin_d15 = GPIO_NUM_45; // R4
        busConfig.pin_henable = GPIO_NUM_41;
        busConfig.pin_vsync   = GPIO_NUM_40;
        busConfig.pin_hsync   = GPIO_NUM_39;
        busConfig.pin_pclk    = GPIO_NUM_0;
        busConfig.freq_write  = 24000000;
        busConfig.hsync_polarity    = 0;
        busConfig.hsync_front_porch = 40;
        busConfig.hsync_pulse_width = 48;
        busConfig.hsync_back_porch  = 40;
        busConfig.vsync_polarity    = 0;
        busConfig.vsync_front_porch = 1;
        busConfig.vsync_pulse_width = 31;
        busConfig.vsync_back_porch  = 13;
        busConfig.pclk_active_neg   = 1;
        busConfig.de_idle_high      = 0;
        busConfig.pclk_idle_high    = 0;
        bus.config(busConfig);

        auto panelConfig = panel.config();
        panelConfig.memory_width  = kScreenW;
        panelConfig.memory_height = kScreenH;
        panelConfig.panel_width   = kScreenW;
        panelConfig.panel_height  = kScreenH;
        panelConfig.offset_x = 0;
        panelConfig.offset_y = 0;
        panel.config(panelConfig);

        panel.setBus(&bus);
        setPanel(&panel);
    }
};

} // namespace

LGFX lcd;

// touch.h uses the global `lcd` for its coordinate clamp, so it must be
// included after the LGFX object above.
#include "touch.h"

namespace {

lv_display_t* display = nullptr;
lv_indev_t*   touchpad = nullptr;

uint32_t tickGet() {
    return millis();
}

void displayFlush(lv_display_t* disp, const lv_area_t* area, uint8_t* pixelMap) {
    (void)area;
    if (!lcd.bus.presentFrameBuffer(pixelMap)) {
        Serial.println("LovyanGFX VSYNC frame switch timeout");
    }
    lv_display_flush_ready(disp);
}

void touchpadRead(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    if (touch_touched()) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = touch_last_x;
        data->point.y = touch_last_y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(200);

    Wire1.begin(19, 20);
    Wire1.setClock(400000);

    if (!lcd.begin()) {
        Serial.println("lcd.begin() failed");
        return;
    }
    delay(200);

    Wire1.end();
    delay(10);
    Wire1.begin(19, 20);
    Wire1.setClock(400000);

    lv_init();
    lv_tick_set_cb(tickGet);
    touch_init();

    lv_color_t* frameBuffer0 = reinterpret_cast<lv_color_t*>(lcd.bus.getFrameBuffer(0));
    lv_color_t* frameBuffer1 = reinterpret_cast<lv_color_t*>(lcd.bus.getFrameBuffer(1));
    Serial.printf("RGB frame buffers: %p, %p\n", frameBuffer0, frameBuffer1);
    if (frameBuffer0 == nullptr || frameBuffer1 == nullptr) {
        Serial.println("RGB double frame buffer allocation failed");
        return;
    }

    display = lv_display_create(lcd.width(), lcd.height());
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, displayFlush);
    lv_display_set_buffers(display, frameBuffer0, frameBuffer1,
                           kScreenW * kScreenH * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_FULL);

    touchpad = lv_indev_create();
    lv_indev_set_type(touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touchpad, touchpadRead);

    bridge::bridge.begin();
    sensors::sensorLink.begin();
    ui::init();

    pinMode(kBacklightPin, OUTPUT);
    digitalWrite(kBacklightPin, HIGH);

    Serial.println("Jaiba ESP32 UI — Phase 1: hub monitor + knob config");
}

void loop() {
    sensors::sensorLink.poll();
    ui::update();
    lv_timer_handler();
    delay(5);
}
