#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zmk/events/ble_passkey_state_changed.h>
#include <zmk/events/ble_pairing_complete.h>
#include <zmk/events/ble_passkey_digits_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(zmk_ble_passkey_layer)
#define BPL_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zmk_ble_passkey_layer)
static const uint8_t passkey_layer = DT_PROP(BPL_NODE, passkey_layer);
static const uint8_t exclude_layers[] = DT_PROP_OR(BPL_NODE, exclude_layers, {});
#endif

static bool layer_was_toggled = false;

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

static const struct bt_conn_auth_cb *zmk_original_cb = NULL;
static struct bt_conn_auth_cb my_intercepted_cb;

extern int __real_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb);
extern int __real_bt_conn_auth_passkey_entry(struct bt_conn *conn, unsigned int passkey);
extern int __real_zmk_event_manager_raise(zmk_event_t *event);

static bool pairing_active = false;
static char passkey_buffer[7] = "";
static uint8_t passkey_len = 0;

static void clear_passkey_buffer(void) {
    passkey_len = 0;
    memset(passkey_buffer, 0, sizeof(passkey_buffer));
    struct ble_passkey_digits_changed ev = { .digits_len = 0 };
    memset(ev.passkey, 0, sizeof(ev.passkey));
    raise_ble_passkey_digits_changed(ev);
}

int __wrap_zmk_event_manager_raise(zmk_event_t *event) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (pairing_active && event->event == &zmk_event_zmk_keycode_state_changed) {
        struct zmk_keycode_state_changed *kc = as_zmk_keycode_state_changed(event);
        if (kc && kc->state) {
            uint16_t key = kc->keycode;
            uint8_t val = 0;
            bool changed = false;

            if (key >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION && key <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
                val = (key == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) ? 0 : (key - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION + 1);
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
                struct ble_passkey_digits_changed digits_ev = { .digits_len = passkey_len };
                strncpy(digits_ev.passkey, passkey_buffer, 7);
                raise_ble_passkey_digits_changed(digits_ev);
            }
        }
    }
#endif
    return __real_zmk_event_manager_raise(event);
}

#include <zmk/split/central.h>

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
    for (int i = 0; i < 8; i++) {
        zmk_split_central_invoke_behavior(i, &binding, event, true);
        zmk_split_central_invoke_behavior(i, &binding, event, false);
    }
#endif
#endif
}

static void my_passkey_entry(struct bt_conn *conn) {
    LOG_DBG("Passkey entry intercepted: Active");
    pairing_active = true;
    clear_passkey_buffer();
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = true});
    sync_to_peripherals(0, 1);
    
    auto_layer_activate();
    
    if (zmk_original_cb && zmk_original_cb->passkey_entry) {
        zmk_original_cb->passkey_entry(conn);
    }
}

static void my_cancel(struct bt_conn *conn) {
    LOG_DBG("Passkey entry intercepted: Cancelled");
    pairing_active = false;
    clear_passkey_buffer();
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
    sync_to_peripherals(0, 0);
    
    auto_layer_deactivate();
    
    if (zmk_original_cb && zmk_original_cb->cancel) {
        zmk_original_cb->cancel(conn);
    }
}

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

int __wrap_bt_conn_auth_passkey_entry(struct bt_conn *conn, unsigned int passkey) {
    LOG_DBG("Passkey entry intercepted: Submitted");
    pairing_active = false;
    clear_passkey_buffer();
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
    sync_to_peripherals(0, 0);
    
    auto_layer_deactivate();
    
    return __real_bt_conn_auth_passkey_entry(conn, passkey);
}

static void my_pairing_complete(struct bt_conn *conn, bool bonded) {
    LOG_DBG("Pairing complete intercepted");
    pairing_active = false;
    clear_passkey_buffer();
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
    sync_to_peripherals(0, 0);
    raise_ble_pairing_complete((struct ble_pairing_complete){.bonded = bonded});
    sync_to_peripherals(1, bonded ? 1 : 0);
    
    auto_layer_deactivate();
}

static void my_pairing_failed(struct bt_conn *conn, enum bt_security_err reason) {
    LOG_DBG("Pairing failed intercepted");
    pairing_active = false;
    clear_passkey_buffer();
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
    sync_to_peripherals(0, 0);
    
    auto_layer_deactivate();
}

static struct bt_conn_auth_info_cb my_auth_info_cb = {
    .pairing_complete = my_pairing_complete,
    .pairing_failed = my_pairing_failed,
};

static void my_disconnected(struct bt_conn *conn, uint8_t reason) {
    if (pairing_active) {
        LOG_DBG("Disconnected while pairing intercepted");
        pairing_active = false;
        clear_passkey_buffer();
        raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
        sync_to_peripherals(0, 0);
        auto_layer_deactivate();
    }
}

static struct bt_conn_cb conn_callbacks = {
    .disconnected = my_disconnected,
};

static int passkey_interceptor_init(void) {
    bt_conn_auth_info_cb_register(&my_auth_info_cb);
    bt_conn_cb_register(&conn_callbacks);
    return 0;
}
SYS_INIT(passkey_interceptor_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
