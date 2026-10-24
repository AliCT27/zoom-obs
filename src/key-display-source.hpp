#pragma once

#include <obs-module.h>

#include <cstdint>
#include <vector>

/* A canvas-sized source that shows the keys the user just pressed (for example "Ctrl + C") as key caps, then fades
 * them out. The picture is made by the plugin on the main thread; this source only draws it. */
namespace zoominator_keys {

/* Registers the source type. Call once from obs_module_load. */
void registerSource();

/* Creates the private source, or nullptr when it cannot be created. Main thread only. */
obs_source_t *create();

/* Hands over a new picture (straight-alpha BGRA, width * height * 4 bytes) and starts showing it. Safe from any thread. */
void show(const std::vector<uint8_t> &bgra, int width, int height);

/* Where the picture's centre sits, as a percentage of the canvas height. Safe from any thread. */
void setPlacement(int yPct);

/* Stops showing anything. Safe from any thread. */
void hide();

} // namespace zoominator_keys
