/* Progress bar: keeps the progress-bar source in the current scene while the option is on and tells it how long the
 * recording has been running. The bar fills up over the recording time limit of the status badge settings. */

#include "zoominator-internal.hpp"

/* Main thread. Cheap when nothing changed, so it runs on the 500 ms timer and on recording events. */
void ZoominatorController::refreshProgressBar()
{
	if (shuttingDown)
		return;

	if (!progressBarEnabled) {
		zoominator_progress::setTime(false, false, 0.0);
		if (progressShown)
			hideProgressBarInAllScenes();
		return;
	}

	zoominator_progress::Style st;
	st.lengthSec = (float)std::max(1, recLimitSec);
	st.thicknessPx = (float)std::clamp(progressBarPx, 2, 40);
	st.position = progressBarPos == 1 ? 1 : 0;
	{
		const uint32_t c = progressBarColor;
		st.color[0] = (float)((c >> 16) & 0xFF) / 255.0f;
		st.color[1] = (float)((c >> 8) & 0xFF) / 255.0f;
		st.color[2] = (float)(c & 0xFF) / 255.0f;
		st.color[3] = 1.0f;
	}
	st.rainbow = progressBarRainbow;
	st.rainbowSec = (float)std::clamp(progressBarSec, 2, 120);
	zoominator_progress::setStyle(st);

	/* No time limit means there is nothing to fill up to. */
	const bool recording = obs_frontend_recording_active();
	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
	const double elapsedMs = (double)recAccumMs + (recRunning ? (double)(nowMs - recSegStartMs) : 0.0);
	zoominator_progress::setTime(recording && recLimitSec > 0, recRunning, elapsedMs);

	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (!scene) {
		if (sceneSource)
			obs_source_release(sceneSource);
		return;
	}
	struct Guard {
		obs_source_t *s;
		~Guard() { obs_source_release(s); }
	} guard{sceneSource};

	if (!progressSource) {
		progressSource = zoominator_progress::create();
		if (!progressSource)
			return;
	}

	obs_sceneitem_t *item = find_scene_item_for_source(scene, progressSource);
	bool fresh = false;
	if (!item) {
		item = obs_scene_add(scene, progressSource);
		if (!item)
			return;
		obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
		vec2 one{};
		one.x = 1.0f;
		one.y = 1.0f;
		obs_sceneitem_set_scale(item, &one);
		vec2 zero{};
		obs_sceneitem_set_pos(item, &zero);
		obs_sceneitem_set_locked(item, true);
		fresh = true;
	}
	progressShown = true; /* counts as an overlay from here on, so the others make room */

	int count = 0;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *, void *param) {
			++*static_cast<int *>(param);
			return true;
		},
		&count);
	if (fresh || (int)obs_sceneitem_get_order_position(item) < count - overlayCount())
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);

	if (!obs_sceneitem_visible(item))
		obs_sceneitem_set_visible(item, true);
}

void ZoominatorController::hideProgressBarInAllScenes()
{
	progressShown = false;
	zoominator_progress::setTime(false, false, 0.0);
	if (!progressSource)
		return;

	obs_frontend_source_list scenes{};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *sceneSource = scenes.sources.array[i];
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		if (!scene)
			continue;
		if (obs_sceneitem_t *item = find_scene_item_for_source(scene, progressSource)) {
			if (obs_sceneitem_visible(item))
				obs_sceneitem_set_visible(item, false);
		}
	}
	obs_frontend_source_list_free(&scenes);
}
