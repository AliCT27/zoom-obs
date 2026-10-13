/* Overlays: click halo, focus dim and focus blur filters in the scene. */

#include "zoominator-internal.hpp"

static void remove_legacy_marker_items(obs_scene_t *scene, obs_source_t *currentMarkerSource)
{
	if (!scene)
		return;

	struct Ctx {
		obs_source_t *current = nullptr;
		std::vector<obs_sceneitem_t *> items;

		static bool enum_cb(obs_scene_t *, obs_sceneitem_t *item, void *param)
		{
			auto *ctx = static_cast<Ctx *>(param);
			if (!ctx || !item)
				return true;

			obs_source_t *src = obs_sceneitem_get_source(item);
			if (!src || src == ctx->current)
				return true;

			const char *name = obs_source_get_name(src);
			const char *id = obs_source_get_id(src);
			const bool markerName = source_name_starts_with(name, kZoominatorMarkerSourceName);
			const bool oldImageSource = id && QString::fromUtf8(id) == QStringLiteral("image_source");
			const bool oldProceduralMarker =
				id && QString::fromUtf8(id) == QString::fromUtf8(kZoominatorMarkerSourceId) &&
				src != ctx->current;
			if (markerName && (oldImageSource || oldProceduralMarker))
				ctx->items.push_back(item);

			return true;
		}
	};

	Ctx ctx{currentMarkerSource, {}};
	obs_scene_enum_items(scene, Ctx::enum_cb, &ctx);
	for (obs_sceneitem_t *item : ctx.items)
		obs_sceneitem_remove(item);
}

void cleanup_legacy_marker_items_all_scenes(obs_source_t *currentMarkerSource)
{
	obs_frontend_source_list scenes{};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *sceneSource = scenes.sources.array[i];
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		if (scene)
			remove_legacy_marker_items(scene, currentMarkerSource);
	}
	obs_frontend_source_list_free(&scenes);
}

static void normalize_marker_scene_item(obs_sceneitem_t *item)
{
	if (!item)
		return;

	// The marker is a small source-local shader quad. Keep the scene item at
	// native size; otherwise OBS may reuse an old stretched/bounded transform and
	// the ring appears across the whole canvas instead of around the cursor.
	obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_rot(item, 0.0f);

	vec2 scale{};
	scale.x = 1.0f;
	scale.y = 1.0f;
	obs_sceneitem_set_scale(item, &scale);

	obs_sceneitem_crop crop{};
	obs_sceneitem_set_crop(item, &crop);

	obs_sceneitem_set_locked(item, true);
	obs_sceneitem_set_visible(item, true);
	obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
}

void ZoominatorController::rebuildMarkerImage()
{
	/* The image is drawn for the larger of the zoomed and not-zoomed sizes and the
	 * scene item is scaled down to the one in use, so neither looks soft. */
	const int size = std::clamp(std::max(markerSize, markerIdleSize), 6, 512);
	const double f = (double)size / (double)std::clamp(markerSize, 6, 512);
	const QString path = markerImagePath();
	if (path.isEmpty())
		return;

	ensure_parent_dir_exists(path);

	QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);

	QPainter painter(&img);
	painter.setRenderHint(QPainter::Antialiasing, true);

	QColor stroke = QColor::fromRgba(markerColor);
	if (!stroke.isValid())
		stroke = QColor(255, 0, 0, 255);
	if (stroke.alpha() == 0)
		stroke.setAlpha(255);
	stroke.setAlpha(255);

	const qreal thickness = std::clamp<qreal>((qreal)markerThickness * f, 1.0, std::max<qreal>(1.0, size / 2.0 - 2.0));
	const qreal margin = thickness * 0.5 + 1.5;
	const QRectF ringRect(margin, margin, size - margin * 2.0, size - margin * 2.0);

	QPen ringPen(stroke);
	ringPen.setWidthF(thickness);
	ringPen.setCosmetic(true);
	painter.setPen(ringPen);
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(ringRect);
	painter.end();

	img.save(path, "PNG");
}

