#include "dots-overlay-filter.hpp"

#include <obs-frontend-api.h>
#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
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
constexpr const char *kStyle = "style";
constexpr int kColorCount = 7;
constexpr const char *kColors[kColorCount] = {"wave_color1", "wave_color2", "wave_color3", "fill_color",
					      "map_dark",    "map_mid",     "map_light"};
/* OBS colour settings are 0xAABBGGRR. Taken from a reference recording: light blue, dark red, orange, peach,
 * then the brightness map for the new picture: dark teal, orange, light pink. */
constexpr uint32_t kDefaultColors[kColorCount] = {0xFFEBC773, 0xFF121A9E, 0xFF1A6BD9, 0xFFA3B8E8,
						  0xFF80991F, 0xFF4085ED, 0xFFCCC2F2};
constexpr const char *kPlay = "play_now";

struct DotsData {
	obs_source_t *context = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *pProgress = nullptr, *pSize = nullptr, *pCell = nullptr, *pDotScale = nullptr, *pDir = nullptr,
		    *pSoft = nullptr, *pJitter = nullptr, *pAccent = nullptr, *pStyle = nullptr,
		    *pColor[kColorCount] = {};

	/* Written by the main thread (update), read by the graphics thread; plain floats, a torn read at worst shows one
	 * odd frame. */
	std::atomic<float> cell{36.0f};
	std::atomic<float> dotScale{0.75f};
	std::atomic<float> durationSec{2.2f};
	std::atomic<float> angleRad{1.9198622f};
	std::atomic<float> soft{0.35f};
	std::atomic<float> jitter{0.30f};
	std::atomic<float> accent{0.6f};
	std::atomic<float> style{1.0f};
	std::atomic<uint32_t> colors[kColorCount] = {kDefaultColors[0], kDefaultColors[1], kDefaultColors[2],
						     kDefaultColors[3], kDefaultColors[4], kDefaultColors[5],
						     kDefaultColors[6]};

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
		d->pStyle = gs_effect_get_param_by_name(d->effect, "style");
		bool colorsOk = true;
		for (int i = 0; i < kColorCount; ++i) {
			const char names[kColorCount][8] = {"color0", "color1", "color2", "color3",
							    "color4", "color5", "color6"};
			d->pColor[i] = gs_effect_get_param_by_name(d->effect, names[i]);
			colorsOk = colorsOk && d->pColor[i];
		}
		if (!d->pProgress || !d->pSize || !d->pCell || !d->pDotScale || !d->pDir || !d->pSoft || !d->pJitter ||
		    !d->pAccent || !d->pStyle || !colorsOk) {
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
	d->style = obs_data_get_int(settings, kStyle) == 0 ? 0.0f : 1.0f;
	for (int i = 0; i < kColorCount; ++i)
		d->colors[i] = (uint32_t)obs_data_get_int(settings, kColors[i]);
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
	obs_data_set_default_int(settings, kStyle, 1);
	for (int i = 0; i < kColorCount; ++i)
		obs_data_set_default_int(settings, kColors[i], (long long)kDefaultColors[i]);
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
	obs_property_t *style = obs_properties_add_list(props, kStyle, obs_module_text("DotsOverlay.Style"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(style, obs_module_text("DotsOverlay.Style.Pastel"), 0);
	obs_property_list_add_int(style, obs_module_text("DotsOverlay.Style.Layered"), 1);
	obs_properties_add_int_slider(props, kDuration, obs_module_text("DotsOverlay.Duration"), 500, 6000, 50);
	obs_properties_add_int_slider(props, kCell, obs_module_text("DotsOverlay.Cell"), 16, 160, 2);
	obs_properties_add_int_slider(props, kDotSize, obs_module_text("DotsOverlay.DotSize"), 30, 100, 1);
	obs_properties_add_int_slider(props, kAngle, obs_module_text("DotsOverlay.Angle"), -180, 180, 5);
	obs_properties_add_int_slider(props, kSoftness, obs_module_text("DotsOverlay.Softness"), 5, 80, 1);
	obs_properties_add_int_slider(props, kJitter, obs_module_text("DotsOverlay.Jitter"), 0, 60, 1);
	obs_properties_add_int_slider(props, kAccent, obs_module_text("DotsOverlay.Accent"), 0, 100, 1);
	obs_properties_add_color(props, kColors[0], obs_module_text("DotsOverlay.Color1"));
	obs_properties_add_color(props, kColors[1], obs_module_text("DotsOverlay.Color2"));
	obs_properties_add_color(props, kColors[2], obs_module_text("DotsOverlay.Color3"));
	obs_properties_add_color(props, kColors[3], obs_module_text("DotsOverlay.Color4"));
	obs_properties_add_color(props, kColors[4], obs_module_text("DotsOverlay.Color5"));
	obs_properties_add_color(props, kColors[5], obs_module_text("DotsOverlay.Color6"));
	obs_properties_add_color(props, kColors[6], obs_module_text("DotsOverlay.Color7"));
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

	/* Not direct rendering: a screen capture draws itself with an opaque effect (its alpha channel is undefined), and
	 * with direct rendering this effect would replace that one and read alpha 0, which shows as a black picture.
	 * Rendering the source into a texture first gives the same opaque picture the rest of OBS sees. This only costs
	 * anything while the effect is playing, otherwise the filter is skipped. */
	if (!obs_source_process_filter_begin(d->context, GS_RGBA, OBS_NO_DIRECT_RENDERING))
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
	gs_effect_set_float(d->pStyle, d->style.load());
	for (int i = 0; i < kColorCount; ++i) {
		const uint32_t c = d->colors[i].load();
		vec4 v;
		vec4_set(&v, (float)(c & 0xFF) / 255.0f, (float)((c >> 8) & 0xFF) / 255.0f,
			 (float)((c >> 16) & 0xFF) / 255.0f, 1.0f);
		gs_effect_set_vec4(d->pColor[i], &v);
	}

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
