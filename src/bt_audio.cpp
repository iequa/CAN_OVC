#include "bt_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_hf_client_api.h"
#include "nvs_flash.h"

static const char *TAG = "BT_AUDIO";

#define LINE_LEN 32
#define META_LEN 96
#define TEXT_MAX_BYTES 20   // keep the whole 3-line payload below 255 bytes
#define TITLE_WINDOW 20
#define TITLE_SCROLL_MS 500

// HFP is needed for caller-ID. It can also take call audio to the ESP32;
// set to 0 if only music metadata is wanted.
#define BT_ENABLE_HFP 1

// Fixed Bluetooth pairing PIN (legacy pairing).
#define BT_PIN "6424"

#define TRANSIENT_MS 2000

typedef enum {
    CALL_NONE = 0,
    CALL_INCOMING,
    CALL_OUTGOING,
    CALL_ACTIVE,
} call_state_t;

static SemaphoreHandle_t s_lock = NULL;

static bool s_a2dp_connected = false;
static bool s_playing = false;
static bool s_avrc_connected = false;
static bool s_hf_slc = false;

// Metadata received over AVRCP
static char s_meta_title[META_LEN];
static char s_meta_artist[META_LEN];
static char s_meta_album[META_LEN];

// Lines currently shown on the GID
static char s_top[LINE_LEN];
static char s_mid[LINE_LEN];
static char s_bot[LINE_LEN];

static char s_caller[LINE_LEN];
static call_state_t s_call_state = CALL_NONE;
static bool s_call_incoming = false;
static uint64_t s_call_started_ms = 0;

// AVRCP playback position / duration
static uint32_t s_song_len_ms = 0;
static uint32_t s_song_pos_ms = 0;
static uint64_t s_pos_updated_us = 0;
static int s_track_num = 0;
static int s_track_count = 0;

static int s_battery_level = -1;   // HFP 0..5, -1 = unknown
static uint64_t s_title_scroll_start_ms = 0;
static uint64_t s_artist_scroll_start_ms = 0;
static uint64_t s_a2dp_meta_deadline_ms = 0;
static bool s_a2dp_meta_seen = false;
static volatile bool s_audio_streaming = false;
static volatile uint64_t s_last_pcm_ms = 0;

// Temporary top-line message (next/prev)
static char s_transient[20];
static uint64_t s_transient_until_ms = 0;

static esp_bd_addr_t s_peer_bda = {0};
static uint8_t s_tl = 0;

// AVRCP/HFP APIs are called from a worker task, not directly from
// Bluedroid callbacks.
static volatile bool s_req_caps = false;
static volatile bool s_req_register = false;
static volatile bool s_req_metadata = false;
static volatile bool s_req_play_status = false;
static volatile bool s_req_pos_register = false;
static volatile bool s_req_hf_connect = false;
static volatile bool s_req_clcc = false;

