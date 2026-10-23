/* Frame: a canvas-sized shader overlay that fills everything outside the captured
 * screen with a colour and draws a glowing, rounded border along the screen's own
 * edges. The screen's current rectangle is pushed to it every frame while zooming
 * (see applyZoomToScene), so the border follows the content as it zooms and pans.
 * It sits above the zoomed sources and below the ones that are not zoomed (a
 * webcam). */

#include "zoominator-internal.hpp"
#include "frame-source.hpp"

static void normalize_frame_scene_item(obs_sceneitem_t *item)
{
	if (!item)
		return;
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	obs_sceneitem_set_rot(item, 0.0f);
	vec2 scale{};
	scale.x = 1.0f;
	scale.y = 1.0f;
	obs_sceneitem_set_scale(item, &scale);
	vec2 pos{};
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_crop crop{};
	obs_sceneitem_set_crop(item, &crop);
	obs_sceneitem_set_locked(item, true);
}

/* Keeps the frame in sync with the current scene and settings. Cheap when nothing
 * changed, so it runs on a timer and after scene or settings changes. */
void ZoominatorController::refreshFrame()
{
	if (shuttingDown)
		return;

	if (!frameEnabled) {
		if (frameSource) {
			zoominator_frame::clearRect();
			hideFrameInAllScenes();
		}
		return;
	}

	{
		auto rgba = [](uint32_t c, float *out) {
			out[0] = (float)((c >> 16) & 0xFF) / 255.0f;
			out[1] = (float)((c >> 8) & 0xFF) / 255.0f;
			out[2] = (float)(c & 0xFF) / 255.0f;
			out[3] = (float)((c >> 24) & 0xFF) / 255.0f;
		};
		zoominator_frame::Style st;
		rgba(frameColor, st.borderColor);
		rgba(frameBgColor, st.bgColor);
		rgba(frameBgColor2, st.bgColor2);
		st.bgMode = std::clamp(frameBgMode, 0, 2);
		st.bgAngleDeg = (float)frameBgAngle;
		st.borderPx = (float)std::clamp(frameBorderPx, 0, 80);
		st.radiusPx = (float)std::clamp(frameRadiusPx, 0, 400);
		st.glowPx = (float)std::clamp(frameGlowPx, 0, 200);
		st.glowStrength = (float)std::clamp(frameGlowPct, 0, 100) / 100.0f;
		st.rainbowBg = frameBgRainbow;
		st.rainbowBorder = frameBorderRainbow;
		st.rainbowSec = (float)std::clamp(frameRainbowSec, 2, 120);
		st.rainbowFlow = frameFlow;
		st.flowWaves = std::clamp(frameFlowWaves, 1, 4);
		zoominator_frame::setStyle(st);
	}

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

	std::vector<obs_sceneitem_t *> items;
	enumerateTargetItemsInCurrentScene(items);

	/* While zooming, the graphics thread publishes the rectangle every frame.
	 * Otherwise it is the content's current (unzoomed) bounds. */
	vec2 mn{}, mx{};
	const bool zooming = zoomActive.load(std::memory_order_acquire);
	const bool haveBounds = zooming ? true : computeTargetBounds(items, mn, mx);
	if (!zooming) {
		if (!haveBounds || mx.x - mn.x < 4.0f || mx.y - mn.y < 4.0f) {
			zoominator_frame::clearRect();
			if (frameSource) {
				if (obs_sceneitem_t *hidden = find_scene_item_for_source(scene, frameSource)) {
					if (obs_sceneitem_visible(hidden))
						obs_sceneitem_set_visible(hidden, false);
				}
			}
			return;
		}
		zoominator_frame::setRect(mn.x, mn.y, mx.x, mx.y);
	}

	if (!frameSource) {
		frameSource = zoominator_frame::create();
		if (!frameSource)
			return;
	}

	obs_sceneitem_t *item = find_scene_item_for_source(scene, frameSource);
	bool fresh = false;
	if (!item) {
		item = obs_scene_add(scene, frameSource);
		if (!item)
			return;
		normalize_frame_scene_item(item);
		fresh = true;
	}

	/* Directly above the topmost zoomed item, so a webcam or logo that is not
	 * zoomed stays above the frame. Re-ordering is only done when it is off,
	 * since every move shows up in OBS's source list. */
	int topPos = -1;
	for (auto *target : items) {
		if (!target || target == item || obs_sceneitem_get_scene(target) != scene)
			continue;
		topPos = std::max(topPos, (int)obs_sceneitem_get_order_position(target));
	}
	if (fresh || topPos < 0 || (int)obs_sceneitem_get_order_position(item) != topPos + 1) {
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);
		topPos = -1;
		for (auto *target : items) {
			if (!target || target == item || obs_sceneitem_get_scene(target) != scene)
				continue;
			topPos = std::max(topPos, (int)obs_sceneitem_get_order_position(target));
		}
		if (topPos < 0)
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
		else
			obs_sceneitem_set_order_position(item, topPos);
	}

	if (!obs_sceneitem_visible(item))
		obs_sceneitem_set_visible(item, true);
}

void ZoominatorController::hideFrameInAllScenes()
{
	if (!frameSource)
		return;

	obs_frontend_source_list scenes{};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *sceneSource = scenes.sources.array[i];
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		if (!scene)
			continue;
		if (obs_sceneitem_t *item = find_scene_item_for_source(scene, frameSource)) {
			if (obs_sceneitem_visible(item))
				obs_sceneitem_set_visible(item, false);
		}
	}
	obs_frontend_source_list_free(&scenes);
}
