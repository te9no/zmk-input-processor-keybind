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
#include <zephyr/init.h>
#include <drivers/behavior.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>
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
    uint8_t index;
    uint8_t mode;

    bool track_remainders;
    const struct zmk_behavior_binding *bindings;
    uint32_t tap_ms;
    uint32_t wait_ms;
    uint32_t tick;

    int32_t threshold;
    int32_t max_threshold;

    int32_t max_pending_activations;
};

struct zip_keybind_data {
    int32_t delta_x;
    int32_t delta_y;

    int32_t last_delta_x;
    int32_t last_delta_y;

    int32_t max_delta;
    uint8_t device_index;

    const struct device *dev;
    struct k_work_delayable press_work;
    int64_t start_time;
};

static inline bool has_pending_movement(const struct zip_keybind_data *data,
                                        const struct zip_keybind_config *cfg) {
    return abs(data->delta_x) >= cfg->tick || abs(data->delta_y) >= cfg->tick;
}

static uint32_t approx_hypot(uint32_t a, uint32_t b) {
    if (a < b) {
        unsigned int tmp = a;
        a = b;
        b = tmp;
    }
    return a + ((b * 3) >> 3);
}

static void keybind_handle_raw(struct zip_keybind_data *data,
                               const struct zip_keybind_config *cfg) {

    data->delta_x = CLAMP(data->delta_x + data->last_delta_x, -data->max_delta, data->max_delta);
    data->delta_y = CLAMP(data->delta_y + data->last_delta_y, -data->max_delta, data->max_delta);

    data->last_delta_x = 0;
    data->last_delta_y = 0;
}

static void keybind_handle_4way(struct zip_keybind_data *data,
                                const struct zip_keybind_config *cfg) {
    int32_t movement = approx_hypot(abs(data->last_delta_x), abs(data->last_delta_y));
    if (abs(data->last_delta_x) > abs(data->last_delta_y)) {
        if (data->last_delta_x < 0)
            movement = -movement;

        data->delta_x = CLAMP(data->delta_x + movement, -data->max_delta, data->max_delta);
    } else {
        if (data->last_delta_y < 0)
            movement = -movement;

        data->delta_y = CLAMP(data->delta_y + movement, -data->max_delta, data->max_delta);
    }

    data->last_delta_x = 0;
    data->last_delta_y = 0;
}

static void keybind_handle_8way(struct zip_keybind_data *data,
                                const struct zip_keybind_config *cfg) {
    int32_t dx = data->last_delta_x;
    int32_t dy = data->last_delta_y;
    int32_t adx = abs(dx);
    int32_t ady = abs(dy);

    if (dy == 0) {
        data->delta_x = CLAMP(data->delta_x + dx, -data->max_delta, data->max_delta);
    } else if (dx == 0) {
        data->delta_y = CLAMP(data->delta_y + dy, -data->max_delta, data->max_delta);
    } else {
        int32_t movement = approx_hypot(adx, ady);

        // check tan for 22.5° sector, which is approx. 5/12
        if (5 * adx > 12 * ady) {
            // horizontal movement (±22.5°)
            data->delta_x = CLAMP(data->delta_x + (dx < 0 ? -movement : movement), -data->max_delta,
                                  data->max_delta);
        } else if (5 * ady > 12 * adx) {
            // vertival movement (±22.5°)
            data->delta_y = CLAMP(data->delta_y + (dy < 0 ? -movement : movement), -data->max_delta,
                                  data->max_delta);
        } else {
            // diagonals
            int32_t delta_x = (dx < 0) ? -movement : movement;
            int32_t delta_y = (dy < 0) ? -movement : movement;
            data->delta_x = CLAMP(data->delta_x + delta_x, -data->max_delta, data->max_delta);
            data->delta_y = CLAMP(data->delta_y + delta_y, -data->max_delta, data->max_delta);
        }
    }

    data->last_delta_x = 0;
    data->last_delta_y = 0;
}

