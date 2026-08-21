/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: MIT
 *
 * @file ble_passkey_interceptor.c
 * @brief Intercepts Zephyr's BLE pairing callbacks to track passkey state for ZMK displays.
 * @details This module hijacks the standard Zephyr Bluetooth connection authentication callbacks
 *          to detect when passkey entry starts, finishes, or fails. It also intercepts ZMK's keycode
 *          events during pairing to track the number of digits entered, enabling dynamic pairing UI
 *          across both the central and peripheral splits without modifying the core ZMK Bluetooth stack.
 */

/* ========================================================================= */
/*                        INCLUDES AND DEPENDENCIES                          */
/* ========================================================================= */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zmk/events/ble_pairing_complete.h>
#include <zmk/events/ble_passkey_digits_changed.h>
#include <zmk/events/ble_passkey_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ========================================================================= */
/*                            STATE AND GLOBALS                              */
/* ========================================================================= */
#if DT_HAS_COMPAT_STATUS_OKAY(zmk_ble_passkey_layer)
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
#define BPL_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zmk_ble_passkey_layer)
static const uint8_t passkey_layer = DT_PROP(BPL_NODE, passkey_layer);
static const uint8_t exclude_layers[] = DT_PROP_OR(BPL_NODE, exclude_layers, {});
#endif
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
static bool layer_was_toggled = false;
#endif


/* ========================================================================= */
/*                            AUTO-LAYER LOGIC                               */
/* ========================================================================= */

/**
 * @brief Automatically activates the dedicated passkey entry layer if configured.
 * @details Skips activation if the user is already on an excluded layer.
 */

static void auto_layer_activate(void) {
#if DT_HAS_COMPAT_STATUS_OKAY(zmk_ble_passkey_layer)
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
    bool should_toggle = true;
    for (int i = 0; i < ARRAY_SIZE(exclude_layers); i++) {
        if (zmk_keymap_layer_active(exclude_layers[i])) {
            should_toggle = false;
            break;
        }
    }
    if (should_toggle) {
        LOG_DBG("Auto-toggling passkey layer %d", passkey_layer);
        zmk_keymap_layer_activate(passkey_layer);
        layer_was_toggled = true;
    }
#endif
#endif
}


static void auto_layer_deactivate(void) {
#if DT_HAS_COMPAT_STATUS_OKAY(zmk_ble_passkey_layer)
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
    if (layer_was_toggled) {
        LOG_DBG("Deactivating passkey layer %d", passkey_layer);
        zmk_keymap_layer_deactivate(passkey_layer);
        layer_was_toggled = false;
    }
#endif
#endif
}


/* ========================================================================= */
/*                      ZEPHYR CALLBACK INTERCEPTION                         */
/* ========================================================================= */

static const struct bt_conn_auth_cb *zmk_original_cb = NULL;
static struct bt_conn_auth_cb my_intercepted_cb;

/**
 * @brief External references to Zephyr and ZMK functions to allow overriding them at link time.
 * @details We use GCC's `-Wl,--wrap=symbol` linker flag in CMake to intercept these functions.
 *          The `__wrap_` functions defined below will be called instead of the original ones,
 *          and we can call the original ones using the `__real_` prefix.
 */
extern int __real_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb);
extern int __real_bt_conn_auth_passkey_entry(struct bt_conn *conn, unsigned int passkey);
extern int __real_zmk_event_manager_raise(zmk_event_t *event);

static bool pairing_active = false;
static char passkey_buffer[7] = "";
static uint8_t passkey_len = 0;

/* ========================================================================= */
/*                          ZMK EVENT INTERCEPTION                           */
/* ========================================================================= */

static void sync_to_peripherals(uint32_t event_type, uint32_t state);

static void clear_passkey_buffer(void) {
    passkey_len = 0;
    memset(passkey_buffer, 0, sizeof(passkey_buffer));
    struct ble_passkey_digits_changed ev = {.digits_len = 0};
    memset(ev.passkey, 0, sizeof(ev.passkey));
    raise_ble_passkey_digits_changed(ev);
    sync_to_peripherals(2, 0);
}


