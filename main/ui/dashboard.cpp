#include "dashboard.h"
#include "assets.h"
#include "core/energy_model.h"
#include "widgets/value_label.h"
#include "widgets/flow_dot.h"
#include "inverter_page.h"
#include "lvgl.h"
#include "esp_log.h"

#include <cstdint>

static const char *TAG = "EcoPower_Dashboard";
static lv_obj_t *g_screen = nullptr;
static lv_timer_t *g_timer = nullptr;
static unsigned g_elapsed_ms = 0;
static unsigned g_animation_step = 0;

struct DashboardView {
    ValueLabel clock;
    ValueLabel date;
    ValueLabel inverter_temp;
    ValueLabel efficiency;
    ValueLabel uptime;
    ValueLabel pv_power;
    ValueLabel pv_vi;
    ValueLabel inverter_power;
    ValueLabel inverter_vf;
    ValueLabel grid_power;
    ValueLabel grid_vf;
    ValueLabel battery_soc;
    ValueLabel battery_vi;
    ValueLabel battery_power_temp;
    ValueLabel house_power;
    ValueLabel house_phases;
    FlowDot flow[8];
};

static DashboardView g_view;

static void create_value_widgets(lv_obj_t *parent)
{
    const lv_color_t primary = lv_color_hex(0xDCE8F2);
    const lv_color_t secondary = lv_color_hex(0xC5CED8);

    g_view.clock.create(parent, 676, 8, 108, 25, &lv_font_montserrat_16, lv_color_white());
    g_view.date.create(parent, 680, 33, 104, 17, &lv_font_montserrat_12, secondary);
    g_view.inverter_temp.create(parent, 41, 119, 54, 21, &lv_font_montserrat_12, primary, LV_TEXT_ALIGN_LEFT);
    g_view.efficiency.create(parent, 44, 276, 51, 20, &lv_font_montserrat_12, primary, LV_TEXT_ALIGN_LEFT);
    g_view.uptime.create(parent, 43, 349, 53, 19, &lv_font_montserrat_12, primary, LV_TEXT_ALIGN_LEFT);

    g_view.pv_power.create(parent, 166, 104, 96, 34, &lv_font_montserrat_16, primary);
    g_view.pv_vi.create(parent, 165, 141, 99, 38, &lv_font_montserrat_12, secondary);
    g_view.inverter_power.create(parent, 321, 132, 116, 43, &lv_font_montserrat_20, primary);
    g_view.inverter_vf.create(parent, 314, 190, 131, 25, &lv_font_montserrat_12, secondary);
    g_view.grid_power.create(parent, 548, 102, 106, 38, &lv_font_montserrat_16, primary);
    g_view.grid_vf.create(parent, 513, 155, 143, 25, &lv_font_montserrat_12, secondary);
    g_view.battery_soc.create(parent, 239, 287, 99, 40, &lv_font_montserrat_20, primary);
    g_view.battery_vi.create(parent, 190, 338, 155, 24, &lv_font_montserrat_12, secondary);
    g_view.battery_power_temp.create(parent, 190, 363, 155, 22, &lv_font_montserrat_12, secondary);
    g_view.house_power.create(parent, 462, 288, 113, 38, &lv_font_montserrat_16, primary);
    g_view.house_phases.create(parent, 468, 326, 106, 58, &lv_font_montserrat_12, secondary, LV_TEXT_ALIGN_LEFT);
}

static void create_flow_widgets(lv_obj_t *parent)
{
    for (int i = 0; i < 3; ++i) g_view.flow[i].create(parent, lv_color_hex(0xFFD21C));
    for (int i = 3; i < 5; ++i) g_view.flow[i].create(parent, lv_color_hex(0x2FFF70));
    for (int i = 5; i < 7; ++i) g_view.flow[i].create(parent, lv_color_hex(0x24A8FF));
    g_view.flow[7].create(parent, lv_color_hex(0x79FF58));
}

