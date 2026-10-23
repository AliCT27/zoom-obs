/* Webcam ring: the circular webcam with a ring and glow, and its optional place
 * and size by numbers (resting and zoomed). The circle and ring are drawn by a
 * filter on the webcam source (cam-ring-filter.cpp); this file attaches it and
 * moves the webcam's scene item with the zoom. */

#include "zoominator-internal.hpp"
#include "cam-ring-filter.hpp"

/* Main thread. Keeps the filter attached to the chosen source and its look in
 * sync with the settings. Cheap when nothing changed, so it runs on a timer. */
void ZoominatorController::refreshCamRing()
{
	if (shuttingDown)
		return;

	const bool wanted = camRingEnabled && !camSourceName.isEmpty();
	obs_source_t *src = wanted ? obs_get_source_by_name(camSourceName.toUtf8().constData()) : nullptr;

	if (camFilter && (!wanted || src != camFilterTarget)) {
		zoominator_camring::detach(camFilterTarget, camFilter);
		camFilter = nullptr;
		if (camFilterTarget) {
			obs_source_release(camFilterTarget);
			camFilterTarget = nullptr;
		}
	}

	if (wanted && src && !camFilter && !camRingUnavailable) {
		camFilter = zoominator_camring::attach(src);
		if (camFilter)
			camFilterTarget = obs_source_get_ref(src);
		else
			camRingUnavailable = true;
	}

	if (camFilter) {
		zoominator_camring::Style st;
		st.ringColor[0] = (float)((camRingColor >> 16) & 0xFF) / 255.0f;
		st.ringColor[1] = (float)((camRingColor >> 8) & 0xFF) / 255.0f;
		st.ringColor[2] = (float)(camRingColor & 0xFF) / 255.0f;
		st.ringColor[3] = 1.0f;
		st.ringPx = (float)std::clamp(camRingPx, 0, 60);
		st.glowPx = (float)std::clamp(camGlowPx, 0, 150);
		st.glowStrength = (float)std::clamp(camGlowPct, 0, 100) / 100.0f;
		st.rainbow = camRainbow;
		st.flow = camFlow;
		st.rainbowSec = (float)std::clamp(camRainbowSec, 2, 120);
		zoominator_camring::setStyle(camFilter, st);
	}

	if (src)
		obs_source_release(src);

	/* Resting place of a webcam placed by numbers. While a zoom runs, the zoom
	 * tick drives it instead. */
	if (camLayoutEnabled && !tickingWanted.load(std::memory_order_acquire)) {
		if (obs_source_t *sceneSource = obs_frontend_get_current_scene()) {
			applyCamLayout(obs_scene_from_source(sceneSource), camProgress, false);
			obs_source_release(sceneSource);
		}
	}
}

/* Main thread. Places the webcam's scene item so its centre and outer diameter
 * (circle + ring + glow) match the numbers, blending the resting and the zoomed
 * place by progress (0 = resting, 1 = fully zoomed). With force == false it only
 * acts when the numbers, the item or the progress changed since the last time, so
 * a webcam dragged in OBS stays where it was dropped until the next change. */
void ZoominatorController::applyCamLayout(obs_scene_t *scene, double progress, bool force)
{
	if (!scene || !camLayoutEnabled || !camRingEnabled || camSourceName.isEmpty())
		return;

	obs_source_t *src = obs_get_source_by_name(camSourceName.toUtf8().constData());
	if (!src)
		return;
	obs_sceneitem_t *item = find_scene_item_for_source(scene, src);
	const float sw = (float)obs_source_get_width(src);
	const float sh = (float)obs_source_get_height(src);
	obs_source_release(src);
	if (!item || sw < 1.0f || sh < 1.0f)
		return;

	/* A webcam ticked on the Sources tab moves with the screen content instead. */
	for (const auto &state : sceneItems) {
		if (state.item == item)
			return;
	}
	if (std::fabs(obs_sceneitem_get_rot(item)) > 0.01f)
		return;

	const double p = clampd(progress, 0.0, 1.0);
	const int key[7] = {camIdleX, camIdleY, camIdleSize, camZoomX, camZoomY, camZoomSize, (int)std::lround(p * 1000.0)};
	if (!force && item == camLayoutItem && std::equal(std::begin(key), std::end(key), std::begin(camLayoutKey)))
		return;
	camLayoutItem = item;
	std::copy(std::begin(key), std::end(key), std::begin(camLayoutKey));

	obs_sceneitem_crop crop{};
	obs_sceneitem_get_crop(item, &crop);
	const float rw = std::max(1.0f, sw - (float)crop.left - (float)crop.right);
	const float rh = std::max(1.0f, sh - (float)crop.top - (float)crop.bottom);

	const double cx = camIdleX + (double)(camZoomX - camIdleX) * p;
	const double cy = camIdleY + (double)(camZoomY - camIdleY) * p;
	const double size = std::max(1.0, camIdleSize + (double)(camZoomSize - camIdleSize) * p);
	const float s = (float)(size / (double)std::min(rw, rh));

	vec2 sc{};
	sc.x = s;
	sc.y = s;
	vec2 pos{};
	pos.x = (float)cx - rw * s * 0.5f;
	pos.y = (float)cy - rh * s * 0.5f;

	const uint32_t topLeft = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	if (obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE)
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	if (obs_sceneitem_get_alignment(item) != topLeft)
		obs_sceneitem_set_alignment(item, topLeft);
	vec2 curSc{}, curPos{};
	obs_sceneitem_get_scale(item, &curSc);
	obs_sceneitem_get_pos(item, &curPos);
	if (!nearly_equal_vec2(curSc, sc, kScaleEpsilon))
		obs_sceneitem_set_scale(item, &sc);
	if (!nearly_equal_vec2(curPos, pos))
		obs_sceneitem_set_pos(item, &pos);
}

bool ZoominatorController::readCamLayout(int &x, int &y, int &size)
{
	if (camSourceName.isEmpty())
		return false;
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	if (!sceneSource)
		return false;
	bool ok = false;
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	obs_source_t *src = scene ? obs_get_source_by_name(camSourceName.toUtf8().constData()) : nullptr;
	if (src) {
		obs_sceneitem_t *item = find_scene_item_for_source(scene, src);
		const float sw = (float)obs_source_get_width(src);
		const float sh = (float)obs_source_get_height(src);
		if (item && sw >= 1.0f && sh >= 1.0f && std::fabs(obs_sceneitem_get_rot(item)) <= 0.01f) {
			const OrigState o = computeOrigState(item);
			if (o.valid) {
				const float rw = std::max(1.0f, sw - (float)o.crop.left - (float)o.crop.right) * o.effectiveScale.x;
				const float rh = std::max(1.0f, sh - (float)o.crop.top - (float)o.crop.bottom) * o.effectiveScale.y;
				x = (int)std::lround(o.effectivePos.x + rw * 0.5f);
				y = (int)std::lround(o.effectivePos.y + rh * 0.5f);
				size = std::max(20, (int)std::lround(std::min(rw, rh)));
				ok = true;
			}
		}
		obs_source_release(src);
	}
	obs_source_release(sceneSource);
	return ok;
}

