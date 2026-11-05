#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t time_sync_start(void);
bool time_sync_get_epoch(time_t *epoch_seconds);

#ifdef __cplusplus
}
#endif

#endif
