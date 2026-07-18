#include "inverter_page.h"
#include "dashboard.h"
#include "drivers/deye_rs485.h"

#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "EcoPower_InverterUI";
static lv_obj_t *g_screen = nullptr;
static lv_obj_t *g_tx = nullptr;
static lv_obj_t *g_rx = nullptr;
static lv_obj_t *g_decoded = nullptr;
static lv_obj_t *g_status = nullptr;
static lv_timer_t *g_timer = nullptr;
static uint32_t g_last_sequence = UINT32_MAX;

static lv_obj_t *make_column(lv_obj_t *parent, int x, int width, const char *title)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, 64);
    lv_obj_set_size(panel, width, 340);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x06111C), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x1F4760), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading = lv_label_create(panel);
    lv_label_set_text(heading, title);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(0x39D8FF), 0);
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *text = lv_label_create(panel);
    lv_obj_set_pos(text, 0, 34);
    lv_obj_set_size(text, width - 22, 285);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(0xD7E5EF), 0);
    lv_obj_set_style_text_line_space(text, 4, 0);
    lv_label_set_text(text, "Waiting for RS485 data...");
    return text;
}

static void refresh_cb(lv_timer_t *)
{
    DeyeDiagnosticSnapshot snapshot = {};
    DeyeRs485Status status = {};
    ecopower_deye_get_diagnostic(&snapshot);
    ecopower_deye_rs485_get_status(&status);

    if (snapshot.sequence != g_last_sequence) {
        g_last_sequence = snapshot.sequence;
        lv_label_set_text(g_tx, snapshot.tx_hex[0] ? snapshot.tx_hex : "(no TX yet)");
        lv_label_set_text(g_rx, snapshot.rx_hex[0] ? snapshot.rx_hex : "(no RX yet)");
        lv_label_set_text(g_decoded, snapshot.decoded[0] ? snapshot.decoded : "No decoded data yet");
    }

    lv_label_set_text_fmt(g_status,
        "RS485  UART1  GPIO15/16  |  Slave %u  |  TX: %lu  RX: %lu  Timeout: %lu  CRC: %lu  |  %s",
        status.slave_address,
        (unsigned long)status.requests_sent,
        (unsigned long)status.valid_responses,
        (unsigned long)status.timeouts,
        (unsigned long)status.crc_errors,
        status.online ? "ONLINE" : "OFFLINE");
}

static void dashboard_cb(lv_event_t *)
{
    ecopower_dashboard_show();
}

extern "C" void ecopower_inverter_page_show(void)
{
    if (!g_screen) {
        g_screen = lv_obj_create(nullptr);
        lv_obj_remove_style_all(g_screen);
        lv_obj_set_size(g_screen, 800, 480);
        lv_obj_set_style_bg_color(g_screen, lv_color_hex(0x020812), 0);
        lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
        lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *title = lv_label_create(g_screen);
        lv_label_set_text(title, "INVERTER / MODBUS RTU DIAGNOSTICS");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_pos(title, 18, 15);

        g_status = lv_label_create(g_screen);
        lv_obj_set_pos(g_status, 18, 43);
        lv_obj_set_width(g_status, 760);
        lv_obj_set_style_text_font(g_status, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(g_status, lv_color_hex(0x8FA9B8), 0);

        g_tx = make_column(g_screen, 12, 245, "TX / SENT");
        g_rx = make_column(g_screen, 277, 245, "RX / RECEIVED");
        g_decoded = make_column(g_screen, 542, 246, "DECODED DATA");

        lv_obj_t *back = lv_btn_create(g_screen);
        lv_obj_set_pos(back, 12, 417);
        lv_obj_set_size(back, 150, 48);
        lv_obj_set_style_bg_color(back, lv_color_hex(0x12354A), 0);
        lv_obj_set_style_radius(back, 8, 0);
        lv_obj_add_event_cb(back, dashboard_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *back_text = lv_label_create(back);
        lv_label_set_text(back_text, "< DASHBOARD");
        lv_obj_set_style_text_font(back_text, &lv_font_montserrat_16, 0);
        lv_obj_center(back_text);

        g_timer = lv_timer_create(refresh_cb, 250, nullptr);
        refresh_cb(nullptr);
    }

    lv_scr_load(g_screen);
    ESP_LOGI(TAG, "Inverter diagnostics page shown");
}
