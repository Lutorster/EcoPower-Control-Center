#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ecopower_assets_init(void);
lv_img_dsc_t *ecopower_load_png_from_sd(const char *path);

#ifdef __cplusplus
}
#endif
