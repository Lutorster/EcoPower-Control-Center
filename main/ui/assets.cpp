#include "assets.h"
#include "drivers/sd_card.h"

#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if LV_USE_PNG
#include "src/extra/libs/png/lv_png.h"
#endif

static const char *TAG_ASSETS = "EcoPower_ASSETS";

struct CachedPng {
    char path[128];
    lv_img_dsc_t dsc;
    uint8_t *data;
    size_t size;
};

static constexpr size_t MAX_CACHED_PNG = 8;
static CachedPng *cache[MAX_CACHED_PNG] = {};
static bool decoder_initialized = false;

void ecopower_assets_init(void)
{
#if LV_USE_PNG
    if (!decoder_initialized) {
        lv_png_init();
        decoder_initialized = true;
        ESP_LOGI(TAG_ASSETS, "PNG decoder enabled");
    }
#else
    ESP_LOGE(TAG_ASSETS, "LV_USE_PNG is disabled");
#endif
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
    ESP_LOGE(TAG_ASSETS, "PNG decoder is not enabled");
    return nullptr;
#else
    if (!path) return nullptr;

    if (CachedPng *existing = find_cached(path)) {
        return &existing->dsc;
    }

    FILE *f = open_with_retry(path);
    if (!f) {
        ESP_LOGE(TAG_ASSETS, "Open failed permanently: %s", path);
        return nullptr;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        ESP_LOGE(TAG_ASSETS, "Seek failed: %s", path);
        return nullptr;
    }

    const long file_size = ftell(f);
    rewind(f);

    if (file_size <= 0) {
        fclose(f);
        ESP_LOGE(TAG_ASSETS, "Bad file size: %s", path);
        return nullptr;
    }

    CachedPng *asset = static_cast<CachedPng *>(heap_caps_calloc(
        1, sizeof(CachedPng), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    if (!asset) {
        fclose(f);
        ESP_LOGE(TAG_ASSETS, "Descriptor allocation failed");
        return nullptr;
    }

    asset->size = static_cast<size_t>(file_size);
    asset->data = static_cast<uint8_t *>(heap_caps_malloc(
        asset->size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!asset->data) {
        fclose(f);
        heap_caps_free(asset);
        ESP_LOGE(TAG_ASSETS, "PSRAM allocation failed: %u bytes", (unsigned)asset->size);
        return nullptr;
    }

    size_t total = 0;
    while (total < asset->size) {
        const size_t chunk = fread(asset->data + total, 1, asset->size - total, f);
        if (chunk == 0) break;
        total += chunk;
    }
    fclose(f);

    if (total != asset->size) {
        ESP_LOGE(TAG_ASSETS, "Read failed: %u/%u for %s",
                 (unsigned)total, (unsigned)asset->size, path);
        heap_caps_free(asset->data);
        heap_caps_free(asset);
        return nullptr;
    }

    strncpy(asset->path, path, sizeof(asset->path) - 1);
    memset(&asset->dsc, 0, sizeof(asset->dsc));
    asset->dsc.header.always_zero = 0;
    asset->dsc.header.w = 0;
    asset->dsc.header.h = 0;
    asset->dsc.header.cf = LV_IMG_CF_RAW_ALPHA;
    asset->dsc.data_size = asset->size;
    asset->dsc.data = asset->data;

    if (!add_to_cache(asset)) {
        ESP_LOGE(TAG_ASSETS, "PNG cache is full");
        heap_caps_free(asset->data);
        heap_caps_free(asset);
        return nullptr;
    }

    ESP_LOGI(TAG_ASSETS, "Loaded PNG to PSRAM: %s (%u bytes)",
             path, (unsigned)asset->size);
    return &asset->dsc;
#endif
}
