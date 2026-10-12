/* Frame: a canvas-sized overlay that fills everything outside the captured area
 * with a colour, rounds its corners and draws a glowing border around it. It sits
 * above the zoomed sources and below the ones that are not zoomed (a webcam), and
 * stays put while the content zooms inside it. */

#include "zoominator-internal.hpp"

#include <QPainterPath>

static constexpr const char *kZoominatorFrameSourceName = "Zoominator Frame";

QString ZoominatorController::frameImagePath() const
{
	char *path = obs_module_config_path("zoominator-frame.png");
	if (!path)
		return {};
	QString p = QString::fromUtf8(path);
	bfree(path);
	return p;
}

void ZoominatorController::rebuildFrameImage(int canvasW, int canvasH, const QRectF &rect)
{
	const QString path = frameImagePath();
	if (path.isEmpty())
		return;
	ensure_parent_dir_exists(path);

	const int w = std::max(8, canvasW);
	const int h = std::max(8, canvasH);

	const QColor color = QColor::fromRgba(frameColor);
	const QColor bg = QColor::fromRgba(frameBgColor);
	const int border = std::clamp(frameBorderPx, 0, 80);
	const int glow = std::clamp(frameGlowPx, 0, 200);
	const double glowStrength = (double)std::clamp(frameGlowPct, 0, 100) / 100.0;
	const double radius = std::clamp((double)frameRadiusPx, 0.0, std::min(rect.width(), rect.height()) * 0.5);

	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	img.fill(bg);

	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing, true);

	auto roundedRect = [&](double grow) {
		QPainterPath path;
		const QRectF r = rect.adjusted(-grow, -grow, grow, grow);
		path.addRoundedRect(r, radius + grow, radius + grow);
		return path;
	};

	/* Glow: thin rings that fade out away from the border, drawn outwards only. */
	for (int i = 0; i < glow; ++i) {
		const double t = (double)i / (double)glow;
		const double a = glowStrength * (1.0 - t) * (1.0 - t);
		if (a <= 0.004)
			continue;
		QColor c = color;
		c.setAlphaF(std::clamp(a, 0.0, 1.0));
		QPen pen(c);
		pen.setWidthF(1.6);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawPath(roundedRect((double)border + (double)i + 0.8));
	}

	/* Border ring around the window. */
	if (border > 0) {
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawPath(roundedRect((double)border));
	}

	/* The window itself: transparent, so the zoomed content shows through. */
	p.setCompositionMode(QPainter::CompositionMode_Clear);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::transparent);
	p.drawPath(roundedRect(0.0));
	p.end();

	img.save(path, "PNG");
}

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
		if (frameSource)
			hideFrameInAllScenes();
		return;
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

	/* While zooming, the originals' bounds are the reference; the live items are moving. */
	vec2 mn{}, mx{};
	bool haveBounds = false;
	if (zoomActive.load(std::memory_order_acquire)) {
		haveBounds = sceneContentBoundsValid;
		mn = sceneContentMin;
		mx = sceneContentMax;
	} else {
		haveBounds = computeTargetBounds(items, mn, mx);
	}

	if (!haveBounds || mx.x - mn.x < 4.0f || mx.y - mn.y < 4.0f) {
		if (frameSource) {
			if (obs_sceneitem_t *item = find_scene_item_for_source(scene, frameSource)) {
				if (obs_sceneitem_visible(item))
					obs_sceneitem_set_visible(item, false);
			}
		}
		return;
	}

	int cw = 0, ch = 0;
	get_canvas_size(cw, ch);
	const QRectF rect((double)mn.x, (double)mn.y, (double)(mx.x - mn.x), (double)(mx.y - mn.y));

	uint64_t hash = 1469598103934665603ULL;
	auto mix = [&hash](uint64_t v) {
		hash ^= v + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
	};
	mix((uint64_t)cw);
	mix((uint64_t)ch);
	mix((uint64_t)std::lround(rect.x() * 2.0));
	mix((uint64_t)std::lround(rect.y() * 2.0));
	mix((uint64_t)std::lround(rect.width() * 2.0));
	mix((uint64_t)std::lround(rect.height() * 2.0));
	mix(frameColor);
	mix(frameBgColor);
	mix((uint64_t)frameBorderPx);
	mix((uint64_t)frameRadiusPx);
	mix((uint64_t)frameGlowPx);
	mix((uint64_t)frameGlowPct);

	const QString path = frameImagePath();
	if (path.isEmpty())
		return;

	if (!frameSource) {
		rebuildFrameImage(cw, ch, rect);
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "file", path.toUtf8().constData());
		obs_data_set_bool(settings, "unload", false);
		frameSource = obs_source_create_private("image_source", kZoominatorFrameSourceName, settings);
		obs_data_release(settings);
		if (!frameSource)
			return;
		frameHash = hash;
	} else if (frameHash != hash) {
		rebuildFrameImage(cw, ch, rect);
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "file", path.toUtf8().constData());
		obs_data_set_bool(settings, "unload", false);
		obs_source_update(frameSource, settings);
		obs_data_release(settings);
		frameHash = hash;
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
