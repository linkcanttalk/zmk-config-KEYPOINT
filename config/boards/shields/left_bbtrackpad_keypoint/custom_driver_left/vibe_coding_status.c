/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <vibe_coding_status/events.h>
#include <raw_hid/events.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define VIBE_CODING_STATUS_DATA_TYPE 0xBB

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

    enum vibe_coding_status status = (enum vibe_coding_status)status_value;
    LOG_INF("vibe_coding_status: received status %u", status_value);

    raise_vibe_coding_status_event((struct vibe_coding_status_event){.status = status});
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