void ZoominatorController::ensureMarkerFilter()
{
	if (!markerSource)
		return;

	if (markerFilter) {
		obs_source_t *existing = obs_source_get_filter_by_name(markerSource, "ZoominatorOpacity");
		if (existing) {
			obs_source_release(existing);
			return;
		}
		obs_source_release(markerFilter);
		markerFilter = nullptr;
	}

	obs_data_t *fsettings = obs_data_create();
	obs_data_set_double(fsettings, "opacity", 100.0);
	markerFilter = obs_source_create_private("color_filter", "ZoominatorOpacity", fsettings);
	obs_data_release(fsettings);

	if (markerFilter)
		obs_source_filter_add(markerSource, markerFilter);
}

void ZoominatorController::applyMarkerOpacity(int opacity255)
{
	ensureMarkerFilter();
	if (!markerFilter)
		return;

	const int clamped = std::clamp(opacity255, 0, 255);
	if (markerCurrentOpacity == clamped)
		return;

	markerCurrentOpacity = clamped;
	const double opacityPct = clamped / 255.0 * 100.0;
	obs_data_t *fsettings = obs_data_create();
	obs_data_set_double(fsettings, "opacity", opacityPct);
	obs_source_update(markerFilter, fsettings);
	obs_data_release(fsettings);
}

void ZoominatorController::ensureMarkerSource()
{
	if (markerSource)
		return;

	rebuildMarkerImage();

	const QString path = markerImagePath();
	if (path.isEmpty())
		return;

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", path.toUtf8().constData());
	obs_data_set_bool(settings, "unload", false);
	markerSource = obs_source_create_private("image_source", kZoominatorMarkerSourceName, settings);
	obs_data_release(settings);

	if (markerSource)
		ensureMarkerFilter();
}

obs_sceneitem_t *ZoominatorController::ensureMarkerItem(obs_scene_t *scene)
{
	if (!scene)
		return nullptr;

	ensureMarkerSource();
	if (!markerSource)
		return nullptr;

	remove_legacy_marker_items(scene, markerSource);

	struct Finder {
		obs_source_t *want = nullptr;
		obs_sceneitem_t *found = nullptr;

		static bool enum_cb(obs_scene_t *, obs_sceneitem_t *item, void *param)
		{
			auto *f = static_cast<Finder *>(param);
			if (!f || f->found)
				return false;
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (src == f->want) {
				f->found = item;
				return false;
			}
			return true;
		}
	};

	Finder finder{markerSource, nullptr};
	obs_scene_enum_items(scene, Finder::enum_cb, &finder);
	if (finder.found) {
		normalize_marker_scene_item(finder.found);
		return finder.found;
	}

	obs_sceneitem_t *item = obs_scene_add(scene, markerSource);
	if (!item)
		return nullptr;

	normalize_marker_scene_item(item);
	return item;
}

void ZoominatorController::hideMarkerInScene(obs_scene_t *scene)
{
	if (!scene || !markerSource)
		return;

	remove_legacy_marker_items(scene, markerSource);

	struct Finder {
		obs_source_t *want = nullptr;
		obs_sceneitem_t *found = nullptr;

		static bool enum_cb(obs_scene_t *, obs_sceneitem_t *item, void *param)
		{
			auto *f = static_cast<Finder *>(param);
			if (!f || f->found)
				return false;
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (src == f->want) {
				f->found = item;
				return false;
			}
			return true;
		}
	};

	Finder finder{markerSource, nullptr};
	obs_scene_enum_items(scene, Finder::enum_cb, &finder);
	if (finder.found)
		obs_sceneitem_set_visible(finder.found, false);
}

