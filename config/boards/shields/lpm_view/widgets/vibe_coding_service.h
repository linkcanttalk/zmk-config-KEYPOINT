#pragma once

#include <stdint.h>

#define VIBE_CODING_SVC_UUID 0x1234
#define VIBE_CODING_CHAR_UUID 0x5678

enum vibe_coding_state {
    VIBE_CODING_IDLE = 0,
    VIBE_CODING_RUNNING = 1,
    VIBE_CODING_WARNING = 2,
    VIBE_CODING_CRITICAL = 3,
};

typedef void (*vibe_coding_state_cb)(enum vibe_coding_state state);

int vibe_coding_service_init(vibe_coding_state_cb cb);
enum vibe_coding_state vibe_coding_service_get_state(void);
