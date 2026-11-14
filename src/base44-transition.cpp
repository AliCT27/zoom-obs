#include "base44-transition.hpp"

#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>

#include <algorithm>
#include <cmath>

namespace zoominator_base44 {
namespace {

constexpr const char *kId = "zoominator_base44_transition";
constexpr const char *kEffectFile = "base44_transition.effect";

constexpr const char *kCell = "cell_px";
constexpr const char *kDotSize = "dot_size_pct";
constexpr const char *kShape = "wave_shape";
constexpr const char *kAngle = "angle_deg";
constexpr const char *kSoftness = "softness_pct";
constexpr const char *kJitter = "jitter_pct";
constexpr const char *kPasses = "passes";
constexpr const char *kPassDelay = "pass_delay_pct";
constexpr const char *kWhite = "white_pct";
constexpr const char *kColor1 = "color1";
constexpr const char *kColor2 = "color2";
constexpr const char *kBackground = "background";

struct Base44Data {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;

	gs_eparam_t *pA = nullptr;
	gs_eparam_t *pB = nullptr;
	gs_eparam_t *pProgress = nullptr;
	gs_eparam_t *pSize = nullptr;
	gs_eparam_t *pCell = nullptr;
	gs_eparam_t *pDotScale = nullptr;
	gs_eparam_t *pShape = nullptr;
	gs_eparam_t *pDir = nullptr;
	gs_eparam_t *pSoft = nullptr;
	gs_eparam_t *pJitter = nullptr;
	gs_eparam_t *pPasses = nullptr;
	gs_eparam_t *pPassDelay = nullptr;
	gs_eparam_t *pColorA = nullptr;
	gs_eparam_t *pColorB = nullptr;
	gs_eparam_t *pBg = nullptr;
	gs_eparam_t *pWhite = nullptr;

