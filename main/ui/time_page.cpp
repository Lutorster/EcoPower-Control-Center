#include "time_page.h"

#include "settings_page.h"
#include "drivers/time_manager.h"
#include "drivers/wifi_manager.h"

#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "EcoPower_TimeUI";

namespace {

lv_obj_t *g_screen = nullptr;
lv_obj_t *g_datetime = nullptr;
lv_obj_t *g_ntp_status = nullptr;
lv_obj_t *g_wifi_status = nullptr;
lv_obj_t *g_action_status = nullptr;
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

void update_values()
{
    char datetime[32] = {};
    const bool have_time =
        ecopower_time_manager_get_datetime(
            datetime,
            sizeof(datetime));

    lv_label_set_text(
        g_datetime,
        have_time ? datetime : "--.--.---- --:--:--");

    const bool synchronized =
        ecopower_time_manager_is_synchronized();

    lv_label_set_text(
        g_ntp_status,
        synchronized ? "SYNCHRONIZED" : "WAITING");

    lv_obj_set_style_text_color(
        g_ntp_status,
        synchronized
            ? lv_color_hex(0x20D878)
            : lv_color_hex(0xFFD21C),
        0);

    const bool wifi_connected =
        ecopower_wifi_manager_get_state() ==
        ECOPOWER_WIFI_CONNECTED;

    lv_label_set_text(
        g_wifi_status,
        wifi_connected ? "CONNECTED" : "DISCONNECTED");

    lv_obj_set_style_text_color(
        g_wifi_status,
        wifi_connected
            ? lv_color_hex(0x20D878)
            : lv_color_hex(0xFF5C5C),
        0);
}

void timer_cb(lv_timer_t *)
{
    update_values();
}

void back_cb(lv_event_t *)
{
    ecopower_settings_page_show();
}

void sync_cb(lv_event_t *)
{
    const esp_err_t error =
        ecopower_time_manager_resynchronize();

    if (error == ESP_OK) {
        lv_label_set_text(
            g_action_status,
            "Synchronization requested");
        lv_obj_set_style_text_color(
            g_action_status,
            lv_color_hex(0x20D878),
            0);
    } else {
        lv_label_set_text(
            g_action_status,
            "Unable to start synchronization");
        lv_obj_set_style_text_color(
            g_action_status,
            lv_color_hex(0xFF5C5C),
            0);
    }

    ESP_LOGI(
        TAG,
        "Manual synchronization result: %s",
        esp_err_to_name(error));
}

lv_obj_t *make_card(
    lv_obj_t *parent,
    int x,
    int y,
    int width,
    int height,
    const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x071725), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x1E5578), 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading = make_label(
        card,
        title,
        &lv_font_montserrat_16,
        lv_color_white());
    lv_obj_set_pos(heading, 0, 0);
    return card;
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
        header,
        "TIME & DATE",
        &lv_font_montserrat_20,
        lv_color_white());
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *clock_card =
        make_card(g_screen, 28, 88, 744, 130, "Current local time");

    g_datetime = make_label(
        clock_card,
        "--.--.---- --:--:--",
        &lv_font_montserrat_28,
        lv_color_white());
    lv_obj_align(g_datetime, LV_ALIGN_CENTER, 0, 15);

    lv_obj_t *status_card =
        make_card(g_screen, 28, 238, 360, 142, "Synchronization");

    lv_obj_t *ntp_caption = make_label(
        status_card, "NTP status", &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(ntp_caption, 0, 40);

    g_ntp_status = make_label(
        status_card, "---", &lv_font_montserrat_14, lv_color_white());
    lv_obj_set_pos(g_ntp_status, 135, 38);

    lv_obj_t *wifi_caption = make_label(
        status_card, "Wi-Fi", &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(wifi_caption, 0, 78);

    g_wifi_status = make_label(
        status_card, "---", &lv_font_montserrat_14, lv_color_white());
    lv_obj_set_pos(g_wifi_status, 135, 76);

    lv_obj_t *info_card =
        make_card(g_screen, 410, 238, 362, 142, "Configuration");

    lv_obj_t *info = make_label(
        info_card,
        "Timezone: Europe/Kyiv\n"
        "Primary: pool.ntp.org\n"
        "Secondary: time.google.com\n"
        "Mode: smooth synchronization",
        &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(info, 0, 36);

    lv_obj_t *sync_button = lv_btn_create(g_screen);
    lv_obj_set_pos(sync_button, 410, 401);
    lv_obj_set_size(sync_button, 200, 50);
    lv_obj_set_style_radius(sync_button, 10, 0);
    lv_obj_set_style_bg_color(sync_button, lv_color_hex(0x1F6C96), 0);
    lv_obj_set_style_shadow_width(sync_button, 0, 0);
    lv_obj_add_event_cb(sync_button, sync_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *sync_text = make_label(
        sync_button,
        "SYNC NOW",
        &lv_font_montserrat_16,
        lv_color_white());
    lv_obj_center(sync_text);

    g_action_status = make_label(
        g_screen,
        "",
        &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(g_action_status, 28, 417);

    g_timer = lv_timer_create(timer_cb, 1000, nullptr);
    update_values();

    ESP_LOGI(TAG, "Time & Date page created");
}

} // namespace

extern "C" void ecopower_time_page_show(void)
{
    if (g_screen == nullptr) {
        create_page();
    }

    update_values();
    lv_scr_load(g_screen);
    ESP_LOGI(TAG, "Time & Date page shown");
}
