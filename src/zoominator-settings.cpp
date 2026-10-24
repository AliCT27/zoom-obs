/* Settings: defaults, load/save, and string conversions for stored enums. */

#include "zoominator-internal.hpp"

#include <limits>

QString ZoominatorController::zoomAnchorModeToString(ZoomAnchorMode mode)
{
	switch (mode) {
	case ZoomAnchorMode::CursorStatic:
		return QStringLiteral("cursor_static");
	case ZoomAnchorMode::CursorFollow:
		break;
	}
	return QStringLiteral("cursor_follow");
}

ZoominatorController::ZoomAnchorMode ZoominatorController::zoomAnchorModeFromString(const QString &value)
{
	/* "center" (zoom about the canvas centre) no longer exists; saved settings that have it hold still instead. */
	if (value == QLatin1String("center") || value == QLatin1String("cursor_static"))
		return ZoomAnchorMode::CursorStatic;
	return ZoomAnchorMode::CursorFollow;
}

QString ZoominatorController::zoomEasingToString(ZoomEasing easing)
{
	switch (easing) {
	case ZoomEasing::EaseOut:
		return QStringLiteral("ease_out");
	case ZoomEasing::Expo:
		return QStringLiteral("expo");
	case ZoomEasing::Punch:
		return QStringLiteral("punch");
	case ZoomEasing::Smooth:
		break;
	}
	return QStringLiteral("smooth");
}

ZoominatorController::ZoomEasing ZoominatorController::zoomEasingFromString(const QString &value)
{
	if (value == QLatin1String("ease_out"))
		return ZoomEasing::EaseOut;
	if (value == QLatin1String("expo"))
		return ZoomEasing::Expo;
	if (value == QLatin1String("punch"))
		return ZoomEasing::Punch;
	return ZoomEasing::Smooth;
}

QString ZoominatorController::focusStyleToString(FocusStyle style)
{
	switch (style) {
	case FocusStyle::Blur:
		return QStringLiteral("blur");
	case FocusStyle::BlurDim:
		return QStringLiteral("blur_dim");
	case FocusStyle::Dim:
		break;
	}
	return QStringLiteral("dim");
}

ZoominatorController::FocusStyle ZoominatorController::focusStyleFromString(const QString &value)
{
	if (value == QLatin1String("blur"))
		return FocusStyle::Blur;
	if (value == QLatin1String("blur_dim"))
		return FocusStyle::BlurDim;
	return FocusStyle::Dim;
}


QString ZoominatorController::configPath() const
{
	char *path = obs_module_config_path("zoominator.json");
	if (!path)
		return {};
	QString p = QString::fromUtf8(path);
	bfree(path);
	return p;
}

QString ZoominatorController::markerImagePath() const
{
	char *path = obs_module_config_path("zoominator-cursor-marker.png");
	if (!path)
		return {};
	QString p = QString::fromUtf8(path);
	bfree(path);
	return p;
}


/* Simple settings that are just "a number or a switch saved under a key". One line per setting here replaces three
 * copies (default, load, save). Settings that need special handling stay written out by hand in loadSettings /
 * saveSettings. */
