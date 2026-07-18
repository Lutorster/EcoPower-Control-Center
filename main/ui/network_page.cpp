#include "network_page.h"

#include "settings_page.h"
#include "drivers/wifi_manager.h"
#include "lvgl.h"
#include "esp_log.h"

#include <cstdio>
#include <cstring>

static const char *TAG = "EcoPower_Network";

static lv_obj_t *g_screen = nullptr;
static lv_obj_t *g_status_text = nullptr;
static lv_obj_t *g_status_dot = nullptr;
static lv_obj_t *g_network_list = nullptr;
static lv_obj_t *g_selected_ssid_label = nullptr;
static lv_obj_t *g_selected_signal_label = nullptr;
static lv_obj_t *g_selected_security_label = nullptr;
static lv_timer_t *g_refresh_timer = nullptr;

static bool g_last_scanning = false;
static char g_selected_ssid[33] = {};
static int8_t g_selected_rssi = 0;
static bool g_selected_secured = false;

namespace {

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
    ecopower_settings_page_show();
}

void update_selected_details()
{
    if (g_selected_ssid[0] == '\0') {
        lv_label_set_text(g_selected_ssid_label, "Not selected");
        lv_label_set_text(g_selected_signal_label, "-- dBm");
        lv_label_set_text(g_selected_security_label, "---");
        return;
    }

    lv_label_set_text(g_selected_ssid_label, g_selected_ssid);

    char signal[20] = {};
    std::snprintf(signal, sizeof(signal), "%d dBm",
                  static_cast<int>(g_selected_rssi));
    lv_label_set_text(g_selected_signal_label, signal);

    lv_label_set_text(
        g_selected_security_label,
        g_selected_secured ? "Password required" : "Open network");
}

void set_selected_network(const EcoPowerWifiNetwork &network)
{
    std::snprintf(
        g_selected_ssid,
        sizeof(g_selected_ssid),
        "%s",
        network.ssid);

    g_selected_rssi = network.rssi;
    g_selected_secured = network.secured;

    update_selected_details();

    ESP_LOGI(
        TAG,
        "Selected network: SSID=%s RSSI=%d secured=%s",
        g_selected_ssid,
        static_cast<int>(g_selected_rssi),
        g_selected_secured ? "yes" : "no");
}

void network_item_event_cb(lv_event_t *event)
{
    auto *network =
        static_cast<EcoPowerWifiNetwork *>(lv_event_get_user_data(event));

    if (network == nullptr) {
        return;
    }

    set_selected_network(*network);
}

void network_item_delete_cb(lv_event_t *event)
{
    void *data = lv_event_get_user_data(event);
    if (data != nullptr) {
        lv_mem_free(data);
    }
}

void clear_network_list()
{
    if (g_network_list != nullptr) {
        lv_obj_clean(g_network_list);
    }
}

void show_scan_results()
{
    EcoPowerWifiNetwork networks[12] = {};
    const size_t count =
        ecopower_wifi_manager_get_scan_results(networks, 12);

    clear_network_list();

    if (count == 0U) {
        lv_obj_t *empty = create_label(
            g_network_list,
            "No Wi-Fi networks found",
            &lv_font_montserrat_14,
            lv_color_hex(0x8FAFC4));
        lv_obj_center(empty);
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *button = lv_btn_create(g_network_list);
        lv_obj_set_width(button, lv_pct(100));
        lv_obj_set_height(button, 44);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x0A2234), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(0x174B69), 0);
        lv_obj_set_style_shadow_width(button, 0, 0);

        char line[64] = {};
        std::snprintf(
            line,
            sizeof(line),
            "%s%s",
            networks[i].secured ? "[LOCK] " : "",
            networks[i].ssid);

        lv_obj_t *ssid = create_label(
            button, line, &lv_font_montserrat_14, lv_color_white());
        lv_obj_align(ssid, LV_ALIGN_LEFT_MID, 8, 0);

        char signal_text[20] = {};
        std::snprintf(
            signal_text,
            sizeof(signal_text),
            "%d dBm",
            static_cast<int>(networks[i].rssi));

        lv_obj_t *signal = create_label(
            button,
            signal_text,
            &lv_font_montserrat_12,
            lv_color_hex(0x65C8FF));
        lv_obj_align(signal, LV_ALIGN_RIGHT_MID, -8, 0);

        auto *stored_network =
            static_cast<EcoPowerWifiNetwork *>(
                lv_mem_alloc(sizeof(EcoPowerWifiNetwork)));

        if (stored_network != nullptr) {
            *stored_network = networks[i];

            lv_obj_add_event_cb(
                button,
                network_item_event_cb,
                LV_EVENT_CLICKED,
                stored_network);

            lv_obj_add_event_cb(
                button,
                network_item_delete_cb,
                LV_EVENT_DELETE,
                stored_network);
        }
    }
}

void refresh_timer_cb(lv_timer_t *)
{
    const bool scanning = ecopower_wifi_manager_is_scanning();

    if (scanning) {
        lv_label_set_text(g_status_text, "Scanning...");
        lv_obj_set_style_bg_color(
            g_status_dot, lv_color_hex(0xFFD21C), 0);
    } else if (g_last_scanning &&
               ecopower_wifi_manager_scan_ready()) {
        lv_label_set_text(g_status_text, "Scan complete");
        lv_obj_set_style_bg_color(
            g_status_dot, lv_color_hex(0x20A8FF), 0);
        show_scan_results();
    }

    g_last_scanning = scanning;
}

