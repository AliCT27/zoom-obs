#pragma once

#include <obs-module.h>

/* A video filter that shows its source as a flat sheet turned in 3D, so the
 * captured screen can lean towards the mouse. The controller keeps one on each
 * target source while "3D camera" is enabled and removes it again afterwards.
 *
 * Everything that has to agree with the turn (the filter, the Frame shader, the
 * spotlight and halo placement) reads one shared Pose, computed in canvas pixels.
 * The sheet turns about the point of the screen that is at the middle of the
 * canvas, so a zoomed view keeps its middle where it is. */
namespace zoominator_tilt {

void registerFilter();

/* Adds a filter to `target` (or reuses one already there); strong ref or nullptr. Main thread only. */
obs_source_t *attach(obs_source_t *target);
void detach(obs_source_t *target, obs_source_t *filter);

struct Pose {
	float yaw = 0.0f, pitch = 0.0f;      /* radians */
	float px = 0.0f, py = 0.0f;          /* pivot: canvas pixel that stays where it is */
	float dist = 1.0f;                   /* camera distance in canvas pixels */
	float scl = 1.0f;                    /* <1 shrinks the sheet (not zoomed), >1 grows it (zoomed) so no gap shows */
	float left = 0.0f, top = 0.0f;       /* the flat sheet's rectangle on the canvas */
	float width = 1.0f, height = 1.0f;
};

/* Thread safe. Turn angles in radians and the camera distance in multiples of the screen's longer side. */
/* keepWhenZoomed: keep the turn while zoomed, fading it only as far as needed near the screen's edges. */
void setAngles(float yaw, float pitch, float perspective, bool keepWhenZoomed);
/* Thread safe. How far the zoom really is: 0 = not zoomed, 1 = fully zoomed. */
void setZoomProgress(float progress);
/* Thread safe. The screen's flat rectangle on the canvas as it is drawn right now. */
void setSheet(float left, float top, float right, float bottom);
/* Forgets the sheet and the angles. */
void reset();
/* Thread safe. Diagnostic: while on (the "debug logging" switch), the tilt filter and the Frame draw outlines of the
 * flat screen, the turned screen and the filter's own sprite, so a mismatch between them can be seen. */
void setDebugGeometry(bool on);
bool debugGeometry();
/* False when nothing is turned (no sheet, or angles ~ 0). */
bool getPose(Pose &out);
/* Where a point of the flat screen (canvas pixels) is shown once the screen is turned. */
void projectPoint(const Pose &pose, double &x, double &y);

} // namespace zoominator_tilt
