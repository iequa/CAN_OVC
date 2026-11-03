#pragma once

#include <stdbool.h>

enum class UiMode {
    OFF = 0,        // stock display, BT layer inactive
    ACTIVE,         // BT overlay is shown
    SUSPENDED,      // BT mode armed, but user is in settings/BC/menu
    CALL,           // temporary call override
};

enum class UiEvent {
    HOLD_ACTIVATE = 0,
    HOLD_DEACTIVATE,
    NAVIGATE_AWAY,
    RETURN_TO_AUX,
};

class AppUi {
public:
    void begin();

    // Called by the future car integration layer.
    void set_aux(bool in_aux);
    void set_call_active(bool active);

    // Button/bus events. In DEV_NO_RADIO_MODE all mode toggles are ignored.
    void handle_event(UiEvent event);

    UiMode mode() const;
    bool is_bt_screen() const;
    bool is_call_active() const;

private:
    UiMode state_ = UiMode::OFF;
    UiMode before_call_ = UiMode::OFF;
    bool aux_ = false;
    bool call_ = false;

    void restore_after_call();
};
