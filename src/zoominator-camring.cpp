/* Webcam ring: the circular webcam with a ring and glow, and its size change
 * while zooming. The circle and ring are drawn by a filter on the webcam source
 * (cam-ring-filter.cpp); this file attaches it and scales the webcam's scene
 * item with the zoom. */

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
		zoominator_camring::setStyle(camFilter, st);
	}

	if (src)
		obs_source_release(src);
}

/* Main thread, when a zoom starts: remember the webcam's own transform so it can
 * be scaled around its centre and put back afterwards. */
void ZoominatorController::captureCamItem(obs_scene_t *scene)
{
	camItem = nullptr;
	camLastValid = false;
	camOrig = {};
	if (!scene || !camRingEnabled || camSourceName.isEmpty() || camZoomPct == 100)
		return;

	obs_source_t *src = obs_get_source_by_name(camSourceName.toUtf8().constData());
	if (!src)
		return;
	obs_sceneitem_t *item = find_scene_item_for_source(scene, src);
	obs_source_release(src);
	if (!item)
		return;

	/* A webcam that is ticked on the Sources tab is zoomed with the content. */
	for (const auto &state : sceneItems) {
		if (state.item == item)
			return;
	}

	const OrigState orig = computeOrigState(item);
	if (!orig.valid || std::fabs(orig.rot) > 0.01f)
		return;

	obs_sceneitem_addref(item);
	camItem = item;
	camOrig = orig;

	const QString key = sceneItemKey(item);
	if (!key.isEmpty()) {
		recoveryTransforms.insert(key, orig);
		if (recoveryActive)
			saveSettings();
	}
}

/* Main thread. Puts the webcam back exactly as it was. */
void ZoominatorController::restoreCamItem()
{
	if (!camItem)
		return;
	if (obs_sceneitem_get_scene(camItem))
		applySceneItemTransform(camItem, camOrig);
	obs_sceneitem_release(camItem);
	camItem = nullptr;
	camLastValid = false;
	camOrig = {};
}
