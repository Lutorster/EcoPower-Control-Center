#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t start_address;
    uint16_t end_address;
    uint16_t block_size;
    uint32_t pause_ms;
    uint8_t max_consecutive_failures;
} EcoPowerDeyeScannerConfig;

esp_err_t ecopower_deye_scanner_start(
    const EcoPowerDeyeScannerConfig *config);

bool ecopower_deye_scanner_is_running(void);
void ecopower_deye_scanner_stop(void);

#ifdef __cplusplus
}
#endif
