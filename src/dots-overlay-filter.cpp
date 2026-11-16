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
constexpr const char *kEnter = "enter_from";
constexpr const char *kExit = "exit_from";
constexpr const char *kSoftness = "softness_pct";
constexpr const char *kJitter = "jitter_pct";
constexpr const char *kAccent = "accent_pct";
constexpr const char *kStyle = "style";
constexpr const char *kRun = "run_mode";
/* The sides a sweep can start from; the vector is the way it travels (pixels, y down), zero = from the centre. */
constexpr const char *kSideNames[9] = {"DotsOverlay.Side.Top",        "DotsOverlay.Side.Bottom",
				       "DotsOverlay.Side.Left",       "DotsOverlay.Side.Right",
				       "DotsOverlay.Side.TopLeft",    "DotsOverlay.Side.TopRight",
				       "DotsOverlay.Side.BottomLeft", "DotsOverlay.Side.BottomRight",
				       "DotsOverlay.Side.Centre"};

vec2 sideDir(int side)
{
	const float k = 0.70710678f;
	static const float dirs[9][2] = {{0.0f, 1.0f}, {0.0f, -1.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f}, {k, k},
					 {-k, k},      {k, -k},       {-k, -k},     {0.0f, 0.0f}};
	vec2 v;
	const int i = std::clamp(side, 0, 8);
	vec2_set(&v, dirs[i][0], dirs[i][1]);
	return v;
}

constexpr int kColorCount = 8;
constexpr const char *kColors[kColorCount] = {"wave_color1", "wave_color2", "wave_color3", "fill_color",
					      "map_dark",    "map_mid",     "map_light",   "band_color"};
/* OBS colour settings are 0xAABBGGRR. Taken from a reference recording: light blue, dark red, orange, peach,
 * then the brightness map for the new picture: dark teal, orange, light pink, then the dot colour of the
 * single band: pastel peach. */
constexpr uint32_t kDefaultColors[kColorCount] = {0xFFEBC773, 0xFF121A9E, 0xFF1A6BD9, 0xFFA3B8E8,
						  0xFF80991F, 0xFF4085ED, 0xFFCCC2F2, 0xFFB3BDED};
constexpr const char *kPlay = "play_now";

struct DotsData {
	obs_source_t *context = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *pProgress = nullptr, *pSize = nullptr, *pCell = nullptr, *pDotScale = nullptr, *pDir = nullptr,
		    *pDirOut = nullptr, *pSoft = nullptr, *pJitter = nullptr, *pAccent = nullptr, *pStyle = nullptr,
		    *pSinglePass = nullptr, *pColor[kColorCount] = {};