void scan_event_cb(lv_event_t *)
{
    const esp_err_t error = ecopower_wifi_manager_scan_async();

    if (error == ESP_OK) {
        lv_label_set_text(g_status_text, "Starting scan...");
        lv_obj_set_style_bg_color(
            g_status_dot, lv_color_hex(0xFFD21C), 0);

        clear_network_list();

        lv_obj_t *progress = create_label(
            g_network_list,
            "Searching for Wi-Fi networks...",
            &lv_font_montserrat_14,
            lv_color_hex(0x8FAFC4));
        lv_obj_center(progress);

        ESP_LOGI(TAG, "Wi-Fi scan requested");
    } else {
        lv_label_set_text(g_status_text, "Scan failed");
        lv_obj_set_style_bg_color(
            g_status_dot, lv_color_hex(0xFF5C5C), 0);

        ESP_LOGE(
            TAG,
            "Unable to start scan: %s",
            esp_err_to_name(error));
    }
}

void connect_event_cb(lv_event_t *)
{
    if (g_selected_ssid[0] == '\0') {
        lv_label_set_text(g_status_text, "Select a network first");
        lv_obj_set_style_bg_color(
            g_status_dot, lv_color_hex(0xFFD21C), 0);
        return;
    }

    ESP_LOGI(
        TAG,
        "CONNECT pressed for SSID: %s",
        g_selected_ssid);

    lv_label_set_text(
        g_status_text,
        "Password input is next stage");
}

void disconnect_event_cb(lv_event_t *)
{
    ESP_LOGI(TAG, "Disconnect will be enabled after connection stage");
}

lv_obj_t *create_action_button(lv_obj_t *parent,
                               int x,
                               const char *text,
                               lv_color_t color,
                               lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, 410);
    lv_obj_set_size(button, 150, 46);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *label = create_label(
        button, text, &lv_font_montserrat_14, lv_color_white());
    lv_obj_center(label);
    return button;
}

lv_obj_t *create_card(lv_obj_t *parent,
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
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = create_label(
        card, title, &lv_font_montserrat_14, lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(title_label, 0, 0);

    return card;
}

void create_detail_row(lv_obj_t *parent,
                       int y,
                       const char *caption,
                       lv_obj_t **value_label)
{
    lv_obj_t *caption_label = create_label(
        parent,
        caption,
        &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(caption_label, 0, y);

    *value_label = create_label(
        parent,
        "---",
        &lv_font_montserrat_14,
        lv_color_white());
    lv_obj_set_pos(*value_label, 0, y + 18);
    lv_label_set_long_mode(*value_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(*value_label, 220);
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
        header, "NETWORK", &lv_font_montserrat_20, lv_color_white());
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *status_card =
        create_card(g_screen, 28, 80, 744, 68, "Wi-Fi status");

    g_status_dot = lv_obj_create(status_card);
    lv_obj_remove_style_all(g_status_dot);
    lv_obj_set_pos(g_status_dot, 0, 29);
    lv_obj_set_size(g_status_dot, 12, 12);
    lv_obj_set_style_radius(g_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(
        g_status_dot, lv_color_hex(0xFF5C5C), 0);
    lv_obj_set_style_bg_opa(g_status_dot, LV_OPA_COVER, 0);

    g_status_text = create_label(
        status_card,
        ecopower_wifi_manager_is_initialized()
            ? "Ready to scan"
            : "Wi-Fi unavailable",
        &lv_font_montserrat_16,
        lv_color_white());
    lv_obj_set_pos(g_status_text, 22, 24);

    lv_obj_t *list_card =
        create_card(g_screen, 28, 162, 470, 226, "Available networks");

    g_network_list = lv_obj_create(list_card);
    lv_obj_remove_style_all(g_network_list);
    lv_obj_set_pos(g_network_list, 0, 28);
    lv_obj_set_size(g_network_list, 446, 184);
    lv_obj_set_flex_flow(g_network_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_network_list, 6, 0);
    lv_obj_set_scroll_dir(g_network_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_network_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *empty = create_label(
        g_network_list,
        "Press SCAN to search for networks",
        &lv_font_montserrat_14,
        lv_color_hex(0x8FAFC4));
    lv_obj_center(empty);

    lv_obj_t *details_card =
        create_card(g_screen, 518, 162, 254, 226, "Selected network");

    create_detail_row(
        details_card, 32, "SSID", &g_selected_ssid_label);
    create_detail_row(
        details_card, 92, "Signal", &g_selected_signal_label);
    create_detail_row(
        details_card, 152, "Security", &g_selected_security_label);

    update_selected_details();

    create_action_button(
        g_screen,
        166,
        "SCAN",
        lv_color_hex(0x1F6C96),
        scan_event_cb);

    create_action_button(
        g_screen,
        325,
        "CONNECT",
        lv_color_hex(0x16824C),
        connect_event_cb);

    create_action_button(
        g_screen,
        484,
        "DISCONNECT",
        lv_color_hex(0x8C3A3A),
        disconnect_event_cb);

    g_refresh_timer = lv_timer_create(
        refresh_timer_cb,
        250,
        nullptr);

    ESP_LOGI(TAG, "Network selection page created");
}

} // namespace

extern "C" void ecopower_network_page_show(void)
{
    if (g_screen == nullptr) {
        create_page();
    }

    lv_scr_load(g_screen);
    ESP_LOGI(TAG, "Network page shown");
}
