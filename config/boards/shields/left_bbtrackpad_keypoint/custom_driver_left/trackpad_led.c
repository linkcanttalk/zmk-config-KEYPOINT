/*
 * Copyright (c) 2023 ZitaoTech
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

#include <zmk/endpoints.h>
#include <zmk/hid_indicators.h>
#include <zmk/backlight.h>
#include <zmk/rgb_underglow.h>
#include <zmk/activity.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include "trackpad_led.h"
#include "a320.h"
#include "vibe_coding_service.h"

#define HID_INDICATORS_CAPS_LOCK (1 << 1)

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(DT_HAS_CHOSEN(zmk_trackpad_led),
             "CONFIG_ZMK_TRACKPAD_LED enabled but no zmk,trackpad_led chosen node found");

static const struct device *const led_dev = DEVICE_DT_GET(DT_CHOSEN(zmk_trackpad_led));

#define CHILD_COUNT(...) +1
#define DT_NUM_CHILD(node_id) (DT_FOREACH_CHILD(node_id, CHILD_COUNT))
#define INDICATOR_LED_NUM_LEDS (DT_NUM_CHILD(DT_CHOSEN(zmk_trackpad_led)))

#define BRT_MIN 10
#define BRT_MAX 100
#define BRT_LOW 20
#define BRT_STEP 3

#define ANIMATION_INTERVAL_MS 20
#define POLLING_INTERVAL_MS 5
#define AUTO_OFF_DELAY_MS 3000

#define FLASH_ON_MS 100   /* USB mode ON duration */
#define FLASH_PERIOD 1000 /* Total USB flash period */

#define PULSE_STEP_MS 10
#define PULSE_SEQ_LEN 41 /* 10 fade-in + 20 hold + 6 fade-out + 5 rest */
#define PULSE_REPEAT_MS 2000

/* Step duration for all effects */
#define EFFECT_STEP_MS 10

/* Pulse body: 10 fade-in + 20 hold + 6 fade-out + 5 rest = 41 steps */
#define PULSE_BODY \
    0, 8, 15, 22, 29, 35, 41, 46, 51, 56, \
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, \
    80, 80, 80, 80, 80, 80, 80, 80, 80, 80, \
    80, 77, 67, 51, 29, 0, 0, 0, 0, 0, 0

/* LED off duration macros (at 10ms per step) */
#define LED_OFF_50MS    0, 0, 0, 0, 0                   /* 5 steps = 50ms */
#define LED_OFF_100MS   LED_OFF_50MS, LED_OFF_50MS      /* 10 steps = 100ms */
#define LED_OFF_200MS   LED_OFF_100MS, LED_OFF_100MS    /* 20 steps = 200ms */
#define LED_OFF_500MS   LED_OFF_200MS, LED_OFF_200MS, LED_OFF_50MS  /* 50 steps = 500ms */
#define LED_OFF_1S      LED_OFF_500MS, LED_OFF_500MS    /* 100 steps = 1s */
#define LED_OFF_2S      LED_OFF_1S, LED_OFF_1S          /* 200 steps = 2s */

/* LED on duration macros (at 10ms per step) */
#define LED_ON_100MS    BRT_MAX, BRT_MAX, BRT_MAX, BRT_MAX, BRT_MAX, \
                        BRT_MAX, BRT_MAX, BRT_MAX, BRT_MAX, BRT_MAX

/* Flash macros: each flash = ON(100ms) + OFF(100ms) = 20 steps */
#define LED_FLASH_ONCE  BRT_MAX, BRT_MAX, BRT_MAX, BRT_MAX, BRT_MAX, \
                        BRT_MAX, BRT_MAX, BRT_MAX, BRT_MAX, BRT_MAX, \
                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0
#define LED_FLASH_3X    LED_FLASH_ONCE, LED_FLASH_ONCE, LED_FLASH_ONCE
#define LED_FLASH_6X    LED_FLASH_3X, LED_FLASH_3X

