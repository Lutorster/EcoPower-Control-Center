#include "value_label.h"
#include <cstdarg>
#include <cstdio>

void ValueLabel::create(lv_obj_t *parent, int x, int y, int width, int height,
                        const lv_font_t *font, lv_color_t color,
                        lv_text_align_t align)
{
    plate_ = lv_obj_create(parent);
    lv_obj_remove_style_all(plate_);
    lv_obj_set_pos(plate_, x, y);
    lv_obj_set_size(plate_, width, height);
    lv_obj_set_style_bg_color(plate_, lv_color_hex(0x06111C), 0);
    lv_obj_set_style_bg_opa(plate_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(plate_, LV_OBJ_FLAG_SCROLLABLE);

    label_ = lv_label_create(plate_);
    lv_obj_set_width(label_, lv_pct(100));
    lv_obj_set_style_text_font(label_, font, 0);
    lv_obj_set_style_text_color(label_, color, 0);
    lv_obj_set_style_text_align(label_, align, 0);
    lv_obj_center(label_);
}

void ValueLabel::set_text(const char *text)
{
    if (label_ != nullptr) lv_label_set_text(label_, text != nullptr ? text : "");
}

void ValueLabel::set_text_fmt(const char *format, ...)
{
    if (label_ == nullptr || format == nullptr) return;
    char buffer[128];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    lv_label_set_text(label_, buffer);
}