void ZoominatorController::updateMarkerAppearance()
{
	const uint32_t appearanceHash = ((uint32_t)std::clamp(markerSize, 6, 512) * 73856093u) ^
					((uint32_t)std::clamp(markerThickness, 1, 64) * 19349663u) ^
					((uint32_t)std::clamp(markerIdleSize, 6, 256) * 83492791u) ^ (markerColor * 2654435761u);

	if (markerSource && markerAppearanceHash == appearanceHash)
		return;

	rebuildMarkerImage();
	ensureMarkerSource();
	if (!markerSource)
		return;

	const QString path = markerImagePath();
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", path.toUtf8().constData());
	obs_data_set_bool(settings, "unload", false);
	obs_source_update(markerSource, settings);
	obs_data_release(settings);
	markerAppearanceHash = appearanceHash;
	markerCurrentOpacity = -1;
}

bool ZoominatorController::captureMarkerClickPosition()
{
	if (!showCursorMarker || !markerOnlyOnClick)
		return false;

	int cx = 0, cy = 0;
	float mx = 0.f, my = 0.f;
	bool inside = false;
	const bool mapped = getCursorPos(cx, cy) && mapCursorToScenePixels(cx, cy, mx, my, inside);
	if (!mapped || !inside)
		return false;

	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
	static constexpr qint64 kMarkerFadeInMs = 110;
	static constexpr qint64 kMarkerHoldMs = 420;
	static constexpr qint64 kMarkerFadeOutMs = 220;
	{
		std::lock_guard<std::mutex> lock(inputMutex);
		markerClickX = mx;
		markerClickY = my;
		markerClickHasPos = true;
		markerClickFlashStartMs = nowMs;
		markerClickFlashHoldUntilMs = nowMs + kMarkerFadeInMs + kMarkerHoldMs;
		markerClickFlashFadeOutEndMs = markerClickFlashHoldUntilMs + kMarkerFadeOutMs;
	}
	ensureTicking(true);
	return true;
}

bool ZoominatorController::isMarkerFlashActive(qint64 nowMs) const
{
	return showCursorMarker && markerOnlyOnClick && markerClickHasPos && markerClickFlashFadeOutEndMs > 0 &&
	       nowMs < markerClickFlashFadeOutEndMs;
}

int ZoominatorController::currentMarkerOpacity(qint64 nowMs)
{
	if (!showCursorMarker || !markerOnlyOnClick || !markerClickHasPos)
		return 0;

	static constexpr qint64 kMarkerFadeInMs = 110;
	if (markerClickFlashFadeOutEndMs <= 0 || nowMs >= markerClickFlashFadeOutEndMs) {
		std::lock_guard<std::mutex> lock(inputMutex);
		markerClickHasPos = false;
		markerClickFlashStartMs = 0;
		markerClickFlashHoldUntilMs = 0;
		markerClickFlashFadeOutEndMs = 0;
		return 0;
	}

	if (nowMs < markerClickFlashStartMs + kMarkerFadeInMs) {
		const double tIn =
			clampd((double)(nowMs - markerClickFlashStartMs) / (double)std::max<qint64>(1, kMarkerFadeInMs),
			       0.0, 1.0);
		return (int)std::round(smoothstep(tIn) * 255.0);
	}

	if (nowMs < markerClickFlashHoldUntilMs)
		return 255;

	const qint64 fadeOutMs = std::max<qint64>(1, markerClickFlashFadeOutEndMs - markerClickFlashHoldUntilMs);
	const double tOut = clampd((double)(nowMs - markerClickFlashHoldUntilMs) / (double)fadeOutMs, 0.0, 1.0);
	return (int)std::round((1.0 - smoothstep(tOut)) * 255.0);
}

