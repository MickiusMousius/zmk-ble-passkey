#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

struct ble_pairing_complete {
    bool bonded;
};

ZMK_EVENT_DECLARE(ble_pairing_complete);
