/* Safe-zone guide: keeps the guide source in the current scene while the option is on and nothing is being
 * recorded, streamed or sent to the virtual camera, and hides it otherwise. */

#include "zoominator-internal.hpp"

/* Main thread. Cheap when nothing changed, so it runs on a timer. */
void ZoominatorController::refreshGuide()
{
	if (shuttingDown)
		return;

	const bool outputActive = obs_frontend_recording_active() || obs_frontend_streaming_active() ||
				  obs_frontend_virtualcam_active();
	if (!guideEnabled || outputActive) {
		if (guideShown)
			hideGuideInAllScenes();
		return;
	}

	zoominator_guide::Style st;
	st.topPx = std::clamp(guideTopPx, 0, 1500);
	st.bottomPx = std::clamp(guideBottomPx, 0, 1500);
	st.leftPx = std::clamp(guideLeftPx, 0, 500);
	st.rightPx = std::clamp(guideRightPx, 0, 500);
	zoominator_guide::setStyle(st);

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

	if (!guideSource) {
		guideSource = zoominator_guide::create();
		if (!guideSource)
			return;
	}

	obs_sceneitem_t *item = find_scene_item_for_source(scene, guideSource);
	bool fresh = false;
	if (!item) {
		item = obs_scene_add(scene, guideSource);
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

	/* On top of everything so the tint is seen over the content. Moved only when it is not already there,
	 * since every move shows up in OBS's source list. */
	int count = 0;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *, void *param) {
			++*static_cast<int *>(param);
			return true;
		},
		&count);
	if (fresh || (int)obs_sceneitem_get_order_position(item) != count - 1)
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);

	if (!obs_sceneitem_visible(item))
		obs_sceneitem_set_visible(item, true);
	guideShown = true;
}

void ZoominatorController::hideGuideInAllScenes()
{
	guideShown = false;
	if (!guideSource)
		return;

	obs_frontend_source_list scenes{};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *sceneSource = scenes.sources.array[i];
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		if (!scene)
			continue;
		if (obs_sceneitem_t *item = find_scene_item_for_source(scene, guideSource)) {
			if (obs_sceneitem_visible(item))
				obs_sceneitem_set_visible(item, false);
		}
	}
	obs_frontend_source_list_free(&scenes);
}