/* Breathing effect: slow sine-like curve from 10 to 30 and back */
#define BREATHING_BODY \
    10, 10, 10, 11, 11, 11, 12, 12, 12, 13, \
    13, 13, 14, 14, 14, 15, 15, 15, 16, 16, \
    16, 17, 17, 17, 18, 18, 18, 19, 19, 19, \
    20, 20, 20, 21, 21, 21, 22, 22, 22, 23, \
    23, 23, 24, 24, 24, 25, 25, 25, 26, 26, \
    26, 27, 27, 27, 28, 28, 28, 29, 29, 29, \
    30, 30, 30, 30, 30, 30, 30, 30, 30, 30, \
    29, 29, 29, 28, 28, 28, 27, 27, 27, 26, \
    26, 26, 25, 25, 25, 24, 24, 24, 23, 23, \
    23, 22, 22, 22, 21, 21, 21, 20, 20, 20, \
    19, 19, 19, 18, 18, 18, 17, 17, 17, 16, \
    16, 16, 15, 15, 15, 14, 14, 14, 13, 13, \
    13, 12, 12, 12, 11, 11, 11, 10, 10, 10

#define VIBE_CODING_LED_BRT BRT_MAX

/* Effect step counts */
#define CRITICAL_FLASH_STEPS    (3 * 20 + 200)   /* 3 flashes + 2s off = 260 steps */
#define WARNING_PULSE_STEPS     (41 + 5 + 41 + 200)  /* pulse + 50ms + pulse + 2s = 287 steps */
#define RUNNING_BREATHE_STEPS   130              /* 130 steps slow breathing = 1.3s */
#define IDLE_TRANSITION_STEPS   (41 + 5 + 41 + 5 + 10 + 200)  /* 2 pulses + on + 2s = 302 steps */
#define TIMEOUT_IDLE_STEPS      (6 * 20 + 200)   /* 6 flashes + 2s off = 320 steps */

enum vc_effect_type {
    VC_EFFECT_NONE,
    VC_EFFECT_CRITICAL,
    VC_EFFECT_WARNING,
    VC_EFFECT_RUNNING,
    VC_EFFECT_IDLE_TRANSITION,
    VC_EFFECT_TIMEOUT_IDLE,
};

struct vc_effect {
    const uint8_t *seq;
    uint16_t len;
    uint8_t step_ms;
    uint8_t repeat_count;
};

static struct k_work_delayable polling_work;
static struct k_work_delayable animation_work;
static struct k_work_delayable auto_off_work;
static struct k_work_delayable usb_flash_work;
static struct k_work_delayable pulse_work;
static struct k_work_delayable pulse_repeat_work;
static struct k_work_delayable scroll_breathing_work;
static struct k_work_delayable effect_work;

static bool capslock_on = false;
static bool touch_active = false;
static bool animation_increasing = true;
static uint8_t brightness = BRT_MIN;

static uint8_t last_valid_brt = BRT_MAX;
static uint8_t last_backlight_brt = 0;
static bool manual_override = false;
static bool keyboard_active = false;

static bool rgb_toggled_on = false;

static bool scroll_breathing_active = false;
static bool scroll_breathing_increasing = true;
static uint8_t scroll_breathing_brightness = BRT_MIN;

static bool usb_flash_state = false;
static bool usb_mode = false;

static bool vibe_coding_effect_active = false;
static bool effect_active = false;
static enum vc_effect_type current_effect_type = VC_EFFECT_NONE;
static const struct vc_effect *current_vc_effect;
static uint16_t effect_step;
static uint8_t effect_repeat_remaining;

static const uint8_t pulse_indicator_layers[] = {1, 2};

#define PULSE_INDICATOR_LAYER_COUNT ARRAY_SIZE(pulse_indicator_layers)

static const uint8_t pulse_seq[PULSE_SEQ_LEN] = {
    PULSE_BODY,
};

static const uint8_t critical_flash_seq[CRITICAL_FLASH_STEPS] = {
    LED_FLASH_3X, LED_OFF_2S,
};

static const uint8_t warning_pulse_seq[WARNING_PULSE_STEPS] = {
    PULSE_BODY, LED_OFF_50MS,
    PULSE_BODY,
    LED_OFF_2S,
};

static const uint8_t running_breathe_seq[RUNNING_BREATHE_STEPS] = {
    BREATHING_BODY,
};

static const uint8_t idle_transition_seq[IDLE_TRANSITION_STEPS] = {
    PULSE_BODY, LED_OFF_50MS,
    PULSE_BODY, LED_OFF_50MS,
    LED_ON_100MS,
    LED_OFF_2S,
};

static const uint8_t timeout_idle_flash_seq[TIMEOUT_IDLE_STEPS] = {
    LED_FLASH_6X,
    LED_OFF_2S,
};

