#include "dashboard.h"
#include "assets.h"
#include "core/energy_model.h"
#include "widgets/value_label.h"
#include "widgets/flow_dot.h"
#include "inverter_page.h"
#include "settings_page.h"
#include "lvgl.h"
#include "esp_log.h"

#include <cstdint>
#include <cstddef>
#include <cmath>

static const char *TAG = "EcoPower_Dashboard";
static lv_obj_t *g_screen = nullptr;
static lv_timer_t *g_timer = nullptr;
static unsigned g_elapsed_ms = 0;
static unsigned g_animation_step = 0;

struct GaugeSegment {
    lv_obj_t *line = nullptr;
    lv_point_t points[2] = {};
};

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
    FlowDot flow[16];
    GaugeSegment inverter_gauge[12];
    lv_obj_t *battery_bar = nullptr;
};

static DashboardView g_view;

/*
 * Затверджений dashboard.png містить демонстраційні цифри.
 * Щоб динамічний текст LVGL не накладався на них, перед створенням
 * написів закриваємо тільки області значень невеликими темними масками.
 * Сам PNG, рамки, іконки та лінії залишаються без змін.
 */
static void create_mask(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *mask = lv_obj_create(parent);
    lv_obj_remove_style_all(mask);
    lv_obj_set_pos(mask, x, y);
    lv_obj_set_size(mask, w, h);
    lv_obj_set_style_bg_color(mask, lv_color_hex(0x03101A), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_COVER, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);
}

static void create_value_masks(lv_obj_t *parent)
{
    // Годинник і дата
    create_mask(parent, 690, 4, 103, 43);

    // Ліва інформаційна панель
    create_mask(parent, 38, 98, 63, 22);
    create_mask(parent, 38, 252, 63, 22);
    create_mask(parent, 37, 328, 66, 35);

    // PV
    create_mask(parent, 181, 88, 76, 30);
    create_mask(parent, 184, 125, 70, 40);

    // Inverter: закриваємо статичну шкалу, статичне значення потужності
    // та нижній рядок параметрів, після чого все малює LVGL.
    create_mask(parent, 315, 82, 136, 92);
    create_mask(parent, 320, 181, 126, 23);

    // Grid
    create_mask(parent, 565, 89, 78, 31);
    create_mask(parent, 516, 142, 126, 24);

    // Battery
    create_mask(parent, 259, 272, 82, 37);
    create_mask(parent, 247, 309, 97, 13);
    create_mask(parent, 200, 324, 143, 21);
    create_mask(parent, 198, 347, 150, 22);

    // House
    create_mask(parent, 476, 272, 92, 38);
    create_mask(parent, 477, 312, 92, 56);
}

