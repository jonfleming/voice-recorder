#pragma once
#include <stdint.h>

typedef enum {
    STATE_IDLE = 0,
    STATE_RECORDING,
    STATE_FILE_BROWSER,
    STATE_PLAYBACK
} app_state_t;

const char* state_name(app_state_t s);
