#include "key-display-source.hpp"

#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <util/platform.h>

#include <algorithm>
#include <mutex>

namespace zoominator_keys {

namespace {

constexpr const char *kSourceId = "zoominator_keys";
constexpr const char *kSourceName = "Zoominator Key Display";
constexpr const char *kEffectFile = "key_label.effect";

constexpr double kFadeInSec = 0.10;
constexpr double kDefaultTotalSec = 1.70; /* from the last key press until the keys are gone */
constexpr double kFadeOutSec = 0.30;

std::mutex g_mutex;
std::vector<uint8_t> g_pixels;
int g_w = 0;
int g_h = 0;
uint64_t g_version = 0;
uint64_t g_firstNs = 0; /* when the current run of key presses became visible */
uint64_t g_lastNs = 0;  /* the latest key press */
bool g_hasPicture = false;
int g_yPct = 58;
double g_totalSec = kDefaultTotalSec;
bool g_loadFailed = false;

struct Data {
	gs_effect_t *effect = nullptr;
	gs_eparam_t *pImage = nullptr;
	gs_eparam_t *pOpacity = nullptr;
	gs_texture_t *texture = nullptr;
	uint64_t textureVersion = 0;
	int texW = 0;
	int texH = 0;
};

const char *getName(void *)
{
	return kSourceName;
}

void loadEffect(Data *d)
{
	char *path = obs_module_file(kEffectFile);
	if (!path) {
		blog(LOG_WARNING, "[Zoominator] Key display: could not locate %s (is the plugin data folder installed?)",
		     kEffectFile);
		return;
	}

	obs_enter_graphics();
	char *error = nullptr;
	d->effect = gs_effect_create_from_file(path, &error);
	if (!d->effect) {
		blog(LOG_WARNING, "[Zoominator] Key display: shader failed to load from %s: %s", path,
		     error ? error : "(no message)");
	} else {
		d->pImage = gs_effect_get_param_by_name(d->effect, "image");
		d->pOpacity = gs_effect_get_param_by_name(d->effect, "opacity");
		if (!d->pImage || !d->pOpacity) {
			blog(LOG_WARNING, "[Zoominator] Key display: %s is missing expected parameters", kEffectFile);
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
	if (d->effect || d->texture) {
		obs_enter_graphics();
		if (d->texture)
			gs_texture_destroy(d->texture);
		if (d->effect)
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

	const uint64_t now = os_gettime_ns();
	double opacity = 0.0;
	int yPct = 58;
	bool needTexture = false;
	std::vector<uint8_t> pixels;
	int w = 0, h = 0;
	uint64_t version = 0;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_hasPicture)
			return;
		const double sinceLast = (double)(now - g_lastNs) / 1e9;
		const double sinceFirst = (double)(now - g_firstNs) / 1e9;
		const double holdSec = std::max(0.0, g_totalSec - kFadeOutSec);
		if (sinceLast >= holdSec + kFadeOutSec)
			return;
		const double fadeIn = std::clamp(sinceFirst / kFadeInSec, 0.0, 1.0);
		const double fadeOut = sinceLast <= holdSec ? 1.0 : 1.0 - (sinceLast - holdSec) / kFadeOutSec;
		opacity = fadeIn * std::clamp(fadeOut, 0.0, 1.0);
		yPct = g_yPct;
		version = g_version;
		w = g_w;
		h = g_h;
		needTexture = version != d->textureVersion || !d->texture;
		if (needTexture)
			pixels = g_pixels;
	}
	if (opacity <= 0.001 || w <= 0 || h <= 0)
		return;

	if (needTexture) {
		if (d->texture) {
			gs_texture_destroy(d->texture);
			d->texture = nullptr;
		}
		if ((int)pixels.size() < w * h * 4)
			return;
		const uint8_t *ptr = pixels.data();
		d->texture = gs_texture_create((uint32_t)w, (uint32_t)h, GS_BGRA, 1, &ptr, 0);
		d->textureVersion = version;
		d->texW = w;
		d->texH = h;
		if (!d->texture)
			return;
	}

	const float cw = (float)canvasWidth(nullptr);
	const float ch = (float)canvasHeight(nullptr);
	if (cw < 1.0f || ch < 1.0f)
		return;
	const float x = std::clamp((cw - (float)d->texW) * 0.5f, 0.0f, cw);
	const float y = ch * (float)std::clamp(yPct, 0, 100) / 100.0f - (float)d->texH * 0.5f;

	gs_effect_set_texture(d->pImage, d->texture);
	gs_effect_set_float(d->pOpacity, (float)opacity);
	while (gs_effect_loop(d->effect, "Draw")) {
		gs_matrix_push();
		gs_matrix_translate3f(x, y, 0.0f);
		gs_draw_sprite(d->texture, 0, (uint32_t)d->texW, (uint32_t)d->texH);
		gs_matrix_pop();
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
	if (g_loadFailed)
		return nullptr;

	obs_source_t *source = obs_source_create_private(kSourceId, kSourceName, nullptr);
	if (!source) {
		g_loadFailed = true;
		blog(LOG_WARNING, "[Zoominator] Key display: could not create the source");
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

void show(const std::vector<uint8_t> &bgra, int width, int height)
{
	if (width <= 0 || height <= 0 || (int)bgra.size() < width * height * 4)
		return;
	const uint64_t now = os_gettime_ns();
	std::lock_guard<std::mutex> lock(g_mutex);
	g_pixels = bgra;
	g_w = width;
	g_h = height;
	++g_version;
	/* A new key press while the last one is still on screen carries on without fading in again. */
	const double sinceLast = (double)(now - g_lastNs) / 1e9;
	if (!g_hasPicture || sinceLast > std::max(0.0, g_totalSec - kFadeOutSec) + kFadeOutSec * 0.5)
		g_firstNs = now;
	g_lastNs = now;
	g_hasPicture = true;
}

void setPlacement(int yPct)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_yPct = yPct;
}

void setDuration(double totalSec)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_totalSec = std::clamp(totalSec, 0.5, 10.0);
}

void hide()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_hasPicture = false;
}

} // namespace zoominator_keys
