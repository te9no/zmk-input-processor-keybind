/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_keybind

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/input_processor.h>

#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <dt-bindings/zmk/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/keymap.h>

// Add function declaration
int zmk_behavior_queue_add(struct zmk_behavior_binding_event *event,
                          struct zmk_behavior_binding binding,
                          bool press, uint32_t wait);

struct zip_keybind_config {
    uint8_t type;
    bool track_remainders;
    const struct zmk_behavior_binding *bindings;  // Changed from key_codes
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

#define DIRECTION_THRESHOLD 50  // 方向キー入力と判定する閾値
// キーコードをZMKの定義に合わせる
#define KEY_UP    KC_UP    // ZMKのキーコード
#define KEY_DOWN  KC_DOWN
#define KEY_LEFT  KC_LEFT
#define KEY_RIGHT KC_RIGHT

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

        LOG_DBG("Queue key event: binding %d", idx);
        zmk_behavior_queue_add(&ev, cfg->bindings[idx], true, cfg->tap_ms);
        zmk_behavior_queue_add(&ev, cfg->bindings[idx], false, cfg->wait_ms);

        if (!cfg->track_remainders) {
            data->delta_x = 0;
            data->delta_y = 0;
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

#define ZIP_KEYBIND_INST(n)                                                                        \
    static struct zip_keybind_data data_##n = {};                                                  \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) == 4, "Must have exactly 4 bindings");             \
    static const struct zmk_behavior_binding bindings_##n[] = DT_INST_PROP(n, bindings);           \
    static struct zip_keybind_config config_##n = {                                                \
        .type = INPUT_EV_REL,                                                                      \
        .track_remainders = DT_INST_PROP_OR(n, track_remainders, false),                          \
        .bindings = bindings_##n,                                                                  \
        .tap_ms = DT_INST_PROP_OR(n, tap_ms, 10),                                                \
        .wait_ms = DT_INST_PROP_OR(n, wait_ms, 50),                                              \
        .tick = DT_INST_PROP_OR(n, tick, DIRECTION_THRESHOLD)                                     \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, &zip_keybind_init, NULL, &data_##n, &config_##n, POST_KERNEL,        \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ZIP_KEYBIND_INST)