static void copy_text(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= dst_len) n = dst_len - 1;
    if (n > TEXT_MAX_BYTES) n = TEXT_MAX_BYTES;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void copy_avrc_text(char *dst, size_t dst_len,
                           const uint8_t *src, int src_len)
{
    if (!dst || dst_len == 0) return;
    if (!src || src_len <= 0) {
        dst[0] = '\0';
        return;
    }

    size_t n = (size_t)src_len;
    if (n >= dst_len) n = dst_len - 1;
    if (n > META_LEN - 1) {
        n = META_LEN - 1;
        // don't split a UTF-8 sequence
        while (n > 0 && (src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';

    // trim trailing CR/LF/spaces
    while (n > 0 && (dst[n - 1] == '\r' || dst[n - 1] == '\n' ||
                     dst[n - 1] == ' ')) {
        dst[--n] = '\0';
    }
}

struct translit_entry {
    uint16_t cp;
    const char *latin;
};

static const translit_entry TRANSLIT[] = {
    {0x0401, "Yo"}, {0x0410, "A"}, {0x0411, "B"}, {0x0412, "V"},
    {0x0413, "G"},  {0x0414, "D"}, {0x0415, "E"}, {0x0416, "Zh"},
    {0x0417, "Z"},  {0x0418, "I"}, {0x0419, "Y"}, {0x041A, "K"},
    {0x041B, "L"},  {0x041C, "M"}, {0x041D, "N"}, {0x041E, "O"},
    {0x041F, "P"},  {0x0420, "R"}, {0x0421, "S"}, {0x0422, "T"},
    {0x0423, "U"},  {0x0424, "F"}, {0x0425, "Kh"}, {0x0426, "Ts"},
    {0x0427, "Ch"}, {0x0428, "Sh"}, {0x0429, "Shch"}, {0x042A, ""},
    {0x042B, "Y"},  {0x042C, ""}, {0x042D, "E"}, {0x042E, "Yu"},
    {0x042F, "Ya"}, {0x0430, "a"}, {0x0431, "b"}, {0x0432, "v"},
    {0x0433, "g"},  {0x0434, "d"}, {0x0435, "e"}, {0x0436, "zh"},
    {0x0437, "z"},  {0x0438, "i"}, {0x0439, "y"}, {0x043A, "k"},
    {0x043B, "l"},  {0x043C, "m"}, {0x043D, "n"}, {0x043E, "o"},
    {0x043F, "p"},  {0x0440, "r"}, {0x0441, "s"}, {0x0442, "t"},
    {0x0443, "u"},  {0x0444, "f"}, {0x0445, "kh"}, {0x0446, "ts"},
    {0x0447, "ch"}, {0x0448, "sh"}, {0x0449, "shch"}, {0x044A, ""},
    {0x044B, "y"},  {0x044C, ""}, {0x044D, "e"}, {0x044E, "yu"},
    {0x044F, "ya"}, {0x0451, "yo"},
    // Ukrainian / Belarusian
    {0x0404, "Ye"}, {0x0454, "ye"}, {0x0406, "I"}, {0x0456, "i"},
    {0x0407, "Yi"}, {0x0457, "yi"}, {0x040E, "U"}, {0x045E, "u"},
    {0x0490, "G"},  {0x0491, "g"},
};

static const char *translit_lookup(uint32_t cp)
{
    for (size_t i = 0; i < sizeof(TRANSLIT) / sizeof(TRANSLIT[0]); i++) {
        if (TRANSLIT[i].cp == cp) return TRANSLIT[i].latin;
    }
    return NULL;
}

// UTF-8 -> readable Latin. Cyrillic is transliterated, other characters
// are passed through (or replaced with a space if control characters).
static void translit_utf8(const char *src, char *dst, size_t dst_len)
{
    if (!dst || dst_len == 0) return;
    size_t out = 0;
    const uint8_t *s = (const uint8_t *)src;

    while (s && *s && out + 1 < dst_len) {
        uint32_t cp = 0;
        size_t len = 0;

        if (*s < 0x80) {
            cp = *s;
            len = 1;
        } else if ((*s & 0xE0) == 0xC0 && s[1]) {
            cp = ((uint32_t)(s[0] & 0x1F) << 6) |
                 (uint32_t)(s[1] & 0x3F);
            len = 2;
        } else if ((*s & 0xF0) == 0xE0 && s[1] && s[2]) {
            cp = ((uint32_t)(s[0] & 0x0F) << 12) |
                 ((uint32_t)(s[1] & 0x3F) << 6) |
                 (uint32_t)(s[2] & 0x3F);
            len = 3;
        } else if ((*s & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
            cp = ((uint32_t)(s[0] & 0x07) << 18) |
                 ((uint32_t)(s[1] & 0x3F) << 12) |
                 ((uint32_t)(s[2] & 0x3F) << 6) |
                 (uint32_t)(s[3] & 0x3F);
            len = 4;
        } else {
            s++;
            continue;
        }

        const char *tr = translit_lookup(cp);
        if (tr) {
            size_t n = strlen(tr);
            if (out + n >= dst_len) break;
            memcpy(dst + out, tr, n);
            out += n;
        } else if (cp < 0x20) {
            dst[out++] = ' ';
        } else {
            if (out + len >= dst_len) break;
            memcpy(dst + out, s, len);
            out += len;
        }
        s += len;
    }

    dst[out] = '\0';
}

static bool is_placeholder(const char *s)
{
    return !s || !s[0] ||
           strcasecmp(s, "not provided") == 0 ||
           strcasecmp(s, "unknown") == 0 ||
           strcasecmp(s, "<unknown>") == 0;
}

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void format_time(char *dst, size_t dst_len, uint32_t ms)
{
    uint32_t total = ms / 1000;
    uint32_t h = total / 3600;
    uint32_t m = (total / 60) % 60;
    uint32_t s = total % 60;

    if (h > 0) {
        snprintf(dst, dst_len, "%u:%02u:%02u", (unsigned)h,
                 (unsigned)m, (unsigned)s);
    } else {
        snprintf(dst, dst_len, "%u:%02u", (unsigned)m, (unsigned)s);
    }
}

static void update_position_locked(void)
{
    if (!s_playing || s_pos_updated_us == 0) return;

    uint64_t elapsed_ms = (now_ms() - s_pos_updated_us);
    uint64_t pos = (uint64_t)s_song_pos_ms + elapsed_ms;
    if (s_song_len_ms && pos > s_song_len_ms) pos = s_song_len_ms;
    s_song_pos_ms = (uint32_t)pos;
    s_pos_updated_us = now_ms();
}

static void set_transient_locked(const char *text)
{
    snprintf(s_transient, sizeof(s_transient), "%s", text);
    s_transient_until_ms = now_ms() + TRANSIENT_MS;
}

static void battery_prefix_locked(char *dst, size_t dst_len)
{
    if (s_battery_level < 0) {
        snprintf(dst, dst_len, "[---]--%% ");
        return;
    }

    int level = s_battery_level;
    if (level > 5) level = 5;
    int pct = level * 20;
    int bars = (level * 3 + 4) / 5;   // 0..3
    char icon[6] = "[---]";
    for (int i = 0; i < bars; i++) {
        icon[1 + i] = '=';
    }
    snprintf(dst, dst_len, "%s%d%% ", icon, pct);
}

static void make_scrolling_line_locked(const char *text, uint64_t start_ms,
                                       char *dst, size_t dst_len)
{
    if (!text || !text[0]) {
        if (dst_len) dst[0] = '\0';
        return;
    }

    size_t len = strlen(text);
    if (len <= TITLE_WINDOW) {
        snprintf(dst, dst_len, "%s", text);
        return;
    }

    // Paused: no scrolling, show the beginning of the line.
    if (!s_playing) {
        size_t n = 0;
        while (n < TITLE_WINDOW && text[n]) {
            unsigned char c = (unsigned char)text[n];
            size_t clen = 1;
            if ((c & 0xE0) == 0xC0) clen = 2;
            else if ((c & 0xF0) == 0xE0) clen = 3;
            else if ((c & 0xF8) == 0xF0) clen = 4;
            if (n + clen > TITLE_WINDOW) break;
            n += clen;
        }
        if (n >= dst_len) n = dst_len - 1;
        memcpy(dst, text, n);
        dst[n] = '\0';
        return;
    }

    char buf[META_LEN * 2 + 8];
    snprintf(buf, sizeof(buf), "%s    %s", text, text);

    size_t cycle = len + 4;
    uint64_t elapsed = now_ms() - start_ms;
    size_t off = (size_t)((elapsed / TITLE_SCROLL_MS) % cycle);

    // don't start in the middle of a UTF-8 sequence
    while (off < sizeof(buf) && (buf[off] & 0xC0) == 0x80) {
        off++;
    }

    size_t n = 0;
    while (n < TITLE_WINDOW && buf[off + n]) {
        unsigned char c = (unsigned char)buf[off + n];
        size_t clen = 1;
        if ((c & 0xE0) == 0xC0) clen = 2;
        else if ((c & 0xF0) == 0xE0) clen = 3;
        else if ((c & 0xF8) == 0xF0) clen = 4;
        if (n + clen > TITLE_WINDOW) break;
        n += clen;
    }

    if (n >= dst_len) n = dst_len - 1;
    memcpy(dst, buf + off, n);
    dst[n] = '\0';
}

static void make_title_line_locked(char *dst, size_t dst_len)
{
    if (!s_meta_title[0]) {
        snprintf(dst, dst_len, "NO TRACK");
        return;
    }
    make_scrolling_line_locked(s_meta_title, s_title_scroll_start_ms,
                               dst, dst_len);
}

static void make_artist_line_locked(char *dst, size_t dst_len)
{
    make_scrolling_line_locked(s_meta_artist, s_artist_scroll_start_ms,
                               dst, dst_len);
}

static void ui_refresh_locked(void)
{
    // Safety net: some phones do not send CALL_SETUP=0 after a cancelled
    // outgoing/incoming call. Do not leave the call screen stuck forever.
    if ((s_call_state == CALL_INCOMING || s_call_state == CALL_OUTGOING) &&
        s_call_started_ms != 0 &&
        now_ms() - s_call_started_ms > 120000) {
        s_call_state = CALL_NONE;
        s_call_incoming = false;
        s_caller[0] = '\0';
        s_call_started_ms = 0;
    }

    if (s_call_state != CALL_NONE) {
        const char *who = s_caller[0] ? s_caller : "UNKNOWN";
        if (s_call_state == CALL_INCOMING) {
            snprintf(s_top, sizeof(s_top), "INCOMING CALL <<");
        } else if (s_call_state == CALL_OUTGOING) {
            snprintf(s_top, sizeof(s_top), "CALL >>");
        } else {
            snprintf(s_top, sizeof(s_top), "CALL");
        }
        snprintf(s_mid, sizeof(s_mid), "%s", who);
        snprintf(s_bot, sizeof(s_bot), "%s",
                 s_call_state == CALL_ACTIVE ? "ACTIVE" : "");
        return;
    }

    if (!s_a2dp_connected) {
        snprintf(s_top, sizeof(s_top), "BT:off");
        snprintf(s_mid, sizeof(s_mid), "WAITING");
        snprintf(s_bot, sizeof(s_bot), "PAIR PHONE");
        return;
    }

    // Some phones send 0xFFFFFFFF for unknown duration/position.
    if (s_song_len_ms > 24u * 3600u * 1000u) s_song_len_ms = 0;
    if (s_song_len_ms && s_song_pos_ms > s_song_len_ms) {
        s_song_pos_ms = s_song_len_ms;
    }

    update_position_locked();

    char status[LINE_LEN];
    if (s_transient[0] && now_ms() < s_transient_until_ms) {
        snprintf(status, sizeof(status), "%s", s_transient);
    } else if (s_playing) {
        if (s_song_len_ms) {
            char cur[10], total[10];
            format_time(cur, sizeof(cur), s_song_pos_ms);
            format_time(total, sizeof(total), s_song_len_ms);
            snprintf(status, sizeof(status), "%s/%s", cur, total);
        } else {
            snprintf(status, sizeof(status), "BT:play");
        }
    } else if (s_meta_title[0]) {
        snprintf(status, sizeof(status), "BT:pause");
    } else {
        snprintf(status, sizeof(status), "BT:ready");
    }

    char battery[12];
    battery_prefix_locked(battery, sizeof(battery));
    snprintf(s_top, sizeof(s_top), "%s", battery);
    strncat(s_top, status, sizeof(s_top) - strlen(s_top) - 1);

    make_title_line_locked(s_mid, sizeof(s_mid));
    make_artist_line_locked(s_bot, sizeof(s_bot));
}

static void clear_metadata_locked(void)
{
    s_playing = false;
    s_a2dp_meta_deadline_ms = 0;
    s_a2dp_meta_seen = false;
    s_meta_title[0] = s_meta_artist[0] = s_meta_album[0] = '\0';
    s_song_len_ms = 0;
    s_song_pos_ms = 0;
    s_pos_updated_us = 0;
    s_track_num = 0;
    s_track_count = 0;
    s_title_scroll_start_ms = now_ms();
    s_artist_scroll_start_ms = now_ms();
    s_transient[0] = '\0';
    ui_refresh_locked();
}

static void set_connected(bool connected)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_a2dp_connected = connected;
        if (!connected) {
            s_playing = false;
            s_avrc_connected = false;
            s_meta_title[0] = s_meta_artist[0] = s_meta_album[0] = '\0';
            s_call_state = CALL_NONE;
            s_caller[0] = '\0';
            s_call_started_ms = 0;
            s_song_len_ms = 0;
            s_song_pos_ms = 0;
            s_pos_updated_us = 0;
            s_track_num = 0;
            s_track_count = 0;
            s_transient[0] = '\0';
            s_battery_level = -1;
            s_audio_streaming = false;
            s_last_pcm_ms = 0;
            s_title_scroll_start_ms = now_ms();
            s_artist_scroll_start_ms = now_ms();
        }
        ui_refresh_locked();
        xSemaphoreGive(s_lock);
    }
}

static void set_playing(bool playing)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        if (!playing) {
            update_position_locked();
            s_title_scroll_start_ms = now_ms();
            s_artist_scroll_start_ms = now_ms();
        }
        s_playing = playing;
        if (playing) {
            s_pos_updated_us = now_ms();
            s_title_scroll_start_ms = now_ms();
            s_artist_scroll_start_ms = now_ms();
        }
        ui_refresh_locked();
        xSemaphoreGive(s_lock);
    }
}

