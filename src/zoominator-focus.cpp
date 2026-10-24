/* Focus spotlight: dim image and mask, idle blur, and the blur filters. Moved out of zoominator-overlays.cpp
 * unchanged. */

#include "zoominator-internal.hpp"

/* ---- Focus spotlight ------------------------------------------------------
 *
 * While zoomed, everything except a soft circle around the cursor is dimmed.
 * It is one image_source scene item (same machinery as the click halo): a
 * black image with a transparent, feathered hole in the middle, centred on the
 * cursor. For the dim to cover the whole canvas wherever the hole sits, the
 * image must be twice the canvas in each direction. Rather than allocate a
 * 2x-canvas texture, it is generated at 1/kFocusImageScale size and the scene
 * item scales it back up; the dim area is flat colour and the feathered edge is
 * smooth, so nothing is lost. */

QString ZoominatorController::focusImagePath() const
{
	char *path = obs_module_config_path("zoominator-focus-dim.png");
	if (!path)
		return {};
	QString p = QString::fromUtf8(path);
	bfree(path);
	return p;
}

/* Canvas pixels the dim image extends beyond "twice the canvas" on each side:
 * the spotlight's full outer radius plus a little slack. */
double ZoominatorController::focusImagePad() const
{
	/* Large enough for both the zoomed radius and the not-zoomed radius (the
	 * image is only ever scaled, see focusIdleScale). */
	const double r = (double)std::max(coneRadius(focusRadius), coneRadius(focusIdleRadius));
	/* The beam has a long tail: it is practically gone at about 2.6x its half-brightness radius. */
	return std::min(2.6 * r + 4.0, 3000.0);
}

/* The spotlight not zoomed is the zoomed image scaled by this factor. */
/* The spotlight is a flashlight beam: the light is brightest at the cursor and falls off continuously with
 * distance, L(d) = exp(-ln2 * (d / R)^p). R is where it has dropped to half, p how hard the edge is (2 is a
 * gentle, natural beam; more is a crisper disc).
 * Spotlight radius sets the size of the zoomed beam; the Light cone setting sets its shape: 0 = a concentrated,
 * crisp beam, 50 = medium, 100 = light that spreads gently (the edge softness is a share of the radius). */
int ZoominatorController::beamRadiusForCone(int cone)
{
	const double c = (double)std::clamp(cone, 0, 100);
	const double r = c <= 50.0 ? 90.0 + (300.0 - 90.0) * (c / 50.0) : 300.0 + (700.0 - 300.0) * ((c - 50.0) / 50.0);
	return (int)std::lround(r);
}

int ZoominatorController::beamSoftnessForCone(int cone, int radius)
{
	const double c = (double)std::clamp(cone, 0, 100);
	const double ratio = c <= 50.0 ? 0.27 * (c / 50.0) : 0.27 + (0.6 - 0.27) * ((c - 50.0) / 50.0);
	return (int)std::lround(ratio * (double)std::clamp(radius, 40, 2000));
}

int ZoominatorController::coneForBeamRadius(int radius)
{
	const double r = (double)std::clamp(radius, 40, 2000);
	const double c = r <= 300.0 ? 50.0 * (r - 90.0) / (300.0 - 90.0) : 50.0 + 50.0 * (r - 300.0) / (700.0 - 300.0);
	return (int)std::lround(std::clamp(c, 0.0, 100.0));
}

/* Exponent of the beam profile for half-brightness radius `halfRadius` and edge softness `softness`. */
static double beamExponent(double halfRadius, double softness)
{
	const double x = softness / std::max(halfRadius, 1.0);
	/* the extra term only bites near x = 0 and makes the beam's edge really crisp (Light cone at 0) */
	return 1.6 + 6.4 * std::exp(-7.4 * x) + 6.0 * std::clamp(1.0 - x / 0.12, 0.0, 1.0);
}

int ZoominatorController::coneRadius(int radius) const
{
	return std::clamp(radius, 12, 2800);
}

int ZoominatorController::coneSoftness() const
{
	return std::clamp(focusSoftness, 0, 840);
}

double ZoominatorController::focusIdleScale() const
{
	return (double)coneRadius(focusIdleRadius) / (double)coneRadius(focusRadius);
}