	float cell = 48.0f;
	float dotScale = 0.55f;
	float shape = 0.0f;
	float angleRad = 0.78539816f;
	float soft = 0.35f;
	float jitter = 0.2f;
	float passes = 5.0f;
	float passDelay = 0.06f;
	float white = 0.7f;
	vec3 colorA{};
	vec3 colorB{};
	vec3 bg{};
};

/* OBS stores a colour setting as 0xAABBGGRR. */
void unpackColor(long long v, vec3 *out)
{
	vec3_set(out, (float)(v & 0xFF) / 255.0f, (float)((v >> 8) & 0xFF) / 255.0f, (float)((v >> 16) & 0xFF) / 255.0f);
}

const char *getName(void *)
{
	return obs_module_text("Base44.Name");
}

void loadEffect(Base44Data *d)
{
	char *path = obs_module_file(kEffectFile);
	if (!path) {
		blog(LOG_WARNING, "[Zoominator] Base44 transition: could not locate %s (is the plugin data folder installed?)",
		     kEffectFile);
		return;
	}

	obs_enter_graphics();
	char *error = nullptr;
	d->effect = gs_effect_create_from_file(path, &error);
	if (!d->effect) {
		blog(LOG_WARNING, "[Zoominator] Base44 transition: shader failed to load from %s: %s", path,
		     error ? error : "(no message)");
	} else {
		d->pA = gs_effect_get_param_by_name(d->effect, "tex_a");
		d->pB = gs_effect_get_param_by_name(d->effect, "tex_b");
		d->pProgress = gs_effect_get_param_by_name(d->effect, "progress");
		d->pSize = gs_effect_get_param_by_name(d->effect, "size_px");
		d->pCell = gs_effect_get_param_by_name(d->effect, "cell_px");
		d->pDotScale = gs_effect_get_param_by_name(d->effect, "dot_scale");
		d->pDir = gs_effect_get_param_by_name(d->effect, "wave_dir");
		/* Optional: an older shader only has the diagonal wave. */
		d->pShape = gs_effect_get_param_by_name(d->effect, "wave_shape");
		d->pSoft = gs_effect_get_param_by_name(d->effect, "wave_soft");
		d->pJitter = gs_effect_get_param_by_name(d->effect, "jitter");
		d->pPasses = gs_effect_get_param_by_name(d->effect, "passes");
		d->pPassDelay = gs_effect_get_param_by_name(d->effect, "pass_delay");
		d->pColorA = gs_effect_get_param_by_name(d->effect, "color_a");
		d->pColorB = gs_effect_get_param_by_name(d->effect, "color_b");
		d->pBg = gs_effect_get_param_by_name(d->effect, "bg_color");
		d->pWhite = gs_effect_get_param_by_name(d->effect, "white_mix");
		if (!d->pA || !d->pB || !d->pProgress || !d->pSize || !d->pCell || !d->pDotScale || !d->pDir || !d->pSoft ||
		    !d->pJitter || !d->pPasses || !d->pPassDelay || !d->pColorA || !d->pColorB || !d->pBg || !d->pWhite) {
			blog(LOG_WARNING, "[Zoominator] Base44 transition: %s is missing expected parameters", kEffectFile);
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
	auto *d = static_cast<Base44Data *>(data);
	d->cell = (float)std::clamp<long long>(obs_data_get_int(settings, kCell), 8, 400);
	d->dotScale = (float)std::clamp<long long>(obs_data_get_int(settings, kDotSize), 5, 100) / 100.0f;
	d->shape = (float)std::clamp<long long>(obs_data_get_int(settings, kShape), 0, 2);
	d->angleRad = (float)obs_data_get_int(settings, kAngle) * 3.14159265f / 180.0f;
	d->soft = (float)std::clamp<long long>(obs_data_get_int(settings, kSoftness), 2, 100) / 100.0f;
	d->jitter = (float)std::clamp<long long>(obs_data_get_int(settings, kJitter), 0, 100) / 100.0f;
	d->passes = (float)std::clamp<long long>(obs_data_get_int(settings, kPasses), 0, 5);
	d->passDelay = (float)std::clamp<long long>(obs_data_get_int(settings, kPassDelay), 0, 20) / 100.0f;
	d->white = (float)std::clamp<long long>(obs_data_get_int(settings, kWhite), 0, 100) / 100.0f;
	unpackColor(obs_data_get_int(settings, kColor1), &d->colorA);
	unpackColor(obs_data_get_int(settings, kColor2), &d->colorB);
	unpackColor(obs_data_get_int(settings, kBackground), &d->bg);
}

void *createData(obs_data_t *settings, obs_source_t *source)
{
	auto *d = new Base44Data();
	d->source = source;
	loadEffect(d);
	update(d, settings);
	return d;
}

void destroyData(void *data)
{
	auto *d = static_cast<Base44Data *>(data);
	if (!d)
		return;
	if (d->effect) {
		obs_enter_graphics();
		gs_effect_destroy(d->effect);
		obs_leave_graphics();
	}
	delete d;
}

void getDefaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, kCell, 48);
	obs_data_set_default_int(settings, kDotSize, 55);
	obs_data_set_default_int(settings, kShape, 0);
	obs_data_set_default_int(settings, kAngle, 45);
	obs_data_set_default_int(settings, kSoftness, 35);
	obs_data_set_default_int(settings, kJitter, 20);
	obs_data_set_default_int(settings, kPasses, 5);
	obs_data_set_default_int(settings, kPassDelay, 6);
	obs_data_set_default_int(settings, kWhite, 70);
	obs_data_set_default_int(settings, kColor1, 0xFF1E8CFF);     /* orange (RGB 255, 140, 30) */
	obs_data_set_default_int(settings, kColor2, 0xFF6EDC3C);     /* green  (RGB 60, 220, 110) */
	obs_data_set_default_int(settings, kBackground, 0xFF0C0808); /* near black */
}

obs_properties_t *getProperties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_int_slider(props, kCell, obs_module_text("Base44.Cell"), 16, 160, 2);
	obs_properties_add_int_slider(props, kDotSize, obs_module_text("Base44.DotSize"), 20, 100, 1);
	obs_property_t *shape = obs_properties_add_list(props, kShape, obs_module_text("Base44.Shape"), OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(shape, obs_module_text("Base44.Shape.Diagonal"), 0);
	obs_property_list_add_int(shape, obs_module_text("Base44.Shape.CircleOut"), 1);
	obs_property_list_add_int(shape, obs_module_text("Base44.Shape.CircleIn"), 2);
	obs_properties_add_int_slider(props, kAngle, obs_module_text("Base44.Angle"), -180, 180, 5);
	obs_properties_add_int_slider(props, kSoftness, obs_module_text("Base44.Softness"), 5, 80, 1);
	obs_properties_add_int_slider(props, kJitter, obs_module_text("Base44.Jitter"), 0, 60, 1);
	obs_properties_add_int_slider(props, kPasses, obs_module_text("Base44.Passes"), 0, 5, 1);
	obs_properties_add_int_slider(props, kPassDelay, obs_module_text("Base44.PassDelay"), 0, 20, 1);
	obs_properties_add_int_slider(props, kWhite, obs_module_text("Base44.White"), 0, 100, 1);
	obs_properties_add_color(props, kColor1, obs_module_text("Base44.Color1"));
	obs_properties_add_color(props, kColor2, obs_module_text("Base44.Color2"));
	obs_properties_add_color(props, kBackground, obs_module_text("Base44.Background"));
	return props;
}

void renderCallback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t cx, uint32_t cy)
{
	auto *d = static_cast<Base44Data *>(data);
	if (!d->effect)
		return;

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	gs_effect_set_texture_srgb(d->pA, a);
	gs_effect_set_texture_srgb(d->pB, b);
	gs_effect_set_float(d->pProgress, std::clamp(t, 0.0f, 1.0f));

	vec2 size;
	vec2_set(&size, (float)cx, (float)cy);
	gs_effect_set_vec2(d->pSize, &size);
	gs_effect_set_float(d->pCell, d->cell);
	gs_effect_set_float(d->pDotScale, d->dotScale);
	vec2 dir;
	vec2_set(&dir, std::cos(d->angleRad), std::sin(d->angleRad));
	gs_effect_set_vec2(d->pDir, &dir);
	if (d->pShape)
		gs_effect_set_float(d->pShape, d->shape);
	gs_effect_set_float(d->pSoft, d->soft);
	gs_effect_set_float(d->pJitter, d->jitter);
	gs_effect_set_float(d->pPasses, d->passes);
	gs_effect_set_float(d->pPassDelay, d->passDelay);
	gs_effect_set_vec3(d->pColorA, &d->colorA);
	gs_effect_set_vec3(d->pColorB, &d->colorB);
	gs_effect_set_vec3(d->pBg, &d->bg);
	gs_effect_set_float(d->pWhite, d->white);

	while (gs_effect_loop(d->effect, "Base44"))
		gs_draw_sprite(nullptr, 0, cx, cy);

	gs_enable_framebuffer_srgb(previous);
}

void videoRender(void *data, gs_effect_t *)
{
	auto *d = static_cast<Base44Data *>(data);
	obs_transition_video_render(d->source, renderCallback);
}

float mixA(void *, float t)
{
	return 1.0f - t;
}

float mixB(void *, float t)
{
	return t;
}

bool audioRender(void *data, uint64_t *tsOut, obs_source_audio_mix *audio, uint32_t mixers, size_t channels,
		 size_t sampleRate)
{
	auto *d = static_cast<Base44Data *>(data);
	return obs_transition_audio_render(d->source, tsOut, audio, mixers, channels, sampleRate, mixA, mixB);
}

gs_color_space getColorSpace(void *data, size_t, const gs_color_space *)
{
	auto *d = static_cast<Base44Data *>(data);
	return obs_transition_video_get_color_space(d->source);
}

} // namespace

void registerTransition()
{
	obs_source_info info = {};
	info.id = kId;
	info.type = OBS_SOURCE_TYPE_TRANSITION;
	info.get_name = getName;
	info.create = createData;
	info.destroy = destroyData;
	info.update = update;
	info.get_defaults = getDefaults;
	info.get_properties = getProperties;
	info.video_render = videoRender;
	info.audio_render = audioRender;
	info.video_get_color_space = getColorSpace;
	obs_register_source(&info);
}

} // namespace zoominator_base44
