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

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/keymap.h>

struct zip_keybind_config {
    uint8_t type;
    bool track_remainders;
    size_t key_codes_len;
    uint16_t key_codes[4];  // UP, DOWN, LEFT, RIGHT
    uint32_t tap_ms;        // タップ時間
    uint32_t wait_ms;       // 待機時間
    uint32_t tick;          // 閾値
};

struct zip_keybind_data {
    int16_t last_x;
    int16_t last_y;
    int16_t delta_x;
    int16_t delta_y;
};

#define DIRECTION_THRESHOLD 50  // 方向キー入力と判定する閾値
#define KEY_UP    103  // 上キーのキーコード
#define KEY_DOWN  108  // 下キーのキーコード
#define KEY_LEFT  105  // 左キーのキーコード
#define KEY_RIGHT 106  // 右キーのキーコード

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
            .position = 0,
            .timestamp = k_uptime_get()
        };

        struct zmk_behavior_binding binding = {
            .behavior_dev = zmk_behavior_get_binding("&kp"),
            .param1 = cfg->key_codes[idx],
            .param2 = 0
        };

        zmk_behavior_queue_add(&ev, binding, true, cfg->tap_ms);
        zmk_behavior_queue_add(&ev, binding, false, cfg->wait_ms);
        k_sleep(K_MSEC(10));

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

// デフォルトのキーコード配列を定義（最初に定義する必要があります）
static const uint16_t default_key_codes[] = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT};

static int zip_keybind_init(const struct device *dev) {
    struct zip_keybind_data *data = dev->data;
    struct zip_keybind_config *config = (struct zip_keybind_config *)dev->config;

    // 座標値の初期化
    data->last_x = 0;
    data->last_y = 0;

    // デフォルトのキーコードを設定
    memcpy(config->key_codes, default_key_codes, sizeof(config->key_codes));
    config->key_codes_len = 4;

    return 0;
}

#define ZIP_KEYBIND_INST(n)                                                                        \
    static struct zip_keybind_data data_##n = {};                                                  \
    static struct zip_keybind_config config_##n = {                                                \
        .type = INPUT_EV_REL,                                                                      \
        .track_remainders = DT_INST_PROP_OR(n, track_remainders, false),                          \
        .key_codes_len = 4,                                                                        \
        .tap_ms = DT_INST_PROP_OR(n, tap_ms, 10),                                                \
        .wait_ms = DT_INST_PROP_OR(n, wait_ms, 50),                                              \
        .tick = DT_INST_PROP_OR(n, tick, DIRECTION_THRESHOLD)                                     \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, &zip_keybind_init, NULL, &data_##n, &config_##n, POST_KERNEL,        \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ZIP_KEYBIND_INST)
