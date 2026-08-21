/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: MIT
 *
 * @file ble_passkey_state_changed.h
 * @brief Event declaration for BLE passkey entry state changes.
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

struct ble_passkey_state_changed {
    bool active;
};


ZMK_EVENT_DECLARE(ble_passkey_state_changed);
