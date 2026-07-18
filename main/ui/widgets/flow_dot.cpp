#include "flow_dot.h"

void FlowDot::create(lv_obj_t *parent, lv_color_t color)
{
    object_ = lv_obj_create(parent);
    lv_obj_remove_style_all(object_);
    lv_obj_set_size(object_, 6, 6);
    lv_obj_set_style_radius(object_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(object_, color, 0);
    lv_obj_set_style_bg_opa(object_, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(object_, color, 0);
    lv_obj_set_style_shadow_width(object_, 8, 0);
    lv_obj_set_style_shadow_opa(object_, LV_OPA_60, 0);
}

void FlowDot::move_horizontal(int x1, int x2, int y, unsigned step, unsigned offset, bool reverse)
{
    if (object_ == nullptr || x2 <= x1) return;
    const unsigned span = static_cast<unsigned>(x2 - x1);
    unsigned position = (step + offset) % (span + 1U);
    if (reverse) position = span - position;
    lv_obj_set_pos(object_, x1 + static_cast<int>(position), y);
}

void FlowDot::move_vertical(int x, int y1, int y2, unsigned step, unsigned offset, bool reverse)
{
    if (object_ == nullptr || y2 <= y1) return;
    const unsigned span = static_cast<unsigned>(y2 - y1);
    unsigned position = (step + offset) % (span + 1U);
    if (reverse) position = span - position;
    lv_obj_set_pos(object_, x, y1 + static_cast<int>(position));
}
