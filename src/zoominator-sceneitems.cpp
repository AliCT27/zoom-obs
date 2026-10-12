/* Scene items: reading, remembering and restoring item transforms. */

#include "zoominator-internal.hpp"

QString ZoominatorController::sceneItemKey(obs_sceneitem_t *item) const
{
	if (!item)
		return {};

	obs_source_t *src = obs_sceneitem_get_source(item);
	obs_scene_t *scene = obs_sceneitem_get_scene(item);
	obs_source_t *sceneSource = scene ? obs_scene_get_source(scene) : nullptr;

	const char *sceneName = sceneSource ? obs_source_get_name(sceneSource) : nullptr;
	const char *sourceName = src ? obs_source_get_name(src) : nullptr;
	const int64_t itemId = obs_sceneitem_get_id(item);

	if (!sceneName || !*sceneName || !sourceName || !*sourceName)
		return {};

	return QStringLiteral("%1::%2::%3")
		.arg(QString::fromUtf8(sceneName))
		.arg(QString::fromUtf8(sourceName))
		.arg(itemId);
}

ZoominatorController::OrigState ZoominatorController::readSceneItemTransform(obs_sceneitem_t *item) const
{
	OrigState state{};
	if (!item)
		return state;

	obs_sceneitem_get_pos(item, &state.pos);
	obs_sceneitem_get_scale(item, &state.scale);
	state.rot = obs_sceneitem_get_rot(item);
	state.align = obs_sceneitem_get_alignment(item);
	state.boundsType = obs_sceneitem_get_bounds_type(item);
	state.boundsAlign = obs_sceneitem_get_bounds_alignment(item);
	obs_sceneitem_get_bounds(item, &state.bounds);
	obs_sceneitem_get_crop(item, &state.crop);
	state.valid = true;
	return state;
}

void ZoominatorController::applySceneItemTransform(obs_sceneitem_t *item, const OrigState &state)
{
	if (!item || !state.valid)
		return;

	obs_sceneitem_set_pos(item, &state.pos);
	obs_sceneitem_set_scale(item, &state.scale);
	obs_sceneitem_set_rot(item, state.rot);
	obs_sceneitem_set_alignment(item, state.align);
	obs_sceneitem_set_bounds_type(item, state.boundsType);
	obs_sceneitem_set_bounds_alignment(item, state.boundsAlign);
	obs_sceneitem_set_bounds(item, &state.bounds);
	obs_sceneitem_set_crop(item, &state.crop);
}


/* The item's transform plus the position and scale it effectively has once
 * bounds and alignment are taken into account. */
ZoominatorController::OrigState ZoominatorController::computeOrigState(obs_sceneitem_t *item) const
{
	if (!item)
		return {};
	OrigState orig = readSceneItemTransform(item);
	if (!orig.valid)
		return orig;

	float renderedW = 0.0f;
	float renderedH = 0.0f;

	obs_source_t *src = obs_sceneitem_get_source(item);
	float visibleW = 0.0f;
	float visibleH = 0.0f;
	if (src) {
		const float sw = (float)obs_source_get_width(src);
		const float sh = (float)obs_source_get_height(src);
		visibleW = sw - (float)orig.crop.left - (float)orig.crop.right;
		visibleH = sh - (float)orig.crop.top - (float)orig.crop.bottom;
	}
	if (visibleW <= 0.0f)
		visibleW = 1.0f;
	if (visibleH <= 0.0f)
		visibleH = 1.0f;

	if (orig.boundsType == OBS_BOUNDS_NONE || orig.bounds.x <= 0.0f || orig.bounds.y <= 0.0f) {
		renderedW = visibleW * orig.scale.x;
		renderedH = visibleH * orig.scale.y;
	} else {
		const float sx = orig.bounds.x / visibleW;
		const float sy = orig.bounds.y / visibleH;

		renderedW = orig.bounds.x;
		renderedH = orig.bounds.y;
		orig.effectiveScale.x = sx;
		orig.effectiveScale.y = sy;
	}

	if (orig.boundsType == OBS_BOUNDS_NONE || orig.bounds.x <= 0.0f || orig.bounds.y <= 0.0f) {
		orig.effectiveScale.x = renderedW / visibleW;
		orig.effectiveScale.y = renderedH / visibleH;
	}

	/* libobs maps a scene item as
	 *     p_scene = R(rot) * (S * p_local - origin) + pos
	 * (update_item_transform, obs-scene.c), where origin is the alignment
	 * offset inside the scaled item. The zoom re-anchors every item to
	 * top-left alignment but deliberately leaves rot untouched, so the
	 * alignment origin must be rotated before it is subtracted. Subtracting it
	 * unrotated anchors a rotated source as if it were axis-aligned, which
	 * swings it off the canvas. Reduces to the old expression when rot == 0. */
	vec2 originOffset{};
	if (orig.align & OBS_ALIGN_RIGHT)
		originOffset.x = renderedW;
	else if (!(orig.align & OBS_ALIGN_LEFT))
		originOffset.x = renderedW * 0.5f;

	if (orig.align & OBS_ALIGN_BOTTOM)
		originOffset.y = renderedH;
	else if (!(orig.align & OBS_ALIGN_TOP))
		originOffset.y = renderedH * 0.5f;

	float originSin = 0.0f;
	float originCos = 1.0f;
	rotation_sin_cos(orig.rot, originSin, originCos);

	orig.effectivePos.x = orig.pos.x - (originOffset.x * originCos - originOffset.y * originSin);
	orig.effectivePos.y = orig.pos.y - (originOffset.x * originSin + originOffset.y * originCos);

	orig.valid = true;
	return orig;
}

