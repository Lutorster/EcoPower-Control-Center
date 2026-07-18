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
static lv_obj_t *g_password_overlay = nullptr;
static lv_obj_t *g_password_ssid_label = nullptr;
static lv_obj_t *g_password_textarea = nullptr;
static lv_obj_t *g_keyboard = nullptr;
static lv_timer_t *g_refresh_timer = nullptr;

static bool g_last_scanning = false;
static bool g_password_visible = false;
static EcoPowerWifiState g_last_wifi_state = ECOPOWER_WIFI_DISCONNECTED;
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

    const EcoPowerWifiState wifi_state =
        ecopower_wifi_manager_get_state();

    if (wifi_state != g_last_wifi_state) {
        switch (wifi_state) {
            case ECOPOWER_WIFI_CONNECTING:
                lv_label_set_text(
                    g_status_text,
                    "Connecting...");
                lv_obj_set_style_bg_color(
                    g_status_dot,
                    lv_color_hex(0xFFD21C),
                    0);
                break;

            case ECOPOWER_WIFI_CONNECTED: {
                char ip_address[16] = {};
                char status_line[64] = {};

                ecopower_wifi_manager_get_ip_address(
                    ip_address,
                    sizeof(ip_address));

                std::snprintf(
                    status_line,
                    sizeof(status_line),
                    "Connected - IP %s",
                    ip_address[0] != '\0'
                        ? ip_address
                        : "pending");

                lv_label_set_text(
                    g_status_text,
                    status_line);

                lv_obj_set_style_bg_color(
                    g_status_dot,
                    lv_color_hex(0x20D878),
                    0);

                char signal[20] = {};
                std::snprintf(
                    signal,
                    sizeof(signal),
                    "%d dBm",
                    static_cast<int>(
                        ecopower_wifi_manager_get_rssi()));

                lv_label_set_text(
                    g_selected_signal_label,
                    signal);

                ESP_LOGI(
                    TAG,
                    "Wi-Fi connected, IP=%s",
                    ip_address);
                break;
            }

            case ECOPOWER_WIFI_FAILED:
                lv_label_set_text(
                    g_status_text,
                    "Connection failed - check password");
                lv_obj_set_style_bg_color(
                    g_status_dot,
                    lv_color_hex(0xFF5C5C),
                    0);
                break;

            case ECOPOWER_WIFI_DISCONNECTED:
            default:
                lv_label_set_text(
                    g_status_text,
                    "Disconnected");
                lv_obj_set_style_bg_color(
                    g_status_dot,
                    lv_color_hex(0x8FAFC4),
                    0);
                break;
        }

        g_last_wifi_state = wifi_state;
    }
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

void password_visibility_event_cb(lv_event_t *)
{
    g_password_visible = !g_password_visible;
    lv_textarea_set_password_mode(
        g_password_textarea,
        !g_password_visible);

    ESP_LOGI(
        TAG,
        "Password visibility: %s",
        g_password_visible ? "shown" : "hidden");
}

void hide_password_dialog()
{
    if (g_password_overlay != nullptr) {
        lv_obj_add_flag(g_password_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    if (g_password_textarea != nullptr) {
        lv_textarea_set_text(g_password_textarea, "");
        g_password_visible = false;
        lv_textarea_set_password_mode(g_password_textarea, true);
    }
}

void password_keyboard_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_READY) {
        const char *password =
            lv_textarea_get_text(g_password_textarea);

        const size_t password_length =
            password != nullptr ? std::strlen(password) : 0U;

        if (g_selected_secured && password_length < 8U) {
            lv_label_set_text(
                g_status_text,
                "Password must contain at least 8 characters");
            lv_obj_set_style_bg_color(
                g_status_dot, lv_color_hex(0xFFD21C), 0);
            return;
        }

        ESP_LOGI(
            TAG,
            "Connecting to SSID=%s, password length=%u",
            g_selected_ssid,
            static_cast<unsigned>(password_length));

        const esp_err_t connect_error =
            ecopower_wifi_manager_connect(
                g_selected_ssid,
                password);

        if (connect_error != ESP_OK) {
            lv_label_set_text(
                g_status_text,
                "Unable to start Wi-Fi connection");
            lv_obj_set_style_bg_color(
                g_status_dot, lv_color_hex(0xFF5C5C), 0);

            ESP_LOGE(
                TAG,
                "Wi-Fi connect failed to start: %s",
                esp_err_to_name(connect_error));
            return;
        }

        hide_password_dialog();

        lv_label_set_text(
            g_status_text,
            "Connecting...");
        lv_obj_set_style_bg_color(
            g_status_dot, lv_color_hex(0xFFD21C), 0);
    } else if (code == LV_EVENT_CANCEL) {
        ESP_LOGI(TAG, "Password input cancelled");
        hide_password_dialog();

        lv_label_set_text(g_status_text, "Password input cancelled");
        lv_obj_set_style_bg_color(
            g_status_dot, lv_color_hex(0x8FAFC4), 0);
    }
}

