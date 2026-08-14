zephyr_library()
zephyr_library_sources(ble_passkey_state_changed.c)
zephyr_library_sources(ble_pairing_complete.c)
zephyr_library_sources(ble_passkey_interceptor.c)
zephyr_library_sources(behaviors/behavior_ble_passkey_sync.c)

zephyr_ld_options(-Wl,--wrap=bt_conn_auth_cb_register)
zephyr_ld_options(-Wl,--wrap=bt_conn_auth_passkey_entry)
