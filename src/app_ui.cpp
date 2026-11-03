#include "app_ui.h"
#include "app_config.h"

void AppUi::begin()
{
    state_ = UiMode::OFF;
    before_call_ = UiMode::OFF;
    aux_ = false;
    call_ = false;
}

UiMode AppUi::mode() const
{
#if DEV_NO_RADIO_MODE
    // Bench emulation: behave as if our tab is always active in the car.
    return call_ ? UiMode::CALL : UiMode::ACTIVE;
#else
    return state_;
#endif
}

bool AppUi::is_bt_screen() const
{
    UiMode m = mode();
    return m == UiMode::ACTIVE || m == UiMode::CALL;
}

bool AppUi::is_call_active() const
{
    return call_;
}

void AppUi::set_aux(bool in_aux)
{
#if !DEV_NO_RADIO_MODE
    aux_ = in_aux;

    if (call_) {
        return;
    }

    if (aux_ && state_ == UiMode::SUSPENDED) {
        state_ = UiMode::ACTIVE;
    } else if (!aux_ && state_ == UiMode::ACTIVE) {
        state_ = UiMode::SUSPENDED;
    }
#else
    (void)in_aux;
#endif
}

void AppUi::set_call_active(bool active)
{
    if (active && !call_) {
        before_call_ = (mode() == UiMode::CALL) ? UiMode::OFF : mode();
        call_ = true;
    } else if (!active && call_) {
        call_ = false;
#if DEV_NO_RADIO_MODE
        // In bench mode mode() is always ACTIVE when there is no call.
        // Nothing else to restore.
#else
        restore_after_call();
#endif
    }
}

void AppUi::handle_event(UiEvent event)
{
#if DEV_NO_RADIO_MODE
    (void)event;
#else
    if (call_) {
        return;
    }

    switch (event) {
    case UiEvent::HOLD_ACTIVATE:
        // Only from OFF and only while the radio is in AUX.
        if (state_ == UiMode::OFF && aux_) {
            state_ = UiMode::ACTIVE;
        }
        break;

    case UiEvent::HOLD_DEACTIVATE:
        // Only when our overlay is actually active in AUX.
        // In SUSPENDED this does nothing by design.
        if (state_ == UiMode::ACTIVE && aux_) {
            state_ = UiMode::OFF;
        }
        break;

    case UiEvent::NAVIGATE_AWAY:
        if (state_ == UiMode::ACTIVE) {
            state_ = UiMode::SUSPENDED;
        }
        break;

    case UiEvent::RETURN_TO_AUX:
        if (state_ == UiMode::SUSPENDED && aux_) {
            state_ = UiMode::ACTIVE;
        }
        break;

    default:
        break;
    }
#endif
}

void AppUi::restore_after_call()
{
    state_ = before_call_;

    // Reconcile with the current AUX state after the call.
    if (state_ == UiMode::ACTIVE && !aux_) {
        state_ = UiMode::SUSPENDED;
    } else if (state_ == UiMode::SUSPENDED && aux_) {
        state_ = UiMode::ACTIVE;
    }
}
