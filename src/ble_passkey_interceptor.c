#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zmk/events/ble_passkey_state_changed.h>
#include <zmk/events/ble_pairing_complete.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static const struct bt_conn_auth_cb *zmk_original_cb = NULL;
static struct bt_conn_auth_cb my_intercepted_cb;

extern int __real_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb);
extern int __real_bt_conn_auth_passkey_entry(struct bt_conn *conn, unsigned int passkey);

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
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = true});
    sync_to_peripherals(0, 1);
    
    if (zmk_original_cb && zmk_original_cb->passkey_entry) {
        zmk_original_cb->passkey_entry(conn);
    }
}

static void my_cancel(struct bt_conn *conn) {
    LOG_DBG("Passkey entry intercepted: Cancelled");
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
    sync_to_peripherals(0, 0);
    
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
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
    sync_to_peripherals(0, 0);
    
    return __real_bt_conn_auth_passkey_entry(conn, passkey);
}

static void my_pairing_complete(struct bt_conn *conn, bool bonded) {
    LOG_DBG("Pairing complete intercepted");
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
    sync_to_peripherals(0, 0);
    raise_ble_pairing_complete((struct ble_pairing_complete){.bonded = bonded});
    sync_to_peripherals(1, bonded ? 1 : 0);
}

static void my_pairing_failed(struct bt_conn *conn, enum bt_security_err reason) {
    LOG_DBG("Pairing failed intercepted");
    raise_ble_passkey_state_changed((struct ble_passkey_state_changed){.active = false});
    sync_to_peripherals(0, 0);
}

static struct bt_conn_auth_info_cb my_auth_info_cb = {
    .pairing_complete = my_pairing_complete,
    .pairing_failed = my_pairing_failed,
};

static int passkey_interceptor_init(void) {
    bt_conn_auth_info_cb_register(&my_auth_info_cb);
    return 0;
}
SYS_INIT(passkey_interceptor_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