static void set_call(call_state_t state, bool incoming, const char *who)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_call_state = state;
        s_call_incoming = incoming;
        if (state == CALL_INCOMING || state == CALL_OUTGOING) {
            if (s_call_started_ms == 0) s_call_started_ms = now_ms();
        } else {
            s_call_started_ms = 0;
        }
        if (who) {
            translit_utf8(who, s_caller, sizeof(s_caller));
        }
        ui_refresh_locked();
        xSemaphoreGive(s_lock);
    }
}

static void clear_call(void)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_call_state = CALL_NONE;
        s_call_incoming = false;
        s_caller[0] = '\0';
        s_call_started_ms = 0;
        ui_refresh_locked();
        xSemaphoreGive(s_lock);
    }
}

static void set_track_attr(uint8_t attr_id, const uint8_t *text, int len)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        switch (attr_id) {
        case ESP_AVRC_MD_ATTR_TITLE: {
            char tmp[META_LEN], latin[META_LEN];
            copy_avrc_text(tmp, sizeof(tmp), text, len);
            translit_utf8(tmp, latin, sizeof(latin));
            if (is_placeholder(latin)) latin[0] = '\0';
            if (latin[0] && s_a2dp_meta_deadline_ms != 0) {
                s_a2dp_meta_seen = true;
            }
            if (strcmp(latin, s_meta_title) != 0) {
                snprintf(s_meta_title, sizeof(s_meta_title), "%s", latin);
                s_title_scroll_start_ms = now_ms();
            }
            break;
        }
        case ESP_AVRC_MD_ATTR_ARTIST: {
            char tmp[META_LEN], latin[META_LEN];
            copy_avrc_text(tmp, sizeof(tmp), text, len);
            translit_utf8(tmp, latin, sizeof(latin));
            if (is_placeholder(latin)) latin[0] = '\0';
            if (latin[0] && s_a2dp_meta_deadline_ms != 0) {
                s_a2dp_meta_seen = true;
            }
            if (strcmp(latin, s_meta_artist) != 0) {
                snprintf(s_meta_artist, sizeof(s_meta_artist), "%s", latin);
                s_artist_scroll_start_ms = now_ms();
            }
            break;
        }
        case ESP_AVRC_MD_ATTR_ALBUM: {
            char tmp[META_LEN], latin[META_LEN];
            copy_avrc_text(tmp, sizeof(tmp), text, len);
            translit_utf8(tmp, latin, sizeof(latin));
            if (is_placeholder(latin)) latin[0] = '\0';
            if (latin[0] && s_a2dp_meta_deadline_ms != 0) {
                s_a2dp_meta_seen = true;
            }
            snprintf(s_meta_album, sizeof(s_meta_album), "%s", latin);
            break;
        }
        case ESP_AVRC_MD_ATTR_TRACK_NUM: {
            char tmp[16];
            copy_avrc_text(tmp, sizeof(tmp), text, len);
            int new_num = atoi(tmp);
            if (s_playing && s_track_num > 0 && new_num > 0 &&
                new_num != s_track_num) {
                set_transient_locked(new_num < s_track_num
                                         ? "<<BT:prev"
                                         : ">>BT:next");
            }
            s_track_num = new_num;
            break;
        }
        case ESP_AVRC_MD_ATTR_NUM_TRACKS: {
            char tmp[16];
            copy_avrc_text(tmp, sizeof(tmp), text, len);
            s_track_count = atoi(tmp);
            break;
        }
        default:
            break;
        }
        ui_refresh_locked();
        xSemaphoreGive(s_lock);
    }
}