void password_textarea_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_FOCUSED &&
        g_keyboard != nullptr) {
        lv_keyboard_set_textarea(
            g_keyboard,
            static_cast<lv_obj_t *>(lv_event_get_target(event)));
    }
}

void show_password_dialog()
{
    if (g_password_overlay == nullptr ||
        g_password_textarea == nullptr ||
        g_keyboard == nullptr) {
        return;
    }

    lv_textarea_set_text(g_password_textarea, "");
    g_password_visible = false;
    lv_textarea_set_password_mode(g_password_textarea, true);
    lv_label_set_text(g_password_ssid_label, g_selected_ssid);
    lv_keyboard_set_textarea(g_keyboard, g_password_textarea);
    lv_obj_clear_flag(g_password_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_password_overlay);

    lv_label_set_text(g_status_text, "Enter Wi-Fi password");
    lv_obj_set_style_bg_color(
        g_status_dot, lv_color_hex(0xFFD21C), 0);
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

    if (g_selected_secured) {
        show_password_dialog();
    } else {
        const esp_err_t connect_error =
            ecopower_wifi_manager_connect(
                g_selected_ssid,
                "");

        if (connect_error == ESP_OK) {
            lv_label_set_text(
                g_status_text,
                "Connecting...");
            lv_obj_set_style_bg_color(
                g_status_dot, lv_color_hex(0xFFD21C), 0);
        } else {
            lv_label_set_text(
                g_status_text,
                "Unable to start Wi-Fi connection");
            lv_obj_set_style_bg_color(
                g_status_dot, lv_color_hex(0xFF5C5C), 0);
        }
    }
}

