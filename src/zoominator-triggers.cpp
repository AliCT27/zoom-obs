/* Triggers: what a hotkey, mouse button or automatic activity does to the zoom. */

#include "zoominator-internal.hpp"

void ZoominatorController::onTriggerDown()
{
	if (debug)
		blog(LOG_INFO, "[Zoominator] Trigger DOWN");

	if (hotkeyMode == "toggle") {
		zoomLatched = !zoomLatched;
		if (zoomLatched) {
			followHasPos = false;
			lastCursorSampleValid = false;
			lastCursorMovementMs = 0;
			mouseTrackingIdle = false;
			targetHasPos = false;
			startZoomIn();
		} else {
			startZoomOut();
		}
		return;
	}

	zoomPressed = true;
	followHasPos = false;
	lastCursorSampleValid = false;
	lastCursorMovementMs = 0;
	mouseTrackingIdle = false;
	targetHasPos = false;
	startZoomIn();
}

void ZoominatorController::onTriggerUp()
{
	if (debug)
		blog(LOG_INFO, "[Zoominator] Trigger UP");
	zoomPressed = false;
	startZoomOut();
}

/* Automatic trigger. Any click or typing keeps the zoom alive; checkAutoIdle()
 * lets go of it once autoHoldMs passes without any. Runs on the thread that
 * owns the input hooks (the Qt main thread on every platform), like
 * onTriggerDown(). */
/* True while the OBS main window (or one of its dialogs, including this
 * plugin's) has focus, so clicking Start/Stop Recording or the settings dialog
 * does not trigger the automatic zoom. Windows only; other platforms report
 * false and keep the old behaviour. */
static bool obs_window_is_foreground()
{
#ifdef _WIN32
	QWidget *mainWin = reinterpret_cast<QWidget *>(obs_frontend_get_main_window());
	if (!mainWin)
		return false;
	HWND fg = GetForegroundWindow();
	if (!fg)
		return false;
	return GetAncestor(fg, GA_ROOTOWNER) == reinterpret_cast<HWND>(mainWin->winId());
#else
	return false;
#endif
}

void ZoominatorController::beginAutoZoom()
{
	if (zoomLatched)
		return;

	zoomLatched = true;
	/* Only reseed the focal point for a fresh zoom. If a zoom-out is still
	 * running, resuming from where the view currently is avoids a visible jump. */
	if (!zoomActive.load(std::memory_order_acquire)) {
		followHasPos = false;
		lastCursorSampleValid = false;
		lastCursorMovementMs = 0;
		mouseTrackingIdle = false;
		targetHasPos = false;
	}
	startZoomIn();
}

void ZoominatorController::onAutoActivity(bool typing)
{
	if (triggerType != "auto" || shuttingDown)
		return;
	if (autoSuspended || obs_window_is_foreground())
		return;

	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
	/* The key that just ran a Zoom In / Zoom Out hotkey is not typing. */
	if (typing && nowMs - autoLastManualKeyMs < 80)
		return;

	autoLastActivityMs = nowMs;
	if (typing) {
		autoLastTypingMs = autoLastActivityMs;
		zoominator_caret::noteTyping();
	} else {
		autoLastClickMs = autoLastActivityMs;
	}
	beginAutoZoom();
}

/* Zoom In hotkey: zoom in now, and let clicks and typing zoom again. */
void ZoominatorController::manualZoomIn()
{
	if (shuttingDown)
		return;
	if (triggerType != "auto") {
		/* Keyboard / mouse trigger: a plain "zoom in and stay zoomed" key. */
		if (zoomLatched)
			return;
		zoomLatched = true;
		followHasPos = false;
		lastCursorSampleValid = false;
		lastCursorMovementMs = 0;
		mouseTrackingIdle = false;
		targetHasPos = false;
		startZoomIn();
		return;
	}
	autoLastManualKeyMs = QDateTime::currentMSecsSinceEpoch();
	autoLastActivityMs = autoLastManualKeyMs;
	autoSuspended = false;
	beginAutoZoom();
}

/* Zoom Out hotkey: zoom out now and keep automatic zooming off until Zoom In. */
void ZoominatorController::manualZoomOut()
{
	if (shuttingDown)
		return;
	if (triggerType != "auto") {
		zoomPressed = false;
		if (!zoomLatched && animDir.load(std::memory_order_relaxed) <= 0 &&
		    !zoomActive.load(std::memory_order_acquire))
			return;
		zoomLatched = false;
		startZoomOut();
		return;
	}
	autoLastManualKeyMs = QDateTime::currentMSecsSinceEpoch();
	autoSuspended = true;
	if (!zoomLatched)
		return;
	zoomLatched = false;
	startZoomOut();
}

/* Focus hotkey: switch the spotlight on or off while recording. Not saved;
 * Apply (or restarting OBS) goes back to the checkbox in the Focus section. */
void ZoominatorController::toggleFocusRuntime()
{
	setFocusRuntime(!focusEnabled);
}

void ZoominatorController::setFocusRuntime(bool on)
{
	if (shuttingDown)
		return;
	focusEnabled = on;
	if (!focusEnabled)
		hideFocusInAllScenes();
	refreshIdleFocusTicking();
	if (debug)
		blog(LOG_INFO, "[Zoominator] Focus %s", focusEnabled ? "ON" : "OFF");
}

void ZoominatorController::checkAutoIdle()
{
	if (triggerType != "auto" || !zoomLatched || !autoZoomOutEnabled)
		return;
	/* Do not cut a zoom-in short: wait until it has settled. */
	if (animDir.load(std::memory_order_relaxed) > 0)
		return;
	if (QDateTime::currentMSecsSinceEpoch() - autoLastActivityMs < autoHoldMs)
		return;

	zoomLatched = false;
	startZoomOut();
}

void ZoominatorController::toggleFollowMouseRuntime()
{
	/* Only meaningful while the anchor actually tracks the cursor; in the other
	 * modes the view is already static, so toggling would just reset the anchor. */
	if (zoomAnchor != ZoomAnchorMode::CursorFollow) {
		if (debug)
			blog(LOG_INFO, "[Zoominator] Follow mouse toggle ignored (zoom anchor is %s)",
			     zoomAnchorModeToString(zoomAnchor).toUtf8().constData());
		return;
	}

	followMouseRuntimeEnabled = !followMouseRuntimeEnabled;
	if (!followMouseRuntimeEnabled && followHasPos) {
		targetX = followX;
		targetY = followY;
		targetHasPos = true;
	} else {
		targetHasPos = false;
	}
	followHasPos = false;

	if (debug) {
		blog(LOG_INFO, "[Zoominator] Follow mouse %s", followMouseRuntimeEnabled ? "ENABLED" : "DISABLED");
	}
}