void ZoominatorController::rebuildFocusImage(int canvasW, int canvasH)
{
	const QString path = focusImagePath();
	if (path.isEmpty())
		return;
	ensure_parent_dir_exists(path);

	/* +2 px (8 canvas px after scaling) so rounding or bilinear filtering at the
	 * border can never expose an undimmed sliver when the cursor sits on an edge. */
	/* Extra room on every side lets the spotlight sit entirely off the canvas
	 * (cursor outside the video, e.g. after the view was frozen) while the dim
	 * still covers the whole canvas. */
	const double pad = focusImagePad();
	/* A smaller not-zoomed radius shrinks the whole image, so make it larger to
	 * still cover the canvas after that. */
	const double minScale = std::min(1.0, focusIdleScale());
	const int w = std::max(8, (int)std::ceil((2.0 * canvasW + 2.0 * pad) / minScale / kFocusImageScale) + 2);
	const int h = std::max(8, (int)std::ceil((2.0 * canvasH + 2.0 * pad) / minScale / kFocusImageScale) + 2);
	const double radius = (double)coneRadius(focusRadius) / kFocusImageScale;
	const double soft = (double)coneSoftness() / kFocusImageScale;
	const double dimAlpha = (double)std::clamp(focusDimPct, 0, 100) / 100.0 * 255.0;
	const double expo = beamExponent(radius, soft);
	const double cx = (double)w * 0.5;
	const double cy = (double)h * 0.5;

	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	for (int y = 0; y < h; ++y) {
		QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
		const double dy = ((double)y + 0.5) - cy;
		for (int x = 0; x < w; ++x) {
			const double dx = ((double)x + 0.5) - cx;
			const double d = std::sqrt(dx * dx + dy * dy);
			/* Light left at this distance; the dimming is what the light does not reach. */
			const double light = std::exp(-0.6931471805599453 * std::pow(d / std::max(radius, 1.0), expo));
			/* Black, so premultiplied and straight alpha are the same value. */
			line[x] = qRgba(0, 0, 0, (int)std::lround((1.0 - light) * dimAlpha));
		}
	}
	img.save(path, "PNG");
}

void ZoominatorController::ensureFocusFilter()
{
	if (!focusRt.source)
		return;

	if (focusRt.filter) {
		obs_source_t *existing = obs_source_get_filter_by_name(focusRt.source, "ZoominatorFocusOpacity");
		if (existing) {
			obs_source_release(existing);
			return;
		}
		obs_source_release(focusRt.filter);
		focusRt.filter = nullptr;
	}

	obs_data_t *fsettings = obs_data_create();
	obs_data_set_double(fsettings, "opacity", 100.0);
	focusRt.filter = obs_source_create_private("color_filter", "ZoominatorFocusOpacity", fsettings);
	obs_data_release(fsettings);

	if (focusRt.filter)
		obs_source_filter_add(focusRt.source, focusRt.filter);
}

void ZoominatorController::applyFocusOpacity(int opacity255)
{
	ensureFocusFilter();
	if (!focusRt.filter)
		return;

	const int clamped = std::clamp(opacity255, 0, 255);
	if (focusRt.currentOpacity == clamped)
		return;

	focusRt.currentOpacity = clamped;
	obs_data_t *fsettings = obs_data_create();
	obs_data_set_double(fsettings, "opacity", clamped / 255.0 * 100.0);
	obs_source_update(focusRt.filter, fsettings);
	obs_data_release(fsettings);
}

void ZoominatorController::ensureFocusSource()
{
	if (focusRt.source)
		return;

	int cw = 0, ch = 0;
	get_canvas_size(cw, ch);
	rebuildFocusImage(cw, ch);

	const QString path = focusImagePath();
	if (path.isEmpty())
		return;

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", path.toUtf8().constData());
	obs_data_set_bool(settings, "unload", false);
	focusRt.source = obs_source_create_private("image_source", kZoominatorFocusSourceName, settings);
	obs_data_release(settings);

	if (focusRt.source) {
		ensureFocusFilter();
		/* Keeps the dim off the background around the screen (the filter stays on the source). */
		if (obs_source_t *mask = zoominator_dimmask::attach(focusRt.source))
			obs_source_release(mask);
		focusRt.appearanceHash = 0; /* force the next updateFocusAppearance to record the real hash */
	}
}