void ZoominatorController::updateMarkerPosition(obs_scene_t *scene, double x, double y, int opacity255, bool idle)
{
	if (!showCursorMarker)
		return;

	const int clampedOpacity = std::clamp(opacity255, 0, 255);
	if (clampedOpacity <= 0) {
		hideMarkerInScene(scene);
		return;
	}

	obs_sceneitem_t *item = ensureMarkerItem(scene);
	if (!item)
		return;

	updateMarkerAppearance();
	applyMarkerOpacity(clampedOpacity);

	if (!scene_item_pointer_is_live(scene, item))
		return;

	normalize_marker_scene_item(item);
	{
		const double imgSize = (double)std::clamp(std::max(markerSize, markerIdleSize), 6, 512);
		const double wanted = (double)std::clamp(idle ? markerIdleSize : markerSize, 6, 512);
		vec2 sc{};
		sc.x = sc.y = (float)(wanted / imgSize);
		obs_sceneitem_set_scale(item, &sc);
	}

	vec2 pos{};
	pos.x = (float)x;
	pos.y = (float)y;
	obs_sceneitem_set_pos(item, &pos);

	if (!obs_sceneitem_visible(item)) {
		obs_sceneitem_set_visible(item, true);
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
	}
}

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
	const double r = (double)std::max(std::clamp(focusRadius, 40, 2000), std::clamp(focusIdleRadius, 40, 2000));
	return r + (double)std::clamp(focusSoftness, 0, 600) * 0.5 + 4.0;
}

/* The spotlight not zoomed is the zoomed image scaled by this factor. */
double ZoominatorController::focusIdleScale() const
{
	return (double)std::clamp(focusIdleRadius, 40, 2000) / (double)std::clamp(focusRadius, 40, 2000);
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
	const double radius = (double)std::clamp(focusRadius, 40, 2000) / kFocusImageScale;
	const double soft = (double)std::clamp(focusSoftness, 0, 600) / kFocusImageScale;
	const double dimAlpha = (double)std::clamp(focusDimPct, 0, 95) / 100.0 * 255.0;
	const double inner = std::max(0.0, radius - soft * 0.5);
	const double outer = radius + soft * 0.5;
	const double cx = (double)w * 0.5;
	const double cy = (double)h * 0.5;

	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	for (int y = 0; y < h; ++y) {
		QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
		const double dy = ((double)y + 0.5) - cy;
		for (int x = 0; x < w; ++x) {
			const double dx = ((double)x + 0.5) - cx;
			const double d = std::sqrt(dx * dx + dy * dy);
			double t = 0.0;
			if (outer > inner)
				t = clampd((d - inner) / (outer - inner), 0.0, 1.0);
			else
				t = d >= radius ? 1.0 : 0.0;
			/* Black, so premultiplied and straight alpha are the same value. */
			line[x] = qRgba(0, 0, 0, (int)std::lround(smoothstep(t) * dimAlpha));
		}
	}
	img.save(path, "PNG");
}

void ZoominatorController::ensureFocusFilter()
{
	if (!focusSource)
		return;

	if (focusFilter) {
		obs_source_t *existing = obs_source_get_filter_by_name(focusSource, "ZoominatorFocusOpacity");
		if (existing) {
			obs_source_release(existing);
			return;
		}
		obs_source_release(focusFilter);
		focusFilter = nullptr;
	}

	obs_data_t *fsettings = obs_data_create();
	obs_data_set_double(fsettings, "opacity", 100.0);
	focusFilter = obs_source_create_private("color_filter", "ZoominatorFocusOpacity", fsettings);
	obs_data_release(fsettings);

	if (focusFilter)
		obs_source_filter_add(focusSource, focusFilter);
}

void ZoominatorController::applyFocusOpacity(int opacity255)
{
	ensureFocusFilter();
	if (!focusFilter)
		return;

	const int clamped = std::clamp(opacity255, 0, 255);
	if (focusCurrentOpacity == clamped)
		return;

	focusCurrentOpacity = clamped;
	obs_data_t *fsettings = obs_data_create();
	obs_data_set_double(fsettings, "opacity", clamped / 255.0 * 100.0);
	obs_source_update(focusFilter, fsettings);
	obs_data_release(fsettings);
}