void ZoominatorController::captureOriginal(obs_sceneitem_t *item)
{
	const OrigState orig = computeOrigState(item);
	if (!orig.valid)
		return;

	const QString key = sceneItemKey(item);
	if (!key.isEmpty())
		recoveryTransforms.insert(key, orig);

	SceneItemState state{};
	state.item = item;
	state.orig = orig;
	sceneItems.push_back(state);
}

void ZoominatorController::captureOriginalSceneItems(const std::vector<obs_sceneitem_t *> &items)
{
	sceneItems.clear();
	sceneContentBoundsValid = false;
	for (auto *item : items)
		captureOriginal(item);

	if (recoveryActive && !sceneItems.empty())
		saveSettings();

	for (const auto &state : sceneItems) {
		if (!state.item || !state.orig.valid)
			continue;
		/* Hidden items are still transformed (so they stay consistent if they
		 * are switched on mid-zoom) but must not drive the framing bounds:
		 * they show nothing, and scenes routinely park them far off-canvas. */
		if (!obs_sceneitem_visible(state.item))
			continue;
		accumulateItemBounds(state.orig, obs_sceneitem_get_source(state.item), sceneContentMin, sceneContentMax,
				     sceneContentBoundsValid);
	}
}

/* Grows [mn, mx] by the axis-aligned bounds of an item as it actually appears,
 * i.e. of the rotated quad anchored at its effective position. Treating a
 * rotated item as an upright box puts the bounds somewhere the source is not,
 * and the framing clamp then pushes the real source off-canvas by exactly that
 * error. */
void ZoominatorController::accumulateItemBounds(const OrigState &orig, obs_source_t *src, vec2 &mn, vec2 &mx,
						bool &valid) const
{
	if (!src)
		return;
	const float sw = (float)obs_source_get_width(src);
	const float sh = (float)obs_source_get_height(src);
	const float visibleW = std::max(1.0f, sw - (float)orig.crop.left - (float)orig.crop.right);
	const float visibleH = std::max(1.0f, sh - (float)orig.crop.top - (float)orig.crop.bottom);
	const float renderedW = visibleW * orig.effectiveScale.x;
	const float renderedH = visibleH * orig.effectiveScale.y;

	float boundsSin = 0.0f;
	float boundsCos = 1.0f;
	rotation_sin_cos(orig.rot, boundsSin, boundsCos);

	const float cornerX[4] = {0.0f, renderedW, 0.0f, renderedW};
	const float cornerY[4] = {0.0f, 0.0f, renderedH, renderedH};

	float minX = orig.effectivePos.x;
	float minY = orig.effectivePos.y;
	float maxX = minX;
	float maxY = minY;
	for (int corner = 0; corner < 4; ++corner) {
		const float px = orig.effectivePos.x + cornerX[corner] * boundsCos - cornerY[corner] * boundsSin;
		const float py = orig.effectivePos.y + cornerX[corner] * boundsSin + cornerY[corner] * boundsCos;
		minX = std::min(minX, px);
		maxX = std::max(maxX, px);
		minY = std::min(minY, py);
		maxY = std::max(maxY, py);
	}

	if (!valid) {
		mn.x = minX;
		mn.y = minY;
		mx.x = maxX;
		mx.y = maxY;
		valid = true;
	} else {
		mn.x = std::min(mn.x, minX);
		mn.y = std::min(mn.y, minY);
		mx.x = std::max(mx.x, maxX);
		mx.y = std::max(mx.y, maxY);
	}
}

/* Bounds of live items as they are right now (not zooming). */
bool ZoominatorController::computeTargetBounds(const std::vector<obs_sceneitem_t *> &items, vec2 &mn, vec2 &mx) const
{
	bool valid = false;
	mn = {};
	mx = {};
	for (auto *item : items) {
		if (!item || !obs_sceneitem_visible(item))
			continue;
		const OrigState orig = computeOrigState(item);
		if (!orig.valid)
			continue;
		accumulateItemBounds(orig, obs_sceneitem_get_source(item), mn, mx, valid);
	}
	return valid;
}

void ZoominatorController::restoreOriginal(obs_sceneitem_t *item)
{
	if (!item)
		return;
	for (const auto &state : sceneItems) {
		if (state.item != item || !state.orig.valid)
			continue;
		applySceneItemTransform(item, state.orig);
		return;
	}
}

void ZoominatorController::restoreOriginalSceneItems(const std::vector<obs_sceneitem_t *> &items)
{
	for (auto *item : items)
		restoreOriginal(item);
}

void ZoominatorController::restoreOriginalSceneItemsFromState()
{
	for (const auto &state : sceneItems) {
		if (state.item && state.orig.valid)
			applySceneItemTransform(state.item, state.orig);
	}
}