/* Regenerates the image when the canvas size or any focus setting changed. */
void ZoominatorController::updateFocusAppearance()
{
	int cw = 0, ch = 0;
	get_canvas_size(cw, ch);

	uint64_t hash = 1469598103934665603ULL;
	auto mix = [&hash](uint64_t v) {
		hash ^= v + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
	};
	mix((uint64_t)cw);
	mix((uint64_t)ch);
	mix((uint64_t)coneRadius(focusRadius));
	mix((uint64_t)coneRadius(focusIdleRadius));
	mix((uint64_t)coneSoftness());
	mix((uint64_t)std::clamp(focusDimPct, 0, 100));

	if (!focusRt.source) {
		ensureFocusSource();
		if (!focusRt.source)
			return;
		focusRt.appearanceHash = hash;
		return;
	}
	if (focusRt.appearanceHash == hash)
		return;

	rebuildFocusImage(cw, ch);
	const QString path = focusImagePath();
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", path.toUtf8().constData());
	obs_data_set_bool(settings, "unload", false);
	obs_source_update(focusRt.source, settings);
	obs_data_release(settings);
	focusRt.appearanceHash = hash;
	focusRt.currentOpacity = -1;
}

static void normalize_focus_scene_item(obs_sceneitem_t *item)
{
	if (!item)
		return;
	obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	obs_sceneitem_set_rot(item, 0.0f);
	vec2 scale{};
	scale.x = (float)kFocusImageScale;
	scale.y = (float)kFocusImageScale;
	obs_sceneitem_set_scale(item, &scale);
	obs_sceneitem_set_scale_filter(item, OBS_SCALE_BILINEAR);
	obs_sceneitem_crop crop{};
	obs_sceneitem_set_crop(item, &crop);
	obs_sceneitem_set_locked(item, true);
}

obs_sceneitem_t *ZoominatorController::ensureFocusItem(obs_scene_t *scene, bool &created)
{
	created = false;
	if (!scene || !focusRt.source)
		return nullptr;

	obs_sceneitem_t *item = find_scene_item_for_source(scene, focusRt.source);
	if (item)
		return item;

	item = obs_scene_add(scene, focusRt.source);
	if (!item)
		return nullptr;
	created = true;
	normalize_focus_scene_item(item);
	obs_sceneitem_set_visible(item, false);
	return item;
}

/* Puts the dim directly above the topmost zoomed item, so overlays that are
 * deliberately not zoomed (a webcam, a logo) stay above it and are not dimmed.
 * If none of the zoomed items sit directly in this scene, falls back to the top. */
void ZoominatorController::placeFocusBelowOverlays(obs_scene_t *scene, obs_sceneitem_t *item)
{
	if (!scene || !item)
		return;

	obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);

	std::vector<obs_sceneitem_t *> live;
	collect_live_scene_items(scene, live);

	int topIndex = -1;
	for (const auto &state : sceneItems) {
		if (!state.item || state.item == item)
			continue;
		if (std::find(live.begin(), live.end(), state.item) == live.end())
			continue;
		if (obs_sceneitem_get_scene(state.item) != scene)
			continue;
		topIndex = std::max(topIndex, (int)obs_sceneitem_get_order_position(state.item));
	}

	/* The frame item sits directly above the zoomed items. The dim must go above
	 * it, otherwise the frame background is left bright until the next frame
	 * refresh moves it back under the dim (a visible half-second glitch). */
	if (frameSource) {
		if (obs_sceneitem_t *frameItem = find_scene_item_for_source(scene, frameSource)) {
			if (frameItem != item && obs_sceneitem_visible(frameItem))
				topIndex = std::max(topIndex, (int)obs_sceneitem_get_order_position(frameItem));
		}
	}

	if (topIndex < 0)
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
	else
		obs_sceneitem_set_order_position(item, topIndex);
}

void ZoominatorController::publishDimSheet()
{
	std::vector<obs_sceneitem_t *> items;
	enumerateTargetItemsInCurrentScene(items);
	vec2 mn{}, mx{};
	if (computeTargetBounds(items, mn, mx))
		zoominator_dimmask::setSheet(mn.x, mn.y, mx.x, mx.y);
	else
		zoominator_dimmask::clearSheet();
}

