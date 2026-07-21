#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ecopower_time_manager_init(void);
esp_err_t ecopower_time_manager_resynchronize(void);

bool ecopower_time_manager_is_initialized(void);
bool ecopower_time_manager_is_synchronized(void);

bool ecopower_time_manager_get_time(
    char *buffer,
    size_t buffer_size);

bool ecopower_time_manager_get_date(
    char *buffer,
    size_t buffer_size);

bool ecopower_time_manager_get_datetime(
    char *buffer,
    size_t buffer_size);

#ifdef __cplusplus
}
#endif
