#include "section_demo.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"

#define DEMO_DWELL_MS 6000

struct DemoCase {
    const char *name;
    uint8_t cmd;
    uint8_t type;
    const char *title;
    const uint8_t *ids;
    const char *const *texts;
    size_t count;
};

static const uint8_t IDS_03_MENU[] = {0x01, 0x02, 0x23};
static const char *const TXT_03_MENU[] = {"AUDIO", "AUX", "AUX"};

static const uint8_t IDS_05_CD[] = {0x01, 0x02, 0x03};
static const char *const TXT_05_CD[] = {"CD IN", "TRACK", "MP3"};

static const uint8_t IDS_07_PHONE[] = {0x02, 0x10, 0x11, 0x12};
static const char *const TXT_07_PHONE[] = {
    "DEMO PHONE", "T07", "PHONE", "DEMO"
};

static const uint8_t IDS_09_NAV[] = {0x02, 0x30, 0x33};
static const char *const TXT_09_NAV[] = {"DEMO NAV", "NAV", "ROAD"};

static const uint8_t IDS_0A_AC[] = {0x23, 0x24, 0x25, 0x26};
static const char *const TXT_0A_AC[] = {"22.0C", "P", "ECO", "3"};

// Reserved: the AC screen accepts arbitrary text in these four fields, but
// it is the factory climate strip. Do not use it on a car with ECC.
static const uint8_t IDS_0A_ANY[] = {0x23, 0x24, 0x25, 0x26};
static const char *const TXT_0A_ANY[] = {"AAAA", "BBBB", "CCCC", "DDDD"};

static const uint8_t IDS_03_EXTRA[] = {0x10, 0x11, 0x12, 0x13, 0x14};
static const char *const TXT_03_EXTRA[] = {
    "T10", "T11", "T12", "T13", "T14"
};

static const uint8_t IDS_03_MENU2[] = {
    0x01, 0x02, 0x03, 0x23, 0x10, 0x11, 0x12
};
static const char *const TXT_03_MENU2[] = {
    "AUDIO", "AUX", "AUX", "AUX", "T03", "MENU", "DEMO"
};

static const uint8_t IDS_01_FM[] = {0x01, 0x02, 0x03};
static const char *const TXT_01_FM[] = {"DEMO FM", "87.5", "FM"};

static const DemoCase CASES[] = {
    {"type03 generic C0", 0xC0, 0x03, "T03 C0", nullptr, nullptr, 0},
    {"type03 generic 40", 0x40, 0x03, "T03 40", nullptr, nullptr, 0},
    {"type03 menu fields", 0xC0, 0x03, "T03 MENU", IDS_03_MENU, TXT_03_MENU, 3},
    {"type03 menu2 fields", 0xC0, 0x03, "T03 MENU2", IDS_03_MENU2, TXT_03_MENU2, 7},
    {"type03 extra rows", 0xC0, 0x03, "T03 4/5", IDS_03_EXTRA, TXT_03_EXTRA, 5},

    {"type05 generic C0", 0xC0, 0x05, "T05 C0", nullptr, nullptr, 0},
    {"type05 generic 40", 0x40, 0x05, "T05 40", nullptr, nullptr, 0},
    {"type05 CD fields", 0xC0, 0x05, "T05 CD", IDS_05_CD, TXT_05_CD, 3},

    {"type07 generic C0", 0xC0, 0x07, "T07 C0", nullptr, nullptr, 0},
    {"type07 phone C0", 0xC0, 0x07, "T07 PH", IDS_07_PHONE, TXT_07_PHONE, 4},
    {"type07 phone 40", 0x40, 0x07, "T07 40", IDS_07_PHONE, TXT_07_PHONE, 4},

    {"type09 generic C0", 0xC0, 0x09, "T09 C0", nullptr, nullptr, 0},
    {"type09 nav C0", 0xC0, 0x09, "T09 NV", IDS_09_NAV, TXT_09_NAV, 3},
    {"type09 nav 40", 0x40, 0x09, "T09 40", IDS_09_NAV, TXT_09_NAV, 3},

    {"type0A AC fields", 0xC0, 0x0A, "T0A AC", IDS_0A_AC, TXT_0A_AC, 4},
    {"type0A arbitrary 4", 0xC0, 0x0A, "T0A ANY", IDS_0A_ANY, TXT_0A_ANY, 4},
    {"type0A generic", 0xC0, 0x0A, "T0A GEN", nullptr, nullptr, 0},

    {"type01 FM fields", 0xC0, 0x01, "T01 FM", IDS_01_FM, TXT_01_FM, 3},

    {"type0B C0", 0xC0, 0x0B, "T0B C0", nullptr, nullptr, 0},
    {"type0B 40", 0x40, 0x0B, "T0B 40", nullptr, nullptr, 0},
    {"type0C C0", 0xC0, 0x0C, "T0C C0", nullptr, nullptr, 0},
    {"type0C 40", 0x40, 0x0C, "T0C 40", nullptr, nullptr, 0},
    {"type0D C0", 0xC0, 0x0D, "T0D C0", nullptr, nullptr, 0},
    {"type0D 40", 0x40, 0x0D, "T0D 40", nullptr, nullptr, 0},
    {"type0E C0", 0xC0, 0x0E, "T0E C0", nullptr, nullptr, 0},
    {"type0E 40", 0x40, 0x0E, "T0E 40", nullptr, nullptr, 0},
    {"type0F C0", 0xC0, 0x0F, "T0F C0", nullptr, nullptr, 0},
    {"type0F 40", 0x40, 0x0F, "T0F 40", nullptr, nullptr, 0},
};

static void send_case(const DemoCase &c)
{
    if (c.ids && c.texts && c.count) {
        display_send_fields(c.cmd, c.type, c.ids, c.texts, c.count);
    } else {
        display_send_text_cmd(c.cmd, c.type, c.title, "SECTION", "DEMO");
    }
}

void SectionDemo::run()
{
    printf("DEMO: %u cases, %d ms each\n",
           (unsigned)(sizeof(CASES) / sizeof(CASES[0])), DEMO_DWELL_MS);

    while (1) {
        for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
            const DemoCase &c = CASES[i];
            printf("\n=== %s: cmd=%02X type=%02X ===\n",
                   c.name, c.cmd, c.type);

            for (int n = 0; n < 4; n++) {
                send_case(c);
                vTaskDelay(pdMS_TO_TICKS(DEMO_DWELL_MS / 4));
            }
        }
    }
}
