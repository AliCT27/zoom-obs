#include "guide-source.hpp"

#include <graphics/graphics.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <mutex>

namespace zoominator_guide {

namespace {

constexpr const char *kSourceId = "zoominator_guide";
constexpr const char *kSourceName = "Zoominator Safe Zone";

std::mutex g_mutex;
Style g_style;

const char *getName(void *)
{
	return kSourceName;
}

void *createData(obs_data_t *, obs_source_t *)
{
	/* Nothing to keep: everything is drawn with OBS's own solid colour effect. */
	return bzalloc(1);
}

void destroyData(void *data)
{
	bfree(data);
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

void videoRender(void *, gs_effect_t *)
{
	const float w = (float)canvasWidth(nullptr);
	const float h = (float)canvasHeight(nullptr);
	if (w < 1.0f || h < 1.0f)
		return;

	Style s;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		s = g_style;
	}
	/* The margins are given for a 1080 x 1920 video; other canvas sizes scale them. */
	const float k = h / 1920.0f;
	const float top = std::clamp((float)s.topPx * k, 0.0f, h * 0.45f);
	const float bottom = std::clamp((float)s.bottomPx * k, 0.0f, h * 0.45f);
	const float left = std::clamp((float)s.leftPx * (w / 1080.0f), 0.0f, w * 0.45f);
	const float right = std::clamp((float)s.rightPx * (w / 1080.0f), 0.0f, w * 0.45f);

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	if (!solid)
		return;
	gs_eparam_t *colorParam = gs_effect_get_param_by_name(solid, "color");
	if (!colorParam)
		return;

	auto rect = [&](float x, float y, float rw, float rh, float r, float g, float b, float a) {
		if (rw < 1.0f || rh < 1.0f)
			return;
		vec4 c;
		vec4_set(&c, r, g, b, a);
		gs_effect_set_vec4(colorParam, &c);
		gs_matrix_push();
		gs_matrix_translate3f(x, y, 0.0f);
		gs_draw_sprite(nullptr, 0, (uint32_t)rw, (uint32_t)rh);
		gs_matrix_pop();
	};

	while (gs_effect_loop(solid, "Solid")) {
		/* Tinted areas the interface covers. */
		rect(0.0f, 0.0f, w, top, 0.9f, 0.1f, 0.15f, 0.35f);
		rect(0.0f, h - bottom, w, bottom, 0.9f, 0.1f, 0.15f, 0.35f);
		rect(0.0f, top, left, h - top - bottom, 0.9f, 0.1f, 0.15f, 0.35f);
		rect(w - right, top, right, h - top - bottom, 0.9f, 0.1f, 0.15f, 0.35f);
		/* The safe area's outline. */
		const float t = 3.0f;
		const float sx = left, sy = top, sw = w - left - right, sh = h - top - bottom;
		rect(sx, sy, sw, t, 0.3f, 1.0f, 0.5f, 0.95f);
		rect(sx, sy + sh - t, sw, t, 0.3f, 1.0f, 0.5f, 0.95f);
		rect(sx, sy, t, sh, 0.3f, 1.0f, 0.5f, 0.95f);
		rect(sx + sw - t, sy, t, sh, 0.3f, 1.0f, 0.5f, 0.95f);
	}
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
	return obs_source_create_private(kSourceId, kSourceName, nullptr);
}

void setStyle(const Style &style)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_style = style;
}

} // namespace zoominator_guide
