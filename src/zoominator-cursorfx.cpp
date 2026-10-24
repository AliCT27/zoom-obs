/* Cursor ring: keeps the cursor-effects source in the current scene while the option is on and tells it where the
 * cursor is. The position comes from onTick (zoomed: published by the graphics tick; not zoomed: read here). */

#include "zoominator-internal.hpp"

namespace {

void normalizeItem(obs_sceneitem_t *item)
{
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	vec2 one{};
	one.x = 1.0f;
	one.y = 1.0f;
	obs_sceneitem_set_scale(item, &one);
	vec2 zero{};
	obs_sceneitem_set_pos(item, &zero);
	obs_sceneitem_set_locked(item, true);
}

} // namespace

/* Main thread. Cheap when nothing changed, so it runs on the 500 ms timer. */
void ZoominatorController::refreshCursorRing()
{
	if (shuttingDown)
		return;

	if (!cursorRingEnabled) {
		zoominator_cursorfx::setCursor(false, 0.0, 0.0, 1.0);
		if (cursorFxShown)
			hideCursorRingInAllScenes();
		return;
	}

	auto rgba = [](uint32_t c, float *out) {
		out[0] = (float)((c >> 16) & 0xFF) / 255.0f;
		out[1] = (float)((c >> 8) & 0xFF) / 255.0f;
		out[2] = (float)(c & 0xFF) / 255.0f;
		out[3] = (float)((c >> 24) & 0xFF) / 255.0f;
	};
	zoominator_cursorfx::Style st;
	st.sizePx = (float)std::clamp(cursorRingSize, 20, 400);
	st.thicknessPx = (float)std::clamp(cursorRingPx, 1, 40);
	rgba(cursorRingColor, st.color);
	st.rainbow = cursorRingRainbow;
	st.rainbowSec = (float)std::clamp(cursorRingSec, 2, 120);
	zoominator_cursorfx::setStyle(st);

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

	if (!cursorFxSource) {
		cursorFxSource = zoominator_cursorfx::create();
		if (!cursorFxSource)
			return;
	}

	obs_sceneitem_t *item = find_scene_item_for_source(scene, cursorFxSource);
	bool fresh = false;
	if (!item) {
		item = obs_scene_add(scene, cursorFxSource);
		if (!item)
			return;
		normalizeItem(item);
		fresh = true;
	}

	/* On top of everything. The safe-zone guide also wants the top, so while it is shown the ring may sit just
	 * below it; otherwise the two would swap places forever. Moved only when needed, since every move shows up
	 * in OBS's source list. */
	int count = 0;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *, void *param) {
			++*static_cast<int *>(param);
			return true;
		},
		&count);
	const int pos = (int)obs_sceneitem_get_order_position(item);
	const int lowest = guideShown ? count - 2 : count - 1;
	if (fresh || pos < lowest)
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);

	if (!obs_sceneitem_visible(item))
		obs_sceneitem_set_visible(item, true);
	cursorFxShown = true;
}

void ZoominatorController::hideCursorRingInAllScenes()
{
	cursorFxShown = false;
	zoominator_cursorfx::setCursor(false, 0.0, 0.0, 1.0);
	if (!cursorFxSource)
		return;

	obs_frontend_source_list scenes{};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *sceneSource = scenes.sources.array[i];
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		if (!scene)
			continue;
		if (obs_sceneitem_t *item = find_scene_item_for_source(scene, cursorFxSource)) {
			if (obs_sceneitem_visible(item))
				obs_sceneitem_set_visible(item, false);
		}
	}
	obs_frontend_source_list_free(&scenes);
}

/* Main thread, view not zoomed: the ring follows the real cursor. */
void ZoominatorController::updateCursorRingIdle()
{
	if (!wantsCursorRingIdle()) {
		zoominator_cursorfx::setCursor(false, 0.0, 0.0, 1.0);
		return;
	}
	int cx = 0, cy = 0;
	float sx = 0.0f, sy = 0.0f;
	bool inside = false;
	const bool ok = getCursorPos(cx, cy) && mapCursorToScenePixels(cx, cy, sx, sy, inside) && inside;
	double dx = (double)sx, dy = (double)sy;
	if (ok)
		applyTiltToIdlePoint(dx, dy);
	zoominator_cursorfx::setCursor(ok, dx, dy, 1.0);
}