static void avrc_request_metadata(void)
{
    uint8_t mask = ESP_AVRC_MD_ATTR_TITLE |
                   ESP_AVRC_MD_ATTR_ARTIST |
                   ESP_AVRC_MD_ATTR_ALBUM |
                   ESP_AVRC_MD_ATTR_TRACK_NUM |
                   ESP_AVRC_MD_ATTR_NUM_TRACKS;
    esp_avrc_ct_send_metadata_cmd(s_tl++ & 0x0F, mask);
}

static void avrc_request_play_status(void)
{
    esp_avrc_ct_send_get_play_status_cmd(s_tl++ & 0x0F);
}

static void avrc_register_notifications(void)
{
    esp_avrc_ct_send_register_notification_cmd(
        s_tl++ & 0x0F, ESP_AVRC_RN_TRACK_CHANGE, 0);
    esp_avrc_ct_send_register_notification_cmd(
        s_tl++ & 0x0F, ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);
    esp_avrc_ct_send_register_notification_cmd(
        s_tl++ & 0x0F, ESP_AVRC_RN_NOW_PLAYING_CHANGE, 0);
    esp_avrc_ct_send_register_notification_cmd(
        s_tl++ & 0x0F, ESP_AVRC_RN_PLAY_POS_CHANGED, 1000);
    esp_avrc_ct_send_register_notification_cmd(
        s_tl++ & 0x0F, ESP_AVRC_RN_BATTERY_STATUS_CHANGE, 0);
    esp_avrc_ct_send_register_notification_cmd(
        s_tl++ & 0x0F, ESP_AVRC_RN_ADDRESSED_PLAYER_CHANGE, 0);
    esp_avrc_ct_send_register_notification_cmd(
        s_tl++ & 0x0F, ESP_AVRC_RN_AVAILABLE_PLAYERS_CHANGE, 0);
}

