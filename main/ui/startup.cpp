#include "startup.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "EcoPower_Boot";
static lv_obj_t *boot_screen = nullptr;

static void set_screen_base(lv_obj_t *screen)
{
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, 800, 480);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x01070E), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

extern "C" void ecopower_boot_show(void)
{
    if (boot_screen == nullptr) {
        boot_screen = lv_obj_create(nullptr);
        set_screen_base(boot_screen);

        lv_obj_t *title = lv_label_create(boot_screen);
        lv_label_set_text(title, "EcoPower");
        lv_obj_set_style_text_color(title, lv_color_hex(0x168BFF), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -18);

        lv_obj_t *status = lv_label_create(boot_screen);
        lv_label_set_text(status, "SYSTEM STARTING...");
        lv_obj_set_style_text_color(status, lv_color_hex(0xB7D7F5), 0);
        lv_obj_set_style_text_font(status, &lv_font_montserrat_20, 0);
        lv_obj_align(status, LV_ALIGN_CENTER, 0, 28);
    }

    lv_scr_load(boot_screen);
    ESP_LOGI(TAG, "Boot screen shown");
}
