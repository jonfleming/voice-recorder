#include "state_machine.h"

const char* state_name(app_state_t s) {
    switch(s){
        case STATE_IDLE: return "IDLE";
        case STATE_RECORDING: return "RECORDING";
        case STATE_FILE_BROWSER: return "FILE_BROWSER";
        case STATE_PLAYBACK: return "PLAYBACK";
        default: return "UNKNOWN";
    }
}
