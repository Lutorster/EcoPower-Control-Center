#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize EcoPower local storage after the SD card has been mounted.
 *
 * The manager creates the base directory tree and becomes the only module
 * that should perform application-level file operations on the SD card.
 */
esp_err_t ecopower_storage_manager_init(bool sd_available);

/** Return true when the SD card and EcoPower directory tree are ready. */
bool ecopower_storage_manager_is_ready(void);

/**
 * Atomically replace a file below /sdcard/ecopower.
 * relative_path example: "state/runtime.bin".
 */
esp_err_t ecopower_storage_write_atomic(
    const char *relative_path,
    const void *data,
    size_t size);

/**
 * Read a complete file below /sdcard/ecopower.
 * out_size receives the actual number of bytes read.
 */
esp_err_t ecopower_storage_read(
    const char *relative_path,
    void *data,
    size_t capacity,
    size_t *out_size);

/** Create a directory below /sdcard/ecopower if it does not exist. */
esp_err_t ecopower_storage_ensure_directory(const char *relative_path);

/** Return true when a file or directory exists below /sdcard/ecopower. */
bool ecopower_storage_exists(const char *relative_path);

/** Append bytes to a file below /sdcard/ecopower and flush them to SD. */
esp_err_t ecopower_storage_append(
    const char *relative_path,
    const void *data,
    size_t size);

#ifdef __cplusplus
}
#endif
