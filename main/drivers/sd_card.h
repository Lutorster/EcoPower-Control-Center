#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool ecopower_sd_init(void);

/* Restore the CH422G output state required for stable SD access,
 * while keeping the LCD/touch state intact. Safe to call repeatedly. */
bool ecopower_sd_prepare_access(void);

#ifdef __cplusplus
}
#endif