/**
 * @brief Wraps the main ZMK event dispatcher to intercept keystrokes during active pairing.
 * @details This intercepts `zmk_keycode_state_changed` to monitor numeric keys entered during
 *          pairing. We update `passkey_buffer` and emit our own custom `ble_passkey_digits_changed`
 *          events to drive UI updates before passing the event along to ZMK.
 */
int __wrap_zmk_event_manager_raise(zmk_event_t *event) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (pairing_active && event->event == &zmk_event_zmk_keycode_state_changed) {
        struct zmk_keycode_state_changed *kc = as_zmk_keycode_state_changed(event);
        if (kc && kc->state) {
            uint16_t key = kc->keycode;
            uint8_t val = 0;
            bool changed = false;

            if (key >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
                key <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
                val = (key == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS)
                          ? 0
                          : (key - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION + 1);
                if (passkey_len < 6) {
                    passkey_buffer[passkey_len++] = '0' + val;
                    changed = true;
                }
            } else if (key >= HID_USAGE_KEY_KEYPAD_1_AND_END && key <= HID_USAGE_KEY_KEYPAD_0_AND_INSERT) {
                val = (key == HID_USAGE_KEY_KEYPAD_0_AND_INSERT) ? 0 : (key - HID_USAGE_KEY_KEYPAD_1_AND_END + 1);
                if (passkey_len < 6) {
                    passkey_buffer[passkey_len++] = '0' + val;
                    changed = true;
                }
            }
            if (changed) {
                struct ble_passkey_digits_changed digits_ev = {.digits_len = passkey_len};
                strncpy(digits_ev.passkey, passkey_buffer, 7);
                raise_ble_passkey_digits_changed(digits_ev);
                sync_to_peripherals(2, passkey_len);
            }
        }
    }
#endif
    return __real_zmk_event_manager_raise(event);
}


/* ========================================================================= */
/*                      SPLIT PERIPHERAL SYNCHRONIZATION                     */
/* ========================================================================= */

#include <zmk/split/central.h>

/**
 * @brief Invokes the custom `zmk_behavior_ble_passkey_sync` behavior on all peripherals.
 * @details This passes pairing state updates across the BLE split link, allowing peripheral
 *          displays to mirror the central's pairing UI.
 */
static void sync_to_peripherals(uint32_t event_type, uint32_t state) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#if DT_HAS_COMPAT_STATUS_OKAY(zmk_behavior_ble_passkey_sync)
    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_COMPAT_GET_ANY_STATUS_OKAY(zmk_behavior_ble_passkey_sync)),
        .param1 = event_type,
        .param2 = state,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    for (int i = 0; i < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; i++) {
        zmk_split_central_invoke_behavior(i, &binding, event, true);
        zmk_split_central_invoke_behavior(i, &binding, event, false);
    }
#endif
#endif
}


/* ========================================================================= */
/*                      ASYNC WORKERS FOR BLE CALLBACKS                      */
/* ========================================================================= */
/* BLE callbacks often run in interrupt context or the BT RX thread. We use work
 * items to safely transition back to the main system workqueue where it is safe
 * to emit ZMK events and update UI state.
 */

static struct k_work passkey_work;
static struct k_work cancel_work;
static struct k_work complete_work;
static struct k_work failed_work;
static struct k_work disconnect_work;
static bool last_bonded = false;

static void passkey_work_handler(struct k_work *w) {
    pairing_active = true;
    clear_passkey_buffer();
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = true});
    sync_to_peripherals(0, 1);
    auto_layer_activate();
}


static void my_passkey_entry(struct bt_conn *conn) {
    LOG_DBG("Passkey entry intercepted: Active");
    k_work_submit(&passkey_work);

    if (zmk_original_cb && zmk_original_cb->passkey_entry) {
        zmk_original_cb->passkey_entry(conn);
    }
}


static void cancel_work_handler(struct k_work *w) {
    pairing_active = false;
    clear_passkey_buffer();
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
    sync_to_peripherals(0, 0);
    auto_layer_deactivate();
}


