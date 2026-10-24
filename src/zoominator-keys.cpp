/* Key display: shows the shortcut the user just pressed ("Ctrl + C") as key caps in the video, then fades it out.
 * The picture is painted here on the main thread; keys-source only draws it. */

#include "zoominator-internal.hpp"

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QLinearGradient>
#include <QPainterPath>

/* Main thread. Paints the key caps for a label such as "Ctrl + Shift + P" and shows them. */
void ZoominatorController::showKeyLabel(const QString &label)
{
	if (shuttingDown || !keyDisplayEnabled || label.isEmpty())
		return;

	obs_video_info ovi{};
	if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0)
		return;
	/* Sizes are given for a 1080-wide canvas. */
	const double k = (double)std::min(ovi.base_width, ovi.base_height) / 1080.0;
	const double textPx = std::clamp(keyDisplaySize, 24, 160) * k;

	QFont font(QStringLiteral("Segoe UI"));
	font.setPixelSize(std::max(8, (int)std::lround(textPx)));
	font.setWeight(QFont::DemiBold);
	const QFontMetrics fm(font);

	const QStringList parts = label.split(QStringLiteral(" + "), Qt::SkipEmptyParts);
	if (parts.isEmpty())
		return;

	const double padX = textPx * 0.5;
	const double padY = textPx * 0.3;
	/* Room for the "+" between two key caps, with some air on both sides of it. */
	const double gap = (double)fm.horizontalAdvance(QStringLiteral("+")) + textPx * 0.6;
	const double radius = textPx * 0.3;
	const double lift = std::max(3.0, textPx * 0.1); /* the visible thickness (front face) of a key cap */
	const double margin = std::max(6.0, textPx * 0.45); /* room for the soft shadow */

	const double capH = fm.height() + 2.0 * padY;
	std::vector<double> capW;
	double total = 0.0;
	for (const QString &p : parts) {
		const double w = std::max(capH, (double)fm.horizontalAdvance(p) + 2.0 * padX);
		capW.push_back(w);
		total += w;
	}
	total += gap * (double)(parts.size() - 1);

	const int imgW = (int)std::ceil(total + 2.0 * margin);
	const int imgH = (int)std::ceil(capH + lift + 2.0 * margin);
	QImage img(imgW, imgH, QImage::Format_ARGB32);
	img.fill(Qt::transparent);
	{
		QPainter p(&img);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setRenderHint(QPainter::TextAntialiasing, true);
		p.setFont(font);
		double x = margin;
		const double line = std::max(1.0, textPx * 0.03);
		for (int i = 0; i < (int)parts.size(); ++i) {
			const QRectF top(x, margin, capW[(size_t)i], capH);
			const QRectF base(x, margin + lift, capW[(size_t)i], capH);

			/* Soft shadow under the key: a few growing, nearly transparent shapes. */
			p.setPen(Qt::NoPen);
			for (int s = 1; s <= 7; ++s) {
				const double grow = textPx * 0.045 * s;
				p.setBrush(QColor(0, 0, 0, 20));
				p.drawRoundedRect(base.adjusted(-grow, -grow * 0.4, grow, grow * 1.3), radius + grow, radius + grow);
			}

			/* The front face (thickness) of the key. */
			QLinearGradient face(0, base.top(), 0, base.bottom());
			face.setColorAt(0.0, QColor(34, 37, 45));
			face.setColorAt(1.0, QColor(10, 11, 15));
			p.setBrush(face);
			p.drawRoundedRect(base, radius, radius);

			/* The top of the key: a light-to-dark gradient with a thin border and a bright rim on its upper edge. */
			QLinearGradient cap(0, top.top(), 0, top.bottom());
			cap.setColorAt(0.0, QColor(74, 80, 96));
			cap.setColorAt(0.55, QColor(48, 52, 63));
			cap.setColorAt(1.0, QColor(36, 39, 48));
			p.setBrush(cap);
			p.setPen(QPen(QColor(14, 15, 20), line * 1.6));
			p.drawRoundedRect(top, radius, radius);
			QLinearGradient rim(0, top.top(), 0, top.bottom());
			rim.setColorAt(0.0, QColor(255, 255, 255, 120));
			rim.setColorAt(0.45, QColor(255, 255, 255, 0));
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(QBrush(rim), line * 1.4));
			const double inset = line * 1.6;
			p.drawRoundedRect(top.adjusted(inset, inset, -inset, -inset), radius - inset, radius - inset);

			/* The label, with a small shadow so it stays readable on a bright background. */
			p.setPen(QColor(0, 0, 0, 130));
			p.drawText(top.translated(0, textPx * 0.04), Qt::AlignCenter, parts[i]);
			p.setPen(QColor(248, 249, 252));
			p.drawText(top, Qt::AlignCenter, parts[i]);

			if (i + 1 < (int)parts.size()) {
				const QRectF plus(x + capW[(size_t)i], margin, gap, capH);
				p.setPen(QColor(0, 0, 0, 130));
				p.drawText(plus.translated(0, textPx * 0.04), Qt::AlignCenter, QStringLiteral("+"));
				p.setPen(QColor(235, 238, 244, 235));
				p.drawText(plus, Qt::AlignCenter, QStringLiteral("+"));
			}
			x += capW[(size_t)i] + gap;
		}
	}

	std::vector<uint8_t> bytes((size_t)imgW * (size_t)imgH * 4u);
	for (int y = 0; y < imgH; ++y)
		memcpy(bytes.data() + (size_t)y * (size_t)imgW * 4u, img.constScanLine(y), (size_t)imgW * 4u);
	zoominator_keys::setPlacement(std::clamp(keyDisplayYPct, 5, 95));
	zoominator_keys::show(bytes, imgW, imgH);
}

/* Main thread. Cheap when nothing changed, so it runs on the 500 ms timer. */
void ZoominatorController::refreshKeyDisplay()
{
	if (shuttingDown)
		return;

	if (!keyDisplayEnabled) {
		zoominator_keys::hide();
		if (keysShown)
			hideKeyDisplayInAllScenes();
		return;
	}
	zoominator_keys::setPlacement(std::clamp(keyDisplayYPct, 5, 95));

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

	if (!keysSource) {
		keysSource = zoominator_keys::create();
		if (!keysSource)
			return;
	}

	obs_sceneitem_t *item = find_scene_item_for_source(scene, keysSource);
	bool fresh = false;
	if (!item) {
		item = obs_scene_add(scene, keysSource);
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
	keysShown = true; /* counts as an overlay from here on, so the others make room */

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

void ZoominatorController::hideKeyDisplayInAllScenes()
{
	keysShown = false;
	zoominator_keys::hide();
	if (!keysSource)
		return;

	obs_frontend_source_list scenes{};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *sceneSource = scenes.sources.array[i];
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		if (!scene)
			continue;
		if (obs_sceneitem_t *item = find_scene_item_for_source(scene, keysSource)) {
			if (obs_sceneitem_visible(item))
				obs_sceneitem_set_visible(item, false);
		}
	}
	obs_frontend_source_list_free(&scenes);
}
