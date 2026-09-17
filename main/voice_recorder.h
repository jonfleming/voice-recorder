#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Voice Recorder app entry. Never returns. Used by the standalone
// firmware and by watch-os after a start-menu selection.
void voice_recorder_run(void);

#ifdef __cplusplus
}
#endif
