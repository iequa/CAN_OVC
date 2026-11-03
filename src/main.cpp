/*
 * CAN_OVC - запуск GID/CID Opel без штатной магнитолы (MS-CAN, 95.238 кбит/с)
 *
 * Проверено на Vectra C full CAN (GID).
 *
 * Что делает прошивка:
 *   1. Эмулирует EHU "radio on" (0x691), иначе дисплей не показывает текст.
 *   2. Отправляет три строки текста по ISO-TP на 0x6C1.
 *      Дисплей подтверждает приём Flow Control-кадром 0x2C1.
 *   3. Поднимает Bluetooth A2DP sink + AVRCP и показывает метаданные
 *      трека и номер звонящего (см. bt_audio.cpp).
 *
 * Формат payload 0x6C1:
 *   [0]  0xC0  команда вывода текста
 *   [1]  0x00
 *   [2]  длина payload - 3
 *   [3]  0x03  главный экран / аудио
 *   [4]  0x10  средняя строка (крупная)
 *   [5]  количество символов UTF-16BE, включая форматирование
 *   ...
 *        0x11  верхняя строка (мелкая)
 *        0x12  нижняя строка (мелкая)
 *
 * ВАЖНО: в payload НЕТ отдельного байта общей длины. Длину содержит
 * First Frame ISO-TP (10 xx). Лишний байт сдвигает все поля, и дисплей
 * молча игнорирует сообщение.
 *
 * Подключение (разъём BID/GID/CID):
 *   1, 2  - GND
 *   3, 5  - +12 В
 *   19    - MS-CAN L
 *   20    - MS-CAN H
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "bt_audio.h"
#include "display.h"
#include "section_demo.h"
#include "app_config.h"
#include "app_ui.h"

// --- Пины ESP32 -> CAN-трансивер ---
#define TX_GPIO GPIO_NUM_17
#define RX_GPIO GPIO_NUM_16

// --- Идентификаторы ---
#define EHU_STATUS_ID 0x691
#define DISPLAY_TX_ID 0x6C1
#define DISPLAY_FC_ID 0x2C1

// --- Периоды ---
#define EHU_PERIOD_MS  1000
// 500 ms gives a smoother title scroll; the GID handles this update rate fine.
#define TEXT_PERIOD_MS 500

// Demo: cycle through 0x6C1 message types and show each for 8 seconds.
// Set to 0 for normal Bluetooth/display operation.
#define DEMO_SECTIONS 0

// "Radio on" для Vectra C. byte4 = 0x03 оставляет дисплей полностью
// включённым на аудио-экране.
static const uint8_t EHU_ON[8] = {
    0x41, 0x00, 0x60, 0x02, 0x03, 0x00, 0x00, 0x2E
};

// --- Протокол дисплея ---
#define DISPLAY_CMD_TEXT 0xC0
#define DISPLAY_MSG_AUDIO 0x03

#define FIELD_TITLE  0x10
#define FIELD_ALBUM  0x11
#define FIELD_ARTIST 0x12

// ESC[fS_gm — обычный шрифт, слева (7 символов UTF-16)
static const uint8_t FMT_LEFT[] = {
    0x00, 0x1B, 0x00, 0x5B, 0x00, 0x66, 0x00, 0x53,
    0x00, 0x5F, 0x00, 0x67, 0x00, 0x6D
};
// ESC[fS_dm — уменьшенный шрифт, слева (7 символов UTF-16)
static const uint8_t FMT_SMALL[] = {
    0x00, 0x1B, 0x00, 0x5B, 0x00, 0x66, 0x00, 0x53,
    0x00, 0x5F, 0x00, 0x64, 0x00, 0x6D
};

static twai_node_handle_t node_hdl = NULL;
static SemaphoreHandle_t tx_mutex = NULL;

// Держится на всё время ISO-TP передачи, чтобы 0x691 не влез между
// First Frame и Consecutive Frames.
static SemaphoreHandle_t display_mutex = NULL;

// Новый драйвер TWAI не копирует данные кадра: буфер должен жить до
// окончания передачи. Поэтому используем постоянные буферы.
static uint8_t tx_bufs[8][8];
static uint8_t tx_buf_idx = 0;

// Flow Control от дисплея
static volatile bool fc_received = false;
static volatile uint32_t fc_id = 0;
static volatile uint8_t fc_sts = 0;
static volatile uint8_t fc_stmin = 0;

// ---------------------------------------------------------------------------
// Приём кадров
// ---------------------------------------------------------------------------
static bool IRAM_ATTR on_rx_done(twai_node_handle_t handle,
                                 const twai_rx_done_event_data_t *edata,
                                 void *user_ctx)
{
    uint8_t buffer[8] = {0};
    twai_frame_t rx_frame = {};
    rx_frame.buffer = buffer;
    rx_frame.buffer_len = sizeof(buffer);

    if (twai_node_receive_from_isr(handle, &rx_frame) != ESP_OK) {
        return false;
    }

    const uint8_t dlc = (rx_frame.header.dlc > 8) ? 8 : (uint8_t)rx_frame.header.dlc;

    if (dlc >= 3 &&
        (buffer[0] == 0x30 || buffer[0] == 0x31 || buffer[0] == 0x32)) {
        fc_id = rx_frame.header.id;
        fc_sts = buffer[0];
        fc_stmin = buffer[2];
        fc_received = true;
    }

    return false;
}

static bool IRAM_ATTR on_state_change(twai_node_handle_t handle,
                                      const twai_state_change_event_data_t *edata,
                                      void *user_ctx)
{
    return false;
}

// ---------------------------------------------------------------------------
// Отправка одного CAN-кадра
// ---------------------------------------------------------------------------
static esp_err_t can_send(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    if (!tx_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(300)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (dlc > 8) dlc = 8;
    uint8_t *buf = tx_bufs[tx_buf_idx];
    tx_buf_idx = (uint8_t)((tx_buf_idx + 1) % 8);

    memset(buf, 0, 8);
    if (dlc && data) memcpy(buf, data, dlc);

    twai_frame_t frame = {};
    frame.header.id = id;
    frame.header.dlc = dlc;
    frame.header.ide = 0;
    frame.header.rtr = 0;
    frame.header.fdf = 0;
    frame.header.brs = 0;
    frame.header.esi = 0;
    frame.buffer = buf;
    frame.buffer_len = dlc;
    frame.tx_queue_priority = 0;

    esp_err_t err = twai_node_transmit(node_hdl, &frame, 100);
    if (err == ESP_OK) {
        err = twai_node_transmit_wait_all_done(node_hdl, 20);
    } else if (err == ESP_ERR_INVALID_STATE) {
        twai_node_recover(node_hdl);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    xSemaphoreGive(tx_mutex);
    return err;
}

// ---------------------------------------------------------------------------
// UTF-8 -> UTF-16BE с фильтром символов, которые понимает дисплей
// ---------------------------------------------------------------------------
static size_t utf8_to_utf16be(const char *src, uint8_t *dst, size_t max_chars)
{
    const uint8_t *s = (const uint8_t *)src;
    size_t out = 0;

    while (*s && out < max_chars) {
        uint32_t cp = 0;
        uint8_t c = *s;

        if (c < 0x80) {
            cp = c;
            s += 1;
        } else if ((c & 0xE0) == 0xC0) {
            if (!s[1]) break;
            cp = ((uint32_t)(c & 0x1F) << 6) | (s[1] & 0x3F);
            s += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (!s[1] || !s[2]) break;
            cp = ((uint32_t)(c & 0x0F) << 12) |
                 ((uint32_t)(s[1] & 0x3F) << 6) |
                 (s[2] & 0x3F);
            s += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (!s[1] || !s[2] || !s[3]) break;
            cp = ((uint32_t)(c & 0x07) << 18) |
                 ((uint32_t)(s[1] & 0x3F) << 12) |
                 ((uint32_t)(s[2] & 0x3F) << 6) |
                 (s[3] & 0x3F);
            s += 4;
        } else {
            s += 1;
            continue;
        }

        // Кириллица дисплеем не поддерживается.
        if (cp > 0xFFFF) continue;
        if (!((cp <= 0x024F) || (cp >= 0x1E00 && cp <= 0x2C6F))) continue;

        dst[out * 2]     = (uint8_t)(cp >> 8);
        dst[out * 2 + 1] = (uint8_t)(cp & 0xFF);
        out++;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Поле: [id][количество символов][UTF-16BE]
// ---------------------------------------------------------------------------
static bool append_field(uint8_t *payload, size_t *pos, size_t max,
                         uint8_t field_id, const char *text,
                         const uint8_t *fmt, uint8_t fmt_chars,
                         size_t *last_count_pos)
{
    uint8_t utf16[128];
    size_t chars = 0;

    if (text && text[0]) {
        chars = utf8_to_utf16be(text, utf16, sizeof(utf16) / 2);
    }

    const size_t data_chars = (chars == 0) ? 1 : (size_t)fmt_chars + chars;
    if (*pos + 2 + data_chars * 2 > max) {
        return false;
    }

    payload[(*pos)++] = field_id;
    const size_t count_pos = (*pos)++;
    if (last_count_pos) *last_count_pos = count_pos;

    if (chars == 0) {
        payload[(*pos)++] = 0x00;
        payload[(*pos)++] = 0x20;
        payload[count_pos] = 1;
    } else {
        if (fmt && fmt_chars) {
            memcpy(payload + *pos, fmt, (size_t)fmt_chars * 2);
            *pos += (size_t)fmt_chars * 2;
        }
        memcpy(payload + *pos, utf16, chars * 2);
        *pos += chars * 2;
        payload[count_pos] = (uint8_t)(fmt_chars + chars);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Сборка сообщения дисплея
// ---------------------------------------------------------------------------
static size_t build_text_payload(uint8_t *payload, size_t max,
                                 const char *title, const char *album,
                                 const char *artist,
                                 uint8_t msg_type = DISPLAY_MSG_AUDIO,
                                 uint8_t cmd = DISPLAY_CMD_TEXT)
{
    if (max < 16) return 0;
    memset(payload, 0, max);

    size_t pos = 4;                 // [C0][00][размер][тип]
    size_t last_count_pos = 0;

    if (!append_field(payload, &pos, max, FIELD_TITLE, title,
                      FMT_LEFT, sizeof(FMT_LEFT) / 2, &last_count_pos)) {
        return 0;
    }
    if (!append_field(payload, &pos, max, FIELD_ALBUM, album,
                      FMT_SMALL, sizeof(FMT_SMALL) / 2, &last_count_pos)) {
        return 0;
    }
    if (!append_field(payload, &pos, max, FIELD_ARTIST, artist,
                      FMT_SMALL, sizeof(FMT_SMALL) / 2, &last_count_pos)) {
        return 0;
    }

    // Если последний CF был бы заполнен полностью, дисплей игнорирует всё
    // сообщение. First Frame забирает 6 байт, каждый CF — 7.
    if ((pos % 7) == 6) {
        if (pos + 2 > max) return 0;
        payload[pos++] = 0x00;
        payload[pos++] = 0x20;
        payload[last_count_pos] += 1;
    }

    if (pos > 255) return 0;

    payload[0] = cmd;
    payload[1] = 0x00;
    payload[2] = (uint8_t)(pos - 3);        // тип + поля
    payload[3] = msg_type;
    return pos;
}

// ---------------------------------------------------------------------------
// ISO-TP: First Frame -> Flow Control -> Consecutive Frames
// ---------------------------------------------------------------------------
static esp_err_t isotp_send(uint32_t tx_id, const uint8_t *payload, size_t len,
                            TickType_t fc_timeout)
{
    if (len == 0 || len > 0xFFF) return ESP_FAIL;

    struct DisplayLock {
        SemaphoreHandle_t m;
        explicit DisplayLock(SemaphoreHandle_t mutex) : m(mutex) {
            if (m) xSemaphoreTake(m, portMAX_DELAY);
        }
        ~DisplayLock() {
            if (m) xSemaphoreGive(m);
        }
    } display_lock(display_mutex);

    const uint32_t expected_fc = tx_id - 0x400;

    if (len <= 7) {
        uint8_t sf[8] = {0};
        sf[0] = (uint8_t)(len & 0x0F);
        memcpy(&sf[1], payload, len);
        return can_send(tx_id, sf, 8);
    }

    uint8_t ff[8] = {0};
    ff[0] = (uint8_t)(0x10 | ((len >> 8) & 0x0F));
    ff[1] = (uint8_t)(len & 0xFF);
    memcpy(&ff[2], payload, 6);

    fc_received = false;
    fc_id = 0;
    fc_sts = 0;
    fc_stmin = 0;

    esp_err_t err = can_send(tx_id, ff, 8);
    if (err != ESP_OK) {
        printf("TX FF failed: %s\n", esp_err_to_name(err));
        return err;
    }

    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < fc_timeout) {
        if (fc_received && fc_id == expected_fc) break;
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    if (!fc_received || fc_id != expected_fc) {
        printf("Timeout: no Flow Control from 0x%03X\n", (unsigned)expected_fc);
        return ESP_ERR_TIMEOUT;
    }
    if (fc_sts == 0x32) {
        printf("Display aborted transmission\n");
        return ESP_FAIL;
    }
    if (fc_sts != 0x30) {
        printf("Unexpected Flow Control: 0x%02X\n", fc_sts);
        return ESP_FAIL;
    }

    size_t offset = 6;
    uint8_t seq = 1;

    while (offset < len) {
        uint8_t cf[8] = {0};
        cf[0] = (uint8_t)(0x20 | (seq & 0x0F));

        size_t chunk = len - offset;
        if (chunk > 7) chunk = 7;
        memcpy(&cf[1], payload + offset, chunk);

        err = can_send(tx_id, cf, 8);
        if (err != ESP_OK) {
            printf("TX CF %u failed: %s\n", (unsigned)seq,
                   esp_err_to_name(err));
            return err;
        }

        offset += chunk;
        seq++;

        if (fc_stmin > 0) {
            vTaskDelay(pdMS_TO_TICKS(fc_stmin <= 0x7F ? fc_stmin : 1));
        }
    }

    return ESP_OK;
}

esp_err_t display_send_text_cmd(uint8_t cmd, uint8_t type,
                                const char *title, const char *album,
                                const char *artist)
{
    uint8_t payload[256];
    size_t len = build_text_payload(payload, sizeof(payload),
                                    title, album, artist, type, cmd);
    if (len == 0) {
        printf("Failed to build display payload\n");
        return ESP_FAIL;
    }

    printf("TX %03X [cmd %02X type %02X]: \"%s\" / \"%s\" / \"%s\" (%u bytes)\n",
           DISPLAY_TX_ID, cmd, type, title ? title : "",
           album ? album : "", artist ? artist : "", (unsigned)len);

    return isotp_send(DISPLAY_TX_ID, payload, len, pdMS_TO_TICKS(300));
}

esp_err_t display_send_text(const char *title, const char *album,
                            const char *artist)
{
    return display_send_text_cmd(DISPLAY_CMD_TEXT, DISPLAY_MSG_AUDIO,
                                 title, album, artist);
}

esp_err_t display_send_fields(uint8_t cmd, uint8_t type,
                              const uint8_t *ids,
                              const char *const *texts,
                              size_t count)
{
    uint8_t payload[256];
    memset(payload, 0, sizeof(payload));

    size_t pos = 4;
    size_t last_count_pos = 0;

    for (size_t i = 0; i < count; i++) {
        if (!append_field(payload, &pos, sizeof(payload), ids[i], texts[i],
                          NULL, 0, &last_count_pos)) {
            return ESP_FAIL;
        }
    }

    if ((pos % 7) == 6) {
        if (pos + 2 > sizeof(payload)) return ESP_FAIL;
        payload[pos++] = 0x00;
        payload[pos++] = 0x20;
        payload[last_count_pos] += 1;
    }
    if (pos > 255) return ESP_FAIL;

    payload[0] = cmd;
    payload[1] = 0x00;
    payload[2] = (uint8_t)(pos - 3);
    payload[3] = type;

    printf("TX %03X [cmd %02X type %02X]: %u fields, %u bytes\n",
           DISPLAY_TX_ID, cmd, type, (unsigned)count, (unsigned)pos);

    return isotp_send(DISPLAY_TX_ID, payload, pos, pdMS_TO_TICKS(300));
}

// ---------------------------------------------------------------------------
// Фоновые задачи
// ---------------------------------------------------------------------------
static void vTaskEhuAlive(void *pvParameters)
{
    while (1) {
        if (display_mutex &&
            xSemaphoreTake(display_mutex, 0) == pdTRUE) {
            can_send(EHU_STATUS_ID, EHU_ON, 8);
            xSemaphoreGive(display_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(EHU_PERIOD_MS));
    }
}

// Кратковременно будим шину: 0x100, 0x170 и "radio on".
static void wake_bus_burst(void)
{
    static const uint8_t w170[3] = {0x55, 0x55, 0x55};

    for (int i = 0; i < 40; i++) {
        can_send(0x100, NULL, 0);
        can_send(0x170, w170, 3);
        can_send(EHU_STATUS_ID, EHU_ON, 8);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
extern "C" void app_main(void)
{
    tx_mutex = xSemaphoreCreateMutex();
    display_mutex = xSemaphoreCreateMutex();
    if (!tx_mutex || !display_mutex) {
        printf("Failed to create RTOS objects\n");
        return;
    }

    twai_onchip_node_config_t node_config = {
        .io_cfg = {
            .tx = TX_GPIO,
            .rx = RX_GPIO,
            .quanta_clk_out = (gpio_num_t)-1,
            .bus_off_indicator = (gpio_num_t)-1
        },
        .clk_src = TWAI_CLK_SRC_DEFAULT,
        .bit_timing = {
            .bitrate = 100000,
            .sp_permill = 0,
            .ssp_permill = 0
        },
        .data_timing = {
            .bitrate = 0,
            .sp_permill = 0,
            .ssp_permill = 0
        },
        .timestamp_resolution_hz = 0,
        .fail_retry_cnt = 0,
        .tx_queue_depth = 16,
        .intr_priority = 0,
        .flags = {
            .enable_self_test = 0,
            .enable_loopback = 0,
            .enable_listen_only = 0,
            .no_receive_rtr = 0,
            .sleep_allow_pd = 0,
            .enable_scheduled_tx = 0
        },
    };

    ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node_hdl));

    // 80 МГц / (40 * (1 + 16 + 4)) = 95.238 кбит/с
    twai_timing_advanced_config_t adv_timing = {
        .brp = 40,
        .prop_seg = 0,
        .tseg_1 = 16,
        .tseg_2 = 4,
        .sjw = 3,
        .ssp_offset = 0
    };
    ESP_ERROR_CHECK(twai_node_reconfig_timing(node_hdl, &adv_timing, NULL));

    twai_event_callbacks_t callbacks = {};
    callbacks.on_rx_done = on_rx_done;
    callbacks.on_state_change = on_state_change;
    ESP_ERROR_CHECK(twai_node_register_event_callbacks(node_hdl, &callbacks, NULL));

    ESP_ERROR_CHECK(twai_node_enable(node_hdl));
    printf("MS-CAN started: 95.238 kbit/s, TX=GPIO%d, RX=GPIO%d\n",
           TX_GPIO, RX_GPIO);

#if !DEMO_SECTIONS
    bt_audio_init();
#endif

    xTaskCreate(vTaskEhuAlive, "EHU_ALIVE", 4096, NULL, 5, NULL);

    wake_bus_burst();
    vTaskDelay(pdMS_TO_TICKS(2500));

#if DEMO_SECTIONS
    SectionDemo demo;
    demo.run();
#else
    printf("Display ready, Bluetooth name: Opel CID BT\n");

    AppUi ui;
    ui.begin();

    // TODO(car): set this from the MS-CAN integration layer:
    // radio is in AUX / our source.
    bool aux_detected = false;

    UiMode last_mode = ui.mode();
    printf("UI mode: %d\n", (int)last_mode);

    char top[48] = "BT:off";
    char middle[48] = "WAITING";
    char bottom[48] = "PAIR PHONE";

    while (1) {
        twai_node_status_t status;
        if (twai_node_get_info(node_hdl, &status, NULL) == ESP_OK &&
            status.state == TWAI_ERROR_BUS_OFF) {
            printf("CAN bus-off, recovering...\n");
            twai_node_recover(node_hdl);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Skeleton inputs. Real AUX/button events will be wired later.
        ui.set_aux(aux_detected);
        ui.set_call_active(bt_audio_is_call_active());

        UiMode mode = ui.mode();
        if (mode != last_mode) {
            printf("UI mode: %d\n", (int)mode);
            last_mode = mode;
        }

        if (ui.is_bt_screen()) {
            // ACTIVE or CALL: Bluetooth overlay / call screen.
            bt_audio_get_lines(top, sizeof(top),
                               middle, sizeof(middle),
                               bottom, sizeof(bottom));
        } else {
            // OFF or SUSPENDED: stock radio screen.
            // Placeholder until the CAN gateway forwards the real radio text.
            snprintf(top, sizeof(top), "RADIO");
            snprintf(middle, sizeof(middle), "NO DATA");
            bottom[0] = '\0';
        }

        // GID: middle = large line, top/bottom = small lines.
        display_send_text(middle, top, bottom);
        vTaskDelay(pdMS_TO_TICKS(TEXT_PERIOD_MS));
    }
#endif
}