static void my_cancel(struct bt_conn *conn) {
    LOG_DBG("Passkey entry intercepted: Cancelled");
    k_work_submit(&cancel_work);

    if (zmk_original_cb && zmk_original_cb->cancel) {
        zmk_original_cb->cancel(conn);
    }
}


/* ========================================================================= */
/*                          INTERCEPTOR REGISTRATION                         */
/* ========================================================================= */

/**
 * @brief Wraps Zephyr's BT connection auth callback registration.
 * @details ZMK's core BLE module calls `bt_conn_auth_cb_register` at boot. We intercept this call
 *          to inject our own `passkey_entry` and `cancel` handlers, while retaining a pointer to
 *          ZMK's original callbacks so we can still pass the events through to them.
 */
int __wrap_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb) {
    if (cb == NULL) {
        return __real_bt_conn_auth_cb_register(NULL);
    }

    zmk_original_cb = cb;

    my_intercepted_cb = *cb;
    my_intercepted_cb.passkey_entry = my_passkey_entry;
    my_intercepted_cb.cancel = my_cancel;

    return __real_bt_conn_auth_cb_register(&my_intercepted_cb);
}


/**
 * @brief Wraps Zephyr's BT connection passkey submission function.
 * @details Intercepting this lets us know when the user has submitted the passkey,
 *          so we can clear our pairing UI states.
 */
int __wrap_bt_conn_auth_passkey_entry(struct bt_conn *conn, unsigned int passkey) {
    LOG_DBG("Passkey entry intercepted: Submitted");
    k_work_submit(&cancel_work);

    return __real_bt_conn_auth_passkey_entry(conn, passkey);
}


static void complete_work_handler(struct k_work *w) {
    pairing_active = false;
    clear_passkey_buffer();
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
    sync_to_peripherals(0, 0);
    raise_ble_pairing_complete((struct ble_pairing_complete){.bonded = last_bonded});
    sync_to_peripherals(1, last_bonded ? 1 : 0);
    auto_layer_deactivate();
}


static void my_pairing_complete(struct bt_conn *conn, bool bonded) {
    LOG_DBG("Pairing complete intercepted");
    last_bonded = bonded;
    k_work_submit(&complete_work);
}


static void failed_work_handler(struct k_work *w) {
    pairing_active = false;
    clear_passkey_buffer();
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
    sync_to_peripherals(0, 0);
    raise_ble_pairing_complete((struct ble_pairing_complete){.bonded = false});
    sync_to_peripherals(1, 0);
    auto_layer_deactivate();
}


static void my_pairing_failed(struct bt_conn *conn, enum bt_security_err reason) {
    LOG_DBG("Pairing failed intercepted");
    k_work_submit(&failed_work);
}


static struct bt_conn_auth_info_cb my_auth_info_cb = {
    .pairing_complete = my_pairing_complete,
    .pairing_failed = my_pairing_failed,
};


static void disconnect_work_handler(struct k_work *w) {
    if (pairing_active) {
        pairing_active = false;
        clear_passkey_buffer();
        raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
        sync_to_peripherals(0, 0);
        auto_layer_deactivate();
    }
}


static void my_disconnected(struct bt_conn *conn, uint8_t reason) {
    if (pairing_active) {
        LOG_DBG("Disconnected while pairing intercepted");
        k_work_submit(&disconnect_work);
    }
}


static struct bt_conn_cb conn_callbacks = {
    .disconnected = my_disconnected,
};


/* ========================================================================= */
/*                              INITIALIZATION                               */
/* ========================================================================= */

static int passkey_interceptor_init(void) {
    k_work_init(&passkey_work, passkey_work_handler);
    k_work_init(&cancel_work, cancel_work_handler);
    k_work_init(&complete_work, complete_work_handler);
    k_work_init(&failed_work, failed_work_handler);
    k_work_init(&disconnect_work, disconnect_work_handler);

    bt_conn_auth_info_cb_register(&my_auth_info_cb);
    bt_conn_cb_register(&conn_callbacks);
    return 0;
}


SYS_INIT(passkey_interceptor_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
