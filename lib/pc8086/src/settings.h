/* settings.h - NOT UPSTREAM. M5PaperDOS keeps these in NVS behind a web UI;
 * here they are the defaults that suit this project, fixed. */
#pragma once

#include <stdbool.h>

static inline bool app_settings_display_partial_refresh(void) { return true; }
static inline bool app_settings_display_clear_on_bottom(void) { return false; }
static inline bool app_settings_gameport_enabled(void) { return false; }
