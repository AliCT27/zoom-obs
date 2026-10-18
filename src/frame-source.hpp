#pragma once

#include <obs-module.h>

/* A canvas-sized OBS source that paints the area around a rectangle with a solid
 * colour and draws a glowing, rounded border along the rectangle's edges. The
 * inside of the rectangle is transparent.
 *
 * The rectangle is the zoomed screen's current position on the canvas. The
 * controller updates it every frame while zooming, so the border follows the
 * screen as it zooms and pans. Drawing is done by a shader (data/frame.effect);
 * nothing is rebuilt when the rectangle moves. */
namespace zoominator_frame {

struct Style {
	float borderColor[4] = {0.18f, 0.80f, 0.25f, 1.0f};
	float bgColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	float bgColor2[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	int bgMode = 0;        /* 0 solid, 1 linear gradient, 2 radial gradient */
	float bgAngleDeg = 90; /* linear: 0 = left to right, 90 = top to bottom */
	float borderPx = 4.0f;
	float radiusPx = 24.0f;
	float glowPx = 80.0f;
	float glowStrength = 0.7f;
};

/* Registers the source type. Call once from obs_module_load. */
void registerSource();

/* Creates the private source, or nullptr if the shader could not be loaded
 * (the reason is logged once). Main thread only. */
obs_source_t *create();

/* Safe to call from any thread. */
void setStyle(const Style &style);

/* Rectangle in canvas pixels: left, top, right, bottom. May extend past the
 * canvas. Safe to call from any thread. */
void setRect(float left, float top, float right, float bottom);


/* Nothing is drawn until a rectangle has been set. */
void clearRect();

} // namespace zoominator_frame