static const struct vc_effect critical_flash_effect = {
    .seq = critical_flash_seq, .len = ARRAY_SIZE(critical_flash_seq),
    .step_ms = EFFECT_STEP_MS, .repeat_count = 0,
};
static const struct vc_effect warning_pulse_effect = {
    .seq = warning_pulse_seq, .len = ARRAY_SIZE(warning_pulse_seq),
    .step_ms = EFFECT_STEP_MS, .repeat_count = 0,
};
static const struct vc_effect running_breathe_effect = {
    .seq = running_breathe_seq, .len = ARRAY_SIZE(running_breathe_seq),
    .step_ms = EFFECT_STEP_MS, .repeat_count = 0,
};
static const struct vc_effect idle_transition_effect = {
    .seq = idle_transition_seq, .len = ARRAY_SIZE(idle_transition_seq),
    .step_ms = EFFECT_STEP_MS, .repeat_count = 3,
};

static const struct vc_effect timeout_idle_flash_effect = {
    .seq = timeout_idle_flash_seq, .len = ARRAY_SIZE(timeout_idle_flash_seq),
    .step_ms = EFFECT_STEP_MS, .repeat_count = 3,
};

static uint8_t pulse_remaining = 0;
static uint8_t pulse_step = 0;
static bool pulse_active = false;
static uint8_t pulse_saved_brt = 0;
static uint8_t led_hw_brt = 0;
static uint8_t last_layer = 0;

static void pulse_work_handler(struct k_work *work);
static void pulse_repeat_work_handler(struct k_work *work);
static bool pulse_layer_indicate(uint8_t layer);
void trackpad_led_pulse(uint8_t count);
static void scroll_breathing_stop(void);
static void scroll_breathing_start(void);
static void effect_stop(void);

static void pulse_stop(void) {
    k_work_cancel_delayable(&pulse_work);
    k_work_cancel_delayable(&pulse_repeat_work);
    pulse_active = false;
    scroll_breathing_stop();
}

static void set_led_brightness(uint8_t level) {
    if (!device_is_ready(led_dev)) {
        LOG_ERR("LED device not ready");
        return;
    }
    for (int i = 0; i < INDICATOR_LED_NUM_LEDS; i++) {
        int err = led_set_brightness(led_dev, i, level);
        if (err < 0) {
            LOG_ERR("Failed to set LED[%d] brightness: %d", i, err);
        }
    }
    led_hw_brt = level;
}

static bool pulse_layer_indicate(uint8_t layer) {
    for (uint8_t i = 0; i < PULSE_INDICATOR_LAYER_COUNT; i++) {
        if (pulse_indicator_layers[i] == layer) {
            return true;
        }
    }
    return false;
}

/* USB flashing handler */
static void usb_flash_work_handler(struct k_work *work) {
    if (!usb_mode) {
        set_led_brightness(0);
        return;
    }

    usb_flash_state = !usb_flash_state;
    set_led_brightness(usb_flash_state ? BRT_MAX : 0);

    k_work_reschedule(&usb_flash_work,
                      K_MSEC(usb_flash_state ? FLASH_ON_MS : (FLASH_PERIOD - FLASH_ON_MS)));
}

static void auto_off_work_handler(struct k_work *work) {
    if (!capslock_on && !touch_active && !pulse_active && !vibe_coding_effect_active) {
        scroll_breathing_stop();
        manual_override = false;
        set_led_brightness(0);
        LOG_DBG("Auto-off triggered after inactivity");
    }
}

static void animation_work_handler(struct k_work *work) {
    if (!capslock_on)
        return;

    if (animation_increasing) {
        brightness += BRT_STEP;
        if (brightness >= BRT_MAX) {
            brightness = BRT_MAX;
            animation_increasing = false;
        }
    } else {
        brightness -= BRT_STEP;
        if (brightness <= BRT_LOW) {
            brightness = BRT_LOW;
            animation_increasing = true;
        }
    }

    set_led_brightness(brightness);
    k_work_reschedule(&animation_work, K_MSEC(ANIMATION_INTERVAL_MS));
}

static void scroll_breathing_work_handler(struct k_work *work) {
    if (!scroll_breathing_active)
        return;

    if (scroll_breathing_increasing) {
        scroll_breathing_brightness += BRT_STEP;
        if (scroll_breathing_brightness >= BRT_MAX) {
            scroll_breathing_brightness = BRT_MAX;
            scroll_breathing_increasing = false;
        }
    } else {
        scroll_breathing_brightness -= BRT_STEP;
        if (scroll_breathing_brightness <= BRT_LOW) {
            scroll_breathing_brightness = BRT_LOW;
            scroll_breathing_increasing = true;
        }
    }

    set_led_brightness(scroll_breathing_brightness);
    k_work_reschedule(&scroll_breathing_work, K_MSEC(ANIMATION_INTERVAL_MS));
}

