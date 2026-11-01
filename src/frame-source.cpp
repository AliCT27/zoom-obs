#include "frame-source.hpp"

#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <util/bmem.h>

#include <mutex>
#include <cmath>

#include <util/platform.h>
#include <algorithm>
#include "tilt-filter.hpp"

namespace zoominator_frame {

namespace {

constexpr const char *kSourceId = "zoominator_frame";
constexpr const char *kSourceName = "Zoominator Frame";
constexpr const char *kEffectFile = "frame.effect";

/* One frame exists at a time, so its state is shared rather than per instance.
 * A mutex keeps the four rectangle values consistent between the graphics
 * thread (which updates them while zooming) and the main thread. */
std::mutex g_mutex;
Style g_style;
bool g_rectValid = false;
float g_rect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
bool g_loadFailed = false;

struct FrameData {
	gs_effect_t *effect = nullptr;
	gs_eparam_t *pCanvas = nullptr;
	gs_eparam_t *pRect = nullptr;
	gs_eparam_t *pTilt = nullptr; /* optional */
	gs_eparam_t *pTiltPivot = nullptr; /* optional */
	gs_eparam_t *pRadius = nullptr;
	gs_eparam_t *pBorder = nullptr;
	gs_eparam_t *pGlow = nullptr;
	gs_eparam_t *pGlowStrength = nullptr;
	gs_eparam_t *pBorderColor = nullptr;
	gs_eparam_t *pBgColor = nullptr;
	gs_eparam_t *pBgColor2 = nullptr;
	gs_eparam_t *pBgMode = nullptr;
	gs_eparam_t *pBgAngle = nullptr;
	gs_eparam_t *pHueBg = nullptr;
	gs_eparam_t *pHueBorder = nullptr;
	gs_eparam_t *pFlowBg = nullptr;
	gs_eparam_t *pFlowBorder = nullptr;
	gs_eparam_t *pFlowWaves = nullptr;
	gs_eparam_t *pDebug = nullptr;
};

const char *getName(void *)
{
	return kSourceName;
}

void loadEffect(FrameData *d)
{
	char *path = obs_module_file(kEffectFile);
	if (!path) {
		blog(LOG_WARNING, "[Zoominator] Frame: could not locate %s (is the plugin data folder installed?)",
		     kEffectFile);
		return;
	}

	obs_enter_graphics();
	char *error = nullptr;
	d->effect = gs_effect_create_from_file(path, &error);
	if (!d->effect) {
		blog(LOG_WARNING, "[Zoominator] Frame: shader failed to load from %s: %s", path,
		     error ? error : "(no message)");
	} else {
		d->pCanvas = gs_effect_get_param_by_name(d->effect, "canvas_px");
		d->pRect = gs_effect_get_param_by_name(d->effect, "rect_px");
		d->pTilt = gs_effect_get_param_by_name(d->effect, "tilt");
		d->pTiltPivot = gs_effect_get_param_by_name(d->effect, "tilt_pivot");
		d->pRadius = gs_effect_get_param_by_name(d->effect, "radius_px");
		d->pBorder = gs_effect_get_param_by_name(d->effect, "border_px");
		d->pGlow = gs_effect_get_param_by_name(d->effect, "glow_px");
		d->pGlowStrength = gs_effect_get_param_by_name(d->effect, "glow_strength");
		d->pBorderColor = gs_effect_get_param_by_name(d->effect, "border_color");
		d->pBgColor = gs_effect_get_param_by_name(d->effect, "bg_color");
		d->pBgColor2 = gs_effect_get_param_by_name(d->effect, "bg_color2");
		d->pBgMode = gs_effect_get_param_by_name(d->effect, "bg_mode");
		d->pBgAngle = gs_effect_get_param_by_name(d->effect, "bg_angle");
		d->pHueBg = gs_effect_get_param_by_name(d->effect, "hue_bg");
		d->pHueBorder = gs_effect_get_param_by_name(d->effect, "hue_border");
		d->pFlowBg = gs_effect_get_param_by_name(d->effect, "flow_bg");
		d->pFlowBorder = gs_effect_get_param_by_name(d->effect, "flow_border");
		d->pFlowWaves = gs_effect_get_param_by_name(d->effect, "flow_waves");
		d->pDebug = gs_effect_get_param_by_name(d->effect, "debug_geom"); /* optional */
		if (!d->pCanvas || !d->pRect || !d->pRadius || !d->pBorder || !d->pGlow || !d->pGlowStrength ||
		    !d->pBorderColor || !d->pBgColor || !d->pBgColor2 || !d->pBgMode || !d->pBgAngle) {
			blog(LOG_WARNING, "[Zoominator] Frame: %s is missing expected parameters", kEffectFile);
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
	auto *d = new FrameData();
	loadEffect(d);
	return d;
}

void destroyData(void *data)
{
	auto *d = static_cast<FrameData *>(data);
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

void setColor(gs_eparam_t *param, const float c[4])
{
	vec4 v;
	vec4_set(&v, c[0], c[1], c[2], c[3]);
	gs_effect_set_vec4(param, &v);
}

void videoRender(void *data, gs_effect_t *)
{
	auto *d = static_cast<FrameData *>(data);
	if (!d || !d->effect)
		return;

	Style s;
	float r[4];
	bool valid;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		s = g_style;
		valid = g_rectValid;
		for (int i = 0; i < 4; ++i)
			r[i] = g_rect[i];
	}
	if (!valid)
		return;

	obs_video_info ovi{};
	if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0)
		return;
	const float w = (float)ovi.base_width;
	const float h = (float)ovi.base_height;

	vec2 canvas;
	vec2_set(&canvas, w, h);
	gs_effect_set_vec2(d->pCanvas, &canvas);
	vec4 rect;
	vec4_set(&rect, r[0], r[1], r[2], r[3]);
	gs_effect_set_vec4(d->pRect, &rect);
	if (d->pTilt && d->pTiltPivot) {
		/* The screen is turned about the pivot: same maths as the filter (see Pose). */
		zoominator_tilt::Pose pose;
		vec4 t;
		vec2 pv;
		if (zoominator_tilt::getPose(pose)) {
			vec4_set(&t, pose.yaw, pose.pitch, pose.dist, pose.scl);
			vec2_set(&pv, pose.px, pose.py);
		} else {
			vec4_set(&t, 0.0f, 0.0f, 0.0f, 1.0f);
			vec2_set(&pv, 0.0f, 0.0f);
		}
		gs_effect_set_vec4(d->pTilt, &t);
		gs_effect_set_vec2(d->pTiltPivot, &pv);
	}
	gs_effect_set_float(d->pRadius, s.radiusPx);
	gs_effect_set_float(d->pBorder, s.borderPx);
	gs_effect_set_float(d->pGlow, s.glowPx);
	gs_effect_set_float(d->pGlowStrength, s.glowStrength);
	setColor(d->pBorderColor, s.borderColor);
	setColor(d->pBgColor, s.bgColor);
	setColor(d->pBgColor2, s.bgColor2);
	gs_effect_set_float(d->pBgMode, (float)s.bgMode);
	gs_effect_set_float(d->pBgAngle, s.bgAngleDeg * 3.14159265f / 180.0f);
	{
		/* One steady turn around the colour wheel every rainbowSec seconds, from the system clock. */
		const double sec = std::max(1.0, (double)s.rainbowSec);
		const double turn = std::fmod((double)os_gettime_ns() / 1e9 / sec, 1.0);
		const float ang = (float)(turn * 6.283185307179586);
		/* Optional: an older shader without these just has no rainbow, the frame still draws. */
		if (d->pHueBg)
			gs_effect_set_float(d->pHueBg, s.rainbowBg ? ang : 0.0f);
		if (d->pHueBorder)
			gs_effect_set_float(d->pHueBorder, s.rainbowBorder ? ang : 0.0f);
		if (d->pFlowBg)
			gs_effect_set_float(d->pFlowBg, s.rainbowBg ? 1.0f : 0.0f);
		if (d->pFlowBorder)
			gs_effect_set_float(d->pFlowBorder, s.rainbowBorder ? 1.0f : 0.0f);
		if (d->pFlowWaves)
			gs_effect_set_float(d->pFlowWaves, 1.0f);
		if (d->pDebug)
			gs_effect_set_float(d->pDebug, zoominator_tilt::debugGeometry() ? 1.0f : 0.0f);
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
		blog(LOG_WARNING, "[Zoominator] Frame: could not create the frame source");
		return nullptr;
	}
	auto *d = static_cast<FrameData *>(obs_obj_get_data(source));
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

void setRect(float left, float top, float right, float bottom)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_rect[0] = left;
	g_rect[1] = top;
	g_rect[2] = right;
	g_rect[3] = bottom;
	g_rectValid = true;
}

void clearRect()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_rectValid = false;
}

} // namespace zoominator_frame
