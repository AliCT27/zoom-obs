#include "focus-blur-filter.hpp"

#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <util/bmem.h>

#include <atomic>

namespace zoominator_blur {

static constexpr const char *kFilterId = "zoominator_focus_blur";
static constexpr const char *kFilterName = "Zoominator Focus Blur";
static constexpr const char *kEffectFile = "focus_blur.effect";

struct BlurData {
	obs_source_t *context = nullptr;
	gs_effect_t *effect = nullptr;

	gs_eparam_t *pTexelSize = nullptr;
	gs_eparam_t *pCenter = nullptr;
	gs_eparam_t *pRadius = nullptr;
	gs_eparam_t *pSoft = nullptr;
	gs_eparam_t *pBlur = nullptr;
	gs_eparam_t *pStrength = nullptr;

	/* Written from other threads, read by the render thread. */
	std::atomic<float> cx{0.0f};
	std::atomic<float> cy{0.0f};
	std::atomic<float> radius{300.0f};
	std::atomic<float> soft{80.0f};
	std::atomic<float> blur{0.0f};
	std::atomic<float> strength{0.0f}; /* 0 = pass-through until the controller says otherwise */
};

static const char *getName(void *)
{
	return kFilterName;
}

static void loadEffect(BlurData *d)
{
	char *path = obs_module_file(kEffectFile);
	if (!path) {
		blog(LOG_WARNING, "[Zoominator] Focus blur: could not locate %s (is the plugin data folder installed?)",
		     kEffectFile);
		return;
	}

	obs_enter_graphics();
	char *error = nullptr;
	d->effect = gs_effect_create_from_file(path, &error);
	if (!d->effect) {
		blog(LOG_WARNING, "[Zoominator] Focus blur: shader failed to load from %s: %s", path,
		     error ? error : "(no message)");
	} else {
		d->pTexelSize = gs_effect_get_param_by_name(d->effect, "texel_size");
		d->pCenter = gs_effect_get_param_by_name(d->effect, "center_px");
		d->pRadius = gs_effect_get_param_by_name(d->effect, "radius_px");
		d->pSoft = gs_effect_get_param_by_name(d->effect, "soft_px");
		d->pBlur = gs_effect_get_param_by_name(d->effect, "blur_px");
		d->pStrength = gs_effect_get_param_by_name(d->effect, "strength");
		if (!d->pTexelSize || !d->pCenter || !d->pRadius || !d->pSoft || !d->pBlur || !d->pStrength) {
			blog(LOG_WARNING, "[Zoominator] Focus blur: %s is missing expected parameters", kEffectFile);
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
	auto *d = new BlurData();
	d->context = source;
	loadEffect(d);
	return d;
}

static void destroy(void *data)
{
	auto *d = static_cast<BlurData *>(data);
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
	auto *d = static_cast<BlurData *>(data);
	if (!d || !d->effect) {
		obs_source_skip_video_filter(d ? d->context : nullptr);
		return;
	}

	const float strength = d->strength.load(std::memory_order_relaxed);
	obs_source_t *target = obs_filter_get_target(d->context);
	const uint32_t w = target ? obs_source_get_base_width(target) : 0;
	const uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if (strength <= 0.001f || w == 0 || h == 0) {
		obs_source_skip_video_filter(d->context);
		return;
	}

	if (!obs_source_process_filter_begin(d->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	vec2 v{};
	v.x = 1.0f / (float)w;
	v.y = 1.0f / (float)h;
	gs_effect_set_vec2(d->pTexelSize, &v);
	v.x = d->cx.load(std::memory_order_relaxed);
	v.y = d->cy.load(std::memory_order_relaxed);
	gs_effect_set_vec2(d->pCenter, &v);
	gs_effect_set_float(d->pRadius, d->radius.load(std::memory_order_relaxed));
	gs_effect_set_float(d->pSoft, d->soft.load(std::memory_order_relaxed));
	gs_effect_set_float(d->pBlur, d->blur.load(std::memory_order_relaxed));
	gs_effect_set_float(d->pStrength, strength);

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

static BlurData *dataOf(obs_source_t *filter)
{
	return filter ? static_cast<BlurData *>(obs_obj_get_data(filter)) : nullptr;
}

obs_source_t *attach(obs_source_t *target)
{
	if (!target)
		return nullptr;

	/* A previous run may have left one behind (a crash saved the collection
	 * mid-zoom), and two attached filters would blur twice. */
	obs_source_t *filter = obs_source_get_filter_by_name(target, kFilterName);
	if (!filter) {
		obs_data_t *settings = obs_data_create();
		filter = obs_source_create_private(kFilterId, kFilterName, settings);
		obs_data_release(settings);
		if (!filter)
			return nullptr;
		obs_source_filter_add(target, filter);
	}

	BlurData *d = dataOf(filter);
	if (!d || !d->effect) {
		obs_source_filter_remove(target, filter);
		obs_source_release(filter);
		return nullptr;
	}
	d->strength.store(0.0f, std::memory_order_relaxed);
	return filter;
}

void detach(obs_source_t *target, obs_source_t *filter)
{
	if (!filter)
		return;
	if (BlurData *d = dataOf(filter))
		d->strength.store(0.0f, std::memory_order_relaxed);
	if (target)
		obs_source_filter_remove(target, filter);
	obs_source_release(filter);
}

void setParams(obs_source_t *filter, float centerX, float centerY, float radius, float softness, float blurRadius,
	       float strength)
{
	BlurData *d = dataOf(filter);
	if (!d)
		return;
	d->cx.store(centerX, std::memory_order_relaxed);
	d->cy.store(centerY, std::memory_order_relaxed);
	d->radius.store(radius, std::memory_order_relaxed);
	d->soft.store(softness, std::memory_order_relaxed);
	d->blur.store(blurRadius, std::memory_order_relaxed);
	d->strength.store(strength, std::memory_order_relaxed);
}

} // namespace zoominator_blur
