#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

struct ble_passkey_digits_changed {
    uint8_t digits_len;
    char passkey[7];
};

ZMK_EVENT_DECLARE(ble_passkey_digits_changed);
