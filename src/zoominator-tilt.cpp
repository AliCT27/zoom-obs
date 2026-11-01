/* 3D tilt: attaching the tilt filter and publishing its pose. */

#include "zoominator-internal.hpp"

void ZoominatorController::detachTilt()
{
	for (auto &t : tiltTargets) {
		if (t.filter && t.target)
			zoominator_tilt::detach(t.target, t.filter);
		if (t.target)
			obs_source_release(t.target);
	}
	tiltTargets.clear();
	tiltYaw = 0.0;
	tiltPitch = 0.0;
	tiltYawA.store(0.0, std::memory_order_relaxed);
	tiltPitchA.store(0.0, std::memory_order_relaxed);
	zoominator_tilt::reset();
}

/* Where a point mapped onto the flat screen (canvas pixels) is shown once the screen is turned. */
void ZoominatorController::applyTiltToIdlePoint(double &x, double &y) const
{
	zoominator_tilt::Pose pose;
	if (tilt3dEnabled && zoominator_tilt::getPose(pose))
		zoominator_tilt::projectPoint(pose, x, y);
}

/* 3D camera: turns the captured screen towards the mouse. The filter sits on the source, so it works
 * the same zoomed and not zoomed. Main thread only, called from every tick. */
void ZoominatorController::updateTilt()
{
	if (!tilt3dEnabled || shuttingDown) {
		if (!tiltTargets.empty())
			detachTilt();
		else if (tiltYawA.load(std::memory_order_relaxed) != 0.0 || tiltPitchA.load(std::memory_order_relaxed) != 0.0)
			detachTilt();
		return;
	}

	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (tiltTargets.empty() || now - tiltSyncMs >= 500) {
		tiltSyncMs = now;
		std::vector<obs_sceneitem_t *> items;
		enumerateTargetItemsInCurrentScene(items);
		bool same = items.size() == tiltTargets.size();
		for (size_t i = 0; same && i < items.size(); ++i)
			same = tiltTargets[i].item == items[i];
		if (same) {
			for (auto &t : tiltTargets)
				obs_source_filter_set_order(t.target, t.filter, OBS_ORDER_MOVE_TOP);
		}
		if (!same) {
			detachTilt();
			for (obs_sceneitem_t *item : items) {
				obs_source_t *src = item ? obs_sceneitem_get_source(item) : nullptr;
				if (!src)
					continue;
				if (obs_source_t *filter = zoominator_tilt::attach(src)) {
					TiltTarget t;
					t.item = item;
					t.target = obs_source_get_ref(src);
					t.filter = filter;
					tiltTargets.push_back(t);
				}
			}
		}
	}
	if (tiltTargets.empty())
		return;

	/* Where the mouse is on the selected screen, -1 (left / top) .. +1 (right / bottom). */
	double nx = 0.0, ny = 0.0;
	int cx = 0, cy = 0, rx = 0, ry = 0, rw = 0, rh = 0;
	if (getCursorPos(cx, cy) && getSelectedScreenRect(rx, ry, rw, rh)) {
		nx = clampd(((double)(cx - rx) / (double)rw - 0.5) * 2.0, -1.0, 1.0);
		ny = clampd(((double)(cy - ry) / (double)rh - 0.5) * 2.0, -1.0, 1.0);
	}
	/* The side the mouse is on swings away from the viewer (or towards, when inverted). */
	const double sign = tilt3dInvert ? -1.0 : 1.0;
	const double deg = 3.14159265358979323846 / 180.0;
	const double targetYaw = -nx * tilt3dMaxX * deg * sign;
	const double targetPitch = ny * tilt3dMaxY * deg * sign;

	const double dt = tiltLastMs > 0 ? clampd((double)(now - tiltLastMs) / 1000.0, 0.001, 0.1) : 0.016;
	tiltLastMs = now;
	const double resp = 1.0 - std::exp(-10.0 * dt);
	tiltYaw += (targetYaw - tiltYaw) * resp;
	tiltPitch += (targetPitch - tiltPitch) * resp;

	tiltYawA.store(tiltYaw, std::memory_order_relaxed);
	tiltPitchA.store(tiltPitch, std::memory_order_relaxed);

	/* One pose for the filter, the Frame and the spotlight / halo. While zooming the graphics thread publishes
	 * the screen's rectangle every frame; otherwise it is the content's resting bounds. */
	zoominator_tilt::setDebugGeometry(debug);
	zoominator_tilt::setAngles((float)tiltYaw, (float)tiltPitch, (float)tilt3dPerspective, tilt3dInZoom);
	zoominator_tilt::setZoomProgress(zoomActive.load(std::memory_order_acquire) ? (float)clampd(animT, 0.0, 1.0) : 0.0f);
	if (!zoomActive.load(std::memory_order_acquire)) {
		std::vector<obs_sceneitem_t *> tItems;
		for (auto &t : tiltTargets)
			tItems.push_back(t.item);
		vec2 mn{}, mx{};
		if (computeTargetBounds(tItems, mn, mx)) {
			zoominator_tilt::setSheet(mn.x, mn.y, mx.x, mx.y);
			/* Same rectangle, same moment: the Frame must not trail the picture it is turned with. */
			if (frameEnabled)
				zoominator_frame::setRect(mn.x, mn.y, mx.x, mx.y);
		}
	}
}