namespace {
struct ScalarSetting {
	const char *key;
	bool ZoominatorController::*boolMember;
	int ZoominatorController::*intMember;
	uint32_t ZoominatorController::*uintMember;
	double ZoominatorController::*dblMember;
	double def;
	double lo;
	double hi;
};

constexpr double kNoMin = (double)std::numeric_limits<int>::min();
constexpr double kNoMax = (double)std::numeric_limits<int>::max();

constexpr ScalarSetting kScalarSettings[] = {
	{"show_status_hud", &ZoominatorController::showStatusHud, nullptr, nullptr, nullptr, 0, 0, 1},
	{"status_hud_corner", nullptr, &ZoominatorController::statusHudCorner, nullptr, nullptr, 1, 0, 3},
	{"guide_enabled", &ZoominatorController::guideEnabled, nullptr, nullptr, nullptr, 0, 0, 1},
	{"guide_top_px", nullptr, &ZoominatorController::guideTopPx, nullptr, nullptr, 270, 0, 1500},
	{"guide_bottom_px", nullptr, &ZoominatorController::guideBottomPx, nullptr, nullptr, 670, 0, 1500},
	{"guide_left_px", nullptr, &ZoominatorController::guideLeftPx, nullptr, nullptr, 65, 0, 500},
	{"guide_right_px", nullptr, &ZoominatorController::guideRightPx, nullptr, nullptr, 65, 0, 500},
	{"cursor_ring_enabled", &ZoominatorController::cursorRingEnabled, nullptr, nullptr, nullptr, 0, 0, 1},
	{"cursor_ring_idle", &ZoominatorController::cursorRingIdle, nullptr, nullptr, nullptr, 1, 0, 1},
	{"cursor_ring_size", nullptr, &ZoominatorController::cursorRingSize, nullptr, nullptr, 90, 20, 400},
	{"cursor_ring_px", nullptr, &ZoominatorController::cursorRingPx, nullptr, nullptr, 6, 1, 40},
	{"cursor_ring_color", nullptr, nullptr, &ZoominatorController::cursorRingColor, nullptr, 0xFFFFFFFF, 0, 0},
	{"cursor_ring_rainbow", &ZoominatorController::cursorRingRainbow, nullptr, nullptr, nullptr, 0, 0, 1},
	{"cursor_ring_sec", nullptr, &ZoominatorController::cursorRingSec, nullptr, nullptr, 10, 2, 120},
	{"countdown_default_record", &ZoominatorController::countdownOnDefaultRecord, nullptr, nullptr, nullptr, 0, 0, 1},
	{"countdown_sec", nullptr, &ZoominatorController::countdownSec, nullptr, nullptr, 3, 1, 10},
	{"rec_limit_sec", nullptr, &ZoominatorController::recLimitSec, nullptr, nullptr, 90, 0, 600},
	{"show_cursor_marker", &ZoominatorController::showCursorMarker, nullptr, nullptr, nullptr, 0, 0, 1},
	{"show_marker_when_not_zoomed", &ZoominatorController::showMarkerWhenNotZoomed, nullptr, nullptr, nullptr, 0, 0, 1},
	{"marker_offset_x", nullptr, &ZoominatorController::markerOffsetX, nullptr, nullptr, 6, -100, 100},
	{"marker_offset_y", nullptr, &ZoominatorController::markerOffsetY, nullptr, nullptr, 10, -100, 100},
	{"marker_size", nullptr, &ZoominatorController::markerSize, nullptr, nullptr, 26, kNoMin, kNoMax},
	{"marker_thickness", nullptr, &ZoominatorController::markerThickness, nullptr, nullptr, 4, kNoMin, kNoMax},
	{"frame_enabled", &ZoominatorController::frameEnabled, nullptr, nullptr, nullptr, 0, 0, 1},
	{"frame_color", nullptr, nullptr, &ZoominatorController::frameColor, nullptr, 0xFF2ECC40, 0, 0},
	{"frame_bg_color", nullptr, nullptr, &ZoominatorController::frameBgColor, nullptr, 0xFF000000, 0, 0},
	{"frame_bg_color2", nullptr, nullptr, &ZoominatorController::frameBgColor2, nullptr, 0xFF0B3A52, 0, 0},
	{"frame_bg_mode", nullptr, &ZoominatorController::frameBgMode, nullptr, nullptr, 0, 0, 2},
	{"frame_bg_angle", nullptr, &ZoominatorController::frameBgAngle, nullptr, nullptr, 90, 0, 360},
	{"frame_border_px", nullptr, &ZoominatorController::frameBorderPx, nullptr, nullptr, 4, 0, 80},
	{"frame_radius_px", nullptr, &ZoominatorController::frameRadiusPx, nullptr, nullptr, 24, 0, 400},
	{"frame_glow_px", nullptr, &ZoominatorController::frameGlowPx, nullptr, nullptr, 80, 0, 200},
	{"frame_glow_pct", nullptr, &ZoominatorController::frameGlowPct, nullptr, nullptr, 70, 0, 100},
	{"frame_bg_rainbow", &ZoominatorController::frameBgRainbow, nullptr, nullptr, nullptr, 0, 0, 1},
	{"frame_border_rainbow", &ZoominatorController::frameBorderRainbow, nullptr, nullptr, nullptr, 0, 0, 1},
	{"frame_rainbow_sec", nullptr, &ZoominatorController::frameRainbowSec, nullptr, nullptr, 10, 2, 120},
	{"cam_ring_enabled", &ZoominatorController::camRingEnabled, nullptr, nullptr, nullptr, 0, 0, 1},
	{"cam_ring_color", nullptr, nullptr, &ZoominatorController::camRingColor, nullptr, 0xFF2EA8DC, 0, 0},
	{"cam_ring_px", nullptr, &ZoominatorController::camRingPx, nullptr, nullptr, 6, 0, 60},
	{"cam_glow_px", nullptr, &ZoominatorController::camGlowPx, nullptr, nullptr, 28, 0, 150},
	{"cam_glow_pct", nullptr, &ZoominatorController::camGlowPct, nullptr, nullptr, 80, 0, 100},
	{"cam_rainbow", &ZoominatorController::camRainbow, nullptr, nullptr, nullptr, 0, 0, 1},
	{"cam_rainbow_sec", nullptr, &ZoominatorController::camRainbowSec, nullptr, nullptr, 10, 2, 120},
	{"cam_layout_enabled", &ZoominatorController::camLayoutEnabled, nullptr, nullptr, nullptr, 0, 0, 1},
	{"cam_idle_x", nullptr, &ZoominatorController::camIdleX, nullptr, nullptr, 960, -20000, 20000},
	{"cam_idle_y", nullptr, &ZoominatorController::camIdleY, nullptr, nullptr, 540, -20000, 20000},
	{"cam_idle_size", nullptr, &ZoominatorController::camIdleSize, nullptr, nullptr, 300, 20, 8000},
	{"cam_zoom_x", nullptr, &ZoominatorController::camZoomX, nullptr, nullptr, 960, -20000, 20000},
	{"cam_zoom_y", nullptr, &ZoominatorController::camZoomY, nullptr, nullptr, 540, -20000, 20000},
	{"cam_zoom_size", nullptr, &ZoominatorController::camZoomSize, nullptr, nullptr, 375, 20, 8000},
	{"idle_follow_x", &ZoominatorController::idleFollowX, nullptr, nullptr, nullptr, 0, 0, 1},
	{"tilt3d_enabled", &ZoominatorController::tilt3dEnabled, nullptr, nullptr, nullptr, 0, 0, 1},
	{"tilt3d_max_x", nullptr, nullptr, nullptr, &ZoominatorController::tilt3dMaxX, 12.0, 0.0, 45.0},
	{"tilt3d_max_y", nullptr, nullptr, nullptr, &ZoominatorController::tilt3dMaxY, 8.0, 0.0, 45.0},
	{"tilt3d_perspective", nullptr, nullptr, nullptr, &ZoominatorController::tilt3dPerspective, 2.0, 0.8, 8.0},
	{"tilt3d_invert", &ZoominatorController::tilt3dInvert, nullptr, nullptr, nullptr, 0, 0, 1},
	{"tilt3d_in_zoom", &ZoominatorController::tilt3dInZoom, nullptr, nullptr, nullptr, 1, 0, 1},
	{"auto_on_click", &ZoominatorController::autoOnClick, nullptr, nullptr, nullptr, 1, 0, 1},
	{"auto_on_typing", &ZoominatorController::autoOnTyping, nullptr, nullptr, nullptr, 1, 0, 1},
	{"caret_follow", &ZoominatorController::caretFollow, nullptr, nullptr, nullptr, 1, 0, 1},
	{"auto_hold_ms", nullptr, &ZoominatorController::autoHoldMs, nullptr, nullptr, 2500, 300, 60000},
	{"auto_zoom_out_enabled", &ZoominatorController::autoZoomOutEnabled, nullptr, nullptr, nullptr, 1, 0, 1},
	{"focus_enabled", &ZoominatorController::focusEnabled, nullptr, nullptr, nullptr, 0, 0, 1},
	{"show_focus_when_not_zoomed", &ZoominatorController::showFocusWhenNotZoomed, nullptr, nullptr, nullptr, 0, 0, 1},
	{"focus_dim_pct", nullptr, &ZoominatorController::focusDimPct, nullptr, nullptr, 55, 0, 100},
	{"focus_blur_px", nullptr, &ZoominatorController::focusBlurPx, nullptr, nullptr, 16, 0, 60},
	{"center_focus", &ZoominatorController::centerFocus, nullptr, nullptr, nullptr, 0, 0, 1},
	{"portrait_cover", &ZoominatorController::portraitCover, nullptr, nullptr, nullptr, 1, 0, 1},
	{"debug", &ZoominatorController::debug, nullptr, nullptr, nullptr, 0, 0, 1},
};

void reset_scalar_settings(ZoominatorController &c)
{
	for (const ScalarSetting &d : kScalarSettings) {
		if (d.boolMember)
			c.*d.boolMember = d.def != 0;
		else if (d.intMember)
			c.*d.intMember = (int)d.def;
		else if (d.dblMember)
			c.*d.dblMember = d.def;
		else
			c.*d.uintMember = (uint32_t)d.def;
	}
}

void load_scalar_settings(ZoominatorController &c, obs_data_t *data)
{
	for (const ScalarSetting &d : kScalarSettings) {
		if (!obs_data_has_user_value(data, d.key))
			continue;
		if (d.boolMember)
			c.*d.boolMember = obs_data_get_bool(data, d.key);
		else if (d.intMember)
			c.*d.intMember = std::clamp((int)obs_data_get_int(data, d.key), (int)d.lo, (int)d.hi);
		else if (d.dblMember)
			c.*d.dblMember = clampd(obs_data_get_double(data, d.key), d.lo, d.hi);
		else
			c.*d.uintMember = (uint32_t)obs_data_get_int(data, d.key);
	}
}

void save_scalar_settings(const ZoominatorController &c, obs_data_t *data)
{
	for (const ScalarSetting &d : kScalarSettings) {
		if (d.boolMember)
			obs_data_set_bool(data, d.key, c.*d.boolMember);
		else if (d.intMember)
			obs_data_set_int(data, d.key, c.*d.intMember);
		else if (d.dblMember)
			obs_data_set_double(data, d.key, c.*d.dblMember);
		else
			obs_data_set_int(data, d.key, (long long)(c.*d.uintMember));
	}
}
} // namespace

