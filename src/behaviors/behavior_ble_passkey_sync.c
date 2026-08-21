/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: MIT
 *
 * @file behavior_ble_passkey_sync.c
 * @brief Behavior driver to synchronize BLE passkey states from the central to peripheral splits.
 * @details This behavior is meant to be invoked globally by the central when passkey pairing events occur.
 *          It relays the pairing state, pairing completion, and passkey digit count across the split
 *          BLE connection so that peripherals can display the correct UI status, as they are not privy
 *          to the central's secure pairing context.
 */

#define DT_DRV_COMPAT zmk_behavior_ble_passkey_sync

/* ========================================================================= */
/*                        INCLUDES AND DEPENDENCIES                          */
/* ========================================================================= */
#include <drivers/behavior.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zmk/events/ble_pairing_complete.h>
#include <zmk/events/ble_passkey_digits_changed.h>
#include <zmk/events/ble_passkey_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* ========================================================================= */
/*                              BEHAVIOR LOGIC                               */
/* ========================================================================= */

static int behavior_ble_passkey_sync_init(const struct device *dev) { return 0; };


/**
 * @brief Processes the behavior invocation.
 * @details When the central invokes this behavior, it sends the event type in param1 and the payload in param2.
 *          The peripheral decodes this and raises the corresponding ZMK events locally to trigger UI updates.
 */
static int behavior_ble_passkey_sync_process(struct zmk_behavior_binding *binding,
                                             struct zmk_behavior_binding_event event) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    // When invoked on the peripheral, param1 is the event type and param2 is the boolean state/value.
    if (binding->param1 == 0) {
        // Event Type 0: Pairing State Changed
        LOG_DBG("Peripheral: Relaying ble_passkey_state_changed (active: %d)", binding->param2);
        raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = (bool)binding->param2});
    } else if (binding->param1 == 1) {
        // Event Type 1: Pairing Complete
        LOG_DBG("Peripheral: Relaying ble_pairing_complete (bonded: %d)", binding->param2);
        raise_ble_pairing_complete((struct ble_pairing_complete){.bonded = (bool)binding->param2});
    } else if (binding->param1 == 2) {
        // Event Type 2: Passkey Digits Changed
        LOG_DBG("Peripheral: Relaying ble_passkey_digits_changed (len: %d)", binding->param2);
        struct ble_passkey_digits_changed digits_ev = {.digits_len = binding->param2};

        // Note: The peripheral doesn't need the actual passkey string since we only display asterisks
        // or a cursor on the peripheral screen for security reasons. Only the central displays the pin!
        memset(digits_ev.passkey, 0, sizeof(digits_ev.passkey));
        raise_ble_passkey_digits_changed(digits_ev);
    }
#endif
    return 0;
}


static int behavior_ble_passkey_sync_released(struct zmk_behavior_binding *binding,
                                              struct zmk_behavior_binding_event event) {
    return 0;
}


/* ========================================================================= */
/*                              DRIVER DEFINITION                            */
/* ========================================================================= */

static const struct behavior_driver_api behavior_ble_passkey_sync_driver_api = {
    .binding_pressed = behavior_ble_passkey_sync_process,
    .binding_released = behavior_ble_passkey_sync_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};


BEHAVIOR_DT_INST_DEFINE(0, behavior_ble_passkey_sync_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_ble_passkey_sync_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
