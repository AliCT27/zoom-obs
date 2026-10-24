#include "progress-bar-source.hpp"

#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace zoominator_progress {

namespace {

constexpr const char *kSourceId = "zoominator_progress";
constexpr const char *kSourceName = "Zoominator Progress Bar";
constexpr const char *kEffectFile = "progress_bar.effect";

std::mutex g_mutex;
Style g_style;
bool g_active = false;
bool g_running = false;
double g_baseMs = 0.0;
uint64_t g_baseNs = 0;
bool g_loadFailed = false;

struct Data {
	gs_effect_t *effect = nullptr;
	gs_eparam_t *pCanvas = nullptr;
	gs_eparam_t *pRect = nullptr;
	gs_eparam_t *pFill = nullptr;
	gs_eparam_t *pColor = nullptr;
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
		blog(LOG_WARNING, "[Zoominator] Progress bar: could not locate %s (is the plugin data folder installed?)",
		     kEffectFile);
		return;
	}

	obs_enter_graphics();
	char *error = nullptr;
	d->effect = gs_effect_create_from_file(path, &error);
	if (!d->effect) {
		blog(LOG_WARNING, "[Zoominator] Progress bar: shader failed to load from %s: %s", path,
		     error ? error : "(no message)");
	} else {
		d->pCanvas = gs_effect_get_param_by_name(d->effect, "canvas_px");
		d->pRect = gs_effect_get_param_by_name(d->effect, "bar_rect");
		d->pFill = gs_effect_get_param_by_name(d->effect, "fill");
		d->pColor = gs_effect_get_param_by_name(d->effect, "bar_color");
		d->pHue = gs_effect_get_param_by_name(d->effect, "hue");
		d->pFlow = gs_effect_get_param_by_name(d->effect, "flow");
		if (!d->pCanvas || !d->pRect || !d->pFill || !d->pColor || !d->pHue || !d->pFlow) {
			blog(LOG_WARNING, "[Zoominator] Progress bar: %s is missing expected parameters", kEffectFile);
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
	bool active, running;
	double baseMs;
	uint64_t baseNs;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		s = g_style;
		active = g_active;
		running = g_running;
		baseMs = g_baseMs;
		baseNs = g_baseNs;
	}
	if (!active)
		return;

	const float w = (float)canvasWidth(nullptr);
	const float h = (float)canvasHeight(nullptr);
	if (w < 1.0f || h < 1.0f)
		return;

	double elapsedMs = baseMs;
	if (running)
		elapsedMs += (double)(os_gettime_ns() - baseNs) / 1e6;
	const float fill = (float)std::clamp(elapsedMs / (std::max(1.0, (double)s.lengthSec) * 1000.0), 0.0, 1.0);

	/* The thickness is given for a 1080-pixel-wide canvas. */
	const float k = std::min(w, h) / 1080.0f;
	const float thick = std::clamp(s.thicknessPx * k, 1.0f, h * 0.2f);
	const float y = s.position == 1 ? h - thick : 0.0f;

	vec2 canvas;
	vec2_set(&canvas, w, h);
	gs_effect_set_vec2(d->pCanvas, &canvas);
	vec4 rect;
	vec4_set(&rect, 0.0f, y, w, thick);
	gs_effect_set_vec4(d->pRect, &rect);
	gs_effect_set_float(d->pFill, fill);
	vec4 col;
	vec4_set(&col, s.color[0], s.color[1], s.color[2], 1.0f);
	gs_effect_set_vec4(d->pColor, &col);
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
		blog(LOG_WARNING, "[Zoominator] Progress bar: could not create the source");
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

void setTime(bool active, bool running, double baseMs)
{
	const uint64_t now = os_gettime_ns();
	std::lock_guard<std::mutex> lock(g_mutex);
	g_active = active;
	g_running = running;
	g_baseMs = baseMs;
	g_baseNs = now;
}

} // namespace zoominator_progress