static void bt_worker_task(void *arg)
{
    (void)arg;
    while (1) {
#if BT_ENABLE_HFP
        if (s_req_hf_connect) {
            s_req_hf_connect = false;
            if (s_a2dp_connected) {
                esp_hf_client_connect(s_peer_bda);
            }
        }
#endif
        if (s_req_caps && s_avrc_connected) {
            s_req_caps = false;
            esp_avrc_ct_send_get_rn_capabilities_cmd(s_tl++ & 0x0F);
        }
        if (s_req_register && s_avrc_connected) {
            s_req_register = false;
            avrc_register_notifications();
        }
        if (s_req_metadata && s_avrc_connected) {
            s_req_metadata = false;
            avrc_request_metadata();
        }
        if (s_req_play_status && s_avrc_connected) {
            s_req_play_status = false;
            avrc_request_play_status();
        }
        if (s_req_pos_register && s_avrc_connected) {
            s_req_pos_register = false;
            esp_avrc_ct_send_register_notification_cmd(
                s_tl++ & 0x0F, ESP_AVRC_RN_PLAY_POS_CHANGED, 1000);
        }
#if BT_ENABLE_HFP
        if (s_req_clcc && s_hf_slc) {
            s_req_clcc = false;
            esp_hf_client_query_current_calls();
        }
#endif
        if (s_a2dp_meta_deadline_ms != 0 &&
            now_ms() > s_a2dp_meta_deadline_ms) {
            if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
                if (!s_a2dp_meta_seen) {
                    ESP_LOGI(TAG, "no metadata after A2DP start, clearing track");
                    clear_metadata_locked();
                }
                s_a2dp_meta_deadline_ms = 0;
                s_a2dp_meta_seen = false;
                xSemaphoreGive(s_lock);
            }
        }
        if (s_audio_streaming && s_playing && s_last_pcm_ms != 0 &&
            now_ms() - s_last_pcm_ms > 2000) {
            ESP_LOGI(TAG, "no PCM for 2s, treating audio as stopped");
            s_audio_streaming = false;
            if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
                clear_metadata_locked();
                xSemaphoreGive(s_lock);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void bt_gap_cb(esp_bt_gap_cb_event_t event,
                      esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "auth complete: status=%d name=%s",
                 param->auth_cmpl.stat, param->auth_cmpl.device_name);
        break;

    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin = {BT_PIN[0], BT_PIN[1], BT_PIN[2], BT_PIN[3]};
        ESP_LOGI(TAG, "PIN request");
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
        break;
    }

    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "SSP confirm: %06" PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;

    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "SSP passkey: %06" PRIu32, param->key_notif.passkey);
        break;

    case ESP_BT_GAP_MODE_CHG_EVT:
        break;

    default:
        break;
    }
}

