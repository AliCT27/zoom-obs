#include "dots-overlay-filter.hpp"

#include <obs-frontend-api.h>
#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace zoominator_dots {
namespace {

constexpr const char *kId = "zoominator_dots_overlay";
constexpr const char *kEffectFile = "dots_overlay.effect";

constexpr const char *kCell = "cell_px";
constexpr const char *kDotSize = "dot_size_pct";
constexpr const char *kDuration = "duration_ms";
constexpr const char *kAngle = "angle_deg";
constexpr const char *kSoftness = "softness_pct";
constexpr const char *kJitter = "jitter_pct";
constexpr const char *kAccent = "accent_pct";
constexpr const char *kPlay = "play_now";

struct DotsData {
	obs_source_t *context = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *pProgress = nullptr, *pSize = nullptr, *pCell = nullptr, *pDotScale = nullptr, *pDir = nullptr,
		    *pSoft = nullptr, *pJitter = nullptr, *pAccent = nullptr;

	/* Written by the main thread (update), read by the graphics thread; plain floats, a torn read at worst shows one
	 * odd frame. */
	std::atomic<float> cell{36.0f};
	std::atomic<float> dotScale{0.75f};
	std::atomic<float> durationSec{2.2f};
	std::atomic<float> angleRad{1.9198622f};
	std::atomic<float> soft{0.35f};
	std::atomic<float> jitter{0.30f};
	std::atomic<float> accent{0.6f};

	/* Start time of the running animation in os_gettime_ns(), 0 = not playing. */
	std::atomic<uint64_t> startNs{0};
};

std::mutex g_registryMutex;
std::vector<DotsData *> g_instances;
obs_hotkey_id g_hotkeyId = OBS_INVALID_HOTKEY_ID;

void play(DotsData *d)
{
	uint64_t now = os_gettime_ns();
	if (now == 0)
		now = 1;
	d->startNs.store(now, std::memory_order_release);
}

void playAll()
{
	std::lock_guard<std::mutex> lock(g_registryMutex);
	for (DotsData *d : g_instances)
		play(d);
}

const char *getName(void *)
{
	return obs_module_text("DotsOverlay.Name");
}

void loadEffect(DotsData *d)
{
	char *path = obs_module_file(kEffectFile);
	if (!path) {
		blog(LOG_WARNING,
		     "[Zoominator] Dots overlay: could not locate %s (is the plugin data folder installed?)",
		     kEffectFile);
		return;
	}

	obs_enter_graphics();
	char *error = nullptr;
	d->effect = gs_effect_create_from_file(path, &error);
	if (!d->effect) {
		blog(LOG_WARNING, "[Zoominator] Dots overlay: shader failed to load from %s: %s", path,
		     error ? error : "(no message)");
	} else {
		d->pProgress = gs_effect_get_param_by_name(d->effect, "progress");
		d->pSize = gs_effect_get_param_by_name(d->effect, "size_px");
		d->pCell = gs_effect_get_param_by_name(d->effect, "cell_px");
		d->pDotScale = gs_effect_get_param_by_name(d->effect, "dot_scale");
		d->pDir = gs_effect_get_param_by_name(d->effect, "wave_dir");
		d->pSoft = gs_effect_get_param_by_name(d->effect, "wave_soft");
		d->pJitter = gs_effect_get_param_by_name(d->effect, "jitter");
		d->pAccent = gs_effect_get_param_by_name(d->effect, "accent");
		if (!d->pProgress || !d->pSize || !d->pCell || !d->pDotScale || !d->pDir || !d->pSoft || !d->pJitter ||
		    !d->pAccent) {
			blog(LOG_WARNING, "[Zoominator] Dots overlay: %s is missing expected parameters", kEffectFile);
			gs_effect_destroy(d->effect);
			d->effect = nullptr;
		}
	}
	obs_leave_graphics();

	bfree(error);
	bfree(path);
}

void update(void *data, obs_data_t *settings)
{
	auto *d = static_cast<DotsData *>(data);
	d->cell = (float)std::clamp<long long>(obs_data_get_int(settings, kCell), 8, 400);
	d->dotScale = (float)std::clamp<long long>(obs_data_get_int(settings, kDotSize), 20, 100) / 100.0f;
	d->durationSec = (float)std::clamp<long long>(obs_data_get_int(settings, kDuration), 300, 10000) / 1000.0f;
	d->angleRad = (float)obs_data_get_int(settings, kAngle) * 3.14159265f / 180.0f;
	d->soft = (float)std::clamp<long long>(obs_data_get_int(settings, kSoftness), 2, 100) / 100.0f;
	d->jitter = (float)std::clamp<long long>(obs_data_get_int(settings, kJitter), 0, 100) / 100.0f;
	d->accent = (float)std::clamp<long long>(obs_data_get_int(settings, kAccent), 0, 100) / 100.0f;
}

void *createData(obs_data_t *settings, obs_source_t *source)
{
	auto *d = new DotsData();
	d->context = source;
	loadEffect(d);
	update(d, settings);
	{
		std::lock_guard<std::mutex> lock(g_registryMutex);
		g_instances.push_back(d);
	}
	return d;
}

void destroyData(void *data)
{
	auto *d = static_cast<DotsData *>(data);
	if (!d)
		return;
	{
		std::lock_guard<std::mutex> lock(g_registryMutex);
		g_instances.erase(std::remove(g_instances.begin(), g_instances.end(), d), g_instances.end());
	}
	if (d->effect) {
		obs_enter_graphics();
		gs_effect_destroy(d->effect);
		obs_leave_graphics();
	}
	delete d;
}

void getDefaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, kCell, 36);
	obs_data_set_default_int(settings, kDotSize, 75);
	obs_data_set_default_int(settings, kDuration, 2200);
	obs_data_set_default_int(settings, kAngle, 110);
	obs_data_set_default_int(settings, kSoftness, 35);
	obs_data_set_default_int(settings, kJitter, 30);
	obs_data_set_default_int(settings, kAccent, 60);
}

