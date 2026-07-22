#include "storage_manager.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr const char *kTag = "EcoPower_Storage";
constexpr const char *kRoot = "/sdcard/ecopower";
constexpr size_t kPathCapacity = 192U;

SemaphoreHandle_t g_mutex = nullptr;
bool g_ready = false;

bool is_safe_relative_path(const char *path)
{
    if (path == nullptr || path[0] == '\0' || path[0] == '/') {
        return false;
    }

    return std::strstr(path, "..") == nullptr;
}

esp_err_t make_full_path(
    const char *relative_path,
    char *output,
    size_t output_size)
{
    if (!is_safe_relative_path(relative_path) ||
        output == nullptr ||
        output_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const int written = std::snprintf(
        output,
        output_size,
        "%s/%s",
        kRoot,
        relative_path);

    if (written < 0 || static_cast<size_t>(written) >= output_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t ensure_directory(const char *path)
{
    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }

    ESP_LOGE(
        kTag,
        "Cannot create directory %s: errno=%d (%s)",
        path,
        errno,
        std::strerror(errno));
    return ESP_FAIL;
}

esp_err_t create_directory_tree()
{
    static constexpr const char *kDirectories[] = {
        kRoot,
        "/sdcard/ecopower/config",
        "/sdcard/ecopower/state",
        "/sdcard/ecopower/history",
        "/sdcard/ecopower/logs",
        "/sdcard/ecopower/backup",
    };

    for (const char *directory : kDirectories) {
        const esp_err_t error = ensure_directory(directory);
        if (error != ESP_OK) {
            return error;
        }
    }

    return ESP_OK;
}

class MutexLock {
public:
    explicit MutexLock(SemaphoreHandle_t mutex)
        : mutex_(mutex), locked_(false)
    {
        if (mutex_ != nullptr) {
            locked_ =
                xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
        }
    }

    ~MutexLock()
    {
        if (locked_) {
            xSemaphoreGive(mutex_);
        }
    }

    bool locked() const { return locked_; }

private:
    SemaphoreHandle_t mutex_;
    bool locked_;
};

} // namespace

extern "C" esp_err_t ecopower_storage_manager_init(bool sd_available)
{
    if (g_ready) {
        return ESP_OK;
    }

    if (!sd_available) {
        ESP_LOGW(kTag, "Storage disabled because SD initialization failed");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_mutex == nullptr) {
        g_mutex = xSemaphoreCreateMutex();
        if (g_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    MutexLock lock(g_mutex);
    if (!lock.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t error = create_directory_tree();
    if (error != ESP_OK) {
        return error;
    }

    g_ready = true;
    ESP_LOGI(kTag, "Local storage ready at %s", kRoot);
    return ESP_OK;
}

extern "C" bool ecopower_storage_manager_is_ready(void)
{
    return g_ready;
}

extern "C" esp_err_t ecopower_storage_write_atomic(
    const char *relative_path,
    const void *data,
    size_t size)
{
    if (!g_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if ((data == nullptr && size != 0U) || relative_path == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    char target[kPathCapacity] = {};
    esp_err_t error = make_full_path(
        relative_path,
        target,
        sizeof(target));
    if (error != ESP_OK) {
        return error;
    }

    char temporary[kPathCapacity] = {};
    char backup[kPathCapacity] = {};

    if (std::snprintf(temporary, sizeof(temporary), "%s.tmp", target) >=
            static_cast<int>(sizeof(temporary)) ||
        std::snprintf(backup, sizeof(backup), "%s.bak", target) >=
            static_cast<int>(sizeof(backup))) {
        return ESP_ERR_INVALID_SIZE;
    }

    MutexLock lock(g_mutex);
    if (!lock.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    FILE *file = std::fopen(temporary, "wb");
    if (file == nullptr) {
        ESP_LOGE(kTag, "Open for write failed: %s", temporary);
        return ESP_FAIL;
    }

    const size_t written =
        size == 0U ? 0U : std::fwrite(data, 1U, size, file);

    bool write_ok = written == size && std::fflush(file) == 0;
    if (write_ok) {
        write_ok = fsync(fileno(file)) == 0;
    }

    if (std::fclose(file) != 0) {
        write_ok = false;
    }

    if (!write_ok) {
        std::remove(temporary);
        ESP_LOGE(kTag, "Write failed: %s", temporary);
        return ESP_FAIL;
    }

    std::remove(backup);
    const bool had_old_file = access(target, F_OK) == 0;

    if (had_old_file && std::rename(target, backup) != 0) {
        std::remove(temporary);
        ESP_LOGE(kTag, "Cannot preserve old file: %s", target);
        return ESP_FAIL;
    }

    if (std::rename(temporary, target) != 0) {
        if (had_old_file) {
            std::rename(backup, target);
        }
        std::remove(temporary);
        ESP_LOGE(kTag, "Atomic replace failed: %s", target);
        return ESP_FAIL;
    }

    if (had_old_file) {
        std::remove(backup);
    }

    return ESP_OK;
}

extern "C" esp_err_t ecopower_storage_read(
    const char *relative_path,
    void *data,
    size_t capacity,
    size_t *out_size)
{
    if (out_size != nullptr) {
        *out_size = 0U;
    }

    if (!g_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (relative_path == nullptr || data == nullptr || capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[kPathCapacity] = {};
    const esp_err_t path_error = make_full_path(
        relative_path,
        path,
        sizeof(path));
    if (path_error != ESP_OK) {
        return path_error;
    }

    MutexLock lock(g_mutex);
    if (!lock.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    FILE *file = std::fopen(path, "rb");
    if (file == nullptr) {
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return ESP_FAIL;
    }

    const long file_size = std::ftell(file);
    if (file_size < 0) {
        std::fclose(file);
        return ESP_FAIL;
    }

    if (static_cast<size_t>(file_size) > capacity) {
        std::fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }

    std::rewind(file);
    const size_t bytes_read = std::fread(
        data,
        1U,
        static_cast<size_t>(file_size),
        file);
    const bool read_ok =
        bytes_read == static_cast<size_t>(file_size) &&
        std::ferror(file) == 0;

    std::fclose(file);

    if (!read_ok) {
        return ESP_FAIL;
    }

    if (out_size != nullptr) {
        *out_size = bytes_read;
    }

    return ESP_OK;
}