static int zip_keybind_handle_event(const struct device *dev, struct input_event *event,
                                    uint32_t param1, uint32_t param2,
                                    struct zmk_input_processor_state *state) {
    const struct zip_keybind_config *cfg = dev->config;
    struct zip_keybind_data *data = dev->data;
    int32_t value = event->value;

    // 起動から10秒経過するまで処理をスキップ
    if (k_uptime_get() - data->start_time < 10000) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    LOG_DBG("dev: %d evt: %d val: %d thresh: %d sync: %d dx: %d dy: %d", state->input_device_index,
            event->code, value, cfg->threshold, (int)event->sync, data->delta_x, data->delta_y);

    // cutoff small or very large movements
    if (cfg->threshold > abs(value) || abs(value) > cfg->max_threshold)
        return ZMK_INPUT_PROC_STOP;

    // Accumulate movement
    if (event->code == INPUT_REL_X) {
        data->last_delta_x = value;
    } else if (event->code == INPUT_REL_Y) {
        data->last_delta_y = value;
    } else {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    // wait until full movement readed
    if (!event->sync)
        return ZMK_INPUT_PROC_STOP;

    int32_t movement = approx_hypot(abs(data->last_delta_x), abs(data->last_delta_y));
    LOG_DBG("movement: %d th: %d max th: %d", movement, cfg->threshold, cfg->max_threshold);
    // cutoff small or very large movements

    if (cfg->mode == 0) {
        keybind_handle_raw(data, cfg);
    } else if (cfg->mode == 1) {
        keybind_handle_4way(data, cfg);
    } else if (cfg->mode == 2) {
        keybind_handle_8way(data, cfg);
    }

    LOG_DBG("dev: %d dx: %d dy: %d tick: %d", state->input_device_index, data->delta_x,
            data->delta_y, cfg->tick);

    if (has_pending_movement(data, cfg)) {
        data->device_index = state->input_device_index;
        // 作業をすぐにスケジュールする (既に保留中の場合は更新)
        k_work_schedule(&data->press_work, K_NO_WAIT);
    }

    return ZMK_INPUT_PROC_STOP;
}

static inline uint32_t get_position(const struct zip_keybind_data *data,
                                    const struct zip_keybind_config *cfg) {
    return ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(data->device_index, cfg->index);
}

static int exec_one_binding(const struct zip_keybind_data *data,
                            const struct zip_keybind_config *cfg, int idx) {
    struct zmk_behavior_binding_event behavior_event = {
        .position = get_position(data, cfg),
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    LOG_DBG("trigger binding: bh: %s 0x%02x 0x%02x tap: %d ms hold %d ms",
            cfg->bindings[idx].behavior_dev, cfg->bindings[idx].param1, cfg->bindings[idx].param2,
            cfg->tap_ms, cfg->wait_ms);

    int ret = zmk_behavior_invoke_binding(&cfg->bindings[idx], behavior_event, true); // キーを押す
    if (ret < 0) {
        return ret;
    }

    if (cfg->tap_ms > 0)
        k_sleep(K_MSEC(cfg->tap_ms)); // 短時間のブロッキングスリープ
    behavior_event.timestamp = k_uptime_get();

    ret = zmk_behavior_invoke_binding(&cfg->bindings[idx], behavior_event, false); // キーを離す
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int exec_two_bindings(const struct zip_keybind_data *data,
                             const struct zip_keybind_config *cfg, int idx, int idy) {
    struct zmk_behavior_binding_event behavior_event = {
        .position =
            ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(data->device_index, cfg->index),
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    LOG_DBG("trigger binding: bh: %s 0x%02x 0x%02x tap: %d ms hold %d ms",
            cfg->bindings[idx].behavior_dev, cfg->bindings[idx].param1, cfg->bindings[idx].param2,
            cfg->tap_ms, cfg->wait_ms);

    int ret = zmk_behavior_invoke_binding(&cfg->bindings[idx], behavior_event, true); // キーを押す
    if (ret < 0) {
        return ret;
    }

    // もう一方のバインディングをトリガーする (内部でタップ遅延が処理される)
    ret = exec_one_binding(data, cfg, idy);
    if (ret < 0) {
        return ret;
    }

    behavior_event.timestamp = k_uptime_get();
    ret = zmk_behavior_invoke_binding(&cfg->bindings[idx], behavior_event, false); // キーを離す
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static void press_work_cb(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct zip_keybind_data *data = CONTAINER_OF(d_work, struct zip_keybind_data, press_work);
    const struct device *dev = data->dev;
    const struct zip_keybind_config *cfg = dev->config;

    // 1回のワーク実行につき、1回の移動「ティック」のみを処理
    int idx = -1;
    int idy = -1;

    if (abs(data->delta_x) >= cfg->tick) {
        if (data->delta_x > 0) { // RIGHT
            idx = 0;
            data->delta_x -= cfg->tick;
        } else { // LEFT
            idx = 1;
            data->delta_x += cfg->tick;
        }
    }

    if (abs(data->delta_y) >= cfg->tick) {
        if (data->delta_y > 0) { // UP
            idy = 3;
            data->delta_y -= cfg->tick;
        } else { // DOWN
            idy = 2;
            data->delta_y += cfg->tick;
        }
    }

    if (idx >= 0 && idy >= 0) {
        exec_two_bindings(data, cfg, idx, idy);
    } else if (idx >= 0) {
        exec_one_binding(data, cfg, idx);
    } else if (idy >= 0) {
        exec_one_binding(data, cfg, idy);
    }

    // 1ティック処理後、さらに保留中の動きがあるか確認
    if (has_pending_movement(data, cfg)) {
        // ある場合、wait-ms 遅延でこのワークアイテムを再スケジュール
        k_work_schedule(&data->press_work, K_MSEC(cfg->wait_ms));
    } else {
        // 保留中の動きがない場合、remainders を追跡しない場合はクリア
        if (!cfg->track_remainders) {
            data->delta_x = 0;
            data->delta_y = 0;
        }
        // スケジュールする必要はない、今回の作業はこれで完了
    }
}

static struct zmk_input_processor_driver_api zip_keybind_driver_api = {
    .handle_event = zip_keybind_handle_event,
};

static int zip_keybind_init(const struct device *dev) {
    struct zip_keybind_data *data = dev->data;
    const struct zip_keybind_config *cfg = dev->config;

    data->dev = dev;
    data->max_delta = cfg->max_pending_activations * cfg->tick;
    data->start_time = k_uptime_get(); // start_timeを初期化

    k_work_init_delayable(&data->press_work, press_work_cb);
    return 0;
}

#define TRANSFORMED_BINDINGS(n)                                                                    \
    {LISTIFY(DT_INST_PROP_LEN(n, bindings), ZMK_KEYMAP_EXTRACT_BINDING, (, ), DT_DRV_INST(n))}

#define ZIP_KEYBIND_INST(n)                                                                        \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) >= 4, "bindings should have at least 4 elements");  \
    static struct zip_keybind_data zip_keybind_data_##n = {                                        \
        .delta_x = 0,                                                                              \
        .delta_y = 0,                                                                              \
    };                                                                                             \
    static struct zmk_behavior_binding zip_keybind_config_bindings_##n[] =                         \
        TRANSFORMED_BINDINGS(n);                                                                   \
    static struct zip_keybind_config zip_keybind_config_##n = {                                    \
        .index = n,                                                                                \
        .mode = DT_INST_PROP_OR(n, mode, 0),                                                       \
        .bindings = zip_keybind_config_bindings_##n,                                               \
        .track_remainders = DT_INST_PROP_OR(n, track_remainders, false),                           \
        .tap_ms = DT_INST_PROP_OR(n, tap_ms, 20),                                                  \
        .wait_ms = DT_INST_PROP_OR(n, wait_ms, 0),                                                 \
        .tick = DT_INST_PROP_OR(n, tick, 10),                                                      \
        .threshold = DT_INST_PROP_OR(n, threshold, 1),                                            \
        .max_threshold = DT_INST_PROP_OR(n, max_threshold, 200),                                   \
        .max_pending_activations = DT_INST_PROP_OR(n, max_pending_activations, 5)};                \
    DEVICE_DT_INST_DEFINE(n, &zip_keybind_init, NULL, &zip_keybind_data_##n,                       \
                          &zip_keybind_config_##n, POST_KERNEL,                                    \
                          CONFIG_INPUT_INIT_PRIORITY, &zip_keybind_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ZIP_KEYBIND_INST)