	/* Written by the main thread (update), read by the graphics thread; plain floats, a torn read at worst shows one
	 * odd frame. */
	std::atomic<float> cell{36.0f};
	std::atomic<float> dotScale{0.75f};
	std::atomic<float> durationSec{2.2f};
	std::atomic<int> enterSide{0}; /* 0 top, see sideDir */
	std::atomic<int> exitSide{0};  /* 0 same as entry, 1 opposite, 2.. = side + 2 */
	std::atomic<float> soft{0.35f};
	std::atomic<float> jitter{0.30f};
	std::atomic<float> accent{0.6f};
	std::atomic<float> style{2.0f};
	std::atomic<float> singlePass{1.0f};
	std::atomic<uint32_t> colors[kColorCount] = {kDefaultColors[0], kDefaultColors[1], kDefaultColors[2],
						     kDefaultColors[3], kDefaultColors[4], kDefaultColors[5],
						     kDefaultColors[6], kDefaultColors[7]};

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
		d->pDirOut = gs_effect_get_param_by_name(d->effect, "wave_dir_out");
		d->pSoft = gs_effect_get_param_by_name(d->effect, "wave_soft");
		d->pJitter = gs_effect_get_param_by_name(d->effect, "jitter");
		d->pAccent = gs_effect_get_param_by_name(d->effect, "accent");
		d->pStyle = gs_effect_get_param_by_name(d->effect, "style");
		d->pSinglePass = gs_effect_get_param_by_name(d->effect, "single_pass");
		bool colorsOk = true;
		for (int i = 0; i < kColorCount; ++i) {
			const char names[kColorCount][8] = {"color0", "color1", "color2", "color3",
							    "color4", "color5", "color6", "color7"};
			d->pColor[i] = gs_effect_get_param_by_name(d->effect, names[i]);
			colorsOk = colorsOk && d->pColor[i];
		}
		if (!d->pProgress || !d->pSize || !d->pCell || !d->pDotScale || !d->pDir || !d->pDirOut || !d->pSoft ||
		    !d->pJitter || !d->pAccent || !d->pStyle || !d->pSinglePass || !colorsOk) {
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
	d->enterSide = (int)std::clamp<long long>(obs_data_get_int(settings, kEnter), 0, 8);
	d->exitSide = (int)std::clamp<long long>(obs_data_get_int(settings, kExit), 0, 10);
	d->soft = (float)std::clamp<long long>(obs_data_get_int(settings, kSoftness), 2, 100) / 100.0f;
	d->jitter = (float)std::clamp<long long>(obs_data_get_int(settings, kJitter), 0, 100) / 100.0f;
	d->accent = (float)std::clamp<long long>(obs_data_get_int(settings, kAccent), 0, 100) / 100.0f;
	d->style = (float)std::clamp<long long>(obs_data_get_int(settings, kStyle), 0, 2);
	d->singlePass = obs_data_get_int(settings, kRun) == 0 ? 1.0f : 0.0f;
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
	obs_data_set_default_int(settings, kEnter, 0);
	obs_data_set_default_int(settings, kExit, 0);
	obs_data_set_default_int(settings, kSoftness, 35);
	obs_data_set_default_int(settings, kJitter, 30);
	obs_data_set_default_int(settings, kAccent, 60);
	obs_data_set_default_int(settings, kStyle, 2);
	obs_data_set_default_int(settings, kRun, 0);
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
	obs_property_list_add_int(style, obs_module_text("DotsOverlay.Style.Band"), 2);
	obs_property_t *run = obs_properties_add_list(props, kRun, obs_module_text("DotsOverlay.Run"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(run, obs_module_text("DotsOverlay.Run.Single"), 0);
	obs_property_list_add_int(run, obs_module_text("DotsOverlay.Run.Both"), 1);
	obs_properties_add_int_slider(props, kDuration, obs_module_text("DotsOverlay.Duration"), 500, 6000, 50);
	obs_properties_add_int_slider(props, kCell, obs_module_text("DotsOverlay.Cell"), 16, 160, 2);
	obs_properties_add_int_slider(props, kDotSize, obs_module_text("DotsOverlay.DotSize"), 30, 100, 1);
	obs_property_t *enter = obs_properties_add_list(props, kEnter, obs_module_text("DotsOverlay.Enter"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int i = 0; i < 9; ++i)
		obs_property_list_add_int(enter, obs_module_text(kSideNames[i]), i);
	obs_property_t *exit = obs_properties_add_list(props, kExit, obs_module_text("DotsOverlay.Exit"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(exit, obs_module_text("DotsOverlay.Exit.Same"), 0);
	obs_property_list_add_int(exit, obs_module_text("DotsOverlay.Exit.Opposite"), 1);
	for (int i = 0; i < 9; ++i)
		obs_property_list_add_int(exit, obs_module_text(kSideNames[i]), i + 2);
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
	obs_properties_add_color(props, kColors[7], obs_module_text("DotsOverlay.Color8"));
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
	const int enterIdx = d->enterSide.load();
	const int exitIdx = d->exitSide.load();
	vec2 dirIn = sideDir(enterIdx);
	vec2 dirOut = dirIn;
	if (exitIdx == 1)
		vec2_set(&dirOut, -dirIn.x, -dirIn.y);
	else if (exitIdx >= 2)
		dirOut = sideDir(exitIdx - 2);
	gs_effect_set_vec2(d->pDir, &dirIn);
	gs_effect_set_vec2(d->pDirOut, &dirOut);
	gs_effect_set_float(d->pSoft, d->soft.load());
	gs_effect_set_float(d->pJitter, d->jitter.load());
	gs_effect_set_float(d->pAccent, d->accent.load());
	gs_effect_set_float(d->pStyle, d->style.load());
	gs_effect_set_float(d->pSinglePass, d->singlePass.load());
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
