#pragma once
#include "lvgl.h"

class ValueLabel {
public:
    void create(lv_obj_t *parent, int x, int y, int width, int height,
                const lv_font_t *font, lv_color_t color,
                lv_text_align_t align = LV_TEXT_ALIGN_CENTER);
    void set_text(const char *text);
    void set_text_fmt(const char *format, ...);
    lv_obj_t *object() const { return label_; }

private:
    lv_obj_t *plate_ = nullptr;
    lv_obj_t *label_ = nullptr;
};