void ZoominatorController::loadSettings()
{
	screenKey.clear();

	hotkeySequence = QStringLiteral("Ctrl+F1");
	hotkeyMode = QStringLiteral("hold");

	triggerType = QStringLiteral("keyboard");
	mouseButton = QStringLiteral("x1");
	modCtrl = true;
	modAlt = false;
	modShift = false;
	modWin = false;
	modLeftCtrl = false;
	modRightCtrl = false;
	modLeftAlt = false;
	modRightAlt = false;
	modLeftShift = false;
	modRightShift = false;
	modLeftWin = false;
	modRightWin = false;

	zoomFactor = 2.0;
	wheelZoomInStep = 0.20;
	wheelZoomOutStep = 0.20;
	wheelZoomMinimum = 1.0;
	wheelZoomMaximum = 5.0;
	wheelZoomExit = 0.0;
	wheelZoomEnabled = false;
	wheelZoomModifier = QStringLiteral("alt");
	animInMs = 180;
	animOutMs = 320;
	zoomAnchor = ZoomAnchorMode::CursorFollow;
	easing = ZoomEasing::Smooth;
	focusRadius = 300;
	focusIdleRadius = 300;
	focusSoftness = 80;
	focusCone = 50;
	focusStyle = FocusStyle::Dim;
	camSourceName.clear();
	manualZoomInSequence.clear();
	manualZoomOutSequence.clear();
	focusToggleSequence.clear();
	soundEnabled = false;
	for (int i = 0; i < 3; ++i) {
		soundOn[i] = true;
		soundFile[i].clear();
		soundName[i].clear();
	}
	soundVolume[0] = 80;
	soundVolume[1] = 80;
	soundVolume[2] = 60;
	soundTypingMode = QStringLiteral("tick");
	focusOnSequence.clear();
	focusOffSequence.clear();
	followMouseRuntimeEnabled = true;
	followSpeed = 8.0;
	centerCursorUntilEdge = true;
	edgeOverflowMarginPct = 0;
	mouseIdleTimeoutMs = 0;

	reset_scalar_settings(*this);
	markerOnlyOnClick = true;
	markerColor = 0xFFFF0000;
	markerIdleSize = 26;

	const QString p = configPath();
	if (p.isEmpty())
		return;

	QByteArray pUtf8 = p.toUtf8();
	obs_data_t *data = obs_data_create_from_json_file_safe(pUtf8.constData(), "bak");
	if (!data)
		return;

	auto getStr = [&](const char *key) -> QString {
		const char *v = obs_data_get_string(data, key);
		return v ? QString::fromUtf8(v) : QString();
	};

	screenKey = getStr("screen_key");
	if (screenKey.isEmpty())
		screenKey = getStr("source_name");
	hotkeySequence = getStr("hotkey");
	if (hotkeySequence.isEmpty())
		hotkeySequence = QStringLiteral("Ctrl+F1");
	hotkeyMode = getStr("hotkey_mode");
	followToggleHotkeySequence = getStr("follow_toggle_hotkey");
	if (hotkeyMode != "toggle")
		hotkeyMode = "hold";

	triggerType = getStr("trigger_type");
	QKeySequence followSeq(followToggleHotkeySequence);
	if (!followSeq.isEmpty()) {
		const QKeyCombination kc = followSeq[0];
		const auto mods = kc.keyboardModifiers();
		const int key = int(kc.key());
		followToggleHotkeyVk = qtKeyToVk(key);
		followToggleModCtrl = mods.testFlag(Qt::ControlModifier);
		followToggleModAlt = mods.testFlag(Qt::AltModifier);
		followToggleModShift = mods.testFlag(Qt::ShiftModifier);
		followToggleModWin = mods.testFlag(Qt::MetaModifier);

#ifdef _WIN32
		const bool keyIsModifier = (followToggleHotkeyVk == VK_CONTROL || followToggleHotkeyVk == VK_LCONTROL ||
					    followToggleHotkeyVk == VK_RCONTROL || followToggleHotkeyVk == VK_MENU ||
					    followToggleHotkeyVk == VK_LMENU || followToggleHotkeyVk == VK_RMENU ||
					    followToggleHotkeyVk == VK_SHIFT || followToggleHotkeyVk == VK_LSHIFT ||
					    followToggleHotkeyVk == VK_RSHIFT || followToggleHotkeyVk == VK_LWIN ||
					    followToggleHotkeyVk == VK_RWIN);
#elif defined(__APPLE__)
		const bool keyIsModifier =
			(followToggleHotkeyVk == kVK_Control || followToggleHotkeyVk == kVK_RightControl ||
			 followToggleHotkeyVk == kVK_Option || followToggleHotkeyVk == kVK_RightOption ||
			 followToggleHotkeyVk == kVK_Shift || followToggleHotkeyVk == kVK_RightShift ||
			 followToggleHotkeyVk == kVK_Command || followToggleHotkeyVk == kVK_RightCommand);
#elif defined(__linux__)
		const bool keyIsModifier = (followToggleHotkeyVk == XK_Control_L ||
					    followToggleHotkeyVk == XK_Control_R || followToggleHotkeyVk == XK_Alt_L ||
					    followToggleHotkeyVk == XK_Alt_R || followToggleHotkeyVk == XK_Shift_L ||
					    followToggleHotkeyVk == XK_Shift_R || followToggleHotkeyVk == XK_Super_L ||
					    followToggleHotkeyVk == XK_Super_R);
#else
		const bool keyIsModifier = false;
#endif

		if (keyIsModifier && mods == Qt::NoModifier) {
#ifdef _WIN32
			followToggleModCtrl = (followToggleHotkeyVk == VK_CONTROL ||
					       followToggleHotkeyVk == VK_LCONTROL ||
					       followToggleHotkeyVk == VK_RCONTROL);
			followToggleModAlt = (followToggleHotkeyVk == VK_MENU || followToggleHotkeyVk == VK_LMENU ||
					      followToggleHotkeyVk == VK_RMENU);
			followToggleModShift = (followToggleHotkeyVk == VK_SHIFT || followToggleHotkeyVk == VK_LSHIFT ||
						followToggleHotkeyVk == VK_RSHIFT);
			followToggleModWin = (followToggleHotkeyVk == VK_LWIN || followToggleHotkeyVk == VK_RWIN);
#elif defined(__APPLE__)
			followToggleModCtrl =
				(followToggleHotkeyVk == kVK_Control || followToggleHotkeyVk == kVK_RightControl);
			followToggleModAlt =
				(followToggleHotkeyVk == kVK_Option || followToggleHotkeyVk == kVK_RightOption);
			followToggleModShift =
				(followToggleHotkeyVk == kVK_Shift || followToggleHotkeyVk == kVK_RightShift);
			followToggleModWin =
				(followToggleHotkeyVk == kVK_Command || followToggleHotkeyVk == kVK_RightCommand);
#elif defined(__linux__)
			followToggleModCtrl =
				(followToggleHotkeyVk == XK_Control_L || followToggleHotkeyVk == XK_Control_R);
			followToggleModAlt = (followToggleHotkeyVk == XK_Alt_L || followToggleHotkeyVk == XK_Alt_R);
			followToggleModShift =
				(followToggleHotkeyVk == XK_Shift_L || followToggleHotkeyVk == XK_Shift_R);
			followToggleModWin = (followToggleHotkeyVk == XK_Super_L || followToggleHotkeyVk == XK_Super_R);
#else
			followToggleHotkeyVk = 0;
#endif
		}

		followToggleHkValid = (followToggleHotkeyVk != 0) &&
				      (followToggleModCtrl || followToggleModAlt || followToggleModShift ||
				       followToggleModWin || key != 0);
	}

	if (triggerType != "mouse" && triggerType != "auto")
		triggerType = "keyboard";

	mouseButton = getStr("mouse_button");
	if (mouseButton.isEmpty())
		mouseButton = "x1";

	if (obs_data_has_user_value(data, "mod_ctrl"))
		modCtrl = obs_data_get_bool(data, "mod_ctrl");
	if (obs_data_has_user_value(data, "mod_alt"))
		modAlt = obs_data_get_bool(data, "mod_alt");
	if (obs_data_has_user_value(data, "mod_shift"))
		modShift = obs_data_get_bool(data, "mod_shift");
	if (obs_data_has_user_value(data, "mod_win"))
		modWin = obs_data_get_bool(data, "mod_win");
	if (obs_data_has_user_value(data, "mod_left_ctrl"))
		modLeftCtrl = obs_data_get_bool(data, "mod_left_ctrl");
	if (obs_data_has_user_value(data, "mod_right_ctrl"))
		modRightCtrl = obs_data_get_bool(data, "mod_right_ctrl");
	if (obs_data_has_user_value(data, "mod_left_alt"))
		modLeftAlt = obs_data_get_bool(data, "mod_left_alt");
	if (obs_data_has_user_value(data, "mod_right_alt"))
		modRightAlt = obs_data_get_bool(data, "mod_right_alt");
	if (obs_data_has_user_value(data, "mod_left_shift"))
		modLeftShift = obs_data_get_bool(data, "mod_left_shift");
	if (obs_data_has_user_value(data, "mod_right_shift"))
		modRightShift = obs_data_get_bool(data, "mod_right_shift");
	if (obs_data_has_user_value(data, "mod_left_win"))
		modLeftWin = obs_data_get_bool(data, "mod_left_win");
	if (obs_data_has_user_value(data, "mod_right_win"))
		modRightWin = obs_data_get_bool(data, "mod_right_win");

	if (obs_data_has_user_value(data, "zoom_factor"))
		zoomFactor = obs_data_get_double(data, "zoom_factor");

	if (zoomFactor < 0.0)
		zoomFactor = 0.0;
	if (obs_data_has_user_value(data, "wheel_zoom_in_step"))
		wheelZoomInStep = obs_data_get_double(data, "wheel_zoom_in_step");
	if (obs_data_has_user_value(data, "wheel_zoom_out_step"))
		wheelZoomOutStep = obs_data_get_double(data, "wheel_zoom_out_step");
	if (obs_data_has_user_value(data, "wheel_zoom_minimum"))
		wheelZoomMinimum = obs_data_get_double(data, "wheel_zoom_minimum");
	if (obs_data_has_user_value(data, "wheel_zoom_maximum"))
		wheelZoomMaximum = obs_data_get_double(data, "wheel_zoom_maximum");
	if (obs_data_has_user_value(data, "wheel_zoom_exit"))
		wheelZoomExit = obs_data_get_double(data, "wheel_zoom_exit");
	wheelZoomInStep = clampd(wheelZoomInStep, 0.01, 2.0);
	wheelZoomOutStep = wheelZoomInStep;
	wheelZoomMinimum = 1.0;
	if (obs_data_has_user_value(data, "wheel_zoom_enabled"))
		wheelZoomEnabled = obs_data_get_bool(data, "wheel_zoom_enabled");
	if (obs_data_has_user_value(data, "wheel_zoom_modifier"))
		wheelZoomModifier = QString::fromUtf8(obs_data_get_string(data, "wheel_zoom_modifier"));
	if (wheelZoomModifier != "ctrl" && wheelZoomModifier != "shift")
		wheelZoomModifier = QStringLiteral("alt");
	wheelZoomMinimum = clampd(wheelZoomMinimum, 1.0, 19.0);
	wheelZoomMaximum = clampd(wheelZoomMaximum, wheelZoomMinimum + 0.1, 20.0);
	wheelZoomExit = clampd(wheelZoomExit, 0.0, 19.0);

	if (obs_data_has_user_value(data, "anim_in_ms"))
		animInMs = (int)obs_data_get_int(data, "anim_in_ms");
	if (obs_data_has_user_value(data, "anim_out_ms"))
		animOutMs = (int)obs_data_get_int(data, "anim_out_ms");
	if (animInMs < 0)
		animInMs = 180;
	if (animOutMs < 0)
		animOutMs = 180;

	if (obs_data_has_user_value(data, "easing"))
		easing = zoomEasingFromString(getStr("easing"));

	if (obs_data_has_user_value(data, "focus_radius"))
		focusRadius = (int)obs_data_get_int(data, "focus_radius");
	focusIdleRadius = focusRadius;
	if (obs_data_has_user_value(data, "focus_idle_radius"))
		focusIdleRadius = (int)obs_data_get_int(data, "focus_idle_radius");
	/* The beam is set by the light cone alone. Settings from before that carry their radius over to it. */
	const bool hasRadius = obs_data_has_user_value(data, "focus_radius");
	if (obs_data_has_user_value(data, "focus_beam")) {
		focusCone = (int)obs_data_get_int(data, "focus_beam");
		if (!hasRadius)
			focusRadius = beamRadiusForCone(std::clamp(focusCone, 0, 100));
	} else {
		focusCone = coneForBeamRadius(focusRadius);
	}
	if (obs_data_has_user_value(data, "focus_style"))
		focusStyle = focusStyleFromString(getStr("focus_style"));
	focusRadius = std::clamp(focusRadius, 40, 2000);
	focusIdleRadius = std::clamp(focusIdleRadius, 40, 2000);
	focusCone = std::clamp(focusCone, 0, 100);
	focusSoftness = beamSoftnessForCone(focusCone, focusRadius);
	camSourceName = getStr("cam_source");
	manualZoomInSequence = getStr("manual_zoom_in_hotkey");
	manualZoomOutSequence = getStr("manual_zoom_out_hotkey");
	focusToggleSequence = getStr("focus_toggle_hotkey");
	{
		static const char *fileKeys[3] = {"sound_zoom_in_file", "sound_zoom_out_file", "sound_typing_file"};
		static const char *nameKeys[3] = {"sound_zoom_in_name", "sound_zoom_out_name", "sound_typing_name"};
		static const char *onKeys[3] = {"sound_zoom_in_on", "sound_zoom_out_on", "sound_typing_on"};
		static const char *volKeys[3] = {"sound_zoom_in_volume", "sound_zoom_out_volume",
						 "sound_typing_volume"};
		if (obs_data_has_user_value(data, "sound_enabled"))
			soundEnabled = obs_data_get_bool(data, "sound_enabled");
		for (int i = 0; i < 3; ++i) {
			soundFile[i] = getStr(fileKeys[i]);
			soundName[i] = getStr(nameKeys[i]);
			soundOn[i] = obs_data_has_user_value(data, onKeys[i]) ? obs_data_get_bool(data, onKeys[i]) : true;
			if (obs_data_has_user_value(data, volKeys[i]))
				soundVolume[i] = std::clamp((int)obs_data_get_int(data, volKeys[i]), 0, 100);
		}
		soundTypingMode = getStr("sound_typing_mode");
		if (soundTypingMode != "loop")
			soundTypingMode = "tick";
	}
	focusOnSequence = getStr("focus_on_hotkey");
	focusOffSequence = getStr("focus_off_hotkey");

	/* `zoom_anchor` supersedes the `follow_mouse` boolean. Configs written by
	 * older builds only have the boolean, so migrate it: following stays
	 * following, and "off" becomes the hold-still mode. */
	if (obs_data_has_user_value(data, "zoom_anchor"))
		zoomAnchor = zoomAnchorModeFromString(getStr("zoom_anchor"));
	else if (obs_data_has_user_value(data, "follow_mouse"))
		zoomAnchor = obs_data_get_bool(data, "follow_mouse") ? ZoomAnchorMode::CursorFollow
								     : ZoomAnchorMode::CursorStatic;
	followMouseRuntimeEnabled = true;
	if (obs_data_has_user_value(data, "follow_speed"))
		followSpeed = obs_data_get_double(data, "follow_speed");
	if (followSpeed <= 0.1)
		followSpeed = 8.0;
	if (obs_data_has_user_value(data, "center_cursor_until_edge"))
		centerCursorUntilEdge = obs_data_get_bool(data, "center_cursor_until_edge");
	if (obs_data_has_user_value(data, "edge_overflow_margin_pct"))
		edgeOverflowMarginPct = (int)obs_data_get_int(data, "edge_overflow_margin_pct");
	else if (obs_data_has_user_value(data, "allow_canvas_overflow"))
		edgeOverflowMarginPct = obs_data_get_bool(data, "allow_canvas_overflow") ? 50 : 0;
	edgeOverflowMarginPct = 0; /* not offered any more */
	if (obs_data_has_user_value(data, "mouse_idle_timeout_ms"))
		mouseIdleTimeoutMs = (int)obs_data_get_int(data, "mouse_idle_timeout_ms");
	mouseIdleTimeoutMs = 0; /* not offered any more */
	centerCursorUntilEdge = true;

	// Continuous cursor tracking was inaccurate on captures whose aspect ratio
	// differs from the OBS canvas. The marker is intentionally click-only.
	markerOnlyOnClick = true;
	if (obs_data_has_user_value(data, "marker_color"))
		markerColor = (uint32_t)obs_data_get_int(data, "marker_color");
	load_scalar_settings(*this, data); /* marker_size is read here, before the idle size falls back to it */
	markerIdleSize = markerSize;
	if (obs_data_has_user_value(data, "marker_idle_size"))
		markerIdleSize = (int)obs_data_get_int(data, "marker_idle_size");
	markerIdleSize = std::clamp(markerIdleSize, 6, 256);


	savedLayouts.clear();
	if (obs_data_array_t *layArr = obs_data_get_array(data, "unzoomed_layouts")) {
		const size_t n = obs_data_array_count(layArr);
		for (size_t i = 0; i < n; ++i) {
			obs_data_t *e = obs_data_array_item(layArr, i);
			if (!e)
				continue;
			SavedItemLayout l;
			const QString key = QString::fromUtf8(obs_data_get_string(e, "canvas"));
			l.name = QString::fromUtf8(obs_data_get_string(e, "name"));
			l.px = obs_data_get_double(e, "px");
			l.py = obs_data_get_double(e, "py");
			l.sx = obs_data_get_double(e, "sx");
			l.sy = obs_data_get_double(e, "sy");
			l.cropL = (int)obs_data_get_int(e, "crop_l");
			l.cropT = (int)obs_data_get_int(e, "crop_t");
			l.cropR = (int)obs_data_get_int(e, "crop_r");
			l.cropB = (int)obs_data_get_int(e, "crop_b");
			if (!key.isEmpty() && !l.name.isEmpty() && l.sx > 0.0 && l.sy > 0.0)
				savedLayouts[key].push_back(l);
			obs_data_release(e);
		}
		obs_data_array_release(layArr);
	}

	includedSources.clear();
	obs_data_array_t *incArr = obs_data_get_array(data, "included_sources");
	if (incArr) {
		const size_t incCount = obs_data_array_count(incArr);
		for (size_t i = 0; i < incCount; i++) {
			obs_data_t *incItem = obs_data_array_item(incArr, i);
			if (incItem) {
				const char *incName = obs_data_get_string(incItem, "name");
				if (incName && *incName)
					includedSources.insert(QString::fromUtf8(incName));
				obs_data_release(incItem);
			}
		}
		obs_data_array_release(incArr);
	}

	recoveryActive = obs_data_get_bool(data, "recovery_active");
	loadRecoveryMap(data);

	obs_data_release(data);

	logi(debug, "[Zoominator] Loaded settings from: %s", pUtf8.constData());

	rebuildTriggersFromSettings();
	emit settingsChanged();
}