bool playClicked(obs_properties_t *, obs_property_t *, void *data)
{
	if (data)
		play(static_cast<DotsData *>(data));
	return false;
}

obs_properties_t *getProperties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_button(props, kPlay, obs_module_text("DotsOverlay.Play"), playClicked);
	obs_properties_add_int_slider(props, kDuration, obs_module_text("DotsOverlay.Duration"), 500, 6000, 50);
	obs_properties_add_int_slider(props, kCell, obs_module_text("DotsOverlay.Cell"), 16, 160, 2);
	obs_properties_add_int_slider(props, kDotSize, obs_module_text("DotsOverlay.DotSize"), 30, 100, 1);
	obs_properties_add_int_slider(props, kAngle, obs_module_text("DotsOverlay.Angle"), -180, 180, 5);
	obs_properties_add_int_slider(props, kSoftness, obs_module_text("DotsOverlay.Softness"), 5, 80, 1);
	obs_properties_add_int_slider(props, kJitter, obs_module_text("DotsOverlay.Jitter"), 0, 60, 1);
	obs_properties_add_int_slider(props, kAccent, obs_module_text("DotsOverlay.Accent"), 0, 100, 1);
	return props;
}

void videoRender(void *data, gs_effect_t *)
{
	auto *d = static_cast<DotsData *>(data);
	if (!d || !d->effect) {
		obs_source_skip_video_filter(d ? d->context : nullptr);
		return;
	}

	const uint64_t start = d->startNs.load(std::memory_order_acquire);
	if (start == 0) {
		obs_source_skip_video_filter(d->context);
		return;
	}
	const double elapsed = (double)(os_gettime_ns() - start) / 1e9;
	const double dur = std::max(0.1f, d->durationSec.load());
	if (elapsed >= dur) {
		uint64_t expected = start;
		d->startNs.compare_exchange_strong(expected, 0);
		obs_source_skip_video_filter(d->context);
		return;
	}

	obs_source_t *target = obs_filter_get_target(d->context);
	const uint32_t w = target ? obs_source_get_base_width(target) : 0;
	const uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if (w == 0 || h == 0) {
		obs_source_skip_video_filter(d->context);
		return;
	}

	if (!obs_source_process_filter_begin(d->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	gs_effect_set_float(d->pProgress, (float)std::clamp(elapsed / dur, 0.0, 1.0));
	vec2 size;
	vec2_set(&size, (float)w, (float)h);
	gs_effect_set_vec2(d->pSize, &size);
	gs_effect_set_float(d->pCell, d->cell.load());
	gs_effect_set_float(d->pDotScale, d->dotScale.load());
	vec2 dir;
	const float a = d->angleRad.load();
	vec2_set(&dir, std::cos(a), std::sin(a));
	gs_effect_set_vec2(d->pDir, &dir);
	gs_effect_set_float(d->pSoft, d->soft.load());
	gs_effect_set_float(d->pJitter, d->jitter.load());
	gs_effect_set_float(d->pAccent, d->accent.load());

	obs_source_process_filter_end(d->context, d->effect, w, h);
}

void hotkeyCallback(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		playAll();
}

void saveCallback(obs_data_t *saveData, bool saving, void *)
{
	if (g_hotkeyId == OBS_INVALID_HOTKEY_ID)
		return;
	if (saving) {
		obs_data_array_t *arr = obs_hotkey_save(g_hotkeyId);
		obs_data_set_array(saveData, "zoominator_dots_overlay_hotkey", arr);
		obs_data_array_release(arr);
	} else {
		obs_data_array_t *arr = obs_data_get_array(saveData, "zoominator_dots_overlay_hotkey");
		obs_hotkey_load(g_hotkeyId, arr);
		obs_data_array_release(arr);
	}
}

} // namespace

void registerFilter()
{
	obs_source_info info = {};
	info.id = kId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = getName;
	info.create = createData;
	info.destroy = destroyData;
	info.update = update;
	info.get_defaults = getDefaults;
	info.get_properties = getProperties;
	info.video_render = videoRender;
	obs_register_source(&info);
}

void startHotkey()
{
	if (g_hotkeyId != OBS_INVALID_HOTKEY_ID)
		return;
	g_hotkeyId = obs_hotkey_register_frontend("zoominator.dots_overlay", obs_module_text("Hotkey.DotsOverlay"),
						  hotkeyCallback, nullptr);
	obs_frontend_add_save_callback(saveCallback, nullptr);
}

void stopHotkey()
{
	if (g_hotkeyId == OBS_INVALID_HOTKEY_ID)
		return;
	obs_frontend_remove_save_callback(saveCallback, nullptr);
	obs_hotkey_unregister(g_hotkeyId);
	g_hotkeyId = OBS_INVALID_HOTKEY_ID;
}

} // namespace zoominator_dots
