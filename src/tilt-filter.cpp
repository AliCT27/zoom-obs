#include "tilt-filter.hpp"

#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <util/bmem.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace zoominator_tilt {

static constexpr const char *kFilterId = "zoominator_tilt3d";
static constexpr const char *kFilterName = "Zoominator 3D Tilt";
static constexpr const char *kEffectFile = "tilt3d.effect";

struct TiltData {
	obs_source_t *context = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *pSize = nullptr;
	gs_eparam_t *pAngles = nullptr;
	gs_eparam_t *pDist = nullptr;
	gs_eparam_t *pScale = nullptr;

	std::atomic<float> yaw{0.0f};
	std::atomic<float> pitch{0.0f};
	std::atomic<float> persp{2.0f};
};

static const char *getName(void *)
{
	return kFilterName;
}

static void loadEffect(TiltData *d)
{
	char *path = obs_module_file(kEffectFile);
	if (!path) {
		blog(LOG_WARNING, "[Zoominator] 3D tilt: could not locate %s", kEffectFile);
		return;
	}
	obs_enter_graphics();
	char *error = nullptr;
	d->effect = gs_effect_create_from_file(path, &error);
	if (!d->effect) {
		blog(LOG_WARNING, "[Zoominator] 3D tilt: shader failed to load from %s: %s", path,
		     error ? error : "(no message)");
	} else {
		d->pSize = gs_effect_get_param_by_name(d->effect, "size_px");
		d->pAngles = gs_effect_get_param_by_name(d->effect, "angles");
		d->pDist = gs_effect_get_param_by_name(d->effect, "dist_px");
		d->pScale = gs_effect_get_param_by_name(d->effect, "scl");
		if (!d->pSize || !d->pAngles || !d->pDist || !d->pScale) {
			blog(LOG_WARNING, "[Zoominator] 3D tilt: %s is missing expected parameters", kEffectFile);
			gs_effect_destroy(d->effect);
			d->effect = nullptr;
		}
	}
	obs_leave_graphics();
	bfree(error);
	bfree(path);
}

static void *create(obs_data_t *, obs_source_t *source)
{
	auto *d = new TiltData();
	d->context = source;
	loadEffect(d);
	return d;
}

static void destroy(void *data)
{
	auto *d = static_cast<TiltData *>(data);
	if (!d)
		return;
	if (d->effect) {
		obs_enter_graphics();
		gs_effect_destroy(d->effect);
		obs_leave_graphics();
	}
	delete d;
}

static void videoRender(void *data, gs_effect_t *)
{
	auto *d = static_cast<TiltData *>(data);
	if (!d || !d->effect) {
		obs_source_skip_video_filter(d ? d->context : nullptr);
		return;
	}

	const float yaw = d->yaw.load(std::memory_order_relaxed);
	const float pitch = d->pitch.load(std::memory_order_relaxed);
	obs_source_t *target = obs_filter_get_target(d->context);
	const uint32_t w = target ? obs_source_get_base_width(target) : 0;
	const uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if ((std::fabs(yaw) < 0.0005f && std::fabs(pitch) < 0.0005f) || w == 0 || h == 0) {
		obs_source_skip_video_filter(d->context);
		return;
	}

	if (!obs_source_process_filter_begin(d->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	const float longSide = (float)std::max(w, h);
	const float dist = std::max(0.5f, d->persp.load(std::memory_order_relaxed)) * longSide;
	/* The edge that swings towards the camera would grow past the frame; shrink the whole sheet to match. */
	const float swing = 0.5f * longSide * std::max(std::fabs(std::sin(yaw)), std::fabs(std::sin(pitch)));
	const float scale = std::clamp((dist - swing) / dist, 0.3f, 1.0f);

	vec2 v{};
	v.x = (float)w;
	v.y = (float)h;
	gs_effect_set_vec2(d->pSize, &v);
	v.x = yaw;
	v.y = pitch;
	gs_effect_set_vec2(d->pAngles, &v);
	gs_effect_set_float(d->pDist, dist);
	gs_effect_set_float(d->pScale, scale);

	obs_source_process_filter_end(d->context, d->effect, w, h);
}

void registerFilter()
{
	obs_source_info info = {};
	info.id = kFilterId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = getName;
	info.create = create;
	info.destroy = destroy;
	info.video_render = videoRender;
	obs_register_source(&info);
}

static TiltData *dataOf(obs_source_t *filter)
{
	return filter ? static_cast<TiltData *>(obs_obj_get_data(filter)) : nullptr;
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
	TiltData *d = dataOf(filter);
	if (!d || !d->effect) {
		obs_source_filter_remove(target, filter);
		obs_source_release(filter);
		return nullptr;
	}
	d->yaw.store(0.0f, std::memory_order_relaxed);
	d->pitch.store(0.0f, std::memory_order_relaxed);
	return filter;
}

void detach(obs_source_t *target, obs_source_t *filter)
{
	if (!filter)
		return;
	if (TiltData *d = dataOf(filter)) {
		d->yaw.store(0.0f, std::memory_order_relaxed);
		d->pitch.store(0.0f, std::memory_order_relaxed);
	}
	if (target)
		obs_source_filter_remove(target, filter);
	obs_source_release(filter);
}

void setParams(obs_source_t *filter, float yaw, float pitch, float perspective)
{
	TiltData *d = dataOf(filter);
	if (!d)
		return;
	d->yaw.store(yaw, std::memory_order_relaxed);
	d->pitch.store(pitch, std::memory_order_relaxed);
	d->persp.store(perspective, std::memory_order_relaxed);
}

} // namespace zoominator_tilt
