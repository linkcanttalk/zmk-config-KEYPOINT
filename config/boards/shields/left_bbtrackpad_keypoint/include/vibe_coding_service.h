#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "vibe_coding_status/events.h"

typedef void (*vibe_coding_status_cb)(enum vibe_coding_status status);

int vibe_coding_service_init(vibe_coding_status_cb cb);
enum vibe_coding_status vibe_coding_service_get_status(void);
bool vibe_coding_service_is_timeout(void);
bool vibe_coding_service_peek_timeout(void);
