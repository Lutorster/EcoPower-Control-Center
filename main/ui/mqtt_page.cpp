#include "mqtt_page.h"

#include "settings_page.h"
#include "drivers/mqtt_manager.h"

#include "lvgl.h"
#include "esp_log.h"

#include <cstdio>

static const char *TAG = "EcoPower_MQTT_UI";

namespace {

lv_obj_t *g_screen = nullptr;
lv_obj_t *g_uri = nullptr;
lv_obj_t *g_user = nullptr;
lv_obj_t *g_password = nullptr;
lv_obj_t *g_topic = nullptr;
lv_obj_t *g_status = nullptr;
lv_obj_t *g_keyboard = nullptr;
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

const char *state_text(EcoPowerMqttState state)
{
    switch (state) {
        case ECOPOWER_MQTT_CONNECTING: return "CONNECTING";
        case ECOPOWER_MQTT_CONNECTED: return "CONNECTED";
        case ECOPOWER_MQTT_ERROR: return "ERROR";
        case ECOPOWER_MQTT_DISCONNECTED: return "DISCONNECTED";
        case ECOPOWER_MQTT_DISABLED:
        default: return "NOT CONFIGURED";
    }
}

lv_color_t state_color(EcoPowerMqttState state)
{
    switch (state) {
        case ECOPOWER_MQTT_CONNECTED:
            return lv_color_hex(0x20D878);
        case ECOPOWER_MQTT_CONNECTING:
            return lv_color_hex(0xFFD21C);
        case ECOPOWER_MQTT_ERROR:
            return lv_color_hex(0xFF5C5C);
        default:
            return lv_color_hex(0x8FAFC4);
    }
}

lv_obj_t *make_input(
    lv_obj_t *parent,
    int x,
    int y,
    int width,
    const char *caption,
    const char *placeholder,
    bool password)
{
    lv_obj_t *label = make_label(
        parent,
        caption,
        &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(label, x, y);

    lv_obj_t *input = lv_textarea_create(parent);
    lv_obj_set_pos(input, x, y + 18);
    lv_obj_set_size(input, width, 44);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_placeholder_text(input, placeholder);
    lv_textarea_set_password_mode(input, password);
    lv_obj_set_style_text_font(input, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(input, lv_color_hex(0x0A2234), 0);
    lv_obj_set_style_text_color(input, lv_color_white(), 0);
    lv_obj_set_style_border_color(input, lv_color_hex(0x2B769F), 0);
    lv_obj_set_style_radius(input, 8, 0);

    lv_obj_add_event_cb(
        input,
        [](lv_event_t *event) {
            if (lv_event_get_code(event) == LV_EVENT_FOCUSED &&
                g_keyboard != nullptr) {
                lv_keyboard_set_textarea(
                    g_keyboard,
                    static_cast<lv_obj_t *>(
                        lv_event_get_target(event)));
                lv_obj_clear_flag(
                    g_keyboard,
                    LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(g_keyboard);
            }
        },
        LV_EVENT_FOCUSED,
        nullptr);

    return input;
}

void update_status()
{
    const EcoPowerMqttState state =
        ecopower_mqtt_manager_get_state();

    lv_label_set_text(g_status, state_text(state));
    lv_obj_set_style_text_color(g_status, state_color(state), 0);
}

void timer_cb(lv_timer_t *)
{
    update_status();
}

void back_cb(lv_event_t *)
{
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    ecopower_settings_page_show();
}

void save_cb(lv_event_t *)
{
    EcoPowerMqttConfig config = {};

    snprintf(config.uri, sizeof(config.uri), "%s",
             lv_textarea_get_text(g_uri));
    snprintf(config.username, sizeof(config.username), "%s",
             lv_textarea_get_text(g_user));
    snprintf(config.password, sizeof(config.password), "%s",
             lv_textarea_get_text(g_password));
    snprintf(config.base_topic, sizeof(config.base_topic), "%s",
             lv_textarea_get_text(g_topic));

    const esp_err_t save_error =
        ecopower_mqtt_manager_save_config(&config);

    if (save_error == ESP_OK) {
        const esp_err_t connect_error =
            ecopower_mqtt_manager_connect();

        ESP_LOGI(
            TAG,
            "MQTT save/connect: %s",
            esp_err_to_name(connect_error));
    } else {
        ESP_LOGE(
            TAG,
            "MQTT save failed: %s",
            esp_err_to_name(save_error));
    }

    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    update_status();
}

void disconnect_cb(lv_event_t *)
{
    ecopower_mqtt_manager_disconnect();
    update_status();
}

void forget_cb(lv_event_t *)
{
    ecopower_mqtt_manager_forget_config();
    lv_textarea_set_text(g_uri, "");
    lv_textarea_set_text(g_user, "");
    lv_textarea_set_text(g_password, "");
    lv_textarea_set_text(g_topic, "ecopower");
    update_status();
}

lv_obj_t *make_button(
    lv_obj_t *parent,
    int x,
    int y,
    int width,
    const char *text,
    lv_color_t color,
    lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, 48);
    lv_obj_set_style_radius(button, 9, 0);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *label = make_label(
        button, text, &lv_font_montserrat_14, lv_color_white());
    lv_obj_center(label);
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

    lv_obj_t *header = lv_obj_create(g_screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, 800, 64);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x07131E), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x123C57), 0);

