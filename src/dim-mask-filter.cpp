#include "dim-mask-filter.hpp"
#include "tilt-filter.hpp"

#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <util/bmem.h>

#include <mutex>

namespace zoominator_dimmask {

static constexpr const char *kFilterId = "zoominator_dim_mask";
static constexpr const char *kFilterName = "Zoominator Dim Mask";
static constexpr const char *kEffectFile = "dim_mask.effect";

struct MaskData {
	obs_source_t *context = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *pItemOrigin = nullptr, *pItemExtent = nullptr, *pSheetOrigin = nullptr, *pSheetSize = nullptr,
		    *pPivot = nullptr, *pAngles = nullptr, *pDist = nullptr, *pScale = nullptr, *pTilted = nullptr;
};

namespace {
std::mutex g_mutex;
bool g_sheetValid = false;
float g_sheet[4] = {0, 0, 0, 0};
float g_item[3] = {0, 0, 1};
} // namespace

void setSheet(float left, float top, float right, float bottom)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_sheet[0] = left;
	g_sheet[1] = top;
	g_sheet[2] = right;
	g_sheet[3] = bottom;
	g_sheetValid = right - left > 4.0f && bottom - top > 4.0f;
}

void clearSheet()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_sheetValid = false;
}

void setItem(float centerX, float centerY, float scale)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_item[0] = centerX;
	g_item[1] = centerY;
	g_item[2] = scale;
}

static const char *getName(void *)
{
	return kFilterName;
}

static void loadEffect(MaskData *d)
{
	char *path = obs_module_file(kEffectFile);
	if (!path) {
		blog(LOG_WARNING, "[Zoominator] dim mask: could not locate %s", kEffectFile);
		return;
	}
	obs_enter_graphics();
	char *error = nullptr;
	d->effect = gs_effect_create_from_file(path, &error);
	if (!d->effect) {
		blog(LOG_WARNING, "[Zoominator] dim mask: shader failed to load from %s: %s", path,
		     error ? error : "(no message)");
	} else {
		d->pItemOrigin = gs_effect_get_param_by_name(d->effect, "item_origin");
		d->pItemExtent = gs_effect_get_param_by_name(d->effect, "item_extent");
		d->pSheetOrigin = gs_effect_get_param_by_name(d->effect, "sheet_origin");
		d->pSheetSize = gs_effect_get_param_by_name(d->effect, "sheet_size");
		d->pPivot = gs_effect_get_param_by_name(d->effect, "pivot");
		d->pAngles = gs_effect_get_param_by_name(d->effect, "angles");
		d->pDist = gs_effect_get_param_by_name(d->effect, "dist_px");
		d->pScale = gs_effect_get_param_by_name(d->effect, "scl");
		d->pTilted = gs_effect_get_param_by_name(d->effect, "tilted");
		if (!d->pItemOrigin || !d->pItemExtent || !d->pSheetOrigin || !d->pSheetSize || !d->pPivot ||
		    !d->pAngles || !d->pDist || !d->pScale || !d->pTilted) {
			blog(LOG_WARNING, "[Zoominator] dim mask: %s is missing expected parameters", kEffectFile);
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
	auto *d = new MaskData();
	d->context = source;
	loadEffect(d);
	return d;
}

static void destroy(void *data)
{
	auto *d = static_cast<MaskData *>(data);
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
	auto *d = static_cast<MaskData *>(data);
	if (!d || !d->effect) {
		obs_source_skip_video_filter(d ? d->context : nullptr);
		return;
	}
	obs_source_t *target = obs_filter_get_target(d->context);
	const uint32_t w = target ? obs_source_get_base_width(target) : 0;
	const uint32_t h = target ? obs_source_get_base_height(target) : 0;

	bool valid;
	float sh[4], item[3];
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		valid = g_sheetValid;
		for (int i = 0; i < 4; ++i)
			sh[i] = g_sheet[i];
		for (int i = 0; i < 3; ++i)
			item[i] = g_item[i];
	}
	if (!valid || w == 0 || h == 0) {
		/* No screen known: leave the dim as it is. */
		obs_source_skip_video_filter(d->context);
		return;
	}

	zoominator_tilt::Pose pose;
	const bool tilted = zoominator_tilt::getPose(pose);

	if (!obs_source_process_filter_begin(d->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	const float ex = (float)w * item[2], ey = (float)h * item[2];
	vec2 v{};
	v.x = item[0] - ex * 0.5f;
	v.y = item[1] - ey * 0.5f;
	gs_effect_set_vec2(d->pItemOrigin, &v);
	v.x = ex;
	v.y = ey;
	gs_effect_set_vec2(d->pItemExtent, &v);
	v.x = tilted ? pose.left : sh[0];
	v.y = tilted ? pose.top : sh[1];
	gs_effect_set_vec2(d->pSheetOrigin, &v);
	v.x = tilted ? pose.width : sh[2] - sh[0];
	v.y = tilted ? pose.height : sh[3] - sh[1];
	gs_effect_set_vec2(d->pSheetSize, &v);
	v.x = tilted ? pose.px : 0.0f;
	v.y = tilted ? pose.py : 0.0f;
	gs_effect_set_vec2(d->pPivot, &v);
	v.x = tilted ? pose.yaw : 0.0f;
	v.y = tilted ? pose.pitch : 0.0f;
	gs_effect_set_vec2(d->pAngles, &v);
	gs_effect_set_float(d->pDist, tilted ? pose.dist : 1.0f);
	gs_effect_set_float(d->pScale, tilted ? pose.scl : 1.0f);
	gs_effect_set_float(d->pTilted, tilted ? 1.0f : 0.0f);

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

static MaskData *dataOf(obs_source_t *filter)
{
	return filter ? static_cast<MaskData *>(obs_obj_get_data(filter)) : nullptr;
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
	MaskData *d = dataOf(filter);
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

} // namespace zoominator_dimmask
