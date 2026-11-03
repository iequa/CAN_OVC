#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_send_text(const char *title, const char *album,
                            const char *artist);
esp_err_t display_send_text_cmd(uint8_t cmd, uint8_t type,
                                const char *title, const char *album,
                                const char *artist);
esp_err_t display_send_fields(uint8_t cmd, uint8_t type,
                              const uint8_t *ids,
                              const char *const *texts,
                              size_t count);

#ifdef __cplusplus
}
#endif
