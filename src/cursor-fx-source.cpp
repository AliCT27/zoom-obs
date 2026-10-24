#include "cursor-fx-source.hpp"

#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace zoominator_cursorfx {

namespace {

constexpr const char *kSourceId = "zoominator_cursorfx";
constexpr const char *kSourceName = "Zoominator Cursor Effects";
constexpr const char *kEffectFile = "cursor_fx.effect";

std::mutex g_mutex;
Style g_style;
bool g_visible = false;
double g_x = 0.0;
double g_y = 0.0;
double g_zoom = 1.0;
bool g_loadFailed = false;

struct Data {
	gs_effect_t *effect = nullptr;
	gs_eparam_t *pCanvas = nullptr;
	gs_eparam_t *pCenter = nullptr;
	gs_eparam_t *pRadius = nullptr;
	gs_eparam_t *pWidth = nullptr;
	gs_eparam_t *pColor = nullptr;
	gs_eparam_t *pAlpha = nullptr;
	gs_eparam_t *pHue = nullptr;
	gs_eparam_t *pFlow = nullptr;
};

const char *getName(void *)
{
	return kSourceName;
}

void loadEffect(Data *d)
{
	char *path = obs_module_file(kEffectFile);
	if (!path) {
		blog(LOG_WARNING, "[Zoominator] Cursor effects: could not locate %s (is the plugin data folder installed?)",
		     kEffectFile);
		return;
	}

	obs_enter_graphics();
	char *error = nullptr;
	d->effect = gs_effect_create_from_file(path, &error);
	if (!d->effect) {
		blog(LOG_WARNING, "[Zoominator] Cursor effects: shader failed to load from %s: %s", path,
		     error ? error : "(no message)");
	} else {
		d->pCanvas = gs_effect_get_param_by_name(d->effect, "canvas_px");
		d->pCenter = gs_effect_get_param_by_name(d->effect, "ring_center");
		d->pRadius = gs_effect_get_param_by_name(d->effect, "ring_radius");
		d->pWidth = gs_effect_get_param_by_name(d->effect, "ring_width");
		d->pColor = gs_effect_get_param_by_name(d->effect, "ring_color");
		d->pAlpha = gs_effect_get_param_by_name(d->effect, "ring_alpha");
		d->pHue = gs_effect_get_param_by_name(d->effect, "hue");
		d->pFlow = gs_effect_get_param_by_name(d->effect, "flow");
		if (!d->pCanvas || !d->pCenter || !d->pRadius || !d->pWidth || !d->pColor || !d->pAlpha || !d->pHue ||
		    !d->pFlow) {
			blog(LOG_WARNING, "[Zoominator] Cursor effects: %s is missing expected parameters", kEffectFile);
			gs_effect_destroy(d->effect);
			d->effect = nullptr;
		}
	}
	obs_leave_graphics();

	bfree(error);
	bfree(path);
}

void *createData(obs_data_t *, obs_source_t *)
{
	auto *d = new Data();
	loadEffect(d);
	return d;
}

void destroyData(void *data)
{
	auto *d = static_cast<Data *>(data);
	if (!d)
		return;
	if (d->effect) {
		obs_enter_graphics();
		gs_effect_destroy(d->effect);
		obs_leave_graphics();
	}
	delete d;
}

uint32_t canvasWidth(void *)
{
	obs_video_info ovi{};
	return obs_get_video_info(&ovi) ? ovi.base_width : 0;
}

uint32_t canvasHeight(void *)
{
	obs_video_info ovi{};
	return obs_get_video_info(&ovi) ? ovi.base_height : 0;
}

void videoRender(void *data, gs_effect_t *)
{
	auto *d = static_cast<Data *>(data);
	if (!d || !d->effect)
		return;

	Style s;
	bool visible;
	double x, y, zoom;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		s = g_style;
		visible = g_visible;
		x = g_x;
		y = g_y;
		zoom = g_zoom;
	}
	if (!visible)
		return;

	const float w = (float)canvasWidth(nullptr);
	const float h = (float)canvasHeight(nullptr);
	if (w < 1.0f || h < 1.0f)
		return;

	const float k = (float)std::clamp(zoom, 1.0, 10.0);
	const float width = std::max(1.0f, s.thicknessPx * k);
	/* sizePx is the outer diameter; the stroke is centred on the radius, so keep it inside that size. */
	const float radius = std::max(1.0f, s.sizePx * k * 0.5f - width * 0.5f);

	vec2 canvas;
	vec2_set(&canvas, w, h);
	gs_effect_set_vec2(d->pCanvas, &canvas);
	vec2 center;
	vec2_set(&center, (float)x, (float)y);
	gs_effect_set_vec2(d->pCenter, &center);
	gs_effect_set_float(d->pRadius, radius);
	gs_effect_set_float(d->pWidth, width);
	vec4 col;
	vec4_set(&col, s.color[0], s.color[1], s.color[2], s.color[3]);
	gs_effect_set_vec4(d->pColor, &col);
	gs_effect_set_float(d->pAlpha, s.color[3]);
	{
		/* One steady turn around the colour wheel every rainbowSec seconds, from the system clock. */
		const double sec = std::max(1.0, (double)s.rainbowSec);
		const double turn = std::fmod((double)os_gettime_ns() / 1e9 / sec, 1.0);
		gs_effect_set_float(d->pHue, (float)(turn * 6.283185307179586));
		gs_effect_set_float(d->pFlow, s.rainbow ? 1.0f : 0.0f);
	}

	while (gs_effect_loop(d->effect, "Draw"))
		gs_draw_sprite(nullptr, 0, (uint32_t)w, (uint32_t)h);
}

} // namespace

void registerSource()
{
	obs_source_info info = {};
	info.id = kSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = getName;
	info.create = createData;
	info.destroy = destroyData;
	info.get_width = canvasWidth;
	info.get_height = canvasHeight;
	info.video_render = videoRender;
	obs_register_source(&info);
}

obs_source_t *create()
{
	if (g_loadFailed)
		return nullptr;

	obs_source_t *source = obs_source_create_private(kSourceId, kSourceName, nullptr);
	if (!source) {
		g_loadFailed = true;
		blog(LOG_WARNING, "[Zoominator] Cursor effects: could not create the source");
		return nullptr;
	}
	auto *d = static_cast<Data *>(obs_obj_get_data(source));
	if (!d || !d->effect) {
		/* loadEffect already logged why. Do not retry on every refresh. */
		g_loadFailed = true;
		obs_source_release(source);
		return nullptr;
	}
	return source;
}

void setStyle(const Style &style)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_style = style;
}

void setCursor(bool visible, double x, double y, double zoom)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_visible = visible;
	g_x = x;
	g_y = y;
	g_zoom = zoom;
}

} // namespace zoominator_cursorfx
