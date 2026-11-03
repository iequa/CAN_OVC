#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bluetooth A2DP sink + AVRCP metadata + HFP caller indication.
 *
 * bt_audio_get_lines() returns three display lines in the order expected
 * by the GID:
 *   top    - small line (album / status)
 *   middle - large line (track title / caller)
 *   bottom - small line (artist / status)
 */
void bt_audio_init(void);
bool bt_audio_get_lines(char *top, size_t top_len,
                        char *middle, size_t middle_len,
                        char *bottom, size_t bottom_len);
bool bt_audio_is_connected(void);
bool bt_audio_is_call_active(void);

#ifdef __cplusplus
}
#endif
