#include "cam-ring-filter.hpp"

#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <util/bmem.h>

#include <algorithm>
#include <cmath>
#include <util/platform.h>
#include <mutex>

namespace zoominator_camring {

namespace {

constexpr const char *kFilterId = "zoominator_cam_ring";
constexpr const char *kFilterName = "Zoominator Webcam Ring";
constexpr const char *kEffectFile = "cam_ring.effect";

struct RingData {
	obs_source_t *context = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *pSize = nullptr;
	gs_eparam_t *pRadius = nullptr;
	gs_eparam_t *pCorner = nullptr;
	gs_eparam_t *pBorder = nullptr;
	gs_eparam_t *pGlow = nullptr;
	gs_eparam_t *pGlowStrength = nullptr;
	gs_eparam_t *pColor = nullptr;
	gs_eparam_t *pHue = nullptr;
	gs_eparam_t *pFlow = nullptr;
	gs_eparam_t *pShadowOff = nullptr;
	gs_eparam_t *pShadowBlur = nullptr;
	gs_eparam_t *pShadowAlpha = nullptr;
	gs_eparam_t *pShadowColor = nullptr;

	std::mutex mutex;
	Style style;
};

const char *getName(void *)
{
	return kFilterName;
}

void loadEffect(RingData *d)
{
	char *path = obs_module_file(kEffectFile);
	if (!path) {
		blog(LOG_WARNING, "[Zoominator] Webcam ring: could not locate %s (is the plugin data folder installed?)",
		     kEffectFile);
		return;
	}

	obs_enter_graphics();
	char *error = nullptr;
	d->effect = gs_effect_create_from_file(path, &error);
	if (!d->effect) {
		blog(LOG_WARNING, "[Zoominator] Webcam ring: shader failed to load from %s: %s", path,
		     error ? error : "(no message)");
	} else {
		d->pSize = gs_effect_get_param_by_name(d->effect, "size_px");
		d->pRadius = gs_effect_get_param_by_name(d->effect, "radius_px");
		d->pBorder = gs_effect_get_param_by_name(d->effect, "border_px");
		d->pGlow = gs_effect_get_param_by_name(d->effect, "glow_px");
		d->pGlowStrength = gs_effect_get_param_by_name(d->effect, "glow_strength");
		d->pColor = gs_effect_get_param_by_name(d->effect, "ring_color");
		/* Optional: an older shader without these just has a plain circle and ring. */
		d->pCorner = gs_effect_get_param_by_name(d->effect, "corner_px");
		d->pHue = gs_effect_get_param_by_name(d->effect, "hue");
		d->pFlow = gs_effect_get_param_by_name(d->effect, "flow");
		d->pShadowOff = gs_effect_get_param_by_name(d->effect, "shadow_off");
		d->pShadowBlur = gs_effect_get_param_by_name(d->effect, "shadow_blur");
		d->pShadowAlpha = gs_effect_get_param_by_name(d->effect, "shadow_alpha");
		d->pShadowColor = gs_effect_get_param_by_name(d->effect, "shadow_color");
		if (!d->pSize || !d->pRadius || !d->pBorder || !d->pGlow || !d->pGlowStrength || !d->pColor) {
			blog(LOG_WARNING, "[Zoominator] Webcam ring: %s is missing expected parameters", kEffectFile);
			gs_effect_destroy(d->effect);
			d->effect = nullptr;
		}
	}
	obs_leave_graphics();

	bfree(error);
	bfree(path);
}

void *createData(obs_data_t *, obs_source_t *source)
{
	auto *d = new RingData();
	d->context = source;
	loadEffect(d);
	return d;
}

void destroyData(void *data)
{
	auto *d = static_cast<RingData *>(data);
	if (!d)
		return;
	if (d->effect) {
		obs_enter_graphics();
		gs_effect_destroy(d->effect);
		obs_leave_graphics();
	}
	delete d;
}

void videoRender(void *data, gs_effect_t *)
{
	auto *d = static_cast<RingData *>(data);
	if (!d || !d->effect) {
		obs_source_skip_video_filter(d ? d->context : nullptr);
		return;
	}

	obs_source_t *target = obs_filter_get_target(d->context);
	const uint32_t w = target ? obs_source_get_base_width(target) : 0;
	const uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if (w == 0 || h == 0) {
		obs_source_skip_video_filter(d->context);
		return;
	}

	Style s;
	{
		std::lock_guard<std::mutex> lock(d->mutex);
		s = d->style;
	}

	/* The ring, glow and shadow have to fit inside the source, so they take their room
	 * from the circle rather than growing past the edge. The shadow reaches as far as its
	 * blur plus its offset beyond the ring. */
	const bool shadowOn = s.shadow && d->pShadowAlpha && s.shadowAlpha > 0.001f;
	const float shadowReach =
		shadowOn ? s.ringPx + s.shadowBlurPx + std::max(std::fabs(s.shadowDx), std::fabs(s.shadowDy)) : 0.0f;
	const float half = 0.5f * (float)std::min(w, h);
	const float wantPad = std::max(s.ringPx + s.glowPx, shadowReach);
	const float pad = std::min(wantPad, half * 0.6f);
	const float scale = wantPad > 0.01f ? pad / wantPad : 0.0f;
	const float ring = s.ringPx * scale;
	const float glow = s.glowPx * scale;
	const float radius = std::max(4.0f, half - pad);

	if (!obs_source_process_filter_begin(d->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	vec2 size;
	vec2_set(&size, (float)w, (float)h);
	gs_effect_set_vec2(d->pSize, &size);
	gs_effect_set_float(d->pRadius, radius);
	if (d->pCorner)
		gs_effect_set_float(d->pCorner, radius * std::clamp(s.corner, 0.0f, 1.0f));
	gs_effect_set_float(d->pBorder, ring);
	gs_effect_set_float(d->pGlow, glow);
	gs_effect_set_float(d->pGlowStrength, s.glowStrength);
	vec4 color;
	vec4_set(&color, s.ringColor[0], s.ringColor[1], s.ringColor[2], s.ringColor[3]);
	gs_effect_set_vec4(d->pColor, &color);
	{
		/* One steady turn around the colour wheel every rainbowSec seconds, from the system clock. */
		const double sec = std::max(1.0, (double)s.rainbowSec);
		const double turn = std::fmod((double)os_gettime_ns() / 1e9 / sec, 1.0);
		if (d->pHue)
			gs_effect_set_float(d->pHue, s.rainbow ? (float)(turn * 6.283185307179586) : 0.0f);
		if (d->pFlow)
			gs_effect_set_float(d->pFlow, s.rainbow ? 1.0f : 0.0f);
	}

	if (d->pShadowAlpha) {
		vec2 off;
		vec2_set(&off, shadowOn ? s.shadowDx * scale : 0.0f, shadowOn ? s.shadowDy * scale : 0.0f);
		if (d->pShadowOff)
			gs_effect_set_vec2(d->pShadowOff, &off);
		if (d->pShadowBlur)
			gs_effect_set_float(d->pShadowBlur, s.shadowBlurPx * scale);
		gs_effect_set_float(d->pShadowAlpha, shadowOn ? std::clamp(s.shadowAlpha, 0.0f, 1.0f) : 0.0f);
		if (d->pShadowColor) {
			vec4 sc;
			vec4_set(&sc, s.shadowColor[0], s.shadowColor[1], s.shadowColor[2], 1.0f);
			gs_effect_set_vec4(d->pShadowColor, &sc);
		}
	}

	obs_source_process_filter_end(d->context, d->effect, w, h);
}

RingData *dataOf(obs_source_t *filter)
{
	return filter ? static_cast<RingData *>(obs_obj_get_data(filter)) : nullptr;
}

} // namespace

void registerFilter()
{
	obs_source_info info = {};
	info.id = kFilterId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = getName;
	info.create = createData;
	info.destroy = destroyData;
	info.video_render = videoRender;
	obs_register_source(&info);
}

obs_source_t *attach(obs_source_t *target)
{
	if (!target)
		return nullptr;

	obs_source_t *filter = obs_source_get_filter_by_name(target, kFilterName);
	if (!filter) {
		obs_data_t *settings = obs_data_create();
		filter = obs_source_create_private(kFilterId, kFilterName, settings);
		obs_data_release(settings);
		if (!filter)
			return nullptr;
		obs_source_filter_add(target, filter);
	}

	RingData *d = dataOf(filter);
	if (!d || !d->effect) {
		obs_source_filter_remove(target, filter);
		obs_source_release(filter);
		return nullptr;
	}
	return filter;
}

void detach(obs_source_t *target, obs_source_t *filter)
{
	if (!filter)
		return;
	if (target)
		obs_source_filter_remove(target, filter);
	obs_source_release(filter);
}

void setStyle(obs_source_t *filter, const Style &style)
{
	RingData *d = dataOf(filter);
	if (!d)
		return;
	std::lock_guard<std::mutex> lock(d->mutex);
	d->style = style;
}

} // namespace zoominator_camring