static void update_labels(const EnergyData &data)
{
    const unsigned seconds_total = 14U * 3600U + 36U * 60U + 15U + g_elapsed_ms / 1000U;
    const unsigned hh = (seconds_total / 3600U) % 24U;
    const unsigned mm = (seconds_total / 60U) % 60U;
    const unsigned ss = seconds_total % 60U;

    g_view.clock.set_text_fmt("%02u:%02u:%02u", hh, mm, ss);
    g_view.date.set_text("18.07.2026");
    g_view.inverter_temp.set_text_fmt("%.1f C", data.inverter_temp_c);
    g_view.efficiency.set_text_fmt("%.1f %%", data.efficiency_pct);
    g_view.uptime.set_text_fmt("%ud %02uh", data.uptime_days, data.uptime_hours);
    g_view.pv_power.set_text_fmt("%.2f kW", data.pv_power_kw);
    g_view.pv_vi.set_text_fmt("V: %.1f V\nI: %.1f A", data.pv_voltage_v, data.pv_current_a);
    g_view.inverter_power.set_text_fmt("%.2f kW", data.inverter_power_kw);
    g_view.inverter_vf.set_text_fmt("V: %.1f V  F: %.2f", data.inverter_voltage_v, data.inverter_frequency_hz);
    g_view.grid_power.set_text_fmt("%.2f kW", data.grid_power_kw);
    g_view.grid_vf.set_text_fmt("V: %.1f V  F: %.2f", data.grid_voltage_v, data.grid_frequency_hz);
    g_view.battery_soc.set_text_fmt("%.0f %%", data.battery_soc_pct);
    g_view.battery_vi.set_text_fmt("V: %.2f V  I: %.1f A", data.battery_voltage_v, data.battery_current_a);
    g_view.battery_power_temp.set_text_fmt("P: %.2f kW  T: %.1f C", data.battery_power_kw, data.battery_temp_c);
    g_view.house_power.set_text_fmt("%.2f kW", data.house_load_kw);
    g_view.house_phases.set_text_fmt("L1: %.2f\nL2: %.2f\nL3: %.2f", data.load_l1_kw, data.load_l2_kw, data.load_l3_kw);
}

static void update_flows(const EnergyData &data)
{
    g_view.flow[0].move_horizontal(260, 306, 120, g_animation_step, 0, false);
    g_view.flow[1].move_horizontal(260, 306, 120, g_animation_step, 18, false);
    g_view.flow[2].move_horizontal(260, 306, 120, g_animation_step, 36, false);

    const bool grid_import = data.grid_power_kw < 0.0f;
    g_view.flow[3].move_horizontal(448, 507, 120, g_animation_step, 0, grid_import);
    g_view.flow[4].move_horizontal(448, 507, 120, g_animation_step, 30, grid_import);

    g_view.flow[5].move_vertical(424, 221, 264, g_animation_step, 0, false);
    g_view.flow[6].move_vertical(424, 221, 264, g_animation_step, 22, false);

    const bool charging = data.battery_power_kw < 0.0f;
    g_view.flow[7].move_vertical(326, 222, 264, g_animation_step, 0, charging);
}

static void timer_cb(lv_timer_t *)
{
    constexpr unsigned tick_ms = 200;
    g_elapsed_ms += tick_ms;
    g_animation_step += 4;
    ecopower_energy_model_tick(tick_ms);

    EnergyData data = {};
    ecopower_energy_model_get(&data);
    update_flows(data);
    if ((g_elapsed_ms % 500U) == 0U) update_labels(data);
}

static void menu_event_cb(lv_event_t *event)
{
    const intptr_t index = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    static const char *names[] = {"Dashboard", "PV", "Battery", "Inverter", "Graphs", "Settings", "Alarm", "More"};
    if (index >= 0 && index < 8) {
        ESP_LOGI(TAG, "Menu pressed: %s", names[index]);
        if (index == 3) ecopower_inverter_page_show();
    }
}

static void create_menu_touch_zones(lv_obj_t *parent)
{
    constexpr int x[8] = {8, 109, 209, 309, 409, 509, 609, 709};
    constexpr int width[8] = {96, 96, 96, 96, 96, 96, 96, 84};
    for (int i = 0; i < 8; ++i) {
        lv_obj_t *button = lv_btn_create(parent);
        lv_obj_remove_style_all(button);
        lv_obj_set_pos(button, x[i], 400);
        lv_obj_set_size(button, width[i], 76);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(button, menu_event_cb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }
}

static void create_error_screen(lv_obj_t *screen)
{
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "DASHBOARD LOAD ERROR\nCheck SD:/assets/dashboard.png");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);
}

extern "C" void ecopower_dashboard_show(void)
{
    if (g_screen == nullptr) {
        g_screen = lv_obj_create(nullptr);
        lv_obj_remove_style_all(g_screen);
        lv_obj_set_size(g_screen, 800, 480);
        lv_obj_set_style_bg_color(g_screen, lv_color_hex(0x020812), 0);
        lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
        lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);

        lv_img_dsc_t *dashboard = ecopower_load_png_from_sd("/sdcard/assets/dashboard.png");
        if (dashboard != nullptr) {
            lv_obj_t *image = lv_img_create(g_screen);
            lv_img_set_src(image, dashboard);
            lv_obj_set_pos(image, 0, 0);
            create_value_widgets(g_screen);
            create_flow_widgets(g_screen);
            ecopower_energy_model_init();
            EnergyData initial = {};
            ecopower_energy_model_get(&initial);
            update_labels(initial);
            update_flows(initial);
            g_timer = lv_timer_create(timer_cb, 200, nullptr);
            ESP_LOGI(TAG, "EcoPower OS v3 widgets ready; demo=%s",
                     ecopower_energy_model_demo_enabled() ? "on" : "off");
        } else {
            create_error_screen(g_screen);
        }
        create_menu_touch_zones(g_screen);
    }

    lv_scr_load(g_screen);
    ESP_LOGI(TAG, "Dashboard shown");
}
