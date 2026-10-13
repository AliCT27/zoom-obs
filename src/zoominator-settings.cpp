/* Settings: defaults, load/save, and string conversions for stored enums. */

#include "zoominator-internal.hpp"

QString ZoominatorController::zoomAnchorModeToString(ZoomAnchorMode mode)
{
	switch (mode) {
	case ZoomAnchorMode::CanvasCenter:
		return QStringLiteral("center");
	case ZoomAnchorMode::CursorStatic:
		return QStringLiteral("cursor_static");
	case ZoomAnchorMode::CursorFollow:
		break;
	}
	return QStringLiteral("cursor_follow");
}

ZoominatorController::ZoomAnchorMode ZoominatorController::zoomAnchorModeFromString(const QString &value)
{
	if (value == QLatin1String("center"))
		return ZoomAnchorMode::CanvasCenter;
	if (value == QLatin1String("cursor_static"))
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
	animInMs = 180;
	animOutMs = 320;
	zoomAnchor = ZoomAnchorMode::CursorFollow;
	easing = ZoomEasing::Smooth;
	autoOnClick = true;
	autoOnTyping = true;
	autoHoldMs = 2500;
	caretFollow = true;
	focusEnabled = false;
	showFocusWhenNotZoomed = false;
	focusDimPct = 55;
	focusRadius = 300;
	focusSoftness = 80;
	focusStyle = FocusStyle::Dim;
	focusBlurPx = 16;
	frameEnabled = false;
	frameColor = 0xFF2ECC40;
	frameBgColor = 0xFF000000;
	frameBgColor2 = 0xFF0B3A52;
	frameBgMode = 0;
	frameBgAngle = 90;
	camRingEnabled = false;
	camSourceName.clear();
	camRingColor = 0xFF2EA8DC;
	camRingPx = 6;
	camGlowPx = 28;
	camGlowPct = 80;
	camZoomPct = 125;
	frameBorderPx = 4;
	frameRadiusPx = 24;
	frameGlowPx = 80;
	frameGlowPct = 70;
	autoZoomOutEnabled = true;
	manualZoomInSequence.clear();
	manualZoomOutSequence.clear();
	focusToggleSequence.clear();
	focusOnSequence.clear();
	focusOffSequence.clear();
	followMouseRuntimeEnabled = true;
	followSpeed = 8.0;
	centerCursorUntilEdge = true;
	edgeOverflowMarginPct = 0;
	centerFocus = false;
	mouseIdleTimeoutMs = 0;
	portraitCover = true;
	showCursorMarker = false;
	showMarkerWhenNotZoomed = false;
	markerOnlyOnClick = true;
	markerColor = 0xFFFF0000;
	markerSize = 26;
	markerThickness = 4;
	debug = false;

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
	wheelZoomInStep = clampd(wheelZoomInStep, 0.01, 2.0);
	wheelZoomOutStep = clampd(wheelZoomOutStep, 0.01, 2.0);
	wheelZoomMinimum = clampd(wheelZoomMinimum, 1.0, 19.0);
	wheelZoomMaximum = clampd(wheelZoomMaximum, wheelZoomMinimum + 0.1, 20.0);

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
	if (obs_data_has_user_value(data, "auto_on_click"))
		autoOnClick = obs_data_get_bool(data, "auto_on_click");
	if (obs_data_has_user_value(data, "auto_on_typing"))
		autoOnTyping = obs_data_get_bool(data, "auto_on_typing");
	if (obs_data_has_user_value(data, "caret_follow"))
		caretFollow = obs_data_get_bool(data, "caret_follow");
	if (obs_data_has_user_value(data, "auto_hold_ms"))
		autoHoldMs = (int)obs_data_get_int(data, "auto_hold_ms");
	autoHoldMs = std::clamp(autoHoldMs, 300, 60000);

	if (obs_data_has_user_value(data, "show_focus_when_not_zoomed"))
		showFocusWhenNotZoomed = obs_data_get_bool(data, "show_focus_when_not_zoomed");
	if (obs_data_has_user_value(data, "focus_enabled"))
		focusEnabled = obs_data_get_bool(data, "focus_enabled");
	if (obs_data_has_user_value(data, "focus_dim_pct"))
		focusDimPct = (int)obs_data_get_int(data, "focus_dim_pct");
	if (obs_data_has_user_value(data, "focus_radius"))
		focusRadius = (int)obs_data_get_int(data, "focus_radius");
	if (obs_data_has_user_value(data, "focus_softness"))
		focusSoftness = (int)obs_data_get_int(data, "focus_softness");
	if (obs_data_has_user_value(data, "focus_style"))
		focusStyle = focusStyleFromString(getStr("focus_style"));
	if (obs_data_has_user_value(data, "focus_blur_px"))
		focusBlurPx = std::clamp((int)obs_data_get_int(data, "focus_blur_px"), 2, 60);
	focusDimPct = std::clamp(focusDimPct, 0, 95);
	focusRadius = std::clamp(focusRadius, 40, 2000);
	focusSoftness = std::clamp(focusSoftness, 0, 600);
	if (obs_data_has_user_value(data, "cam_ring_enabled"))
		camRingEnabled = obs_data_get_bool(data, "cam_ring_enabled");
	camSourceName = getStr("cam_source");
	if (obs_data_has_user_value(data, "cam_ring_color"))
		camRingColor = (uint32_t)obs_data_get_int(data, "cam_ring_color");
	if (obs_data_has_user_value(data, "cam_ring_px"))
		camRingPx = std::clamp((int)obs_data_get_int(data, "cam_ring_px"), 0, 60);
	if (obs_data_has_user_value(data, "cam_glow_px"))
		camGlowPx = std::clamp((int)obs_data_get_int(data, "cam_glow_px"), 0, 150);
	if (obs_data_has_user_value(data, "cam_glow_pct"))
		camGlowPct = std::clamp((int)obs_data_get_int(data, "cam_glow_pct"), 0, 100);
	if (obs_data_has_user_value(data, "cam_zoom_pct"))
		camZoomPct = std::clamp((int)obs_data_get_int(data, "cam_zoom_pct"), 30, 300);
	if (obs_data_has_user_value(data, "frame_enabled"))
		frameEnabled = obs_data_get_bool(data, "frame_enabled");
	if (obs_data_has_user_value(data, "frame_color"))
		frameColor = (uint32_t)obs_data_get_int(data, "frame_color");
	if (obs_data_has_user_value(data, "frame_bg_color"))
		frameBgColor = (uint32_t)obs_data_get_int(data, "frame_bg_color");
	if (obs_data_has_user_value(data, "frame_bg_color2"))
		frameBgColor2 = (uint32_t)obs_data_get_int(data, "frame_bg_color2");
	if (obs_data_has_user_value(data, "frame_bg_mode"))
		frameBgMode = std::clamp((int)obs_data_get_int(data, "frame_bg_mode"), 0, 2);
	if (obs_data_has_user_value(data, "frame_bg_angle"))
		frameBgAngle = std::clamp((int)obs_data_get_int(data, "frame_bg_angle"), 0, 360);
	if (obs_data_has_user_value(data, "frame_border_px"))
		frameBorderPx = std::clamp((int)obs_data_get_int(data, "frame_border_px"), 0, 80);
	if (obs_data_has_user_value(data, "frame_radius_px"))
		frameRadiusPx = std::clamp((int)obs_data_get_int(data, "frame_radius_px"), 0, 400);
	if (obs_data_has_user_value(data, "frame_glow_px"))
		frameGlowPx = std::clamp((int)obs_data_get_int(data, "frame_glow_px"), 0, 200);
	if (obs_data_has_user_value(data, "frame_glow_pct"))
		frameGlowPct = std::clamp((int)obs_data_get_int(data, "frame_glow_pct"), 0, 100);
	if (obs_data_has_user_value(data, "auto_zoom_out_enabled"))
		autoZoomOutEnabled = obs_data_get_bool(data, "auto_zoom_out_enabled");
	manualZoomInSequence = getStr("manual_zoom_in_hotkey");
	manualZoomOutSequence = getStr("manual_zoom_out_hotkey");
	focusToggleSequence = getStr("focus_toggle_hotkey");
	focusOnSequence = getStr("focus_on_hotkey");
	focusOffSequence = getStr("focus_off_hotkey");

	/* `zoom_anchor` supersedes the `follow_mouse` boolean. Configs written by
	 * older builds only have the boolean, so migrate it: following stays
	 * following, and "off" keeps its old meaning of zooming about the canvas
	 * centre rather than silently switching to the new static-cursor mode. */
	if (obs_data_has_user_value(data, "zoom_anchor"))
		zoomAnchor = zoomAnchorModeFromString(getStr("zoom_anchor"));
	else if (obs_data_has_user_value(data, "follow_mouse"))
		zoomAnchor = obs_data_get_bool(data, "follow_mouse") ? ZoomAnchorMode::CursorFollow
								     : ZoomAnchorMode::CanvasCenter;
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
	if (obs_data_has_user_value(data, "center_focus"))
		centerFocus = obs_data_get_bool(data, "center_focus");
	if (obs_data_has_user_value(data, "mouse_idle_timeout_ms"))
		mouseIdleTimeoutMs = (int)obs_data_get_int(data, "mouse_idle_timeout_ms");
	mouseIdleTimeoutMs = 0; /* not offered any more */
	centerCursorUntilEdge = true;

	if (obs_data_has_user_value(data, "portrait_cover"))
		portraitCover = obs_data_get_bool(data, "portrait_cover");
	if (obs_data_has_user_value(data, "show_cursor_marker"))
		showCursorMarker = obs_data_get_bool(data, "show_cursor_marker");
	if (obs_data_has_user_value(data, "show_marker_when_not_zoomed"))
		showMarkerWhenNotZoomed = obs_data_get_bool(data, "show_marker_when_not_zoomed");
	// Continuous cursor tracking was inaccurate on captures whose aspect ratio
	// differs from the OBS canvas. The marker is intentionally click-only.
	markerOnlyOnClick = true;
	if (obs_data_has_user_value(data, "marker_color"))
		markerColor = (uint32_t)obs_data_get_int(data, "marker_color");
	if (obs_data_has_user_value(data, "marker_size"))
		markerSize = (int)obs_data_get_int(data, "marker_size");
	if (obs_data_has_user_value(data, "marker_thickness"))
		markerThickness = (int)obs_data_get_int(data, "marker_thickness");

	if (obs_data_has_user_value(data, "debug"))
		debug = obs_data_get_bool(data, "debug");

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

QString ZoominatorController::presetPath() const
{
	char *path = obs_module_config_path("zoominator-preset.json");
	if (!path)
		return {};
	QString p = QString::fromUtf8(path);
	bfree(path);
	return p;
}

bool ZoominatorController::hasUserPreset() const
{
	const QString p = presetPath();
	return !p.isEmpty() && QFileInfo::exists(p);
}

/* Settings that belong to this computer or this moment rather than to a look. */
static void erase_machine_specific_keys(obs_data_t *data)
{
	static const char *const keys[] = {"screen_key", "source_name", "included_sources", "recovery_active",
					   "recovery_transforms"};
	for (const char *key : keys)
		obs_data_erase(data, key);
}

bool ZoominatorController::saveUserPreset()
{
	const QString cfg = configPath();
	const QString dst = presetPath();
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

/* Lays the saved preset over the current settings file and reloads it, so the
 * target screen and ticked sources stay as they are. */
bool ZoominatorController::applyUserPreset()
{
	const QString cfg = configPath();
	const QString src = presetPath();
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
	obs_data_set_int(data, "anim_in_ms", animInMs);
	obs_data_set_int(data, "anim_out_ms", animOutMs);
	obs_data_set_string(data, "zoom_anchor", zoomAnchorModeToString(zoomAnchor).toUtf8().constData());
	obs_data_set_string(data, "easing", zoomEasingToString(easing).toUtf8().constData());
	obs_data_set_bool(data, "auto_on_click", autoOnClick);
	obs_data_set_bool(data, "auto_on_typing", autoOnTyping);
	obs_data_set_bool(data, "caret_follow", caretFollow);
	obs_data_set_int(data, "auto_hold_ms", autoHoldMs);
	obs_data_set_bool(data, "focus_enabled", focusEnabled);
	obs_data_set_bool(data, "show_focus_when_not_zoomed", showFocusWhenNotZoomed);
	obs_data_set_int(data, "focus_dim_pct", focusDimPct);
	obs_data_set_int(data, "focus_radius", focusRadius);
	obs_data_set_int(data, "focus_softness", focusSoftness);
	obs_data_set_string(data, "focus_style", focusStyleToString(focusStyle).toUtf8().constData());
	obs_data_set_int(data, "focus_blur_px", focusBlurPx);
	obs_data_set_bool(data, "cam_ring_enabled", camRingEnabled);
	obs_data_set_string(data, "cam_source", camSourceName.toUtf8().constData());
	obs_data_set_int(data, "cam_ring_color", (long long)camRingColor);
	obs_data_set_int(data, "cam_ring_px", camRingPx);
	obs_data_set_int(data, "cam_glow_px", camGlowPx);
	obs_data_set_int(data, "cam_glow_pct", camGlowPct);
	obs_data_set_int(data, "cam_zoom_pct", camZoomPct);
	obs_data_set_bool(data, "frame_enabled", frameEnabled);
	obs_data_set_int(data, "frame_color", (long long)frameColor);
	obs_data_set_int(data, "frame_bg_color", (long long)frameBgColor);
	obs_data_set_int(data, "frame_bg_color2", (long long)frameBgColor2);
	obs_data_set_int(data, "frame_bg_mode", frameBgMode);
	obs_data_set_int(data, "frame_bg_angle", frameBgAngle);
	obs_data_set_int(data, "frame_border_px", frameBorderPx);
	obs_data_set_int(data, "frame_radius_px", frameRadiusPx);
	obs_data_set_int(data, "frame_glow_px", frameGlowPx);
	obs_data_set_int(data, "frame_glow_pct", frameGlowPct);
	obs_data_set_bool(data, "auto_zoom_out_enabled", autoZoomOutEnabled);
	obs_data_set_string(data, "manual_zoom_in_hotkey", manualZoomInSequence.toUtf8().constData());
	obs_data_set_string(data, "manual_zoom_out_hotkey", manualZoomOutSequence.toUtf8().constData());
	obs_data_set_string(data, "focus_toggle_hotkey", focusToggleSequence.toUtf8().constData());
	obs_data_set_string(data, "focus_on_hotkey", focusOnSequence.toUtf8().constData());
	obs_data_set_string(data, "focus_off_hotkey", focusOffSequence.toUtf8().constData());
	/* Kept so a downgrade to an older build still finds a setting it understands. */
	obs_data_set_bool(data, "follow_mouse", zoomAnchor == ZoomAnchorMode::CursorFollow);
	followMouseRuntimeEnabled = true;
	obs_data_set_double(data, "follow_speed", followSpeed);
	obs_data_set_bool(data, "center_cursor_until_edge", centerCursorUntilEdge);
	obs_data_set_int(data, "edge_overflow_margin_pct", edgeOverflowMarginPct);
	obs_data_set_bool(data, "center_focus", centerFocus);
	obs_data_set_bool(data, "allow_canvas_overflow", edgeOverflowMarginPct > 0);
	obs_data_set_int(data, "mouse_idle_timeout_ms", mouseIdleTimeoutMs);
	obs_data_set_bool(data, "portrait_cover", portraitCover);
	obs_data_set_bool(data, "show_cursor_marker", showCursorMarker);
	obs_data_set_bool(data, "show_marker_when_not_zoomed", showMarkerWhenNotZoomed);
	obs_data_set_bool(data, "marker_only_on_click", true);
	obs_data_set_int(data, "marker_color", (long long)markerColor);
	obs_data_set_int(data, "marker_size", markerSize);
	obs_data_set_int(data, "marker_thickness", markerThickness);
	obs_data_set_bool(data, "debug", debug);
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

	QByteArray pUtf8 = p.toUtf8();
	obs_data_save_json_safe(data, pUtf8.constData(), "tmp", "bak");
	obs_data_release(data);

	logi(debug, "[Zoominator] Saved settings to: %s", pUtf8.constData());

	rebuildTriggersFromSettings();
	emit settingsChanged();
}

