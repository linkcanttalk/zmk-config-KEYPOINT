#pragma once

#include <zmk/event_manager.h>

enum vibe_coding_status {
    VIBE_CODING_STATUS_IDLE = 0,
    VIBE_CODING_STATUS_RUNNING = 1,
    VIBE_CODING_STATUS_WARNING = 2,
    VIBE_CODING_STATUS_CRITICAL = 3,
};

struct vibe_coding_status_event {
    enum vibe_coding_status status;
};

ZMK_EVENT_DECLARE(vibe_coding_status_event);
