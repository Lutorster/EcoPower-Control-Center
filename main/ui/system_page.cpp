#include "system_page.h"

#include "settings_page.h"
#include "app/app_version.h"
#include "drivers/time_manager.h"
#include "drivers/wifi_manager.h"
#include "drivers/rs485_port.h"
#include "drivers/mqtt_manager.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include <cstdio>

static const char *TAG = "EcoPower_SystemUI";

namespace {

lv_obj_t *g_screen = nullptr;
lv_obj_t *g_info = nullptr;
lv_timer_t *g_timer = nullptr;

lv_obj_t *make_label(
    lv_obj_t *parent,
    const char *text,
    const lv_font_t *font,
    lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

const char *mqtt_state_text(EcoPowerMqttState state)
{
    switch (state) {
        case ECOPOWER_MQTT_CONNECTING:
            return "Connecting";
        case ECOPOWER_MQTT_CONNECTED:
            return "Connected";
        case ECOPOWER_MQTT_ERROR:
            return "Error";
        case ECOPOWER_MQTT_DISABLED:
            return "Not configured";
        case ECOPOWER_MQTT_DISCONNECTED:
        default:
            return "Disconnected";
    }
}

void update_info()
{
    if (g_info == nullptr) {
        return;
    }

    const size_t psram_total =
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t psram_free =
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t internal_free =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    const uint64_t uptime_seconds =
        static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL;

    const uint32_t days =
        static_cast<uint32_t>(uptime_seconds / 86400ULL);
    const uint32_t hours =
        static_cast<uint32_t>((uptime_seconds % 86400ULL) / 3600ULL);
    const uint32_t minutes =
        static_cast<uint32_t>((uptime_seconds % 3600ULL) / 60ULL);

    char text[640] = {};
    snprintf(
        text,
        sizeof(text),
        "EcoPower OS: %s\n"
        "ESP-IDF: %s\n"
        "Hardware Flash: 8 MB\n"
        "PSRAM: %.1f MB total / %.1f MB free\n"
        "Internal RAM free: %.1f KB\n"
        "Uptime: %lud %02luh %02lum\n\n"
        "Wi-Fi: %s\n"
        "NTP: %s\n"
        "RS485: %s\n"
        "MQTT: %s",
        ECOPOWER_APP_VERSION,
        esp_get_idf_version(),
        static_cast<double>(psram_total) / (1024.0 * 1024.0),
        static_cast<double>(psram_free) / (1024.0 * 1024.0),
        static_cast<double>(internal_free) / 1024.0,
        static_cast<unsigned long>(days),
        static_cast<unsigned long>(hours),
        static_cast<unsigned long>(minutes),
        ecopower_wifi_manager_get_state() == ECOPOWER_WIFI_CONNECTED
            ? "Connected"
            : "Disconnected",
        ecopower_time_manager_is_synchronized()
            ? "Synchronized"
            : "Not synchronized",
        ecopower_rs485_is_initialized()
            ? "Ready"
            : "Not initialized",
        mqtt_state_text(ecopower_mqtt_manager_get_state()));

    lv_label_set_text(g_info, text);
}

void timer_cb(lv_timer_t *)
{
    update_info();
}

void back_cb(lv_event_t *)
{
    ecopower_settings_page_show();
}

void restart_cb(lv_event_t *)
{
    ESP_LOGW(TAG, "System restart requested from UI");
    esp_restart();
}

void create_page()
{
    g_screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(g_screen);
    lv_obj_set_size(g_screen, 800, 480);
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(0x020812), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(g_screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, 800, 64);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x07131E), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x123C57), 0);

    lv_obj_t *back = lv_btn_create(header);
    lv_obj_set_pos(back, 14, 10);
    lv_obj_set_size(back, 84, 44);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x12324A), 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *back_text = make_label(
        back, "< BACK", &lv_font_montserrat_14, lv_color_white());
    lv_obj_center(back_text);

    lv_obj_t *title = make_label(
        header, "SYSTEM", &lv_font_montserrat_20, lv_color_white());
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *panel = lv_obj_create(g_screen);
    lv_obj_set_pos(panel, 28, 88);
    lv_obj_set_size(panel, 744, 300);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x071725), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x1E5578), 0);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    g_info = make_label(
        panel,
        "Loading system information...",
        &lv_font_montserrat_14,
        lv_color_hex(0xDCE8F2));
    lv_obj_set_pos(g_info, 0, 0);
    lv_obj_set_width(g_info, 700);
    lv_obj_set_style_text_line_space(g_info, 7, 0);

    lv_obj_t *restart = lv_btn_create(g_screen);
    lv_obj_set_pos(restart, 572, 406);
    lv_obj_set_size(restart, 200, 50);
    lv_obj_set_style_radius(restart, 10, 0);
    lv_obj_set_style_bg_color(restart, lv_color_hex(0x8C3A3A), 0);
    lv_obj_set_style_shadow_width(restart, 0, 0);
    lv_obj_add_event_cb(restart, restart_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *restart_text = make_label(
        restart,
        "RESTART DEVICE",
        &lv_font_montserrat_14,
        lv_color_white());
    lv_obj_center(restart_text);

    lv_obj_t *note = make_label(
        g_screen,
        "Restart is immediate. Network and saved settings remain unchanged.",
        &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(note, 28, 424);

    g_timer = lv_timer_create(timer_cb, 1000, nullptr);
    update_info();

    ESP_LOGI(TAG, "System page created");
}

} // namespace

extern "C" void ecopower_system_page_show(void)
{
    if (g_screen == nullptr) {
        create_page();
    }

    update_info();
    lv_scr_load(g_screen);
    ESP_LOGI(TAG, "System page shown");
}