QString ZoominatorController::presetPath(const QString &id) const
{
	/* The two original presets keep their old file names. */
	QString file = QStringLiteral("zoominator-preset-%1.json").arg(id);
	if (id == QStringLiteral("v1"))
		file = QStringLiteral("zoominator-preset.json");
	else if (id == QStringLiteral("h1"))
		file = QStringLiteral("zoominator-preset-horizontal.json");
	char *path = obs_module_config_path(file.toUtf8().constData());
	if (!path)
		return {};
	QString p = QString::fromUtf8(path);
	bfree(path);
	return p;
}

QString ZoominatorController::presetIndexPath() const
{
	char *path = obs_module_config_path("zoominator-presets.json");
	if (!path)
		return {};
	QString p = QString::fromUtf8(path);
	bfree(path);
	return p;
}

bool ZoominatorController::hasUserPreset(const QString &id) const
{
	const QString p = presetPath(id);
	return !p.isEmpty() && QFileInfo::exists(p);
}

static void save_preset_index(const QString &path, const QVector<ZoominatorController::PresetInfo> &list)
{
	if (path.isEmpty())
		return;
	obs_data_t *root = obs_data_create();
	obs_data_array_t *arr = obs_data_array_create();
	for (const auto &pi : list) {
		obs_data_t *row = obs_data_create();
		obs_data_set_string(row, "id", pi.id.toUtf8().constData());
		obs_data_set_string(row, "name", pi.name.toUtf8().constData());
		obs_data_set_bool(row, "horizontal", pi.horizontal);
		obs_data_array_push_back(arr, row);
		obs_data_release(row);
	}
	obs_data_set_array(root, "presets", arr);
	obs_data_array_release(arr);
	ensure_parent_dir_exists(path);
	const QByteArray u = path.toUtf8();
	obs_data_save_json_safe(root, u.constData(), "tmp", "bak");
	obs_data_release(root);
}