void ZoominatorController::ensureFocusSource()
{
	if (focusSource)
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
	focusSource = obs_source_create_private("image_source", kZoominatorFocusSourceName, settings);
	obs_data_release(settings);

	if (focusSource) {
		ensureFocusFilter();
		focusAppearanceHash = 0; /* force the next updateFocusAppearance to record the real hash */
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
	mix((uint64_t)std::clamp(focusRadius, 40, 2000));
	mix((uint64_t)std::clamp(focusIdleRadius, 40, 2000));
	mix((uint64_t)std::clamp(focusSoftness, 0, 600));
	mix((uint64_t)std::clamp(focusDimPct, 0, 95));

	if (!focusSource) {
		ensureFocusSource();
		if (!focusSource)
			return;
		focusAppearanceHash = hash;
		return;
	}
	if (focusAppearanceHash == hash)
		return;

	rebuildFocusImage(cw, ch);
	const QString path = focusImagePath();
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", path.toUtf8().constData());
	obs_data_set_bool(settings, "unload", false);
	obs_source_update(focusSource, settings);
	obs_data_release(settings);
	focusAppearanceHash = hash;
	focusCurrentOpacity = -1;
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
	if (!scene || !focusSource)
		return nullptr;

	obs_sceneitem_t *item = find_scene_item_for_source(scene, focusSource);
	if (item)
		return item;

	item = obs_scene_add(scene, focusSource);
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

	if (topIndex < 0)
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
	else
		obs_sceneitem_set_order_position(item, topIndex);
}

void ZoominatorController::updateFocus(obs_scene_t *scene, double x, double y, int opacity255, bool idle)
{
	if (!focusEnabled || !scene)
		return;

	if (std::clamp(opacity255, 0, 255) <= 0) {
		hideFocusInScene(scene);
		return;
	}

	updateFocusAppearance();
	if (!focusSource)
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
			k = idleS + (1.0 - idleS) * (double)std::clamp(opacity255, 0, 255) / 255.0;
		vec2 sc{};
		sc.x = sc.y = (float)(kFocusImageScale * k);
		obs_sceneitem_set_scale(item, &sc);
	}

	if (!obs_sceneitem_visible(item)) {
		normalize_focus_scene_item(item);
		vec2 sc2{};
		sc2.x = sc2.y = (float)(kFocusImageScale * (idle ? focusIdleScale() : 1.0));
		obs_sceneitem_set_scale(item, &sc2);
		placeFocusBelowOverlays(scene, item);
		obs_sceneitem_set_visible(item, true);
	}
	focusShown = true;
}

/* Focus while the view is not zoomed: the spotlight follows the cursor at the
 * unzoomed position. Main thread only. Dim styles only (the blur needs the zoom
 * machinery's source filters). */
void ZoominatorController::updateIdleFocus(bool on)
{
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (scene) {
		bool shown = false;
		if (on) {
			int cx = 0, cy = 0;
			float sx = 0.0f, sy = 0.0f;
			bool inside = false;
			if (getCursorPos(cx, cy) && mapCursorToScenePixels(cx, cy, sx, sy, inside) && inside) {
				updateFocus(scene, (double)sx, (double)sy, 255, true);
				shown = focusShown;
			}
		}
		if (!shown && focusShown) {
			hideFocusInScene(scene);
			focusShown = false;
		}
	}
	if (sceneSource)
		obs_source_release(sceneSource);
}

void ZoominatorController::refreshIdleFocusTicking()
{
	if (wantsIdleFocus())
		ensureTicking(true);
}

void ZoominatorController::hideFocusInScene(obs_scene_t *scene)
{
	if (!scene || !focusSource)
		return;
	if (obs_sceneitem_t *item = find_scene_item_for_source(scene, focusSource)) {
		if (obs_sceneitem_visible(item))
			obs_sceneitem_set_visible(item, false);
	}
}

void ZoominatorController::hideFocusInAllScenes()
{
	focusShown = false;
	if (!focusSource)
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

