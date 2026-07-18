#pragma once
#include "lvgl.h"

class FlowDot {
public:
    void create(lv_obj_t *parent, lv_color_t color);
    void move_horizontal(int x1, int x2, int y, unsigned step, unsigned offset, bool reverse);
    void move_vertical(int x, int y1, int y2, unsigned step, unsigned offset, bool reverse);
private:
    lv_obj_t *object_ = nullptr;
};
