/* Click halo: image, scene item, fade in/out, size and position. Moved out of zoominator-overlays.cpp
 * unchanged. */

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

/* Drawn larger when the halo grows with the zoom, so it stays sharp. */
int ZoominatorController::markerImageSize() const
{
	const int base = std::max(markerSize, markerIdleSize);
	return std::clamp(base * 3, 6, 512);
}

void ZoominatorController::rebuildMarkerImage()
{
	/* The image is drawn for the larger of the zoomed and not-zoomed sizes and the
	 * scene item is scaled down to the one in use, so neither looks soft. */
	const int size = markerImageSize();
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
	markerZCount = 0; /* the next halo starts with fresh zoom readings */
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
	/* A halo that is still showing hands its opacity to the new one, so back-to-back clicks do not blink. */
	const int carryOpacity = currentMarkerOpacity(nowMs);
	static constexpr qint64 kMarkerFadeInMs = 110;
	static constexpr qint64 kMarkerHoldMs = 420;
	static constexpr qint64 kMarkerFadeOutMs = 220;
	{
		std::lock_guard<std::mutex> lock(inputMutex);
		markerClickX = mx;
		markerClickY = my;
		markerClickHasPos = true;
		markerFadeInFrom = std::clamp(carryOpacity, 0, 255);
		markerFlashStartedZoomed.store(zoomedView() || animT > 0.001, std::memory_order_relaxed);
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
		const double from = (double)markerFadeInFrom;
		return (int)std::round(from + (255.0 - from) * smoothstep(tIn));
	}

	if (nowMs < markerClickFlashHoldUntilMs)
		return 255;

	const qint64 fadeOutMs = std::max<qint64>(1, markerClickFlashFadeOutEndMs - markerClickFlashHoldUntilMs);
	const double tOut = clampd((double)(nowMs - markerClickFlashHoldUntilMs) / (double)fadeOutMs, 0.0, 1.0);
	return (int)std::round((1.0 - smoothstep(tOut)) * 255.0);
}

void ZoominatorController::updateMarkerPosition(obs_scene_t *scene, double x, double y, int opacity255,
						double zoomK)
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

	/* The zoom that is really on screen, read from the captured screen's own transform (the one that scales the
	 * picture and the cursor arrow). The zoom value published by the graphics thread can be stale for a frame
	 * when a new zoom starts, which made the halo flash large for one frame. */
	double zNow = 1.0;
	double arrowScaleX = 1.0, arrowScaleY = 1.0;
	{
		std::vector<obs_sceneitem_t *> targets;
		enumerateTargetItemsInCurrentScene(targets);
		if (!targets.empty() && targets.front()) {
			vec2 isc{};
			obs_sceneitem_get_scale(targets.front(), &isc);
			arrowScaleX = (double)isc.x;
			arrowScaleY = (double)isc.y;
			for (const auto &state : sceneItems) {
				if (state.item == targets.front() && state.orig.valid && state.orig.effectiveScale.x > 0.0001f) {
					zNow = std::max(1.0, (double)isc.x / (double)state.orig.effectiveScale.x);
					break;
				}
			}
		}
	}
	(void)zoomK;
	/* A one-frame glitch in a reading must not show: take the middle of the last three readings. */
	if (markerZCount == 0) {
		markerZHist[0] = markerZHist[1] = markerZHist[2] = zNow;
	} else {
		markerZHist[0] = markerZHist[1];
		markerZHist[1] = markerZHist[2];
		markerZHist[2] = zNow;
	}
	if (markerZCount < 3)
		++markerZCount;
	{
		const double a = markerZHist[0], b = markerZHist[1], c = markerZHist[2];
		zNow = std::max(std::min(a, b), std::min(std::max(a, b), c));
	}
	{
		const double imgSize = (double)markerImageSize();
		/* The not-zoomed size at 1x, the zoomed size times the zoom from 1.5x up, blended in between. */
		const double idleW = (double)std::clamp(markerIdleSize, 6, 512);
		const double zoomW = (double)std::clamp(markerSize, 6, 512) * zNow;
		const double blend = std::clamp((zNow - 1.0) / 0.5, 0.0, 1.0);
		const double wanted = idleW + (zoomW - idleW) * blend;
		vec2 sc{};
		sc.x = sc.y = (float)(wanted / imgSize);
		obs_sceneitem_set_scale(item, &sc);
	}

	/* The cursor arrow is drawn by the capture and grows with the zoom, but its hot spot is the tip: centre the
	 * halo on the arrow's middle instead (offsets in capture pixels at 1x). */
	x += (double)markerOffsetX * arrowScaleX;
	y += (double)markerOffsetY * arrowScaleY;

	vec2 pos{};
	pos.x = (float)x;
	pos.y = (float)y;
	obs_sceneitem_set_pos(item, &pos);

	if (!obs_sceneitem_visible(item)) {
		obs_sceneitem_set_visible(item, true);
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
	}
}
