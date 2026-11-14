#pragma once

#include <obs-module.h>

/* A video filter that plays a "dots" effect over the live picture, once, when it is started: the picture is covered
 * by a grid of round dots, then the dots give way again to the clean live picture. Add it to a source under
 * Filters > Effect Filters. It is started by the OBS hotkey "Zoominator: Play dots overlay" (Settings > Hotkeys) or
 * by the "Play now" button in the filter's settings. */
namespace zoominator_dots {

/* Registers the filter type. Call once from obs_module_load. */
void registerFilter();

/* Registers / removes the hotkey. Call from the controller's initialize() and shutdown(). Main thread only. */
void startHotkey();
void stopHotkey();

} // namespace zoominator_dots
