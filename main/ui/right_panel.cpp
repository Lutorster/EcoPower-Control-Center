#include "right_panel.h"

#include "core/daily_energy.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr int kGraphWidth = 98;
constexpr int kGraphHeight = 69;
constexpr int kMaxDrawPoints = 98;

lv_obj_t *g_pv_value = nullptr;
lv_obj_t *g_battery_value = nullptr;
lv_obj_t *g_consumption_value = nullptr;
lv_obj_t *g_export_value = nullptr;
lv_obj_t *g_canvas = nullptr;

static uint8_t g_canvas_buffer[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(
        kGraphWidth,
        kGraphHeight)];

lv_point_t g_line_points[kMaxDrawPoints] = {};
lv_point_t g_fill_points[kMaxDrawPoints + 2] = {};


lv_obj_t *create_value_label(
    lv_obj_t *parent,
    int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, 704, y);
    lv_obj_set_size(label, 80, 18);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(
        label,
        &lv_font_montserrat_12,
        0);
    lv_obj_set_style_text_color(
        label,
        lv_color_hex(0xDCE8F2),
        0);
    lv_obj_set_style_text_align(
        label,
        LV_TEXT_ALIGN_RIGHT,
        0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

void set_energy_value(
    lv_obj_t *label,
    float value)
{
    if (label == nullptr) {
        return;
    }

    char text[32] = {};
    snprintf(text, sizeof(text), "%.2f kWh", value);
    lv_label_set_text(label, text);
}

void clear_canvas()
{
    if (g_canvas == nullptr) {
        return;
    }

    lv_canvas_fill_bg(
        g_canvas,
        lv_color_hex(0x03101A),
        LV_OPA_COVER);
}

void draw_soc_history(
    const EcoPowerDailyEnergyData &daily)
{
    if (g_canvas == nullptr) {
        return;
    }

    clear_canvas();

    const size_t source_count = std::min(
        daily.soc_history_count,
        static_cast<size_t>(ECOPOWER_SOC_HISTORY_POINTS));

    if (source_count < 2U) {
        return;
    }

    /*
     * The 98-pixel canvas shows the newest 24 hours.
     * Source data is already returned oldest-to-newest.
     */
    const size_t draw_count = std::min(
        source_count,
        static_cast<size_t>(kMaxDrawPoints));

    const size_t first_source =
        source_count > draw_count
            ? source_count - draw_count
            : 0U;

    for (size_t index = 0U;
         index < draw_count;
         ++index) {
        const size_t source =
            first_source +
            (index * (source_count - first_source - 1U)) /
                (draw_count - 1U);

        float soc = daily.soc_history[source];
        soc = std::clamp(soc, 0.0f, 100.0f);

        const int x =
            static_cast<int>(
                (index * (kGraphWidth - 1)) /
                (draw_count - 1U));

        const int y =
            static_cast<int>(
                std::lround(
                    (100.0f - soc) *
                    static_cast<float>(kGraphHeight - 2) /
                    100.0f));

        g_line_points[index].x =
            static_cast<lv_coord_t>(x);
        g_line_points[index].y =
            static_cast<lv_coord_t>(
                std::clamp(y, 0, kGraphHeight - 2));

        g_fill_points[index] = g_line_points[index];
    }

    g_fill_points[draw_count].x =
        g_line_points[draw_count - 1U].x;
    g_fill_points[draw_count].y =
        kGraphHeight - 1;

    g_fill_points[draw_count + 1U].x =
        g_line_points[0].x;
    g_fill_points[draw_count + 1U].y =
        kGraphHeight - 1;

    lv_draw_rect_dsc_t fill_dsc;
    lv_draw_rect_dsc_init(&fill_dsc);
    fill_dsc.bg_color = lv_color_hex(0x0A6A32);
    fill_dsc.bg_opa = LV_OPA_30;
    fill_dsc.border_width = 0;

    lv_canvas_draw_polygon(
        g_canvas,
        g_fill_points,
        static_cast<uint32_t>(draw_count + 2U),
        &fill_dsc);

    /* Soft glow under the main SOC line. */
    lv_draw_line_dsc_t glow_dsc;
    lv_draw_line_dsc_init(&glow_dsc);
    glow_dsc.color = lv_color_hex(0x79FF58);
    glow_dsc.width = 7;
    glow_dsc.opa = LV_OPA_20;
    glow_dsc.round_start = 1;
    glow_dsc.round_end = 1;

    lv_canvas_draw_line(
        g_canvas,
        g_line_points,
        static_cast<uint32_t>(draw_count),
        &glow_dsc);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x79FF58);
    line_dsc.width = 3;
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;

    lv_canvas_draw_line(
        g_canvas,
        g_line_points,
        static_cast<uint32_t>(draw_count),
        &line_dsc);
}

} // namespace

extern "C" void ecopower_right_panel_create(
    lv_obj_t *parent)
{
    if (parent == nullptr || g_pv_value != nullptr) {
        return;
    }


    g_pv_value = create_value_label(parent, 101);
    g_battery_value = create_value_label(parent, 143);
    g_consumption_value = create_value_label(parent, 188);
    g_export_value = create_value_label(parent, 230);

    g_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(
        g_canvas,
        g_canvas_buffer,
        kGraphWidth,
        kGraphHeight,
        LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_obj_set_pos(g_canvas, 687, 287);
    lv_obj_clear_flag(g_canvas, LV_OBJ_FLAG_SCROLLABLE);

    clear_canvas();
    ecopower_right_panel_update();
}

extern "C" void ecopower_right_panel_update(void)
{
    if (g_pv_value == nullptr) {
        return;
    }

    EcoPowerDailyEnergyData daily = {};
    if (!ecopower_daily_energy_get(&daily)) {
        set_energy_value(g_pv_value, 0.0f);
        set_energy_value(g_battery_value, 0.0f);
        set_energy_value(g_consumption_value, 0.0f);
        set_energy_value(g_export_value, 0.0f);
        clear_canvas();
        return;
    }

    set_energy_value(
        g_pv_value,
        daily.pv_production_kwh);
    set_energy_value(
        g_battery_value,
        daily.battery_charged_kwh);
    set_energy_value(
        g_consumption_value,
        daily.consumption_kwh);
    set_energy_value(
        g_export_value,
        daily.grid_export_kwh);

    draw_soc_history(daily);
}
