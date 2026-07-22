#pragma once

#include "lvgl.h"
#include <cstddef>

struct FlowPoint {
    lv_coord_t x;
    lv_coord_t y;
};

class FlowDot {
public:
    void create(lv_obj_t *parent, lv_color_t color);

    // Рух точки по маршруту, який складається з послідовності точок.
    // Підтримує горизонтальні, вертикальні та діагональні сегменти.
    void move_path(const FlowPoint *points,
                   std::size_t point_count,
                   unsigned step,
                   unsigned offset,
                   bool reverse);

    // Старі методи залишені для сумісності з іншими файлами.
    void move_horizontal(int x1, int x2, int y,
                         unsigned step, unsigned offset, bool reverse);

    void move_vertical(int x, int y1, int y2,
                       unsigned step, unsigned offset, bool reverse);

private:
    lv_obj_t *object_ = nullptr;
};
