#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ECOPOWER_HA_DISABLED = 0,
    ECOPOWER_HA_WAITING_MQTT,
    ECOPOWER_HA_PUBLISHING,
    ECOPOWER_HA_READY,
    ECOPOWER_HA_ERROR
} EcoPowerHaState;

esp_err_t ecopower_ha_discovery_init(void);
EcoPowerHaState ecopower_ha_discovery_get_state(void);
bool ecopower_ha_discovery_is_ready(void);

#ifdef __cplusplus
}
#endif