static void scroll_breathing_stop(void) {
    scroll_breathing_active = false;
    k_work_cancel_delayable(&scroll_breathing_work);
}

static void scroll_breathing_start(void) {
    scroll_breathing_active = true;
    scroll_breathing_brightness = BRT_MIN;
    scroll_breathing_increasing = true;
    k_work_reschedule(&scroll_breathing_work, K_NO_WAIT);
}

static void effect_stop(void) {
    k_work_cancel_delayable(&effect_work);
    effect_active = false;
    current_effect_type = VC_EFFECT_NONE;
}

static void effect_work_handler(struct k_work *work) {
    if (!effect_active) {
        return;
    }

    set_led_brightness(current_vc_effect->seq[effect_step]);
    effect_step++;

    if (effect_step >= current_vc_effect->len) {
        if (current_vc_effect->repeat_count == 0) {
            effect_step = 0;
        } else {
            effect_repeat_remaining--;
            if (effect_repeat_remaining > 0) {
                effect_step = 0;
            } else {
                effect_active = false;
                current_effect_type = VC_EFFECT_NONE;
                set_led_brightness(0);
                LOG_INF("Effect finished");
                return;
            }
        }
    }

    k_work_reschedule(&effect_work, K_MSEC(current_vc_effect->step_ms));
}

static void effect_start(const struct vc_effect *effect, enum vc_effect_type type) {
    effect_stop();
    current_vc_effect = effect;
    current_effect_type = type;
    effect_step = 0;
    effect_repeat_remaining = effect->repeat_count;
    effect_active = true;
    k_work_reschedule(&effect_work, K_NO_WAIT);
    LOG_INF("Effect started (type=%d)", type);
}

static void vibe_coding_effect_stop(void) {
    if (vibe_coding_effect_active) {
        vibe_coding_effect_active = false;
        effect_stop();
        set_led_brightness(0);
    }
}

static const struct vc_effect *get_vc_effect(enum vc_effect_type type) {
    switch (type) {
    case VC_EFFECT_CRITICAL:      return &critical_flash_effect;
    case VC_EFFECT_WARNING:       return &warning_pulse_effect;
    case VC_EFFECT_RUNNING:       return &running_breathe_effect;
    case VC_EFFECT_IDLE_TRANSITION: return &idle_transition_effect;
    case VC_EFFECT_TIMEOUT_IDLE:  return &timeout_idle_flash_effect;
    default:                      return NULL;
    }
}

static void vibe_coding_effect_update(void) {
    enum vibe_coding_status vc_status = vibe_coding_service_get_status();
    uint8_t current_layer = zmk_keymap_highest_layer_active();
    bool should_activate = (current_layer == 0) && !capslock_on
                           && (vc_status != VIBE_CODING_STATUS_IDLE) && !usb_mode;

    if (should_activate) {
        enum vc_effect_type new_type = VC_EFFECT_NONE;
        switch (vc_status) {
        case VIBE_CODING_STATUS_CRITICAL: new_type = VC_EFFECT_CRITICAL; break;
        case VIBE_CODING_STATUS_WARNING:  new_type = VC_EFFECT_WARNING; break;
        case VIBE_CODING_STATUS_RUNNING:  new_type = VC_EFFECT_RUNNING; break;
        default: break;
        }

        if (new_type != current_effect_type) {
            if (!vibe_coding_effect_active) {
                pulse_stop();
                scroll_breathing_stop();
            }
            vibe_coding_effect_active = true;
            effect_start(get_vc_effect(new_type), new_type);
            LOG_INF("Vibe coding LED ON (status=%d)", vc_status);
        }
    } else if (vibe_coding_effect_active) {
        vibe_coding_effect_active = false;
        if (vc_status == VIBE_CODING_STATUS_IDLE && current_layer == 0 && !capslock_on && !usb_mode) {
            if (vibe_coding_service_is_timeout()) {
                effect_start(&timeout_idle_flash_effect, VC_EFFECT_TIMEOUT_IDLE);
            } else {
                effect_start(&idle_transition_effect, VC_EFFECT_IDLE_TRANSITION);
            }
        } else {
            effect_stop();
            set_led_brightness(0);
        }
        LOG_INF("Vibe coding LED OFF");
    } else if (vc_status == VIBE_CODING_STATUS_IDLE && current_layer == 0 && !capslock_on && !usb_mode) {
        if (vibe_coding_service_is_timeout()) {
            pulse_stop();
            scroll_breathing_stop();
            effect_start(&timeout_idle_flash_effect, VC_EFFECT_TIMEOUT_IDLE);
            LOG_INF("Vibe coding LED timeout from IDLE");
        }
    }
}