// Internal A2DP codec decodes SBC to PCM and hands it to this callback.
// Audio output is not wired yet, so just consume the buffer.
static void a2d_data_cb(const uint8_t *buf, uint32_t len)
{
    (void)buf;
    static bool first = true;
    if (len) {
        s_audio_streaming = true;
        s_last_pcm_ms = now_ms();
        if (first) {
            first = false;
            ESP_LOGI(TAG, "A2DP PCM data received (%u bytes)",
                     (unsigned)len);
        }
    }
}

static void a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            memcpy(s_peer_bda, param->conn_stat.remote_bda,
                   sizeof(esp_bd_addr_t));
            ESP_LOGI(TAG, "A2DP connected: %02x:%02x:%02x:%02x:%02x:%02x",
                     s_peer_bda[0], s_peer_bda[1], s_peer_bda[2],
                     s_peer_bda[3], s_peer_bda[4], s_peer_bda[5]);
            set_connected(true);
#if BT_ENABLE_HFP
            // The phone may expose HFP on the same ACL. Try to bring up
            // the hands-free client for caller-ID events.
            s_req_hf_connect = true;
#endif
        } else if (param->conn_stat.state ==
                   ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            ESP_LOGI(TAG, "A2DP disconnected");
            set_connected(false);
        }
        break;

    case ESP_A2D_AUDIO_STATE_EVT: {
        bool started =
            param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED;
        ESP_LOGI(TAG, "A2DP audio %s", started ? "started" : "stopped");

        // A2DP audio can start because of another app (video, TikTok).
        // AVRCP play status is the source of truth for the music player.
        if (started) {
            s_audio_streaming = true;
            s_last_pcm_ms = now_ms();
            if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG, "A2DP start: playing=%d title='%s'",
                         (int)s_playing, s_meta_title);
                // A2DP can start for another app (video/TikTok) while the
                // music player is paused. Clear the old screen first.
                ESP_LOGI(TAG, "A2DP start: clearing old track metadata");
                clear_metadata_locked();
                // If this was a real music resume, fresh metadata should
                // arrive within this window and restore the title.
                s_a2dp_meta_deadline_ms = now_ms() + 2000;
                s_a2dp_meta_seen = false;
                xSemaphoreGive(s_lock);
            }
            s_req_play_status = true;
            s_req_metadata = true;
        } else {
            s_audio_streaming = false;
        }
        break;
    }

    case ESP_A2D_AUDIO_CFG_EVT:
        ESP_LOGI(TAG, "A2DP audio configured, codec type=%d",
                 param->audio_cfg.mcc.type);
        break;

    default:
        break;
    }
}

