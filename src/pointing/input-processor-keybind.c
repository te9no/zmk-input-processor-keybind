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
#include <zmk/behavior_queue.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT zmk_input_processor_keybind

#define DIRECTION_THRESHOLD 10

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct zip_keybind_config {
    uint8_t type;
    bool track_remainders;
    size_t bindings_len;
    const struct zmk_behavior_binding *bindings;
    uint32_t tap_ms;
    uint32_t wait_ms;
    uint32_t tick;
};

struct zip_keybind_data {
    int16_t last_x;
    int16_t last_y;
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

    // 動きを蓄積
    if (event->code == INPUT_REL_X) {
        data->delta_x = value;
    } else if (event->code == INPUT_REL_Y) {
        data->delta_y = value;
    }

    // キーイベントの生成判定
    int idx = -1;
    if (abs(data->delta_x) > cfg->tick) {
        idx = data->delta_x > 0 ? 0 : 1;  // LEFT : RIGHT (順番入れ替え)
    } else if (abs(data->delta_y) > cfg->tick) {
        idx = data->delta_y > 0 ? 3 : 2;  // UP : DOWN
    }
    k_sleep(K_MSEC(10));

    if (idx != -1) {
        struct zmk_behavior_binding_event ev = {
            .position = INT32_MAX,
            .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
            .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
        };

        const struct zmk_behavior_binding *binding = NULL;
        if (idx == 0) binding = &cfg->bindings[1];
        else if (idx == 1) binding = &cfg->bindings[0];
        else if (idx == 2) binding = &cfg->bindings[3];
        else if (idx == 3) binding = &cfg->bindings[2];

        if (binding) {
            LOG_DBG("Queue key event: binding %d", idx);
            zmk_behavior_queue_add(&ev, *binding, true, cfg->tap_ms);
            zmk_behavior_queue_add(&ev, *binding, false, cfg->wait_ms);

            if (!cfg->track_remainders) {
                data->delta_x = 0;
                data->delta_y = 0;
            }
        }
    }

    return ZMK_INPUT_PROC_STOP;
}

static struct zmk_input_processor_driver_api sy_driver_api = {
    .handle_event = zip_keybind_handle_event,
};

static int zip_keybind_init(const struct device *dev) {
    struct zip_keybind_data *data = dev->data;

    // Only initialize coordinates
    data->last_x = 0;
    data->last_y = 0;
    data->delta_x = 0;
    data->delta_y = 0;

    return 0;
}


#define TRANSFORMED_BINDINGS(n)                                                                    \
    {LISTIFY(DT_INST_PROP_LEN(n, bindings), ZMK_KEYMAP_EXTRACT_BINDING, (, ), DT_DRV_INST(n))}

#define ZIP_KEYBIND_INST(n)                                                                        \
    static struct zip_keybind_data zip_keybind_data_##n = {};                                      \
    static struct zmk_behavior_binding config_##n_bindings[] = TRANSFORMED_BINDINGS(n);            \
    static struct zip_keybind_config config_##n = {                                                \
        .bindings_len = DT_INST_PROP_LEN(n, bindings),                                             \
        .bindings = config_##n_bindings,                                                           \
        .type = INPUT_EV_REL,                                                                      \
        .track_remainders = DT_INST_PROP_OR(n, track_remainders, false),                           \
        .tap_ms = DT_INST_PROP_OR(n, tap_ms, 30),                                                  \
        .wait_ms = DT_INST_PROP_OR(n, wait_ms, 15),                                                \
        .tick = DT_INST_PROP_OR(n, tick, DIRECTION_THRESHOLD)};                                    \
    DEVICE_DT_INST_DEFINE(n, &zip_keybind_init, NULL, &zip_keybind_data_##n, &config_##n,          \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ZIP_KEYBIND_INST)
// DT_INST_FOREACH_CHILD(0, ZIP_KEYBIND_INST)
