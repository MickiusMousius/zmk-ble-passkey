/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_ble_passkey_sync

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/events/ble_passkey_state_changed.h>
#include <zmk/events/ble_pairing_complete.h>
#include <zmk/events/ble_passkey_digits_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int behavior_ble_passkey_sync_init(const struct device *dev) {
    return 0;
};

static int behavior_ble_passkey_sync_process(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    // When invoked on the peripheral, param1 is the event type and param2 is the boolean state/value.
    if (binding->param1 == 0) {
        LOG_DBG("Peripheral: Relaying ble_passkey_state_changed (active: %d)", binding->param2);
        raise_ble_passkey_state_changed((struct ble_passkey_state_changed){
            .active = (bool)binding->param2
        });
    } else if (binding->param1 == 1) {
        LOG_DBG("Peripheral: Relaying ble_pairing_complete (bonded: %d)", binding->param2);
        raise_ble_pairing_complete((struct ble_pairing_complete){
            .bonded = (bool)binding->param2
        });
    } else if (binding->param1 == 2) {
        LOG_DBG("Peripheral: Relaying ble_passkey_digits_changed (len: %d)", binding->param2);
        struct ble_passkey_digits_changed digits_ev = { .digits_len = binding->param2 };
        memset(digits_ev.passkey, 0, sizeof(digits_ev.passkey)); // The peripheral doesn't need the actual passkey string, just the length!
        raise_ble_passkey_digits_changed(digits_ev);
    }
#endif
    return 0;
}

static int behavior_ble_passkey_sync_released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    return 0;
}

static const struct behavior_driver_api behavior_ble_passkey_sync_driver_api = {
    .binding_pressed = behavior_ble_passkey_sync_process,
    .binding_released = behavior_ble_passkey_sync_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_ble_passkey_sync_init, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_ble_passkey_sync_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
