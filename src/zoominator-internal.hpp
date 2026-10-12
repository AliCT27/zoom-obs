#pragma once

/* Shared includes and small helpers for the Zoominator controller sources.
 * ZoominatorController is implemented across several .cpp files, each focused on
 * one job (see the comment at the top of each). */

#include "zoominator-controller.hpp"
#include "focus-blur-filter.hpp"
#include "frame-source.hpp"
#include "caret-locator.hpp"


#include <obs-frontend-api.h>
#include <obs.h>
#include <util/platform.h>

#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QRect>
#include <QKeySequence>
#include <QKeyCombination>
#include <QRegularExpression>
#include <QDateTime>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPen>

#include <cmath>
#include <algorithm>

#include <QFileInfo>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#endif

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#endif

#include "zoominator-dialog.hpp"

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysymdef.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>

#undef Bool
#undef None
#undef Status
#undef CursorShape
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#undef Above
#undef Below
#undef Unsorted

static constexpr long X11_None = 0L;
#endif

static constexpr const char *kZoominatorMarkerSourceName = "Zoominator Cursor Marker";
static constexpr const char *kZoominatorMarkerSourceId = "zoominator_marker_source";
static constexpr const char *kZoominatorFocusSourceName = "Zoominator Focus Dim";
/* The focus image is generated at 1/k size and drawn k times larger. */
static constexpr double kFocusImageScale = 4.0;

static inline double clampd(double v, double lo, double hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}


inline bool nearly_equal(float a, float b, float eps = 0.5f)
{
	return std::fabs(a - b) <= eps;
}

inline bool nearly_equal_vec2(const vec2 &a, const vec2 &b, float eps = 0.01f)
{
	return nearly_equal(a.x, b.x, eps) && nearly_equal(a.y, b.y, eps);
}

/* Scene-item rotation, in the same sense libobs applies it (about +Z, degrees):
 *   x' = x*cos - y*sin
 *   y' = x*sin + y*cos
 */
inline void rotation_sin_cos(float degrees, float &sinOut, float &cosOut)
{
	if (degrees == 0.0f) {
		sinOut = 0.0f;
		cosOut = 1.0f;
		return;
	}
	const double rad = (double)degrees * 3.14159265358979323846 / 180.0;
	sinOut = (float)std::sin(rad);
	cosOut = (float)std::cos(rad);
}

/* Write-suppression threshold for scale. Position is in pixels, where the 0.01
 * default is a sane "nothing changed" epsilon, but scale is a ratio: 0.01 there
 * is 1% of the source, i.e. tens of pixels of rendered size. Sharing the pixel
 * epsilon made a slow zoom accumulate scale silently for several frames and
 * then apply it in one step, while position kept updating every frame. Small
 * enough here that even an 8K source stays under a hundredth of a pixel. */
static constexpr float kScaleEpsilon = 1.0e-5f;
static constexpr double kWheelZoomSmoothingRate = 14.0;

inline void logi(bool enabled, const char *fmt, ...)
{
	if (!enabled)
		return;
	va_list args;
	va_start(args, fmt);
	blogva(LOG_INFO, fmt, args);
	va_end(args);
}

inline bool source_name_starts_with(const char *name, const char *prefix)
{
	if (!name || !prefix || !*prefix)
		return false;
	return QString::fromUtf8(name).startsWith(QString::fromUtf8(prefix));
}

inline void collect_live_scene_items(obs_scene_t *scene, std::vector<obs_sceneitem_t *> &items)
{
	if (!scene)
		return;

	struct Ctx {
		std::vector<obs_sceneitem_t *> *items = nullptr;

		static bool enum_cb(obs_scene_t *, obs_sceneitem_t *item, void *param)
		{
			auto *ctx = static_cast<Ctx *>(param);
			if (!ctx || !ctx->items || !item)
				return true;

			ctx->items->push_back(item);

			obs_source_t *src = obs_sceneitem_get_source(item);
			obs_scene_t *subScene = src ? obs_scene_from_source(src) : nullptr;
			if (subScene)
				collect_live_scene_items(subScene, *ctx->items);

			return true;
		}
	};

	Ctx ctx{&items};
	obs_scene_enum_items(scene, Ctx::enum_cb, &ctx);
}

inline bool scene_item_pointer_is_live(obs_scene_t *scene, obs_sceneitem_t *item)
{
	if (!scene || !item)
		return false;

	std::vector<obs_sceneitem_t *> liveItems;
	collect_live_scene_items(scene, liveItems);
	return std::find(liveItems.begin(), liveItems.end(), item) != liveItems.end();
}


inline double smoothstep(double t)
{
	return t * t * (3.0 - 2.0 * t);
}


inline void ensure_parent_dir_exists(const QString &filePath)
{
	if (filePath.isEmpty())
		return;
	QFileInfo fi(filePath);
	const QString dir = fi.absolutePath();
	if (dir.isEmpty())
		return;
	QByteArray dirUtf8 = dir.toUtf8();
	os_mkdirs(dirUtf8.constData());
}


inline void get_canvas_size(int &w, int &h)
{
	obs_video_info ovi{};
	if (obs_get_video_info(&ovi)) {
		w = (int)ovi.base_width;
		h = (int)ovi.base_height;
	} else {
		w = 1920;
		h = 1080;
	}
}

inline obs_sceneitem_t *find_scene_item_for_source(obs_scene_t *scene, obs_source_t *source)
{
	struct Finder {
		obs_source_t *want = nullptr;
		obs_sceneitem_t *found = nullptr;

		static bool enum_cb(obs_scene_t *, obs_sceneitem_t *item, void *param)
		{
			auto *f = static_cast<Finder *>(param);
			if (!f || f->found)
				return false;
			if (obs_sceneitem_get_source(item) == f->want) {
				f->found = item;
				return false;
			}
			return true;
		}
	};

	if (!scene || !source)
		return nullptr;
	Finder finder{source, nullptr};
	obs_scene_enum_items(scene, Finder::enum_cb, &finder);
	return finder.found;
}

int qtKeyToVk(int qtKey); /* zoominator-hotkeys.cpp */

/* Defined in zoominator-overlays.cpp. */
void cleanup_legacy_marker_items_all_scenes(obs_source_t *currentMarkerSource = nullptr);
