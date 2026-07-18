#include "assets.h"
#include "drivers/sd_card.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LODEPNG_NO_COMPILE_CPP

extern "C" {
#include "src/extra/libs/png/lodepng.h"
}

//#if LV_USE_PNG
//#include "src/extra/libs/png/lodepng.h"
//#endif


static const char *TAG_ASSETS = "EcoPower_ASSETS";

struct CachedPng {
    char path[128];
    lv_img_dsc_t dsc;
    uint8_t *pixels;
    size_t size;
};

static constexpr size_t MAX_CACHED_PNG = 8;
static CachedPng *cache[MAX_CACHED_PNG] = {};

void ecopower_assets_init(void)
{
    ESP_LOGI(TAG_ASSETS, "Static PNG pre-decoder ready");
}

static CachedPng *find_cached(const char *path)
{
    for (auto *item : cache) {
        if (item && strcmp(item->path, path) == 0) return item;
    }
    return nullptr;
}

static bool add_to_cache(CachedPng *item)
{
    for (auto &slot : cache) {
        if (!slot) {
            slot = item;
            return true;
        }
    }
    return false;
}

static FILE *open_with_retry(const char *path)
{
    for (int attempt = 1; attempt <= 4; ++attempt) {
        FILE *f = fopen(path, "rb");
        if (f) return f;
        ESP_LOGW(TAG_ASSETS, "Open attempt %d failed: %s", attempt, path);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    return nullptr;
}

lv_img_dsc_t *ecopower_load_png_from_sd(const char *path)
{
#if !LV_USE_PNG
    ESP_LOGE(TAG_ASSETS, "LV_USE_PNG is disabled");
    return nullptr;
#else
    if (!path) return nullptr;
    if (CachedPng *existing = find_cached(path)) return &existing->dsc;

    FILE *f = open_with_retry(path);
    if (!f) {
        ESP_LOGE(TAG_ASSETS, "Open failed permanently: %s", path);
        return nullptr;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return nullptr;
    }
    const long file_size = ftell(f);
    rewind(f);
    if (file_size <= 0) {
        fclose(f);
        return nullptr;
    }

    uint8_t *compressed = static_cast<uint8_t *>(heap_caps_malloc(
        static_cast<size_t>(file_size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!compressed) {
        fclose(f);
        ESP_LOGE(TAG_ASSETS, "Compressed PNG allocation failed");
        return nullptr;
    }

    const size_t read = fread(compressed, 1, static_cast<size_t>(file_size), f);
    fclose(f);
    if (read != static_cast<size_t>(file_size)) {
        heap_caps_free(compressed);
        ESP_LOGE(TAG_ASSETS, "PNG read failed: %u/%u", (unsigned)read, (unsigned)file_size);
        return nullptr;
    }

    unsigned char *rgba = nullptr;
    unsigned width = 0;
    unsigned height = 0;
    const unsigned decode_error = lodepng_decode32(&rgba, &width, &height,
                                                    compressed, static_cast<size_t>(file_size));
    heap_caps_free(compressed);
    if (decode_error != 0 || rgba == nullptr || width == 0 || height == 0) {
        ESP_LOGE(TAG_ASSETS, "PNG decode failed (%u): %s", decode_error,
                 lodepng_error_text(decode_error));
        free(rgba);
        return nullptr;
    }

    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t rgb565_size = pixel_count * sizeof(lv_color_t);
    lv_color_t *rgb565 = static_cast<lv_color_t *>(heap_caps_malloc(
        rgb565_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!rgb565) {
        free(rgba);
        ESP_LOGE(TAG_ASSETS, "RGB565 PSRAM allocation failed: %u bytes", (unsigned)rgb565_size);
        return nullptr;
    }

    for (size_t i = 0; i < pixel_count; ++i) {
        rgb565[i] = lv_color_make(rgba[i * 4U], rgba[i * 4U + 1U], rgba[i * 4U + 2U]);
    }
    free(rgba);

    CachedPng *asset = static_cast<CachedPng *>(heap_caps_calloc(
        1, sizeof(CachedPng), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!asset) {
        heap_caps_free(rgb565);
        return nullptr;
    }

    strncpy(asset->path, path, sizeof(asset->path) - 1U);
    asset->pixels = reinterpret_cast<uint8_t *>(rgb565);
    asset->size = rgb565_size;
    asset->dsc.header.always_zero = 0;
    asset->dsc.header.w = width;
    asset->dsc.header.h = height;
    asset->dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    asset->dsc.data_size = rgb565_size;
    asset->dsc.data = asset->pixels;

    if (!add_to_cache(asset)) {
        heap_caps_free(asset->pixels);
        heap_caps_free(asset);
        return nullptr;
    }

    ESP_LOGI(TAG_ASSETS, "PNG decoded once to RGB565 PSRAM: %s (%ux%u, %u bytes)",
             path, width, height, (unsigned)rgb565_size);
    return &asset->dsc;
#endif
}
