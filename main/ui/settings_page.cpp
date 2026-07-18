#include "settings_page.h"

#include "dashboard.h"
#include "network_page.h"
#include "lvgl.h"
#include "esp_log.h"

#include <cstdint>

static const char *TAG = "EcoPower_Settings";
static lv_obj_t *g_screen = nullptr;

namespace {

enum class SettingsItem : intptr_t {
    Network = 0,
    TimeDate,
    Display,
    System,
    About,
};

lv_obj_t *create_label(lv_obj_t *parent,
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

void back_event_cb(lv_event_t *)
{
    ecopower_dashboard_show();
}

void item_event_cb(lv_event_t *event)
{
    const auto item = static_cast<SettingsItem>(
        reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));

    switch (item) {
        case SettingsItem::Network:
            ESP_LOGI(TAG, "Network settings selected");
            ecopower_network_page_show();
            break;
        case SettingsItem::TimeDate:
            ESP_LOGI(TAG, "Time & Date settings selected");
            break;
        case SettingsItem::Display:
            ESP_LOGI(TAG, "Display settings selected");
            break;
        case SettingsItem::System:
            ESP_LOGI(TAG, "System settings selected");
            break;
        case SettingsItem::About:
            ESP_LOGI(TAG, "About selected");
            break;
    }
}

lv_obj_t *create_settings_button(lv_obj_t *parent,
                                 int x,
                                 int y,
                                 const char *title,
                                 const char *subtitle,
                                 SettingsItem item)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, 342, 68);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x071725), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x1E5578), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(
        button,
        item_event_cb,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(static_cast<intptr_t>(item)));

    lv_obj_t *title_label = create_label(
        button, title, &lv_font_montserrat_16, lv_color_white());
    lv_obj_set_pos(title_label, 16, 10);

    lv_obj_t *subtitle_label = create_label(
        button, subtitle, &lv_font_montserrat_12, lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(subtitle_label, 16, 37);

    lv_obj_t *arrow = create_label(
        button, ">", &lv_font_montserrat_20, lv_color_hex(0x22B8FF));
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -16, 0);

    return button;
}

void create_page()
{
    g_screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(g_screen);
    lv_obj_set_size(g_screen, 800, 480);
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(0x020812), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Header
    lv_obj_t *header = lv_obj_create(g_screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_pos(header, 0, 0);
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
    lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *back_label = create_label(
        back, "< BACK", &lv_font_montserrat_14, lv_color_white());
    lv_obj_center(back_label);

    lv_obj_t *title = create_label(
        header, "SETTINGS", &lv_font_montserrat_20, lv_color_white());
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *subtitle = create_label(
        g_screen,
        "EcoPower OS configuration",
        &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(subtitle, 28, 78);

    create_settings_button(
        g_screen, 28, 108,
        "Network",
        "Wi-Fi connection, IP address and signal",
        SettingsItem::Network);

    create_settings_button(
        g_screen, 430, 108,
        "Time & Date",
        "Automatic NTP or manual clock setup",
        SettingsItem::TimeDate);

    create_settings_button(
        g_screen, 28, 194,
        "Display",
        "Brightness and screen preferences",
        SettingsItem::Display);

    create_settings_button(
        g_screen, 430, 194,
        "System",
        "Diagnostics, restart and storage",
        SettingsItem::System);

    create_settings_button(
        g_screen, 28, 280,
        "About",
        "Version and device information",
        SettingsItem::About);

    lv_obj_t *status_panel = lv_obj_create(g_screen);
    lv_obj_set_pos(status_panel, 430, 280);
    lv_obj_set_size(status_panel, 342, 126);
    lv_obj_set_style_radius(status_panel, 12, 0);
    lv_obj_set_style_bg_color(status_panel, lv_color_hex(0x071725), 0);
    lv_obj_set_style_bg_opa(status_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_panel, 1, 0);
    lv_obj_set_style_border_color(status_panel, lv_color_hex(0x1E5578), 0);
    lv_obj_set_style_pad_all(status_panel, 14, 0);
    lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status_title = create_label(
        status_panel, "Current status", &lv_font_montserrat_16, lv_color_white());
    lv_obj_set_pos(status_title, 0, 0);

    lv_obj_t *status = create_label(
        status_panel,
        "Wi-Fi: not configured\n"
        "Time: not synchronized\n"
        "Mode: local",
        &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(status, 0, 30);

    ESP_LOGI(TAG, "Settings page created");
}

} // namespace

extern "C" void ecopower_settings_page_show(void)
{
    if (g_screen == nullptr) {
        create_page();
    }

    lv_scr_load(g_screen);
    ESP_LOGI(TAG, "Settings page shown");
}