void disconnect_event_cb(lv_event_t *)
{
    const esp_err_t error =
        ecopower_wifi_manager_disconnect();

    if (error == ESP_OK) {
        lv_label_set_text(g_status_text, "Disconnected");
        lv_obj_set_style_bg_color(
            g_status_dot, lv_color_hex(0x8FAFC4), 0);

        ESP_LOGI(TAG, "Wi-Fi disconnect requested");
    } else {
        lv_label_set_text(
            g_status_text,
            "Disconnect failed");
        lv_obj_set_style_bg_color(
            g_status_dot, lv_color_hex(0xFF5C5C), 0);

        ESP_LOGE(
            TAG,
            "Wi-Fi disconnect failed: %s",
            esp_err_to_name(error));
    }
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

    g_password_overlay = lv_obj_create(g_screen);
    lv_obj_remove_style_all(g_password_overlay);
    lv_obj_set_pos(g_password_overlay, 0, 0);
    lv_obj_set_size(g_password_overlay, 800, 480);
    lv_obj_set_style_bg_color(
        g_password_overlay, lv_color_hex(0x020812), 0);
    lv_obj_set_style_bg_opa(
        g_password_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_password_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * Compact password panel at the top.
     * The keyboard occupies the complete remaining screen area.
     */
    lv_obj_t *password_card =
        lv_obj_create(g_password_overlay);
    lv_obj_set_pos(password_card, 8, 6);
    lv_obj_set_size(password_card, 784, 104);
    lv_obj_set_style_radius(password_card, 10, 0);
    lv_obj_set_style_bg_color(
        password_card, lv_color_hex(0x071725), 0);
    lv_obj_set_style_bg_opa(password_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(password_card, 1, 0);
    lv_obj_set_style_border_color(
        password_card, lv_color_hex(0x1E5578), 0);
    lv_obj_set_style_pad_all(password_card, 10, 0);
    lv_obj_clear_flag(password_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *password_title = create_label(
        password_card,
        "WI-FI PASSWORD",
        &lv_font_montserrat_16,
        lv_color_white());
    lv_obj_set_pos(password_title, 0, 0);

    lv_obj_t *password_ssid_caption = create_label(
        password_card,
        "Network:",
        &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(password_ssid_caption, 185, 3);

    g_password_ssid_label = create_label(
        password_card,
        g_selected_ssid,
        &lv_font_montserrat_14,
        lv_color_hex(0x65C8FF));
    lv_obj_set_pos(g_password_ssid_label, 255, 1);
    lv_label_set_long_mode(
        g_password_ssid_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_password_ssid_label, 500);

    lv_obj_t *password_caption = create_label(
        password_card,
        "Password",
        &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(password_caption, 0, 36);

    g_password_textarea = lv_textarea_create(password_card);
    lv_obj_set_pos(g_password_textarea, 85, 29);
    lv_obj_set_size(g_password_textarea, 615, 50);
    lv_textarea_set_one_line(g_password_textarea, true);
    lv_textarea_set_password_mode(g_password_textarea, true);
    lv_textarea_set_max_length(g_password_textarea, 63);
    lv_textarea_set_placeholder_text(
        g_password_textarea, "Enter password");

    lv_obj_set_style_text_font(
        g_password_textarea,
        &lv_font_montserrat_16,
        LV_PART_MAIN);

    /* Light password characters/stars on the dark input field. */
    lv_obj_set_style_text_color(
        g_password_textarea,
        lv_color_hex(0xEAF6FF),
        LV_PART_MAIN);

    lv_obj_set_style_text_color(
        g_password_textarea,
        lv_color_hex(0xEAF6FF),
        LV_PART_CURSOR);

    lv_obj_set_style_radius(g_password_textarea, 8, 0);
    lv_obj_set_style_bg_color(
        g_password_textarea, lv_color_hex(0x0A2234), 0);
    lv_obj_set_style_bg_opa(
        g_password_textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(
        g_password_textarea, 1, 0);
    lv_obj_set_style_border_color(
        g_password_textarea, lv_color_hex(0x2B769F), 0);

    lv_obj_add_event_cb(
        g_password_textarea,
        password_textarea_event_cb,
        LV_EVENT_FOCUSED,
        nullptr);

    lv_obj_t *show_button = lv_btn_create(password_card);
    lv_obj_set_pos(show_button, 708, 29);
    lv_obj_set_size(show_button, 52, 50);
    lv_obj_set_style_radius(show_button, 8, 0);
    lv_obj_set_style_bg_color(
        show_button, lv_color_hex(0x12324A), 0);
    lv_obj_set_style_bg_color(
        show_button,
        lv_color_hex(0x1F6C96),
        LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(show_button, 0, 0);
    lv_obj_add_event_cb(
        show_button,
        password_visibility_event_cb,
        LV_EVENT_CLICKED,
        nullptr);

    lv_obj_t *show_label = create_label(
        show_button,
        "SHOW",
        &lv_font_montserrat_12,
        lv_color_hex(0xEAF6FF));
    lv_obj_center(show_label);

    /*
     * Keyboard starts directly below the compact panel.
     * 800x480 screen: Y=114, H=360 leaves a 6 px bottom margin.
     */
    g_keyboard = lv_keyboard_create(g_password_overlay);
    lv_obj_set_pos(g_keyboard, 0, 0);
    lv_obj_set_size(g_keyboard, 790, 360);
    lv_keyboard_set_mode(
        g_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(
        g_keyboard, g_password_textarea);

    lv_obj_set_style_bg_color(
        g_keyboard, lv_color_hex(0x071725), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(
        g_keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(
        g_keyboard, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(
        g_keyboard, lv_color_hex(0x1E5578), LV_PART_MAIN);
    lv_obj_set_style_radius(
        g_keyboard, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(
        g_keyboard, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(
        g_keyboard, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(
        g_keyboard, 4, LV_PART_MAIN);

    lv_obj_set_style_bg_color(
        g_keyboard, lv_color_hex(0x12324A), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(
        g_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(
        g_keyboard, lv_color_hex(0xEAF6FF), LV_PART_ITEMS);
    lv_obj_set_style_text_font(
        g_keyboard, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_radius(
        g_keyboard, 6, LV_PART_ITEMS);
    lv_obj_set_style_border_width(
        g_keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(
        g_keyboard, lv_color_hex(0x2B769F), LV_PART_ITEMS);

    const lv_style_selector_t items_pressed =
        static_cast<lv_style_selector_t>(LV_PART_ITEMS) |
        static_cast<lv_style_selector_t>(LV_STATE_PRESSED);
    const lv_style_selector_t items_checked =
        static_cast<lv_style_selector_t>(LV_PART_ITEMS) |
        static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
    const lv_style_selector_t items_disabled =
        static_cast<lv_style_selector_t>(LV_PART_ITEMS) |
        static_cast<lv_style_selector_t>(LV_STATE_DISABLED);

    lv_obj_set_style_bg_color(
        g_keyboard, lv_color_hex(0x1F6C96), items_pressed);
    lv_obj_set_style_text_color(
        g_keyboard, lv_color_hex(0xEAF6FF), items_pressed);

    lv_obj_set_style_bg_color(
        g_keyboard, lv_color_hex(0x174B69), items_checked);
    lv_obj_set_style_text_color(
        g_keyboard, lv_color_hex(0xEAF6FF), items_checked);

    lv_obj_set_style_bg_color(
        g_keyboard, lv_color_hex(0x12324A), items_disabled);
    lv_obj_set_style_text_color(
        g_keyboard, lv_color_hex(0x8FAFC4), items_disabled);
    lv_obj_set_style_bg_opa(
        g_keyboard, LV_OPA_COVER, items_disabled);

    lv_obj_add_event_cb(
        g_keyboard,
        password_keyboard_event_cb,
        LV_EVENT_READY,
        nullptr);
    lv_obj_add_event_cb(
        g_keyboard,
        password_keyboard_event_cb,
        LV_EVENT_CANCEL,
        nullptr);

    lv_obj_add_flag(
        g_password_overlay, LV_OBJ_FLAG_HIDDEN);

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