void ZoominatorController::updateFocus(obs_scene_t *scene, double x, double y, int opacity255, bool idle,
				       int progress255)
{
	if (!focusEnabled || !scene)
		return;

	if (std::clamp(opacity255, 0, 255) <= 0) {
		hideFocusInScene(scene);
		return;
	}

	updateFocusAppearance();
	if (!focusRt.source)
		return;

	bool created = false;
	obs_sceneitem_t *item = ensureFocusItem(scene, created);
	if (!item)
		return;

	applyFocusOpacity(opacity255);

	if (!scene_item_pointer_is_live(scene, item))
		return;

	/* Keep the image covering the whole canvas: the centre may leave the canvas
	 * by at most the padding, which is enough to push the spotlight fully off it. */
	int cw = 0, ch = 0;
	get_canvas_size(cw, ch);
	const double pad = focusImagePad();
	vec2 pos{};
	pos.x = (float)clampd(x, -pad, (double)cw + pad);
	pos.y = (float)clampd(y, -pad, (double)ch + pad);
	obs_sceneitem_set_pos(item, &pos);

	/* Not zoomed: the not-zoomed radius. While zooming the radius eases to the
	 * zoomed one (only when the spotlight is also shown when not zoomed). */
	{
		const double idleS = focusIdleScale();
		double k = 1.0;
		if (idle)
			k = idleS;
		else if (showFocusWhenNotZoomed)
			k = idleS + (1.0 - idleS) * (double)std::clamp(progress255 >= 0 ? progress255 : opacity255, 0, 255) / 255.0;
		vec2 sc{};
		sc.x = sc.y = (float)(kFocusImageScale * k);
		obs_sceneitem_set_scale(item, &sc);
		zoominator_dimmask::setItem(pos.x, pos.y, sc.x);
		zoominator_dimmask::setRadius(frameEnabled ? (float)std::clamp(frameRadiusPx, 0, 400) : 0.0f);
		if (!zoomActive.load(std::memory_order_acquire))
			publishDimSheet();
	}

	if (!obs_sceneitem_visible(item)) {
		normalize_focus_scene_item(item);
		vec2 sc2{};
		sc2.x = sc2.y = (float)(kFocusImageScale * (idle ? focusIdleScale() : 1.0));
		obs_sceneitem_set_scale(item, &sc2);
		placeFocusBelowOverlays(scene, item);
		obs_sceneitem_set_visible(item, true);
	}
	focusRt.shown = true;
}

/* Focus while the view is not zoomed: the spotlight follows the cursor at the
 * unzoomed position (dim image and/or blur filter). Main thread only. */
void ZoominatorController::detachIdleBlurs()
{
	for (auto &b : idleBlurs) {
		if (b.filter && b.target)
			zoominator_blur::detach(b.target, b.filter);
		if (b.target)
			obs_source_release(b.target);
	}
	idleBlurs.clear();
}

/* Blur around the cursor while not zoomed. The view is unzoomed here, so the
 * item's current transform is its own "original" one. */
void ZoominatorController::updateIdleBlur(bool on, bool cursorOk, float sx, float sy)
{
	if (!on || !wantsBlur()) {
		detachIdleBlurs();
		return;
	}

	std::vector<obs_sceneitem_t *> items;
	enumerateTargetItemsInCurrentScene(items);

	bool same = items.size() == idleBlurs.size();
	for (size_t i = 0; same && i < items.size(); ++i)
		same = idleBlurs[i].item == items[i];
	if (!same) {
		detachIdleBlurs();
		int attached = 0;
		for (obs_sceneitem_t *item : items) {
			obs_source_t *src = item ? obs_sceneitem_get_source(item) : nullptr;
			IdleBlur b;
			b.item = item;
			if (src) {
				if (obs_source_t *filter = zoominator_blur::attach(src)) {
					b.target = obs_source_get_ref(src);
					b.filter = filter;
					++attached;
				}
			}
			idleBlurs.push_back(b);
		}
		if (!items.empty() && attached == 0) {
			blurUnavailable = true;
			blog(LOG_WARNING, "[Zoominator] Focus blur unavailable (shader not loaded); falling back to dim.");
			detachIdleBlurs();
			return;
		}
	}

	for (auto &b : idleBlurs) {
		if (!b.filter || !b.item)
			continue;
		if (!cursorOk) {
			zoominator_blur::setParams(b.filter, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f);
			continue;
		}
		const OrigState o = computeOrigState(b.item);
		const float osx = std::max(o.effectiveScale.x, 0.0001f);
		const float osy = std::max(o.effectiveScale.y, 0.0001f);
		const float cxSrc = (sx - o.effectivePos.x) / osx + (float)o.crop.left;
		const float cySrc = (sy - o.effectivePos.y) / osy + (float)o.crop.top;
		const float shown = std::max(0.0001f, (osx + osy) * 0.5f);
		const float blurSrc = std::min(12.0f, (float)focusBlurPx / shown);
		const double idleS = focusIdleScale();
		zoominator_blur::setParams(b.filter, cxSrc, cySrc, (float)coneRadius(focusIdleRadius) / shown,
					   (float)(coneSoftness() * idleS) / shown, blurSrc, 1.0f);
	}
}