static void avrc_cb(esp_avrc_ct_cb_event_t event,
                    esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        s_avrc_connected = param->conn_stat.connected;
        ESP_LOGI(TAG, "AVRCP %s",
                 s_avrc_connected ? "connected" : "disconnected");
        if (s_avrc_connected) {
            s_req_caps = true;
            s_req_metadata = true;
            s_req_play_status = true;
        }
        break;

    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
        s_req_register = true;
        s_req_metadata = true;
        s_req_play_status = true;
        break;

    case ESP_AVRC_CT_METADATA_RSP_EVT:
        ESP_LOGI(TAG, "metadata attr=0x%02X len=%d",
                 param->meta_rsp.attr_id, param->meta_rsp.attr_length);
        set_track_attr(param->meta_rsp.attr_id,
                       param->meta_rsp.attr_text,
                       param->meta_rsp.attr_length);
        break;

    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
        if (param->change_ntf.event_id == ESP_AVRC_RN_TRACK_CHANGE) {
            // Do not show next/prev here: some phones emit TRACK_CHANGE
            // on pause/resume too. A real change is confirmed later by a
            // changed title or track number.
            s_req_metadata = true;
            s_req_play_status = true;
            s_req_register = true;
        } else if (param->change_ntf.event_id ==
                   ESP_AVRC_RN_NOW_PLAYING_CHANGE) {
            // Pause/resume and some players emit this even without a real
            // track change. Refresh metadata, but do not show next/prev.
            s_req_metadata = true;
            s_req_play_status = true;
            s_req_register = true;
        } else if (param->change_ntf.event_id ==
                   ESP_AVRC_RN_PLAY_STATUS_CHANGE) {
            bool playing =
                param->change_ntf.event_parameter.playback ==
                ESP_AVRC_PLAYBACK_PLAYING;
            ESP_LOGI(TAG, "AVRCP play status: %s",
                     playing ? "playing" : "not playing");
            set_playing(playing);
            if (playing) {
                // Refresh metadata: after an app switch (video/TikTok)
                // the old title/artist may no longer be valid.
                s_req_metadata = true;
            }
            s_req_play_status = true;
            s_req_register = true;
        } else if (param->change_ntf.event_id ==
                   ESP_AVRC_RN_PLAY_POS_CHANGED) {
            if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
                s_song_pos_ms = param->change_ntf.event_parameter.play_pos;
                s_pos_updated_us = now_ms();
                ui_refresh_locked();
                xSemaphoreGive(s_lock);
            }
            s_req_pos_register = true;
        } else if (param->change_ntf.event_id ==
                   ESP_AVRC_RN_BATTERY_STATUS_CHANGE) {
            // AVRCP battery is only a coarse category. Use it only as a
            // fallback until HFP reports 0..5.
            if (s_battery_level < 0) {
                int level = 0;
                switch (param->change_ntf.event_parameter.batt) {
                case ESP_AVRC_BATT_NORMAL:      level = 3; break;
                case ESP_AVRC_BATT_WARNING:     level = 1; break;
                case ESP_AVRC_BATT_CRITICAL:    level = 0; break;
                case ESP_AVRC_BATT_EXTERNAL:    level = 5; break;
                case ESP_AVRC_BATT_FULL_CHARGE: level = 5; break;
                default: break;
                }
                if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
                    s_battery_level = level;
                    ui_refresh_locked();
                    xSemaphoreGive(s_lock);
                }
            }
            s_req_register = true;
        } else if (param->change_ntf.event_id ==
                   ESP_AVRC_RN_ADDRESSED_PLAYER_CHANGE ||
                   param->change_ntf.event_id ==
                   ESP_AVRC_RN_AVAILABLE_PLAYERS_CHANGE) {
            // Android switches the active media player when another app
            // (video/TikTok) starts producing audio.
            ESP_LOGI(TAG, "AVRCP active player changed, clearing track");
            if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
                clear_metadata_locked();
                xSemaphoreGive(s_lock);
            }
            s_req_metadata = true;
            s_req_play_status = true;
            s_req_register = true;
        }
        break;

    case ESP_AVRC_CT_PLAY_STATUS_RSP_EVT:
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            s_song_len_ms = param->play_status_rsp.song_length;
            s_song_pos_ms = param->play_status_rsp.song_position;
            s_pos_updated_us = now_ms();
            s_playing = (param->play_status_rsp.play_status ==
                         ESP_AVRC_PLAYBACK_PLAYING);
            ui_refresh_locked();
            xSemaphoreGive(s_lock);
            if (s_playing && !s_meta_title[0]) {
                s_req_metadata = true;
            }
        }
        break;

    default:
        break;
    }
}