/* The list of presets; the first start (or a missing index) seeds the two original ones. */
QVector<ZoominatorController::PresetInfo> ZoominatorController::listPresets()
{
	QVector<PresetInfo> list;
	const QString ip = presetIndexPath();
	if (!ip.isEmpty() && QFileInfo::exists(ip)) {
		const QByteArray u = ip.toUtf8();
		if (obs_data_t *root = obs_data_create_from_json_file_safe(u.constData(), "bak")) {
			if (obs_data_array_t *arr = obs_data_get_array(root, "presets")) {
				const size_t n = obs_data_array_count(arr);
				for (size_t i = 0; i < n; ++i) {
					obs_data_t *row = obs_data_array_item(arr, i);
					if (!row)
						continue;
					PresetInfo pi;
					pi.id = QString::fromUtf8(obs_data_get_string(row, "id"));
					pi.name = QString::fromUtf8(obs_data_get_string(row, "name"));
					pi.horizontal = obs_data_get_bool(row, "horizontal");
					if (!pi.id.isEmpty())
						list.push_back(pi);
					obs_data_release(row);
				}
				obs_data_array_release(arr);
			}
			obs_data_release(root);
			/* The two main presets always exist and come first in their section. */
			bool hasV = false, hasH = false;
			for (const auto &pi : list) {
				hasV = hasV || pi.id == QStringLiteral("v1");
				hasH = hasH || pi.id == QStringLiteral("h1");
			}
			if (!hasH)
				list.push_front({QStringLiteral("h1"), QString(), true});
			if (!hasV)
				list.push_front({QStringLiteral("v1"), QString(), false});
			if (!hasV || !hasH)
				save_preset_index(ip, list);
			return list;
		}
	}
	list.push_back({QStringLiteral("v1"), QString(), false});
	list.push_back({QStringLiteral("h1"), QString(), true});
	save_preset_index(ip, list);
	return list;
}