    lv_obj_t *back = make_button(
        header, 14, 10, 84, "< BACK",
        lv_color_hex(0x12324A), back_cb);
    (void)back;

    lv_obj_t *title = make_label(
        header, "MQTT", &lv_font_montserrat_20, lv_color_white());
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *status_caption = make_label(
        header, "Status:", &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(status_caption, 610, 17);

    g_status = make_label(
        header, "---", &lv_font_montserrat_12, lv_color_white());
    lv_obj_set_pos(g_status, 660, 17);

    g_uri = make_input(
        g_screen, 28, 86, 744,
        "Broker URI",
        "mqtt://192.168.1.10:1883",
        false);

    g_user = make_input(
        g_screen, 28, 158, 355,
        "Username",
        "Optional",
        false);

    g_password = make_input(
        g_screen, 417, 158, 355,
        "Password",
        "Optional",
        true);

    g_topic = make_input(
        g_screen, 28, 230, 744,
        "Base topic",
        "ecopower",
        false);

    make_button(
        g_screen, 28, 318, 180,
        "SAVE & CONNECT",
        lv_color_hex(0x16824C),
        save_cb);

    make_button(
        g_screen, 226, 318, 160,
        "DISCONNECT",
        lv_color_hex(0x8C6A22),
        disconnect_cb);

    make_button(
        g_screen, 404, 318, 180,
        "FORGET CONFIG",
        lv_color_hex(0x8C3A3A),
        forget_cb);

    lv_obj_t *note = make_label(
        g_screen,
        "Stage 1 stores MQTT settings in NVS and connects automatically after reboot.",
        &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(note, 28, 382);

    g_keyboard = lv_keyboard_create(g_screen);
    lv_obj_set_pos(g_keyboard, 0, 104);
    lv_obj_set_size(g_keyboard, 800, 376);
    lv_obj_set_style_bg_color(
        g_keyboard, lv_color_hex(0x071725), LV_PART_MAIN);
    lv_obj_set_style_text_font(
        g_keyboard, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_add_event_cb(
        g_keyboard,
        [](lv_event_t *) {
            lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
        },
        LV_EVENT_READY,
        nullptr);
    lv_obj_add_event_cb(
        g_keyboard,
        [](lv_event_t *) {
            lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
        },
        LV_EVENT_CANCEL,
        nullptr);
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);

    EcoPowerMqttConfig config = {};
    if (ecopower_mqtt_manager_get_config(&config)) {
        lv_textarea_set_text(g_uri, config.uri);
        lv_textarea_set_text(g_user, config.username);
        lv_textarea_set_text(g_password, config.password);
        lv_textarea_set_text(g_topic, config.base_topic);
    } else {
        lv_textarea_set_text(g_topic, "ecopower");
    }

    g_timer = lv_timer_create(timer_cb, 500, nullptr);
    update_status();

    ESP_LOGI(TAG, "MQTT settings page created");
}

} // namespace

extern "C" void ecopower_mqtt_page_show(void)
{
    if (g_screen == nullptr) {
        create_page();
    }

    update_status();
    lv_scr_load(g_screen);
    ESP_LOGI(TAG, "MQTT settings page shown");
}