static void pulse_work_handler(struct k_work *work) {
    set_led_brightness(pulse_seq[pulse_step]);
    pulse_step++;

    if (pulse_step >= PULSE_SEQ_LEN) {
        pulse_remaining--;
        if (pulse_remaining > 0) {
            pulse_step = 0;
            k_work_reschedule(&pulse_work, K_NO_WAIT);
        } else {
            pulse_active = false;
            if (touch_active && pulse_layer_indicate(zmk_keymap_highest_layer_active())) {
                set_led_brightness(pulse_saved_brt > 0 ? pulse_saved_brt : 0);
            } else if (touch_active && !rgb_toggled_on) {
                scroll_breathing_start();
            } else {
                set_led_brightness(0);
            }
        }
        return;
    }

    k_work_reschedule(&pulse_work, K_MSEC(PULSE_STEP_MS));
}

void trackpad_led_pulse(uint8_t count) {
    scroll_breathing_stop();
    k_work_cancel_delayable(&pulse_work);
    if (!pulse_active) {
        pulse_saved_brt = led_hw_brt;
    }
    pulse_active = true;
    pulse_remaining = count;
    pulse_step = 0;
    if (count > 0) {
        k_work_reschedule(&pulse_work, K_NO_WAIT);
    }
}

static bool last_rgb_state = false;

