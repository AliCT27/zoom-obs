#include "tilt-filter.hpp"

#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <util/bmem.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace zoominator_tilt {

static constexpr const char *kFilterId = "zoominator_tilt3d";
static constexpr const char *kFilterName = "Zoominator 3D Tilt";
static constexpr const char *kEffectFile = "tilt3d.effect";

struct TiltData {
	obs_source_t *context = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *pOrigin = nullptr;
	gs_eparam_t *pSize = nullptr;
	gs_eparam_t *pPivot = nullptr;
	gs_eparam_t *pAngles = nullptr;
	gs_eparam_t *pDist = nullptr;
	gs_eparam_t *pScale = nullptr;
};

namespace {
std::mutex g_mutex;
float g_yaw = 0.0f, g_pitch = 0.0f, g_persp = 2.0f;
bool g_sheetValid = false;
float g_sheet[4] = {0.0f, 0.0f, 0.0f, 0.0f};

float satf(float v)
{
	return std::clamp(v, 0.0f, 1.0f);
}

/* Pivot on one axis: the middle of the sheet while the sheet is no bigger than the canvas, the sheet point under
 * the middle of the canvas once it is zoomed past it. Blended so nothing jumps while zooming. */
float pivotAxis(float lo, float hi, float canvasMid)
{
	const float mid = 0.5f * (lo + hi);
	const float under = std::clamp(canvasMid, lo, hi);
	const float t = satf(((hi - lo) / (2.0f * canvasMid) - 1.0f) / 0.25f);
	return mid + (under - mid) * t;
}
} // namespace

void setAngles(float yaw, float pitch, float perspective)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_yaw = yaw;
	g_pitch = pitch;
	g_persp = perspective;
}

void setSheet(float left, float top, float right, float bottom)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_sheet[0] = left;
	g_sheet[1] = top;
	g_sheet[2] = right;
	g_sheet[3] = bottom;
	g_sheetValid = right - left > 1.0f && bottom - top > 1.0f;
}

void reset()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_sheetValid = false;
	g_yaw = 0.0f;
	g_pitch = 0.0f;
}

bool getPose(Pose &out)
{
	float yaw, pitch, persp, sh[4];
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_sheetValid)
			return false;
		yaw = g_yaw;
		pitch = g_pitch;
		persp = g_persp;
		for (int i = 0; i < 4; ++i)
			sh[i] = g_sheet[i];
	}
	if (std::fabs(yaw) < 0.0005f && std::fabs(pitch) < 0.0005f)
		return false;
	obs_video_info ovi{};
	if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0)
		return false;
	const float cw = (float)ovi.base_width, ch = (float)ovi.base_height;
	const float w = sh[2] - sh[0], h = sh[3] - sh[1];

	/* The more the view is zoomed in, the less the picture is turned: a turn is a big move of a zoomed picture and
	 * would push its corners away from the canvas edges, which the zoom framing places them on. */
	const float zoomRatio = std::max(1.0f, std::max(w / cw, h / ch));
	yaw /= zoomRatio;
	pitch /= zoomRatio;
	if (std::fabs(yaw) < 0.0005f && std::fabs(pitch) < 0.0005f)
		return false;

	const float longCanvas = std::max(cw, ch);
	const float longSheet = std::min(std::max(w, h), longCanvas);
	const float sy = std::fabs(std::sin(yaw)), sp = std::fabs(std::sin(pitch));

	out.yaw = yaw;
	out.pitch = pitch;
	out.px = pivotAxis(sh[0], sh[2], cw * 0.5f);
	out.py = pivotAxis(sh[1], sh[3], ch * 0.5f);
	out.dist = std::max(0.5f, persp) * longSheet;
	out.left = sh[0];
	out.top = sh[1];
	out.width = w;
	out.height = h;

	/* Not zoomed: shrink so the turned sheet fits exactly inside the flat sheet's rectangle. The filter can only
	 * draw inside that rectangle, so a corner swinging towards the camera would otherwise be cut off flat. Each
	 * corner is turned (without scaling) and the scale that keeps it inside the rectangle is taken. */
	float shrink = 1.0f;
	{
		Pose probe = out;
		probe.scl = 1.0f;
		const double cxs[2] = {sh[0], sh[2]};
		const double cys[2] = {sh[1], sh[3]};
		for (int i = 0; i < 2; ++i) {
			for (int j = 0; j < 2; ++j) {
				double x = cxs[i], y = cys[j];
				projectPoint(probe, x, y);
				const double dx = x - out.px, dy = y - out.py;
				if (dx > 0.5)
					shrink = std::min(shrink, (float)((sh[2] - out.px) / dx));
				else if (dx < -0.5)
					shrink = std::min(shrink, (float)((sh[0] - out.px) / dx));
				if (dy > 0.5)
					shrink = std::min(shrink, (float)((sh[3] - out.py) / dy));
				else if (dy < -0.5)
					shrink = std::min(shrink, (float)((sh[1] - out.py) / dy));
			}
		}
		shrink = std::clamp(shrink, 0.3f, 1.0f);
	}
	const float zx = satf((w / cw - 1.0f) / 0.25f);
	const float zy = satf((h / ch - 1.0f) / 0.25f);
	const float needX = 1.005f * (1.0f + 0.5f * cw * sy / out.dist) / std::max(0.5f, std::cos(yaw));
	const float needY = 1.005f * (1.0f + 0.5f * ch * sp / out.dist) / std::max(0.5f, std::cos(pitch));
	const float grow = std::max(1.0f + zx * (needX - 1.0f), 1.0f + zy * (needY - 1.0f));
	const float zoomT = std::max(zx, zy);
	out.scl = shrink + (grow - shrink) * zoomT;
	return true;
}