QString ZoominatorController::addPreset(const QString &name, bool horizontal)
{
	QVector<PresetInfo> list = listPresets();
	const QString id = QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
	list.push_back({id, name.trimmed(), horizontal});
	if (!saveUserPreset(id))
		return {};
	save_preset_index(presetIndexPath(), list);
	return id;
}

bool ZoominatorController::removePreset(const QString &id)
{
	if (id == QStringLiteral("v1") || id == QStringLiteral("h1"))
		return false; /* the main presets cannot be deleted */
	QVector<PresetInfo> list = listPresets();
	bool found = false;
	for (int i = 0; i < list.size(); ++i) {
		if (list[i].id == id) {
			list.removeAt(i);
			found = true;
			break;
		}
	}
	if (!found)
		return false;
	save_preset_index(presetIndexPath(), list);
	const QString pp = presetPath(id);
	if (!pp.isEmpty())
		QFile::remove(pp);
	return true;
}

bool ZoominatorController::renamePreset(const QString &id, const QString &name)
{
	QVector<PresetInfo> list = listPresets();
	for (auto &pi : list) {
		if (pi.id == id) {
			pi.name = name.trimmed();
			save_preset_index(presetIndexPath(), list);
			return true;
		}
	}
	return false;
}

/* Settings that belong to this computer or this moment rather than to a look. */
static void erase_machine_specific_keys(obs_data_t *data)
{
	static const char *const keys[] = {"screen_key", "source_name", "included_sources", "unzoomed_layouts", "recovery_active",
					   "recovery_transforms"};
	for (const char *key : keys)
		obs_data_erase(data, key);
}

bool ZoominatorController::saveUserPreset(const QString &id)
{
	const QString cfg = configPath();
	const QString dst = presetPath(id);
	if (cfg.isEmpty() || dst.isEmpty())
		return false;

	const QByteArray cfgUtf8 = cfg.toUtf8();
	obs_data_t *data = obs_data_create_from_json_file_safe(cfgUtf8.constData(), "bak");
	if (!data)
		return false;
	erase_machine_specific_keys(data);

	ensure_parent_dir_exists(dst);
	const QByteArray dstUtf8 = dst.toUtf8();
	const bool ok = obs_data_save_json_safe(data, dstUtf8.constData(), "tmp", "bak");
	obs_data_release(data);
	if (ok)
		logi(debug, "[Zoominator] Saved preset to: %s", dstUtf8.constData());
	return ok;
}

/* Export: one JSON file with the settings (no target screen / sources / recovery data, those
 * belong to this computer) and both presets. */
