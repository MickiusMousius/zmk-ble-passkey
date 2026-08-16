# ZMK Module: BLE Passkey

This repository is a Zephyr module designed to be used with [ZMK Firmware](https://zmk.dev/). It provides an interceptor for Bluetooth Low Energy (BLE) pairing events, allowing you to track the pairing process, capture passkey digits as they are typed, and broadcast these states to ZMK's native event system for custom UI integrations (like drawing a passkey entry prompt on a display).

# Features

- **Passkey Entry Detection**: Intercepts ZMK's BLE pairing process to detect exactly when a passkey is required and when the pairing process completes or fails.
- **Keystroke Capture**: Automatically monitors numeric keystrokes (0-9 and Numpad 0-9) during the pairing process to build up the typed passkey in real-time.
- **Split Keyboard Support**: Seamlessly syncs pairing state, passkey completion, and bonded status from the central half to peripheral halves over BLE using a custom ZMK behavior (`zmk,behavior-ble-passkey-sync`).
- **Event System Integration**: Broadcasts state changes and typed digits to ZMK's native event system, allowing you to trigger custom display widgets or animations during Bluetooth pairing.
- **Auto-Layer Toggling**: Automatically activates a specific keymap layer (such as a dedicated NumPad layer) when pairing starts, and automatically deactivates it when pairing completes or is cancelled.

# Installation

To use this module, you need to add it to your ZMK configuration repository's `west.yml` file (usually located in the `config/` directory).

Open your `west.yml` and add a new remote (pointing to the GitHub account hosting this repository) and add the module to the `projects` list.

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    # Add the remote where this repository is hosted
    - name: MickiusMousius
      url-base: https://github.com/MickiusMousius

  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    # Add this module
    - name: zmk-ble-passkey
      remote: MickiusMousius
      revision: main

  self:
    path: config
```

# Devicetree Configuration

To enable split keyboard synchronization for passkey events (so your peripheral can also display the pairing status), you must declare a node in your keyboard's devicetree (e.g., in your `board.dts`, `shield.dtsi`, or `.overlay` files) using the `zmk,behavior-ble-passkey-sync` compatible string.

**Note:** This node must be added to *both* the central and peripheral devicetrees.

```dts
/ {
    behaviors {
        bps: ble_passkey_sync {
            compatible = "zmk,behavior-ble-passkey-sync";
            #binding-cells = <2>;
        };
    };
};
```

When this node is present, the central half will automatically find it and use it to sync pairing state (active/inactive) and pairing completion (bonded status) to all connected peripherals.

### Auto-Layer Toggling

One of the most useful features of this module is the ability to automatically switch to a specific keymap layer during pairing. Since passkeys are always numeric, you can use this feature to automatically expose a number pad (or a layer containing numbers) on your keyboard as soon as a passkey is required.

To use the auto-layer toggling feature, you can define a `zmk,ble-passkey-layer` node in the root of your central devicetree (or `.keymap`):

```dts
/ {
    ble_passkey_layer {
        compatible = "zmk,ble-passkey-layer";
        // The ID of the layer to toggle on during pairing
        passkey-layer = <3>; 
        // (Optional) If any of these layers are active, the auto-toggle will NOT occur
        exclude-layers = <4 5>; 
    };
};
```

When this node is present, the interceptor will automatically activate your `passkey-layer` as soon as pairing starts, and automatically deactivate it when pairing concludes or the connection drops. If you are already on a layer listed in `exclude-layers`, the auto-toggle is aborted so it doesn't disrupt your workflow.

#### Adding Visual Indicators with PK Underglow

If you are using the [zmk-pk-underglow](https://github.com/MickiusMousius/zmk-pk-underglow) module, you can combine its layer indicator feature with this module's auto-layer toggling to create a dedicated pairing visual (e.g., illuminating the number pad keys, blinking an enter key, and playing a fireworks animation upon successful pairing).

To do this, simply add the `ble-pairing-layer;` property to your designated pairing layer in your underglow configuration:

```dts
&pk_underglow {
    // ...
    layer_ble_pairing {
        layer-id = <3>; // Matches the passkey-layer above
        ble-pairing-layer; // Marks this layer as the dedicated pairing indicator layer
        bindings = < ... >;
    };
};
```

When pairing starts, `zmk-ble-passkey` will automatically activate the layer. Then, `zmk-pk-underglow` will detect that a `ble-pairing-layer` is active and force a transient override on the underglow hardware (even if the user had underglow disabled), locking the brightness and rendering your indicator layout. Upon a successful pair, it will seamlessly play the fireworks animation across both the central and peripheral halves!

# Using the Event System

The module integrates deeply with ZMK's event system, broadcasting passkey state and typed digits. You can listen to these events in your custom ZMK code (e.g., custom display widgets) to build rich user interfaces.

## Available Events

The module defines three main event types:

1. **`ble_passkey_state_changed`**: Fired when passkey entry becomes active (pairing started) or inactive (pairing cancelled, failed, or completed).
   - `active` (bool): True if passkey entry is currently active.
2. **`ble_passkey_digits_changed`**: Fired every time a valid numeric key is pressed during an active passkey entry session.
   - `digits_len` (uint8_t): The number of digits currently typed (0-6).
   - `passkey` (char[7]): A null-terminated string containing the currently typed digits.
3. **`ble_pairing_complete`**: Fired when the pairing process concludes successfully.
   - `bonded` (bool): True if the device successfully bonded.

## Example: Updating a Display Widget

You can subscribe to these events in a custom display widget to show a pairing prompt and the digits as they are typed.

```c
#include <zephyr/kernel.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_passkey_state_changed.h>
#include <zmk/events/ble_passkey_digits_changed.h>
#include <zmk/events/ble_pairing_complete.h>

// State listener for when pairing starts/stops
static int passkey_state_event_handler(const zmk_event_t *eh) {
    const struct ble_passkey_state_changed *ev = as_ble_passkey_state_changed(eh);
    if (ev) {
        printk("Pairing Active: %d\n", ev->active);
        // Show or hide the passkey prompt on your display...
    }
    return ZMK_EV_EVENT_BUBBLE;
}

// Digits listener for when keys are typed
static int passkey_digits_event_handler(const zmk_event_t *eh) {
    const struct ble_passkey_digits_changed *ev = as_ble_passkey_digits_changed(eh);
    if (ev) {
        printk("Typed Passkey: %s (%d digits)\n", ev->passkey, ev->digits_len);
        // Update the typed numbers on your display...
    }
    return ZMK_EV_EVENT_BUBBLE;
}

// Register listeners
ZMK_LISTENER(my_passkey_widget, passkey_state_event_handler);
ZMK_SUBSCRIPTION(my_passkey_widget, ble_passkey_state_changed);

ZMK_LISTENER(my_passkey_digits_widget, passkey_digits_event_handler);
ZMK_SUBSCRIPTION(my_passkey_digits_widget, ble_passkey_digits_changed);
```

> **Note on Threading:** ZMK's event dispatcher runs in its own system work queue. If your event handlers trigger display redraws or other UI changes, make sure to submit those tasks to the `zmk_display_work_q()` to prevent race conditions or threading issues!

# Split Architecture Details

The BLE Passkey Interceptor is primarily driven by the central half of the keyboard, as it manages the Bluetooth connections to the host.

- **Central Role**: Intercepts the Zephyr Bluetooth authentication callbacks. When a passkey is required, it captures numeric keystrokes directly from the ZMK event queue before they are sent to the host. It also automatically discovers the `ble_passkey_sync` behavior and invokes it on all connected peripherals to keep their state perfectly synchronized.
- **Peripheral Role**: Relies entirely on the sync behavior. When the central invokes the `zmk,behavior-ble-passkey-sync` behavior over the split BLE link, the peripheral locally raises `ble_passkey_state_changed` or `ble_pairing_complete` events so its own display widgets can react simultaneously with the central.

# License

This project is licensed under the MIT License.
