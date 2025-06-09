/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/input/input.h>
#include <zephyr/device.h>
#include <zephyr/sys/dlist.h>
#include <drivers/behavior.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/keys.h>
#include <zmk/virtual_key_position.h>
#include <zmk/behavior_queue.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT zmk_input_processor_keybind

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct zip_keybind_config {
    uint8_t type;
    bool track_remainders;
    const struct zmk_behavior_binding *bindings;
    uint32_t tap_ms;
    uint32_t wait_ms;
    uint32_t tick;
};

struct zip_keybind_data {
    int16_t delta_x;
    int16_t delta_y;
};

static int zip_keybind_handle_event(const struct device *dev, struct input_event *event,
                                    uint32_t param1, uint32_t param2,
                                    struct zmk_input_processor_state *state) {
    const struct zip_keybind_config *cfg = dev->config;
    struct zip_keybind_data *data = dev->data;
    int16_t value = (int16_t)event->value;

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    // LOG_DBG("zip_keybind_handle_event val: %d", value);
    // k_sleep(K_MSEC(10));
    //  動きを蓄積
    if (event->code == INPUT_REL_X) {
        data->delta_x += value;
    } else if (event->code == INPUT_REL_Y) {
        data->delta_y += value;
    } else {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    LOG_DBG("dx: %d (%d) dy: %d (%d) tick: %d", data->delta_x, abs(data->delta_x), data->delta_y,
            abs(data->delta_y), cfg->tick);

    int idx = -1;
    if (abs(data->delta_x) >= cfg->tick) {
        idx = data->delta_x > 0 ? 0 : 1; // LEFT : RIGHT (順番入れ替え)
        data->delta_x %= cfg->tick;
    } else if (abs(data->delta_y) >= cfg->tick) {
        idx = data->delta_y > 0 ? 3 : 2; // UP : DOWN
        data->delta_y %= cfg->tick;
    }

    if (idx != -1) {
        // Remap rel code
        // event->code = INPUT_REL_MISC;

        struct zmk_behavior_binding_event ev = {
            .position = 12345 + idx,
            .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
            .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
        };

        const struct zmk_behavior_binding *binding = NULL;
        if (idx == 0) {
            binding = &cfg->bindings[1];
        } else if (idx == 1) {
            binding = &cfg->bindings[0];
        } else if (idx == 2) {
            binding = &cfg->bindings[3];
        } else if (idx == 3) {
            binding = &cfg->bindings[2];
        } else {
            LOG_WRN("Invalid IDX %d", idx);
        }

        if (binding) {
            LOG_DBG("trigger binding: %s 0x%02x 0x%02x tap: %d ms hold %d ms",
                    binding->behavior_dev, binding->param1, binding->param2, cfg->tap_ms,
                    cfg->wait_ms);

            zmk_behavior_queue_add(&ev, *binding, true, cfg->tap_ms);
            zmk_behavior_queue_add(&ev, *binding, false, cfg->wait_ms);
        }

        return ZMK_INPUT_PROC_STOP;
    }

    return ZMK_INPUT_PROC_STOP;
}

static struct zmk_input_processor_driver_api sy_driver_api = {
    .handle_event = zip_keybind_handle_event,
};

static int zip_keybind_init(const struct device *dev) {
    struct zip_keybind_data *data = dev->data;

    data->delta_x = 0;
    data->delta_y = 0;

    return 0;
}

#define TRANSFORMED_BINDINGS(n)                                                                    \
    {LISTIFY(DT_INST_PROP_LEN(n, bindings), ZMK_KEYMAP_EXTRACT_BINDING, (, ), DT_DRV_INST(n))}

#define ZIP_KEYBIND_INST(n)                                                                        \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) >= 4, "bindings should have at least 4 elements");  \
    static struct zip_keybind_data zip_keybind_data_##n = {};                                      \
    static struct zmk_behavior_binding zip_keybind_config_##n_bindings[] =                         \
        TRANSFORMED_BINDINGS(n);                                                                   \
    static struct zip_keybind_config zip_keybind_config_##n = {                                    \
        .bindings = zip_keybind_config_##n_bindings,                                               \
        .type = INPUT_EV_REL,                                                                      \
        .track_remainders = DT_INST_PROP_OR(n, track_remainders, true),                            \
        .tap_ms = DT_INST_PROP_OR(n, tap_ms, 30),                                                  \
        .wait_ms = DT_INST_PROP_OR(n, wait_ms, 15),                                                \
        .tick = DT_INST_PROP_OR(n, tick, 10)};                                                     \
    DEVICE_DT_INST_DEFINE(n, &zip_keybind_init, NULL, &zip_keybind_data_##n,                       \
                          &zip_keybind_config_##n, POST_KERNEL,                                    \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ZIP_KEYBIND_INST)
