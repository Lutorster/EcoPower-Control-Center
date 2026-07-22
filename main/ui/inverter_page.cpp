#include "inverter_page.h"

#include "dashboard.h"
#include "deye/deye_config.h"
#include "deye/deye_driver.h"

#include "lvgl.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>

static const char *TAG = "EcoPower_InverterUI";

namespace {

constexpr uint16_t kTestStartAddress = 0;
constexpr uint16_t kTestRegisterCount = 8;

lv_obj_t *g_screen = nullptr;
lv_obj_t *g_connection_value = nullptr;
lv_obj_t *g_slave_value = nullptr;
lv_obj_t *g_baud_value = nullptr;
lv_obj_t *g_requests_value = nullptr;
lv_obj_t *g_errors_value = nullptr;
lv_obj_t *g_last_result_value = nullptr;
lv_obj_t *g_registers_value = nullptr;
lv_obj_t *g_read_button = nullptr;
lv_obj_t *g_read_button_label = nullptr;
lv_timer_t *g_refresh_timer = nullptr;

SemaphoreHandle_t g_result_mutex = nullptr;
bool g_read_in_progress = false;
bool g_result_ready = false;
EcoPowerModbusResult g_last_result = {};
uint16_t g_registers[kTestRegisterCount] = {};

lv_obj_t *create_label(
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

lv_obj_t *create_card(
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
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading = create_label(
        card,
        title,
        &lv_font_montserrat_16,
        lv_color_white());
    lv_obj_set_pos(heading, 0, 0);

    return card;
}

void create_status_row(
    lv_obj_t *parent,
    int y,
    const char *caption,
    lv_obj_t **value)
{
    lv_obj_t *caption_label = create_label(
        parent,
        caption,
        &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(caption_label, 0, y);

    *value = create_label(
        parent,
        "---",
        &lv_font_montserrat_14,
        lv_color_white());
    lv_obj_set_pos(*value, 135, y - 2);
    lv_obj_set_width(*value, 185);
    lv_label_set_long_mode(*value, LV_LABEL_LONG_DOT);
}

const char *modbus_status_text(EcoPowerModbusStatus status)
{
    switch (status) {
        case ECOPOWER_MODBUS_OK:
            return "OK";
        case ECOPOWER_MODBUS_TIMEOUT:
            return "TIMEOUT";
        case ECOPOWER_MODBUS_BAD_CRC:
            return "BAD CRC";
        case ECOPOWER_MODBUS_BAD_RESPONSE:
            return "BAD RESPONSE";
        case ECOPOWER_MODBUS_EXCEPTION:
            return "MODBUS EXCEPTION";
        case ECOPOWER_MODBUS_IO_ERROR:
        default:
            return "I/O ERROR";
    }
}

void update_read_button(bool busy)
{
    if (g_read_button == nullptr ||
        g_read_button_label == nullptr) {
        return;
    }

    if (busy) {
        lv_label_set_text(g_read_button_label, "READING...");
        lv_obj_add_state(g_read_button, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(g_read_button_label, "READ TEST");
        lv_obj_clear_state(g_read_button, LV_STATE_DISABLED);
    }
}

void read_task(void *)
{
    uint16_t local_registers[kTestRegisterCount] = {};

    const EcoPowerModbusResult result =
        ecopower_deye_read_holding_registers(
            kTestStartAddress,
            kTestRegisterCount,
            local_registers);

    if (g_result_mutex != nullptr &&
        xSemaphoreTake(g_result_mutex, portMAX_DELAY) == pdTRUE) {
        g_last_result = result;
        std::memcpy(
            g_registers,
            local_registers,
            sizeof(g_registers));
        g_result_ready = true;
        g_read_in_progress = false;
        xSemaphoreGive(g_result_mutex);
    }

    vTaskDelete(nullptr);
}

void read_test_event_cb(lv_event_t *)
{
    if (!ecopower_deye_driver_is_initialized()) {
        lv_label_set_text(
            g_last_result_value,
            "Driver is not initialized");
        lv_obj_set_style_text_color(
            g_last_result_value,
            lv_color_hex(0xFF5C5C),
            0);
        return;
    }

    if (g_result_mutex == nullptr) {
        return;
    }

    bool can_start = false;

    if (xSemaphoreTake(g_result_mutex, portMAX_DELAY) == pdTRUE) {
        if (!g_read_in_progress) {
            g_read_in_progress = true;
            g_result_ready = false;
            can_start = true;
        }
        xSemaphoreGive(g_result_mutex);
    }

    if (!can_start) {
        return;
    }

    update_read_button(true);

    const BaseType_t created = xTaskCreate(
        read_task,
        "deye_read_test",
        4096,
        nullptr,
        4,
        nullptr);

    if (created != pdPASS) {
        if (xSemaphoreTake(g_result_mutex, portMAX_DELAY) == pdTRUE) {
            g_read_in_progress = false;
            xSemaphoreGive(g_result_mutex);
        }

        update_read_button(false);
        lv_label_set_text(
            g_last_result_value,
            "Unable to create read task");
        lv_obj_set_style_text_color(
            g_last_result_value,
            lv_color_hex(0xFF5C5C),
            0);
    }
}

void refresh_timer_cb(lv_timer_t *)
{
    EcoPowerDeyeDiagnostics diagnostics = {};
    ecopower_deye_get_diagnostics(&diagnostics);

    const bool initialized =
        ecopower_deye_driver_is_initialized();

    lv_label_set_text(
        g_connection_value,
        initialized ? "READY" : "NOT INITIALIZED");

    lv_obj_set_style_text_color(
        g_connection_value,
        initialized
            ? lv_color_hex(0x20D878)
            : lv_color_hex(0xFF5C5C),
        0);

    lv_label_set_text_fmt(
        g_slave_value,
        "%u",
        static_cast<unsigned>(
            ECOPOWER_DEYE_SLAVE_ADDRESS));

    lv_label_set_text_fmt(
        g_baud_value,
        "%d 8N1",
        ECOPOWER_DEYE_BAUD_RATE);

    lv_label_set_text_fmt(
        g_requests_value,
        "%lu",
        static_cast<unsigned long>(
            diagnostics.successful_requests));

    lv_label_set_text_fmt(
        g_errors_value,
        "%lu",
        static_cast<unsigned long>(
            diagnostics.failed_requests));

    bool result_ready = false;
    bool read_in_progress = false;
    EcoPowerModbusResult result = {};
    uint16_t registers[kTestRegisterCount] = {};

    if (g_result_mutex != nullptr &&
        xSemaphoreTake(g_result_mutex, 0) == pdTRUE) {
        result_ready = g_result_ready;
        read_in_progress = g_read_in_progress;

        if (result_ready) {
            result = g_last_result;
            std::memcpy(
                registers,
                g_registers,
                sizeof(registers));
            g_result_ready = false;
        }

        xSemaphoreGive(g_result_mutex);
    }

    update_read_button(read_in_progress);

    if (!result_ready) {
        return;
    }

    if (result.status == ECOPOWER_MODBUS_OK) {
        lv_label_set_text_fmt(
            g_last_result_value,
            "OK - %u bytes",
            static_cast<unsigned>(
                result.received_bytes));

        lv_obj_set_style_text_color(
            g_last_result_value,
            lv_color_hex(0x20D878),
            0);

        char text[420] = {};
        size_t used = 0U;

        for (uint16_t index = 0;
             index < kTestRegisterCount;
             ++index) {
            const int written = snprintf(
                text + used,
                sizeof(text) - used,
                "%5u  =  %5u   (0x%04X)\n",
                static_cast<unsigned>(
                    kTestStartAddress + index),
                static_cast<unsigned>(
                    registers[index]),
                static_cast<unsigned>(
                    registers[index]));

            if (written <= 0 ||
                static_cast<size_t>(written) >=
                    sizeof(text) - used) {
                break;
            }

            used += static_cast<size_t>(written);
        }

        lv_label_set_text(g_registers_value, text);
    } else {
        if (result.status == ECOPOWER_MODBUS_EXCEPTION) {
            lv_label_set_text_fmt(
                g_last_result_value,
                "%s 0x%02X",
                modbus_status_text(result.status),
                static_cast<unsigned>(
                    result.exception_code));
        } else {
            lv_label_set_text(
                g_last_result_value,
                modbus_status_text(result.status));
        }

        lv_obj_set_style_text_color(
            g_last_result_value,
            lv_color_hex(0xFF5C5C),
            0);

        lv_label_set_text_fmt(
            g_registers_value,
            "No register data.\n\n"
            "Test request:\n"
            "Function: 0x03\n"
            "Start: %u\n"
            "Count: %u\n\n"
            "Check A/B polarity, inverter port,\n"
            "slave address and baud rate.",
            static_cast<unsigned>(
                kTestStartAddress),
            static_cast<unsigned>(
                kTestRegisterCount));
    }
}

void dashboard_event_cb(lv_event_t *)
{
    ecopower_dashboard_show();
}

void create_page()
{
    g_result_mutex = xSemaphoreCreateMutex();

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
    lv_obj_set_style_border_side(
        header,
        LV_BORDER_SIDE_BOTTOM,
        0);
    lv_obj_set_style_border_color(
        header,
        lv_color_hex(0x123C57),
        0);

    lv_obj_t *back = lv_btn_create(header);
    lv_obj_set_pos(back, 14, 10);
    lv_obj_set_size(back, 126, 44);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_set_style_bg_color(
        back,
        lv_color_hex(0x12324A),
        0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(
        back,
        dashboard_event_cb,
        LV_EVENT_CLICKED,
        nullptr);

    lv_obj_t *back_label = create_label(
        back,
        "< DASHBOARD",
        &lv_font_montserrat_14,
        lv_color_white());
    lv_obj_center(back_label);

    lv_obj_t *title = create_label(
        header,
        "INVERTER / MODBUS RTU",
        &lv_font_montserrat_20,
        lv_color_white());
    lv_obj_align(title, LV_ALIGN_CENTER, 25, 0);

    lv_obj_t *connection_card = create_card(
        g_screen,
        28,
        82,
        350,
        300,
        "Connection");

    create_status_row(
        connection_card,
        40,
        "RS485 driver",
        &g_connection_value);
    create_status_row(
        connection_card,
        80,
        "Slave address",
        &g_slave_value);
    create_status_row(
        connection_card,
        120,
        "Baud / format",
        &g_baud_value);
    create_status_row(
        connection_card,
        160,
        "Successful",
        &g_requests_value);
    create_status_row(
        connection_card,
        200,
        "Errors",
        &g_errors_value);
    create_status_row(
        connection_card,
        240,
        "Last result",
        &g_last_result_value);

    lv_obj_t *register_card = create_card(
        g_screen,
        398,
        82,
        374,
        300,
        "Read-only register test");

    lv_obj_t *test_info = create_label(
        register_card,
        "Function 0x03 | Start 0 | Count 8",
        &lv_font_montserrat_12,
        lv_color_hex(0x8FAFC4));
    lv_obj_set_pos(test_info, 0, 34);

    g_registers_value = create_label(
        register_card,
        "Press READ TEST after connecting\n"
        "the RS485 cable to the inverter.",
        &lv_font_montserrat_14,
        lv_color_white());
    lv_obj_set_pos(g_registers_value, 0, 66);
    lv_obj_set_width(g_registers_value, 330);
    lv_label_set_long_mode(
        g_registers_value,
        LV_LABEL_LONG_WRAP);

    g_read_button = lv_btn_create(g_screen);
    lv_obj_set_pos(g_read_button, 398, 402);
    lv_obj_set_size(g_read_button, 180, 54);
    lv_obj_set_style_radius(g_read_button, 10, 0);
    lv_obj_set_style_bg_color(
        g_read_button,
        lv_color_hex(0x16824C),
        0);
    lv_obj_set_style_bg_color(
        g_read_button,
        lv_color_hex(0x0E5132),
        LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(
        g_read_button,
        0,
        0);
    lv_obj_add_event_cb(
        g_read_button,
        read_test_event_cb,
        LV_EVENT_CLICKED,
        nullptr);

    g_read_button_label = create_label(
        g_read_button,
        "READ TEST",
        &lv_font_montserrat_16,
        lv_color_white());
    lv_obj_center(g_read_button_label);

    lv_obj_t *warning = create_label(
        g_screen,
        "READ ONLY - inverter settings are not changed",
        &lv_font_montserrat_12,
        lv_color_hex(0xFFD21C));
    lv_obj_set_pos(warning, 28, 420);

    g_refresh_timer = lv_timer_create(
        refresh_timer_cb,
        250,
        nullptr);

    refresh_timer_cb(nullptr);

    ESP_LOGI(
        TAG,
        "New Modbus diagnostics page created");
}

} // namespace

extern "C" void ecopower_inverter_page_show(void)
{
    if (g_screen == nullptr) {
        create_page();
    }

    lv_scr_load(g_screen);
    ESP_LOGI(TAG, "Inverter diagnostics page shown");
}
