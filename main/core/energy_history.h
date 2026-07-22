#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start local five-minute energy history recording on the SD card. */
esp_err_t ecopower_energy_history_init(void);

#ifdef __cplusplus
}
#endif