static void create_inverter_gauge(lv_obj_t *parent)
{
    constexpr int center_x = 382;
    constexpr int center_y = 149;
    constexpr int inner_radius = 45;
    constexpr int outer_radius = 58;
    constexpr int segment_count = 12;

    // Розширена напівкругла шкала з більшим радіусом.
    for (int i = 0; i < segment_count; ++i) {
        const float angle_deg = 205.0f + (130.0f * static_cast<float>(i) /
                                          static_cast<float>(segment_count - 1));
        const float angle_rad = angle_deg * 3.1415926535f / 180.0f;

        GaugeSegment &segment = g_view.inverter_gauge[i];

        segment.points[0].x = static_cast<lv_coord_t>(
            center_x + std::cos(angle_rad) * inner_radius);
        segment.points[0].y = static_cast<lv_coord_t>(
            center_y + std::sin(angle_rad) * inner_radius);

        segment.points[1].x = static_cast<lv_coord_t>(
            center_x + std::cos(angle_rad) * outer_radius);
        segment.points[1].y = static_cast<lv_coord_t>(
            center_y + std::sin(angle_rad) * outer_radius);

        segment.line = lv_line_create(parent);
        lv_line_set_points(segment.line, segment.points, 2);
        lv_obj_set_style_line_width(segment.line, 7, 0);
        lv_obj_set_style_line_rounded(segment.line, true, 0);
        lv_obj_set_style_line_color(segment.line, lv_color_hex(0x173047), 0);
        lv_obj_clear_flag(segment.line, LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void update_inverter_gauge(float power_kw)
{
    constexpr float nominal_power_kw = 16.0f;
    constexpr int segment_count = 12;

    float load_pct = std::fabs(power_kw) / nominal_power_kw;
    if (load_pct < 0.0f) load_pct = 0.0f;
    if (load_pct > 1.0f) load_pct = 1.0f;

    int active_segments = static_cast<int>(std::ceil(load_pct * segment_count));
    if (power_kw == 0.0f) active_segments = 0;

    lv_color_t active_color = lv_color_hex(0x20A8FF);
    if (load_pct >= 0.95f) {
        active_color = lv_color_hex(0xFF3B30);
    } else if (load_pct >= 0.80f) {
        active_color = lv_color_hex(0xFFD21C);
    }

    for (int i = 0; i < segment_count; ++i) {
        const bool active = i < active_segments;
        lv_obj_set_style_line_color(
            g_view.inverter_gauge[i].line,
            active ? active_color : lv_color_hex(0x173047),
            0);
    }
}

static void create_battery_bar(lv_obj_t *parent)
{
    g_view.battery_bar = lv_bar_create(parent);
    lv_obj_set_pos(g_view.battery_bar, 249, 311);
    lv_obj_set_size(g_view.battery_bar, 92, 7);
    lv_bar_set_range(g_view.battery_bar, 0, 100);
    lv_bar_set_value(g_view.battery_bar, 0, LV_ANIM_OFF);

    lv_obj_set_style_radius(g_view.battery_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_view.battery_bar, lv_color_hex(0x173047), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_view.battery_bar, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_radius(g_view.battery_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_view.battery_bar, lv_color_hex(0x5CDA3A),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_view.battery_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_clear_flag(g_view.battery_bar, LV_OBJ_FLAG_SCROLLABLE);
}

static void update_battery_bar(float soc_pct)
{
    if (g_view.battery_bar == nullptr) return;

    int soc = static_cast<int>(std::lround(soc_pct));
    if (soc < 0) soc = 0;
    if (soc > 100) soc = 100;

    lv_color_t color = lv_color_hex(0x5CDA3A);
    if (soc <= 10) {
        color = lv_color_hex(0xFF3B30);
    } else if (soc <= 20) {
        color = lv_color_hex(0xFFD21C);
    }

    lv_obj_set_style_bg_color(g_view.battery_bar, color, LV_PART_INDICATOR);
    lv_bar_set_value(g_view.battery_bar, soc, LV_ANIM_ON);
}

static void create_value_widgets(lv_obj_t *parent)
{
    const lv_color_t primary = lv_color_hex(0xDCE8F2);
    const lv_color_t secondary = lv_color_hex(0xC5CED8);

    g_view.clock.create(parent, 690, 7, 102, 23,
                        &lv_font_montserrat_16, lv_color_white());
    g_view.date.create(parent, 694, 30, 98, 16,
                       &lv_font_montserrat_12, secondary);

    g_view.inverter_temp.create(parent, 39, 100, 62, 20,
                                &lv_font_montserrat_12, primary, LV_TEXT_ALIGN_LEFT);
    g_view.efficiency.create(parent, 39, 254, 62, 20,
                             &lv_font_montserrat_12, primary, LV_TEXT_ALIGN_LEFT);
    g_view.uptime.create(parent, 39, 330, 64, 31,
                         &lv_font_montserrat_12, primary, LV_TEXT_ALIGN_LEFT);

    g_view.pv_power.create(parent, 181, 91, 76, 27,
                           &lv_font_montserrat_16, primary);
    g_view.pv_vi.create(parent, 184, 126, 70, 38,
                        &lv_font_montserrat_12, secondary);

    g_view.inverter_power.create(parent, 329, 133, 106, 31,
                                 &lv_font_montserrat_20, primary);
    g_view.inverter_vf.create(parent, 320, 184, 126, 20,
                              &lv_font_montserrat_12, secondary);

    g_view.grid_power.create(parent, 565, 92, 78, 28,
                             &lv_font_montserrat_16, primary);
    g_view.grid_vf.create(parent, 516, 145, 126, 20,
                          &lv_font_montserrat_12, secondary);

    g_view.battery_soc.create(parent, 259, 276, 82, 33,
                              &lv_font_montserrat_20, primary);
    g_view.battery_vi.create(parent, 200, 326, 143, 19,
                             &lv_font_montserrat_12, secondary);
    g_view.battery_power_temp.create(parent, 198, 349, 150, 20,
                                     &lv_font_montserrat_12, secondary);

    g_view.house_power.create(parent, 476, 276, 92, 33,
                              &lv_font_montserrat_16, primary);
    g_view.house_phases.create(parent, 477, 314, 92, 53,
                               &lv_font_montserrat_12, secondary, LV_TEXT_ALIGN_LEFT);
}

static void create_flow_widgets(lv_obj_t *parent)
{
    // 5 PV + 5 Grid + 3 House + 3 Battery = 16 крапок.
    // На всіх маршрутах зберігаємо крок приблизно 12 px.
    for (int i = 0; i < 5; ++i) {
        g_view.flow[i].create(parent, lv_color_hex(0xFFD21C));
    }

    for (int i = 5; i < 10; ++i) {
        g_view.flow[i].create(parent, lv_color_hex(0x79FF58));
    }

    for (int i = 10; i < 13; ++i) {
        g_view.flow[i].create(parent, lv_color_hex(0x24A8FF));
    }

    for (int i = 13; i < 16; ++i) {
        g_view.flow[i].create(parent, lv_color_hex(0x79FF58));
    }
}

static void update_labels(const EnergyData &data)
{
    const unsigned seconds_total =
        14U * 3600U + 36U * 60U + 15U + g_elapsed_ms / 1000U;
    const unsigned hh = (seconds_total / 3600U) % 24U;
    const unsigned mm = (seconds_total / 60U) % 60U;
    const unsigned ss = seconds_total % 60U;

    g_view.clock.set_text_fmt("%02u:%02u:%02u", hh, mm, ss);
    g_view.date.set_text("18.07.2026");

    g_view.inverter_temp.set_text_fmt("%.1f C", data.inverter_temp_c);
    g_view.efficiency.set_text_fmt("%.1f %%", data.efficiency_pct);
    g_view.uptime.set_text_fmt("%ud %02uh", data.uptime_days, data.uptime_hours);

    g_view.pv_power.set_text_fmt("%.2f kW", data.pv_power_kw);
    g_view.pv_vi.set_text_fmt("V: %.1f V\nI: %.1f A",
                              data.pv_voltage_v, data.pv_current_a);

    g_view.inverter_power.set_text_fmt("%.2f kW", data.inverter_power_kw);
    update_inverter_gauge(data.inverter_power_kw);
    g_view.inverter_vf.set_text_fmt("V: %.1f V  F: %.1f",
                                    data.inverter_voltage_v,
                                    data.inverter_frequency_hz);

    g_view.grid_power.set_text_fmt("%.2f kW", data.grid_power_kw);
    g_view.grid_vf.set_text_fmt("V: %.1f V  F: %.1f",
                                data.grid_voltage_v,
                                data.grid_frequency_hz);

    g_view.battery_soc.set_text_fmt("%.0f %%", data.battery_soc_pct);
    update_battery_bar(data.battery_soc_pct);
    g_view.battery_vi.set_text_fmt("V: %.2f V   I: %.1f A",
                                   data.battery_voltage_v,
                                   data.battery_current_a);
    g_view.battery_power_temp.set_text_fmt("P: %.2f kW  T: %.1f C",
                                           data.battery_power_kw,
                                           data.battery_temp_c);

    g_view.house_power.set_text_fmt("%.2f kW", data.house_load_kw);
    g_view.house_phases.set_text_fmt("L1: %.2f\nL2: %.2f\nL3: %.2f",
                                     data.load_l1_kw,
                                     data.load_l2_kw,
                                     data.load_l3_kw);
}

static void update_flows(const EnergyData &data)
{
    /*
     * PV та Grid мають довжину маршруту 60 px і 5 крапок.
     * Battery та House мають довжину 36 px і 3 крапки.
     * В обох випадках відстань між сусідніми крапками дорівнює 12 px,
     * тому при переході через кінець маршруту крапки не збиваються разом.
     */
    static constexpr unsigned kDotSpacingPx = 12U;

    static constexpr FlowPoint pv_to_inverter[] = {
        {267, 112},
        {294, 112},
        {299, 114},
        {302, 118},
        {302, 127},
        {305, 131},
        {310, 133},
        {314, 133},
    };

    static constexpr FlowPoint grid_to_inverter[] = {
        {499, 112},
        {468, 112},
        {463, 114},
        {460, 118},
        {460, 127},
        {457, 131},
        {452, 133},
    };

    // Синю вертикальну лінію зміщено вправо на 1 px.
    static constexpr FlowPoint inverter_to_house[] = {
        {417, 210},
        {417, 246},
    };

    static constexpr FlowPoint inverter_to_battery[] = {
        {342, 210},
        {342, 246},
    };

    for (int i = 0; i < 5; ++i) {
        g_view.flow[i].move_path(
            pv_to_inverter,
            sizeof(pv_to_inverter) / sizeof(pv_to_inverter[0]),
            g_animation_step,
            static_cast<unsigned>(i) * kDotSpacingPx,
            false);
    }

    const bool grid_import = data.grid_power_kw < 0.0f;
    for (int i = 0; i < 5; ++i) {
        g_view.flow[5 + i].move_path(
            grid_to_inverter,
            sizeof(grid_to_inverter) / sizeof(grid_to_inverter[0]),
            g_animation_step,
            static_cast<unsigned>(i) * kDotSpacingPx,
            !grid_import);
    }

    for (int i = 0; i < 3; ++i) {
        g_view.flow[10 + i].move_path(
            inverter_to_house,
            sizeof(inverter_to_house) / sizeof(inverter_to_house[0]),
            g_animation_step,
            static_cast<unsigned>(i) * kDotSpacingPx,
            false);
    }

    const bool charging = data.battery_power_kw < 0.0f;
    for (int i = 0; i < 3; ++i) {
        g_view.flow[13 + i].move_path(
            inverter_to_battery,
            sizeof(inverter_to_battery) / sizeof(inverter_to_battery[0]),
            g_animation_step,
            static_cast<unsigned>(i) * kDotSpacingPx,
            !charging);
    }
}

static void timer_cb(lv_timer_t *)
{
    constexpr unsigned tick_ms = 200;
    g_elapsed_ms += tick_ms;
    g_animation_step += 3U;

    ecopower_energy_model_tick(tick_ms);

    EnergyData data = {};
    ecopower_energy_model_get(&data);

    update_flows(data);

    if ((g_elapsed_ms % 1000U) == 0U) {
        update_labels(data);
    }
}

static void menu_event_cb(lv_event_t *event)
{
    const intptr_t index =
        reinterpret_cast<intptr_t>(lv_event_get_user_data(event));

    static const char *names[] = {
        "Dashboard", "PV", "Battery", "Inverter",
        "Graphs", "Settings", "Alarm", "More"
    };

    if (index >= 0 && index < 8) {
        ESP_LOGI(TAG, "Menu pressed: %s", names[index]);

        if (index == 3) {
            ecopower_inverter_page_show();
        } else if (index == 5) {
            ecopower_settings_page_show();
        }
    }
}

static void create_menu_touch_zones(lv_obj_t *parent)
{
    constexpr int x[8] = {8, 109, 209, 309, 409, 509, 609, 709};
    constexpr int width[8] = {96, 96, 96, 96, 96, 96, 96, 84};

    for (int i = 0; i < 8; ++i) {
        lv_obj_t *button = lv_btn_create(parent);
        lv_obj_remove_style_all(button);
        lv_obj_set_pos(button, x[i], 393);
        lv_obj_set_size(button, width[i], 79);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(
            button,
            menu_event_cb,
            LV_EVENT_CLICKED,
            reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }
}

static void create_error_screen(lv_obj_t *screen)
{
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(
        label,
        "DASHBOARD LOAD ERROR\nCheck SD:/assets/dashboard.png");

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

        lv_img_dsc_t *dashboard =
            ecopower_load_png_from_sd("/sdcard/assets/dashboard.png");

        if (dashboard != nullptr) {
            lv_obj_t *image = lv_img_create(g_screen);
            lv_img_set_src(image, dashboard);
            lv_obj_set_pos(image, 0, 0);

            create_value_masks(g_screen);
            create_inverter_gauge(g_screen);
            create_battery_bar(g_screen);
            create_value_widgets(g_screen);
            create_flow_widgets(g_screen);

            ecopower_energy_model_init();

            EnergyData initial = {};
            ecopower_energy_model_get(&initial);
            update_labels(initial);
            update_flows(initial);

            g_timer = lv_timer_create(timer_cb, 200, nullptr);

            ESP_LOGI(TAG,
                     "EcoPower OS dashboard ready; demo=%s",
                     ecopower_energy_model_demo_enabled() ? "on" : "off");
        } else {
            create_error_screen(g_screen);
        }

        create_menu_touch_zones(g_screen);
    }

    lv_scr_load(g_screen);
    ESP_LOGI(TAG, "Dashboard shown");
}