static void hf_cb(esp_hf_client_cb_event_t event,
                  esp_hf_client_cb_param_t *param)
{
    switch (event) {
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        if (param->conn_stat.state ==
            ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED) {
            s_hf_slc = true;
            ESP_LOGI(TAG, "HFP SLC connected");
        } else if (param->conn_stat.state ==
                   ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED) {
            s_hf_slc = false;
            clear_call();
        }
        break;

    case ESP_HF_CLIENT_CLIP_EVT:
        // HFP CLIP carries the number. Contact names require PBAP.
        if (param->clip.number) {
            ESP_LOGI(TAG, "CLIP: %s", param->clip.number);
            set_call(CALL_INCOMING, true, param->clip.number);
        }
        break;

    case ESP_HF_CLIENT_CCWA_EVT:
        if (param->ccwa.number) {
            ESP_LOGI(TAG, "CCWA: %s", param->ccwa.number);
            set_call(CALL_INCOMING, true, param->ccwa.number);
        }
        break;

    case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            s_battery_level = param->battery_level.value;
            ui_refresh_locked();
            xSemaphoreGive(s_lock);
        }
        break;

    case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT:
        switch (param->call_setup.status) {
        case ESP_HF_CALL_SETUP_STATUS_INCOMING:
            set_call(CALL_INCOMING, true, NULL);
            break;
        case ESP_HF_CALL_SETUP_STATUS_OUTGOING_DIALING:
        case ESP_HF_CALL_SETUP_STATUS_OUTGOING_ALERTING:
            set_call(CALL_OUTGOING, false, "");
            // AT+CLCC gives us the number for an outgoing call.
            s_req_clcc = true;
            break;
        case ESP_HF_CALL_SETUP_STATUS_IDLE:
            // Incoming or outgoing call was cancelled before it became
            // active (caller hung up, user rejected/cancelled).
            if (s_call_state == CALL_INCOMING ||
                s_call_state == CALL_OUTGOING) {
                clear_call();
            }
            break;
        default:
            break;
        }
        break;

    case ESP_HF_CLIENT_CIND_CALL_EVT:
        if (param->call.status == ESP_HF_CALL_STATUS_NO_CALLS) {
            clear_call();
        } else if (s_call_state == CALL_NONE) {
            set_call(CALL_ACTIVE, false, NULL);
        } else {
            set_call(CALL_ACTIVE, s_call_incoming, NULL);
        }
        break;

    case ESP_HF_CLIENT_CLCC_EVT:
        if (param->clcc.number) {
            ESP_LOGI(TAG, "CLCC: dir=%d number=%s",
                     param->clcc.dir, param->clcc.number);
            if (param->clcc.dir == ESP_HF_CURRENT_CALL_DIRECTION_OUTGOING) {
                set_call(CALL_OUTGOING, false, param->clcc.number);
            } else {
                set_call(CALL_INCOMING, true, param->clcc.number);
            }
        }
        break;

    default:
        break;
    }
}

void bt_audio_init(void)
{
    if (s_lock) return;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        ESP_LOGE(TAG, "failed to create mutex");
        return;
    }

    s_meta_title[0] = s_meta_artist[0] = s_meta_album[0] = '\0';
    s_caller[0] = '\0';
    s_title_scroll_start_ms = now_ms();
    s_artist_scroll_start_ms = now_ms();
    ui_refresh_locked();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "controller init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_bluedroid_config_t bd_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    // Disable SSP so legacy pairing asks for the fixed PIN.
    bd_cfg.ssp_en = false;
    ret = esp_bluedroid_init_with_cfg(&bd_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_bt_gap_register_callback(bt_gap_cb);
    esp_bt_gap_set_device_name("Opel CID BT");

    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap,
                                  sizeof(iocap));

    esp_bt_pin_code_t pin = {BT_PIN[0], BT_PIN[1], BT_PIN[2], BT_PIN[3]};
    esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, pin);

    // AVRCP must be initialized before A2DP sink.
    esp_avrc_ct_register_callback(avrc_cb);
    ret = esp_avrc_ct_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AVRCP init failed: %s", esp_err_to_name(ret));
    }

    esp_a2d_register_callback(a2d_cb);
    ret = esp_a2d_sink_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A2DP sink init failed: %s", esp_err_to_name(ret));
    } else {
        esp_a2d_sink_register_data_callback(a2d_data_cb);
    }

#if BT_ENABLE_HFP
    esp_hf_client_register_callback(hf_cb);
    ret = esp_hf_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HFP client init failed: %s", esp_err_to_name(ret));
    }
#endif

    xTaskCreate(bt_worker_task, "BT_WORKER", 4096, NULL, 5, NULL);

    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                             ESP_BT_GENERAL_DISCOVERABLE);

    ESP_LOGI(TAG, "Bluetooth ready, name='Opel CID BT'");
}

bool bt_audio_get_lines(char *top, size_t top_len,
                        char *middle, size_t middle_len,
                        char *bottom, size_t bottom_len)
{
    if (!s_lock) return false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }

    ui_refresh_locked();

    if (top && top_len) copy_text(top, top_len, s_top);
    if (middle && middle_len) copy_text(middle, middle_len, s_mid);
    if (bottom && bottom_len) copy_text(bottom, bottom_len, s_bot);

    bool connected = s_a2dp_connected;
    xSemaphoreGive(s_lock);
    return connected;
}

bool bt_audio_is_connected(void)
{
    if (!s_lock) return false;
    bool connected = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        connected = s_a2dp_connected;
        xSemaphoreGive(s_lock);
    }
    return connected;
}

bool bt_audio_is_call_active(void)
{
    if (!s_lock) return false;
    bool active = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        active = (s_call_state != CALL_NONE);
        xSemaphoreGive(s_lock);
    }
    return active;
}