bool ZoominatorController::exportSettings(const QString &path)
{
	const QString cfg = configPath();
	if (cfg.isEmpty() || path.isEmpty())
		return false;
	const QByteArray cfgUtf8 = cfg.toUtf8();
	obs_data_t *data = obs_data_create_from_json_file_safe(cfgUtf8.constData(), "bak");
	if (!data)
		return false;
	obs_data_array_t *layouts = obs_data_get_array(data, "unzoomed_layouts");
	erase_machine_specific_keys(data);
	if (layouts) {
		obs_data_set_array(data, "unzoomed_layouts", layouts);
		obs_data_array_release(layouts);
	}

	obs_data_t *bundle = obs_data_create();
	obs_data_set_string(bundle, "zoominator_export", "1");
	obs_data_set_obj(bundle, "settings", data);
	obs_data_array_t *presetArr = obs_data_array_create();
	for (const PresetInfo &pi : listPresets()) {
		const QString pp = presetPath(pi.id);
		if (pp.isEmpty() || !QFileInfo::exists(pp))
			continue;
		const QByteArray ppUtf8 = pp.toUtf8();
		if (obs_data_t *preset = obs_data_create_from_json_file_safe(ppUtf8.constData(), "bak")) {
			obs_data_t *row = obs_data_create();
			obs_data_set_string(row, "id", pi.id.toUtf8().constData());
			obs_data_set_string(row, "name", pi.name.toUtf8().constData());
			obs_data_set_bool(row, "horizontal", pi.horizontal);
			obs_data_set_obj(row, "data", preset);
			obs_data_array_push_back(presetArr, row);
			obs_data_release(row);
			obs_data_release(preset);
		}
	}
	obs_data_set_array(bundle, "presets", presetArr);
	obs_data_array_release(presetArr);
	obs_data_release(data);

	const QByteArray outUtf8 = path.toUtf8();
	const bool ok = obs_data_save_json_safe(bundle, outUtf8.constData(), "tmp", "bak");
	obs_data_release(bundle);
	return ok;
}

/* Import: lays the file over the current settings (target screen and sources stay) and
 * restores the two presets it carries. Also accepts a plain settings file. */
bool ZoominatorController::importSettings(const QString &path)
{
	const QString cfg = configPath();
	if (cfg.isEmpty() || path.isEmpty() || !QFileInfo::exists(path))
		return false;
	const QByteArray inUtf8 = path.toUtf8();
	obs_data_t *bundle = obs_data_create_from_json_file_safe(inUtf8.constData(), "bak");
	if (!bundle)
		return false;

	obs_data_t *settings = obs_data_get_obj(bundle, "settings");
	if (!settings) {
		settings = bundle;
		obs_data_addref(settings);
	}
	obs_data_array_t *layouts = obs_data_get_array(settings, "unzoomed_layouts");
	erase_machine_specific_keys(settings);

	const QByteArray cfgUtf8 = cfg.toUtf8();
	obs_data_t *data = obs_data_create_from_json_file_safe(cfgUtf8.constData(), "bak");
	if (!data)
		data = obs_data_create();
	obs_data_apply(data, settings);
	if (layouts) {
		obs_data_set_array(data, "unzoomed_layouts", layouts);
		obs_data_array_release(layouts);
	}
	ensure_parent_dir_exists(cfg);
	bool ok = obs_data_save_json_safe(data, cfgUtf8.constData(), "tmp", "bak");
	obs_data_release(data);
	obs_data_release(settings);

	if (ok) {
		QVector<PresetInfo> list = listPresets();
		auto upsert = [&](const QString &id, const QString &name, bool horizontal, obs_data_t *preset) {
			if (id.isEmpty() || !preset)
				return;
			const QString pp = presetPath(id);
			if (pp.isEmpty())
				return;
			ensure_parent_dir_exists(pp);
			const QByteArray ppUtf8 = pp.toUtf8();
			obs_data_save_json_safe(preset, ppUtf8.constData(), "tmp", "bak");
			for (auto &pi : list) {
				if (pi.id == id) {
					pi.name = name;
					pi.horizontal = horizontal;
					return;
				}
			}
			list.push_back({id, name, horizontal});
		};
		if (obs_data_array_t *arr = obs_data_get_array(bundle, "presets")) {
			const size_t n = obs_data_array_count(arr);
			for (size_t i = 0; i < n; ++i) {
				obs_data_t *row = obs_data_array_item(arr, i);
				if (!row)
					continue;
				obs_data_t *pd = obs_data_get_obj(row, "data");
				upsert(QString::fromUtf8(obs_data_get_string(row, "id")),
				       QString::fromUtf8(obs_data_get_string(row, "name")), obs_data_get_bool(row, "horizontal"),
				       pd);
				if (pd)
					obs_data_release(pd);
				obs_data_release(row);
			}
			obs_data_array_release(arr);
		}
		/* Files from before presets could be added. */
		for (int i = 0; i < 2; ++i) {
			obs_data_t *preset = obs_data_get_obj(bundle, i == 1 ? "preset_horizontal" : "preset_vertical");
			if (!preset)
				continue;
			upsert(i == 1 ? QStringLiteral("h1") : QStringLiteral("v1"), QString(), i == 1, preset);
			obs_data_release(preset);
		}
		save_preset_index(presetIndexPath(), list);
	}
	obs_data_release(bundle);
	if (!ok)
		return false;

	loadSettings();
	rebuildRuntimeHooks();
	return true;
}

/* Lays the saved preset over the current settings file and reloads it, so the
 * target screen and ticked sources stay as they are. */
bool ZoominatorController::applyUserPreset(const QString &id)
{
	const QString cfg = configPath();
	const QString src = presetPath(id);
	if (cfg.isEmpty() || src.isEmpty() || !QFileInfo::exists(src))
		return false;

	const QByteArray srcUtf8 = src.toUtf8();
	obs_data_t *preset = obs_data_create_from_json_file_safe(srcUtf8.constData(), "bak");
	if (!preset)
		return false;
	erase_machine_specific_keys(preset);

	const QByteArray cfgUtf8 = cfg.toUtf8();
	obs_data_t *data = obs_data_create_from_json_file_safe(cfgUtf8.constData(), "bak");
	if (!data)
		data = obs_data_create();
	obs_data_apply(data, preset);
	ensure_parent_dir_exists(cfg);
	const bool ok = obs_data_save_json_safe(data, cfgUtf8.constData(), "tmp", "bak");
	obs_data_release(data);
	obs_data_release(preset);
	if (!ok)
		return false;

	loadSettings();
	rebuildRuntimeHooks();
	return true;
}

void ZoominatorController::notifySettingsChanged()
{
	emit settingsChanged();
}

