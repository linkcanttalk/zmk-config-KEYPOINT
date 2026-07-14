#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include "vibe_coding_service.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static enum vibe_coding_state current_state = VIBE_CODING_IDLE;
static vibe_coding_state_cb state_callback;

static ssize_t write_state(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (len != sizeof(uint8_t)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    const uint8_t *value = buf;
    if (*value > VIBE_CODING_CRITICAL) {
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    current_state = (enum vibe_coding_state)*value;
    LOG_DBG("Vibe Coding state changed to %d", current_state);

    if (state_callback) {
        state_callback(current_state);
    }

    return len;
}

BT_GATT_SERVICE_DEFINE(vibe_coding_svc, BT_GATT_PRIMARY_SERVICE(
    BT_UUID_DECLARE_16(VIBE_CODING_SVC_UUID)),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(VIBE_CODING_CHAR_UUID),
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE,
                           NULL, write_state, &current_state),
);

int vibe_coding_service_init(vibe_coding_state_cb cb) {
    state_callback = cb;
    return 0;
}

enum vibe_coding_state vibe_coding_service_get_state(void) {
    return current_state;
}
