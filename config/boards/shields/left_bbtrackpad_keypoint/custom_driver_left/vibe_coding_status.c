/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <raw_hid/events.h>
#include <vibe_coding_service.h>
#include <vibe_coding_status/events.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define VIBE_CODING_STATUS_DATA_TYPE 0xBB
#define VIBE_CODING_TIMEOUT_SECONDS 5

static enum vibe_coding_status current_status = VIBE_CODING_STATUS_IDLE;
static vibe_coding_status_cb status_callback;
static struct k_work_delayable idle_timeout_work;
static bool timeout_flag;

static void idle_timeout_handler(struct k_work *work) {
    current_status = VIBE_CODING_STATUS_IDLE;
    timeout_flag = true;
    LOG_DBG("Vibe Coding status timed out, reset to IDLE");

    if (status_callback) {
        status_callback(current_status);
    }
}

ZMK_EVENT_IMPL(vibe_coding_status_event);

static void process_raw_hid_data(uint8_t *data, uint8_t length) {
    if (length < 2) {
        LOG_WRN("vibe_coding_status: received data too short (%u bytes)", length);
        return;
    }

    uint8_t data_type = data[0];
    if (data_type != VIBE_CODING_STATUS_DATA_TYPE) {
        return;
    }

    uint8_t status_value = data[1];
    if (status_value > VIBE_CODING_STATUS_CRITICAL) {
        LOG_WRN("vibe_coding_status: invalid status value %u", status_value);
        return;
    }

    current_status = (enum vibe_coding_status)status_value;
    timeout_flag = false;
    LOG_DBG("Vibe Coding status changed to %d", current_status);

    k_work_reschedule(&idle_timeout_work, K_SECONDS(VIBE_CODING_TIMEOUT_SECONDS));

    raise_vibe_coding_status_event((struct vibe_coding_status_event){.status = current_status});

    if (status_callback) {
        status_callback(current_status);
    }
}

static int raw_hid_received_event_listener(const zmk_event_t *eh) {
    struct raw_hid_received_event *event = as_raw_hid_received_event(eh);
    if (event) {
        process_raw_hid_data(event->data, event->length);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(process_vibe_coding_status_event, raw_hid_received_event_listener);
ZMK_SUBSCRIPTION(process_vibe_coding_status_event, raw_hid_received_event);

int vibe_coding_service_init(vibe_coding_status_cb cb) {
    status_callback = cb;
    k_work_init_delayable(&idle_timeout_work, idle_timeout_handler);
    return 0;
}

enum vibe_coding_status vibe_coding_service_get_status(void) {
    return current_status;
}

bool vibe_coding_service_is_timeout(void) {
    if (timeout_flag) {
        timeout_flag = false;
        return true;
    }
    return false;
}

bool vibe_coding_service_peek_timeout(void) {
    return timeout_flag;
}
