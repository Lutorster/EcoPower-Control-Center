#include "flow_dot.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace {

constexpr lv_coord_t kDotRadius = 2;

unsigned segment_length(const FlowPoint &a, const FlowPoint &b)
{
    const float dx = static_cast<float>(b.x - a.x);
    const float dy = static_cast<float>(b.y - a.y);
    return static_cast<unsigned>(std::lround(std::sqrt(dx * dx + dy * dy)));
}

void set_dot_center(lv_obj_t *object, lv_coord_t center_x, lv_coord_t center_y)
{
    // LVGL задає позицію об'єкта за його лівим верхнім кутом.
    // Віднімаємо радіус, щоб центр крапки точно йшов по центру лінії.
    lv_obj_set_pos(object,
                   static_cast<lv_coord_t>(center_x - kDotRadius),
                   static_cast<lv_coord_t>(center_y - kDotRadius));
}

} // namespace

void FlowDot::create(lv_obj_t *parent, lv_color_t color)
{
    object_ = lv_obj_create(parent);
    lv_obj_remove_style_all(object_);
    lv_obj_set_size(object_, 5, 5);
    lv_obj_set_style_radius(object_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(object_, color, 0);
    lv_obj_set_style_bg_opa(object_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(object_, LV_OBJ_FLAG_SCROLLABLE);
}

void FlowDot::move_path(const FlowPoint *points,
                        std::size_t point_count,
                        unsigned step,
                        unsigned offset,
                        bool reverse)
{
    if (object_ == nullptr || points == nullptr || point_count < 2U) {
        return;
    }

    unsigned total_length = 0U;
    for (std::size_t i = 0; i + 1U < point_count; ++i) {
        total_length += segment_length(points[i], points[i + 1U]);
    }

    if (total_length == 0U) {
        set_dot_center(object_, points[0].x, points[0].y);
        return;
    }

    unsigned distance = (step + offset) % total_length;
    if (reverse) {
        distance = (total_length - distance) % total_length;
    }

    for (std::size_t i = 0; i + 1U < point_count; ++i) {
        const FlowPoint &a = points[i];
        const FlowPoint &b = points[i + 1U];
        const unsigned length = segment_length(a, b);

        if (length == 0U) {
            continue;
        }

        if (distance < length) {
            const int dx = static_cast<int>(b.x) - static_cast<int>(a.x);
            const int dy = static_cast<int>(b.y) - static_cast<int>(a.y);

            const lv_coord_t center_x = static_cast<lv_coord_t>(
                static_cast<int>(a.x) +
                (dx * static_cast<int>(distance)) / static_cast<int>(length));

            const lv_coord_t center_y = static_cast<lv_coord_t>(
                static_cast<int>(a.y) +
                (dy * static_cast<int>(distance)) / static_cast<int>(length));

            set_dot_center(object_, center_x, center_y);
            return;
        }

        distance -= length;
    }

    set_dot_center(object_,
                   points[point_count - 1U].x,
                   points[point_count - 1U].y);
}

void FlowDot::move_horizontal(int x1, int x2, int y,
                              unsigned step, unsigned offset, bool reverse)
{
    const FlowPoint points[] = {
        {static_cast<lv_coord_t>(x1), static_cast<lv_coord_t>(y)},
        {static_cast<lv_coord_t>(x2), static_cast<lv_coord_t>(y)},
    };

    move_path(points, 2U, step, offset, reverse);
}

void FlowDot::move_vertical(int x, int y1, int y2,
                            unsigned step, unsigned offset, bool reverse)
{
    const FlowPoint points[] = {
        {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y1)},
        {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y2)},
    };

    move_path(points, 2U, step, offset, reverse);
}