void ZoominatorController::updateIdleFocus(bool on)
{
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (scene) {
		bool shown = false;
		bool cursorOk = false;
		float sx = 0.0f, sy = 0.0f;
		if (on) {
			int cx = 0, cy = 0;
			bool inside = false;
			cursorOk = getCursorPos(cx, cy) && mapCursorToScenePixels(cx, cy, sx, sy, inside) && inside;
			if (cursorOk && wantsDim()) {
				double dx = (double)sx, dy = (double)sy;
				applyTiltToIdlePoint(dx, dy);
				updateFocus(scene, dx, dy, 255, true);
				shown = focusRt.shown;
			}
		}
		if (!shown && focusRt.shown) {
			hideFocusInScene(scene);
			focusRt.shown = false;
		}
		updateIdleBlur(on, cursorOk, sx, sy);
	}
	if (sceneSource)
		obs_source_release(sceneSource);
}

void ZoominatorController::refreshIdleFocusTicking()
{
	if (wantsIdleFocus() || idleFollowX || tilt3dEnabled || wantsCursorRingIdle())
		ensureTicking(true);
}

void ZoominatorController::hideFocusInScene(obs_scene_t *scene)
{
	if (!scene || !focusRt.source)
		return;
	if (obs_sceneitem_t *item = find_scene_item_for_source(scene, focusRt.source)) {
		if (obs_sceneitem_visible(item))
			obs_sceneitem_set_visible(item, false);
	}
}

void ZoominatorController::hideFocusInAllScenes()
{
	focusRt.shown = false;
	if (!focusRt.source)
		return;

	obs_frontend_source_list scenes{};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *sceneSource = scenes.sources.array[i];
		if (obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr)
			hideFocusInScene(scene);
	}
	obs_frontend_source_list_free(&scenes);
}


void ZoominatorController::attachBlurFilters()
{
	blurUnavailable = false;
	if (!wantsBlur())
		return;
	int attached = 0;
	for (auto &state : sceneItems) {
		if (!state.item || !state.orig.valid || state.blurFilter)
			continue;
		obs_source_t *src = obs_sceneitem_get_source(state.item);
		if (!src)
			continue;
		obs_source_t *filter = zoominator_blur::attach(src);
		if (!filter)
			continue;
		state.blurTarget = obs_source_get_ref(src);
		state.blurFilter = filter;
		++attached;
		if (showFocusWhenNotZoomed) {
			/* Start from the not-zoomed look so the blur does not drop out until the
			 * first zoom frame arrives. */
			int cx = 0, cy = 0;
			float sx = 0.0f, sy = 0.0f;
			bool inside = false;
			if (getCursorPos(cx, cy) && mapCursorToScenePixels(cx, cy, sx, sy, inside) && inside) {
				const float osx = std::max(state.orig.effectiveScale.x, 0.0001f);
				const float osy = std::max(state.orig.effectiveScale.y, 0.0001f);
				const float cxSrc = (sx - state.orig.effectivePos.x) / osx + (float)state.orig.crop.left;
				const float cySrc = (sy - state.orig.effectivePos.y) / osy + (float)state.orig.crop.top;
				const float shown = std::max(0.0001f, (osx + osy) * 0.5f);
				zoominator_blur::setParams(filter, cxSrc, cySrc, (float)coneRadius(focusIdleRadius) / shown,
							   (float)(coneSoftness() * focusIdleScale()) / shown,
							   std::min(12.0f, (float)focusBlurPx / shown), 1.0f);
			}
		}
	}
	if (attached == 0) {
		blurUnavailable = true;
		blog(LOG_WARNING, "[Zoominator] Focus blur unavailable (shader not loaded); falling back to dim.");
	}
}

void ZoominatorController::detachBlurFilters()
{
	for (auto &state : sceneItems) {
		if (!state.blurFilter)
			continue;
		zoominator_blur::detach(state.blurTarget, state.blurFilter);
		state.blurFilter = nullptr;
		if (state.blurTarget) {
			obs_source_release(state.blurTarget);
			state.blurTarget = nullptr;
		}
	}
}