static void polling_work_handler(struct k_work *work) {
    enum zmk_transport transport = zmk_endpoints_selected().transport;
    bool current_capslock = (zmk_hid_indicators_get_current_profile() & HID_INDICATORS_CAPS_LOCK);
    bool current_touch = tp_is_touched();
    bool current_active = (zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
    uint8_t current_brt = zmk_backlight_get_brt();
    bool current_rgb = false;
    zmk_rgb_underglow_get_state(&current_rgb);

    /* ---------------- USB mode ---------------- */
    if (transport == ZMK_TRANSPORT_USB) {
        if (!usb_mode) {
            usb_mode = true;
            usb_flash_state = false;
            pulse_stop();
            vibe_coding_effect_active = false;
            effect_stop();
            k_work_reschedule(&usb_flash_work, K_NO_WAIT);
            LOG_INF("Entered USB flash mode");
        }
        k_work_reschedule(&polling_work, K_MSEC(POLLING_INTERVAL_MS));
        return;
    }

    /* ---------------- BLE mode ---------------- */
    if (usb_mode) {
        usb_mode = false;
        k_work_cancel_delayable(&usb_flash_work);
        set_led_brightness(0);
        LOG_INF("Exited USB flash mode");
    }

    if (current_active != keyboard_active) {
        keyboard_active = current_active;
        if (keyboard_active) {
            last_backlight_brt = current_brt;
        }
    }

    /* CapsLock animation */
    if (current_capslock != capslock_on) {
        capslock_on = current_capslock;
        if (capslock_on) {
            pulse_stop();
            vibe_coding_effect_stop();
            effect_stop();
            brightness = BRT_MIN;
            animation_increasing = true;
            k_work_reschedule(&animation_work, K_NO_WAIT);
        } else {
            k_work_cancel_delayable(&animation_work);
            manual_override = false;

            if (current_touch) {
                touch_active = true;
                manual_override = true;
                if (keyboard_active) {
                    last_valid_brt = MAX(BRT_MIN, current_brt);
                }
                pulse_stop();
                set_led_brightness(last_valid_brt);
                k_work_cancel_delayable(&auto_off_work);
            } else {
                pulse_stop();
                set_led_brightness(0);
            }
        }
    }

    /* Touch event handling */
    if (!capslock_on && current_touch != touch_active) {
        touch_active = current_touch;
        if (touch_active) {
            pulse_stop();
            manual_override = true;
            if (keyboard_active) {
                last_valid_brt = MAX(BRT_MIN, current_brt);
            }

            if (rgb_toggled_on) {
                set_led_brightness(last_valid_brt);
            } else {
                scroll_breathing_start();
            }

            k_work_cancel_delayable(&auto_off_work);
        } else {
            k_work_reschedule(&auto_off_work, K_MSEC(AUTO_OFF_DELAY_MS));
        }
    }

    /* Backlight brightness change */
    if (!capslock_on && !touch_active && current_brt != last_backlight_brt && keyboard_active) {
        last_backlight_brt = current_brt;
        if (current_brt > 0) {
            manual_override = true;
            last_valid_brt = MAX(BRT_MIN, current_brt);
            pulse_stop();
            set_led_brightness(last_valid_brt);
            k_work_reschedule(&auto_off_work, K_MSEC(AUTO_OFF_DELAY_MS));
        }
    }

    /* RGB state change - update trackpad mode */
    if (current_rgb != last_rgb_state) {
        last_rgb_state = current_rgb;
        rgb_toggled_on = current_rgb;
        LOG_INF("RGB state changed: %s", rgb_toggled_on ? "ON (Mouse)" : "OFF (Scroll)");
    }

    /* Vibe coding LED effect */
    if (!usb_mode && !capslock_on) {
        vibe_coding_effect_update();
    } else if (vibe_coding_effect_active) {
        vibe_coding_effect_active = false;
    }

    k_work_reschedule(&polling_work, K_MSEC(POLLING_INTERVAL_MS));
}

static int layer_change_listener(const zmk_event_t *eh) {
    uint8_t current_layer = zmk_keymap_highest_layer_active();
    if (current_layer != last_layer) {
        last_layer = current_layer;
        k_work_cancel_delayable(&pulse_repeat_work);

        /* Cancel vibe coding effect when leaving layer 0 */
        if (current_layer != 0 && vibe_coding_effect_active) {
            vibe_coding_effect_stop();
        }
        if (current_layer != 0 && current_effect_type != VC_EFFECT_NONE) {
            effect_stop();
        }

        if (!capslock_on && !touch_active && pulse_layer_indicate(current_layer)) {
            if (!pulse_active) {
                pulse_saved_brt = led_hw_brt;
            }
            trackpad_led_pulse(current_layer);
            k_work_reschedule(&pulse_repeat_work, K_MSEC(PULSE_REPEAT_MS));
            LOG_INF("Layer %d pulse %d times", current_layer, current_layer);
        }
    }
    return 0;
}

ZMK_LISTENER(layer_change_led_listener, layer_change_listener);
ZMK_SUBSCRIPTION(layer_change_led_listener, zmk_layer_state_changed);

uint8_t indicator_tp_get_last_valid_brightness(void) { return last_valid_brt; }

bool indicator_tp_is_rgb_on(void) { return rgb_toggled_on; }

static void pulse_repeat_work_handler(struct k_work *work) {
    uint8_t current_layer = zmk_keymap_highest_layer_active();
    if (!capslock_on && !touch_active && pulse_layer_indicate(current_layer) && !pulse_active) {
        trackpad_led_pulse(current_layer);
    }
    if (!capslock_on && !touch_active && pulse_layer_indicate(current_layer)) {
        k_work_reschedule(&pulse_repeat_work, K_MSEC(PULSE_REPEAT_MS));
    }
}

static int indicator_tp_init(void) {
    if (!device_is_ready(led_dev)) {
        LOG_ERR("LED indicator_tp device not ready");
        return -ENODEV;
    }

    set_led_brightness(0);
    usb_mode = false;
    usb_flash_state = false;
    last_backlight_brt = zmk_backlight_get_brt();
    capslock_on = touch_active = manual_override = keyboard_active = false;
    last_layer = zmk_keymap_highest_layer_active();

    k_work_init_delayable(&polling_work, polling_work_handler);
    k_work_init_delayable(&animation_work, animation_work_handler);
    k_work_init_delayable(&auto_off_work, auto_off_work_handler);
    k_work_init_delayable(&usb_flash_work, usb_flash_work_handler);
    k_work_init_delayable(&pulse_work, pulse_work_handler);
    k_work_init_delayable(&pulse_repeat_work, pulse_repeat_work_handler);
    k_work_init_delayable(&scroll_breathing_work, scroll_breathing_work_handler);
    k_work_init_delayable(&effect_work, effect_work_handler);

    vibe_coding_service_init(NULL);

    k_work_reschedule(&polling_work, K_NO_WAIT);
    return 0;
}

SYS_INIT(indicator_tp_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