void ZoominatorController::saveSettings()
{
	const QString p = configPath();
	if (p.isEmpty())
		return;

	ensure_parent_dir_exists(p);

	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "screen_key", screenKey.toUtf8().constData());
	obs_data_set_string(data, "hotkey", hotkeySequence.toUtf8().constData());
	obs_data_set_string(data, "hotkey_mode", hotkeyMode.toUtf8().constData());
	obs_data_set_string(data, "follow_toggle_hotkey", followToggleHotkeySequence.toUtf8().constData());

	obs_data_set_string(data, "trigger_type", triggerType.toUtf8().constData());
	obs_data_set_string(data, "mouse_button", mouseButton.toUtf8().constData());
	obs_data_set_bool(data, "mod_ctrl", modCtrl);
	obs_data_set_bool(data, "mod_alt", modAlt);
	obs_data_set_bool(data, "mod_shift", modShift);
	obs_data_set_bool(data, "mod_win", modWin);
	obs_data_set_bool(data, "mod_left_ctrl", modLeftCtrl);
	obs_data_set_bool(data, "mod_right_ctrl", modRightCtrl);
	obs_data_set_bool(data, "mod_left_alt", modLeftAlt);
	obs_data_set_bool(data, "mod_right_alt", modRightAlt);
	obs_data_set_bool(data, "mod_left_shift", modLeftShift);
	obs_data_set_bool(data, "mod_right_shift", modRightShift);
	obs_data_set_bool(data, "mod_left_win", modLeftWin);
	obs_data_set_bool(data, "mod_right_win", modRightWin);

	obs_data_set_double(data, "zoom_factor", zoomFactor);
	obs_data_set_double(data, "wheel_zoom_in_step", wheelZoomInStep);
	obs_data_set_double(data, "wheel_zoom_out_step", wheelZoomOutStep);
	obs_data_set_double(data, "wheel_zoom_minimum", wheelZoomMinimum);
	obs_data_set_double(data, "wheel_zoom_maximum", wheelZoomMaximum);
	obs_data_set_double(data, "wheel_zoom_exit", wheelZoomExit);
	obs_data_set_bool(data, "wheel_zoom_enabled", wheelZoomEnabled);
	obs_data_set_string(data, "wheel_zoom_modifier", wheelZoomModifier.toUtf8().constData());
	obs_data_set_int(data, "anim_in_ms", animInMs);
	obs_data_set_int(data, "anim_out_ms", animOutMs);
	obs_data_set_string(data, "zoom_anchor", zoomAnchorModeToString(zoomAnchor).toUtf8().constData());
	obs_data_set_string(data, "easing", zoomEasingToString(easing).toUtf8().constData());
	obs_data_set_int(data, "focus_radius", focusRadius);
	obs_data_set_int(data, "focus_idle_radius", focusIdleRadius);
	obs_data_set_int(data, "focus_beam", focusCone);
	obs_data_set_string(data, "focus_style", focusStyleToString(focusStyle).toUtf8().constData());
	obs_data_set_string(data, "cam_source", camSourceName.toUtf8().constData());
	obs_data_set_string(data, "manual_zoom_in_hotkey", manualZoomInSequence.toUtf8().constData());
	obs_data_set_string(data, "manual_zoom_out_hotkey", manualZoomOutSequence.toUtf8().constData());
	obs_data_set_string(data, "focus_toggle_hotkey", focusToggleSequence.toUtf8().constData());
	obs_data_set_bool(data, "sound_enabled", soundEnabled);
	obs_data_set_string(data, "sound_zoom_in_file", soundFile[0].toUtf8().constData());
	obs_data_set_string(data, "sound_zoom_out_file", soundFile[1].toUtf8().constData());
	obs_data_set_string(data, "sound_typing_file", soundFile[2].toUtf8().constData());
	obs_data_set_string(data, "sound_zoom_in_name", soundName[0].toUtf8().constData());
	obs_data_set_string(data, "sound_zoom_out_name", soundName[1].toUtf8().constData());
	obs_data_set_string(data, "sound_typing_name", soundName[2].toUtf8().constData());
	obs_data_set_bool(data, "sound_zoom_in_on", soundOn[0]);
	obs_data_set_bool(data, "sound_zoom_out_on", soundOn[1]);
	obs_data_set_bool(data, "sound_typing_on", soundOn[2]);
	obs_data_set_int(data, "sound_zoom_in_volume", soundVolume[0]);
	obs_data_set_int(data, "sound_zoom_out_volume", soundVolume[1]);
	obs_data_set_int(data, "sound_typing_volume", soundVolume[2]);
	obs_data_set_string(data, "sound_typing_mode", soundTypingMode.toUtf8().constData());
	obs_data_set_string(data, "focus_on_hotkey", focusOnSequence.toUtf8().constData());
	obs_data_set_string(data, "focus_off_hotkey", focusOffSequence.toUtf8().constData());
	/* Kept so a downgrade to an older build still finds a setting it understands. */
	obs_data_set_bool(data, "follow_mouse", zoomAnchor == ZoomAnchorMode::CursorFollow);
	followMouseRuntimeEnabled = true;
	obs_data_set_double(data, "follow_speed", followSpeed);
	obs_data_set_bool(data, "center_cursor_until_edge", centerCursorUntilEdge);
	obs_data_set_int(data, "edge_overflow_margin_pct", edgeOverflowMarginPct);
	obs_data_set_bool(data, "allow_canvas_overflow", edgeOverflowMarginPct > 0);
	obs_data_set_int(data, "mouse_idle_timeout_ms", mouseIdleTimeoutMs);
	save_scalar_settings(*this, data);
	obs_data_set_bool(data, "marker_only_on_click", true);
	obs_data_set_int(data, "marker_color", (long long)markerColor);
	obs_data_set_int(data, "marker_idle_size", markerIdleSize);
	obs_data_set_bool(data, "recovery_active", recoveryActive);
	saveRecoveryMap(data);

	obs_data_array_t *incArr = obs_data_array_create();
	for (const QString &incName : includedSources) {
		obs_data_t *incItem = obs_data_create();
		obs_data_set_string(incItem, "name", incName.toUtf8().constData());
		obs_data_array_push_back(incArr, incItem);
		obs_data_release(incItem);
	}
	obs_data_set_array(data, "included_sources", incArr);
	obs_data_array_release(incArr);

	{
		obs_data_array_t *layArr = obs_data_array_create();
		for (auto it = savedLayouts.constBegin(); it != savedLayouts.constEnd(); ++it) {
			for (const SavedItemLayout &l : it.value()) {
				obs_data_t *e = obs_data_create();
				obs_data_set_string(e, "canvas", it.key().toUtf8().constData());
				obs_data_set_string(e, "name", l.name.toUtf8().constData());
				obs_data_set_double(e, "px", l.px);
				obs_data_set_double(e, "py", l.py);
				obs_data_set_double(e, "sx", l.sx);
				obs_data_set_double(e, "sy", l.sy);
				obs_data_set_int(e, "crop_l", l.cropL);
				obs_data_set_int(e, "crop_t", l.cropT);
				obs_data_set_int(e, "crop_r", l.cropR);
				obs_data_set_int(e, "crop_b", l.cropB);
				obs_data_array_push_back(layArr, e);
				obs_data_release(e);
			}
		}
		obs_data_set_array(data, "unzoomed_layouts", layArr);
		obs_data_array_release(layArr);
	}

	QByteArray pUtf8 = p.toUtf8();
	obs_data_save_json_safe(data, pUtf8.constData(), "tmp", "bak");
	obs_data_release(data);

	logi(debug, "[Zoominator] Saved settings to: %s", pUtf8.constData());

	rebuildTriggersFromSettings();
	emit settingsChanged();
}

