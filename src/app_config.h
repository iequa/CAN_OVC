#pragma once

// 1 = bench / no radio:
//     behave as before, the Bluetooth screen is always "active".
// 0 = car mode:
//     explicit OFF / ACTIVE / SUSPENDED state machine, AUX detection and
//     radio-button events must be supplied by the car integration layer.
#define DEV_NO_RADIO_MODE 1

// Convenience inverse flag for readability.
#define ON_CAR_MODE (DEV_NO_RADIO_MODE == 0)