void projectPoint(const Pose &p, double &x, double &y)
{
	const double sa = std::sin(p.yaw), ca = std::cos(p.yaw);
	const double sb = std::sin(p.pitch), cb = std::cos(p.pitch);
	const double X = x - p.px, Y = y - p.py;
	const double hy = Y * cb;
	const double z1 = Y * sb;
	const double hx = X * ca + z1 * sa;
	const double hz = -X * sa + z1 * ca;
	const double k = (double)p.dist / std::max(0.01 * (double)p.dist, hz + (double)p.dist);
	x = p.px + hx * k * (double)p.scl;
	y = p.py + hy * k * (double)p.scl;
}

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
		d->pOrigin = gs_effect_get_param_by_name(d->effect, "sheet_origin");
		d->pSize = gs_effect_get_param_by_name(d->effect, "sheet_size");
		d->pPivot = gs_effect_get_param_by_name(d->effect, "pivot");
		d->pAngles = gs_effect_get_param_by_name(d->effect, "angles");
		d->pDist = gs_effect_get_param_by_name(d->effect, "dist_px");
		d->pScale = gs_effect_get_param_by_name(d->effect, "scl");
		if (!d->pOrigin || !d->pSize || !d->pPivot || !d->pAngles || !d->pDist || !d->pScale) {
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

	obs_source_t *target = obs_filter_get_target(d->context);
	const uint32_t w = target ? obs_source_get_base_width(target) : 0;
	const uint32_t h = target ? obs_source_get_base_height(target) : 0;
	Pose pose;
	if (!getPose(pose) || w == 0 || h == 0) {
		obs_source_skip_video_filter(d->context);
		return;
	}

	if (!obs_source_process_filter_begin(d->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	vec2 v{};
	v.x = pose.left;
	v.y = pose.top;
	gs_effect_set_vec2(d->pOrigin, &v);
	v.x = pose.width;
	v.y = pose.height;
	gs_effect_set_vec2(d->pSize, &v);
	v.x = pose.px;
	v.y = pose.py;
	gs_effect_set_vec2(d->pPivot, &v);
	v.x = pose.yaw;
	v.y = pose.pitch;
	gs_effect_set_vec2(d->pAngles, &v);
	gs_effect_set_float(d->pDist, pose.dist);
	gs_effect_set_float(d->pScale, pose.scl);

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

} // namespace zoominator_tilt
