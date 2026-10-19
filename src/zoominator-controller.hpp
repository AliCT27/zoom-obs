#pragma once

#include <obs-module.h>
#include <QMap>
#include <QVector>
#include <obs-frontend-api.h>

#include <QObject>
#include <QRectF>
#include <QPointer>
#include <QKeySequence>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>
#include <QElapsedTimer>
#include <QString>
#include <QHash>
#include <atomic>
#include <mutex>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef WIN32_LEAN_AND_MEAN
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#elif defined(__linux__)

struct _XDisplay;
#endif

class ZoominatorDialog;

class ZoominatorController final : public QObject {
	Q_OBJECT

public:
	/* Where the zoom is anchored: on the cursor and held still, or following it. */
	enum class ZoomAnchorMode {
		CursorStatic,
		CursorFollow,
	};

	static QString zoomAnchorModeToString(ZoomAnchorMode mode);
	static ZoomAnchorMode zoomAnchorModeFromString(const QString &value);

	/* Shape of the zoom in/out animation. Punch overshoots the target a little
	 * on the way in, which reads as a snappy "camera push" in short videos. */
	enum class ZoomEasing {
		Smooth,
		EaseOut,
		Expo,
		Punch,
	};

	static QString zoomEasingToString(ZoomEasing easing);
	static ZoomEasing zoomEasingFromString(const QString &value);

	/* How Focus highlights the area around the cursor. Blur needs the shader
	 * in data/focus_blur.effect; without it the controller falls back to Dim. */
	enum class FocusStyle {
		Dim,
		Blur,
		BlurDim,
	};

	static QString focusStyleToString(FocusStyle style);
	static FocusStyle focusStyleFromString(const QString &value);

	static ZoominatorController &instance();

	void initialize();
	void shutdown();
	void showDialog();
	void saveSettings();
	void loadSettings();

	/* Sound effects (main thread). */
	QString soundDir() const;
	void ensureSoundSources();
	void releaseSoundSources();
	void previewSound(int slot);
	void playSound(int slot);
	void onTypingSound();

	/* "My preset": a snapshot of every setting except the machine-specific ones
	 * (target screen, ticked sources, recovery data). saveUserPreset() saves what
	 * is currently stored in the settings file, so call saveSettings() first. */
	/* Two presets: vertical (1080x1920, the original file) and horizontal (1920x1080). */
	struct PresetInfo {
		QString id;
		QString name; /* empty = built-in default name */
		bool horizontal = false;
	};
	QVector<PresetInfo> listPresets();
	QString addPreset(const QString &name, bool horizontal); /* snapshots the saved settings; returns the id */
	bool removePreset(const QString &id);
	bool renamePreset(const QString &id, const QString &name);
	bool hasUserPreset(const QString &id) const;
	bool canvasIs(int w, int h) const;
	/* Scales the captured screen to the canvas width (whole screen visible, centred
	 * vertically) once the view is at rest. Done on the main thread. */
	void requestFitScreen();
	QString lastFitSummary;

	/* The user's own "not zoomed" layout per canvas size ("1080x1920"). The Fit
	 * button restores it; without one it falls back to the automatic fit. */
	struct SavedItemLayout {
		QString name;
		double px = 0.0, py = 0.0, sx = 1.0, sy = 1.0;
		int cropL = 0, cropT = 0, cropR = 0, cropB = 0;
	};
	QMap<QString, QVector<SavedItemLayout>> savedLayouts;
	/* Saves the resting layout under "1080x1920" / "1920x1080". Returns how many items
	 * were saved, or -1 when the canvas currently has a different size. */
	int saveCurrentLayout(int wantW, int wantH);
	bool hasSavedLayoutForCanvas() const;
	/* Backup: everything except this computer's screen and sources, plus both presets. */
	bool exportSettings(const QString &path);
	bool importSettings(const QString &path);
	bool saveUserPreset(const QString &id);
	bool applyUserPreset(const QString &id);
	void notifySettingsChanged();
	void rebuildRuntimeHooks();

	QString screenKey;
	QString hotkeySequence;
	QString hotkeyMode;
	QString followToggleHotkeySequence;

	QString triggerType;
	QString mouseButton;
	bool modCtrl = false;
	bool modAlt = false;
	bool modShift = false;
	bool modWin = false;
	bool modLeftCtrl = false;
	bool modRightCtrl = false;
	bool modLeftAlt = false;
	bool modRightAlt = false;
	bool modLeftShift = false;
	bool modRightShift = false;
	bool modLeftWin = false;
	bool modRightWin = false;

	double zoomFactor = 2.0;
	double wheelZoomInStep = 0.20;
	double wheelZoomOutStep = 0.20;
	double wheelZoomMinimum = 1.0;
	double wheelZoomMaximum = 5.0;
	/* Scrolling out by this much zoom (counted from where the scroll-out started) returns to the full
	 * unzoomed screen. 0 = off (only at 1x). */
	double wheelZoomExit = 0.0;
	double wheelOutAccum = 0.0;
	qint64 lastExtCheckMs = 0;
	bool wheelZoomEnabled = false;
	/* Not zoomed: pan the capture sideways so it follows the mouse in x. */
	bool idleFollowX = false;
	/* 3D camera: the captured screen leans towards the mouse. */
	bool tilt3dEnabled = false;
	double tilt3dMaxX = 12.0;  /* degrees of turn when the mouse is at the left / right edge */
	double tilt3dMaxY = 8.0;   /* degrees of turn when the mouse is at the top / bottom edge */
	double tilt3dPerspective = 2.0; /* camera distance in multiples of the screen's longer side */
	bool tilt3dInvert = false;
	bool tilt3dInZoom = true; /* keep the turn while zoomed (fades only near the screen's edges) */
	QString wheelZoomModifier = QStringLiteral("alt");
	int animInMs = 180;
	int animOutMs = 320;
	ZoomAnchorMode zoomAnchor = ZoomAnchorMode::CursorFollow;
	ZoomEasing easing = ZoomEasing::Smooth;
	/* Automatic trigger (triggerType == "auto"): zoom in on a click and/or on
	 * typing, then zoom back out once autoHoldMs passes with no activity. */
	bool autoOnClick = true;
	bool autoOnTyping = true;
	int autoHoldMs = 2500;
	/* Automatic mode: zoom back out by itself after autoHoldMs without activity.
	 * When off, only the Zoom Out hotkey zooms out. */
	bool autoZoomOutEnabled = true;
	/* While typing in auto mode, follow the text caret instead of the mouse (Windows). */
	bool caretFollow = true;
	/* Frame: fills everything outside the captured area with a colour and draws a
	 * glowing border around it. The zoom happens inside the frame. */
	bool frameEnabled = false;
	uint32_t frameColor = 0xFF2ECC40;
	uint32_t frameBgColor = 0xFF000000;
	uint32_t frameBgColor2 = 0xFF0B3A52;
	int frameBgMode = 0; /* 0 solid, 1 linear gradient, 2 radial gradient */
	int frameBgAngle = 90;

	/* Webcam ring: turns the chosen source (a phone camera, a webcam) into a circle
	 * with a ring and glow, and scales it with the zoom. */
	bool camRingEnabled = false;
	QString camSourceName;
	uint32_t camRingColor = 0xFF2EA8DC;
	int camRingPx = 6;
	int camGlowPx = 28;
	int camGlowPct = 80;
	int camZoomPct = 125; /* size at full zoom, in percent of normal; 100 = unchanged */
	int frameBorderPx = 4;
	int frameRadiusPx = 24;
	int frameGlowPx = 80;
	int frameGlowPct = 70;
	/* Focus: dims everything except a soft spotlight around the cursor while zoomed. */
	bool focusEnabled = false;
	bool showFocusWhenNotZoomed = false;
	int focusDimPct = 55;
	int focusRadius = 300;
	int focusIdleRadius = 300;
	int focusSoftness = 80;
	int focusCone = 50; /* "light cone" 0..100: small = tight flashlight, large = wide gentle light */
	/* Size = focusRadius; shape = light cone; focusSoftness is derived from both. beamRadiusForCone only migrates
	 * settings saved by the cone-only version (no radius stored). */
	static int beamRadiusForCone(int cone);
	static int beamSoftnessForCone(int cone, int radius);
	static int coneForBeamRadius(int radius); /* inverse, to carry old radius settings over */
	FocusStyle focusStyle = FocusStyle::Dim;
	/* Blur radius in canvas pixels for the Blur styles. */
	int focusBlurPx = 16;
	/* Automatic mode only: zoom in (and stay zoomed) / zoom out on demand. */
	QString manualZoomInSequence;
	QString manualZoomOutSequence;
	QString focusToggleSequence;

	/* Sound effects. Files the user picked are copied into the plugin's config
	 * folder (sounds/); empty means the bundled default. */
	bool soundEnabled = false;
	bool soundOn[3] = {true, true, true}; /* each sound can be switched off on its own */
	QString soundFile[3]; /* 0 = zoom in, 1 = zoom out, 2 = typing */
	QString soundName[3]; /* original file name, for display */
	int soundVolume[3] = {80, 80, 60};
	QString soundTypingMode = QStringLiteral("tick"); /* "tick" or "loop" */
	QString focusOnSequence;
	QString focusOffSequence;
	bool followMouseRuntimeEnabled = true;
	double followSpeed = 8.0;
	bool centerCursorUntilEdge = true;
	/* Keep the focus point (cursor or text caret) exactly in the middle of the
	 * canvas while zoomed, even near the edges of the capture. */
	bool centerFocus = false;
	int edgeOverflowMarginPct = 0;
	int mouseIdleTimeoutMs = 0;
	bool portraitCover = true;
	bool showCursorMarker = false;
	bool showMarkerWhenNotZoomed = false;
	/* The halo is centred on the cursor arrow (not its tip) and grows with the zoom, so it stays around the arrow. */
	bool markerFollowZoom = true;
	std::atomic<bool> markerFlashStartedZoomed{false}; /* a click halo that began while zoomed finishes as zoomed */
	bool markerOnlyOnClick = true;
	uint32_t markerColor = 0xFFFF0000;
	int markerSize = 26;
	int markerIdleSize = 26;
	int markerThickness = 4;
	bool debug = false;

	QSet<QString> includedSources;

signals:
	void settingsChanged();

private slots:
	void onTick();

private:
	ZoominatorController();
	~ZoominatorController() override;
	ZoominatorController(const ZoominatorController &) = delete;
	ZoominatorController &operator=(const ZoominatorController &) = delete;

	QString configPath() const;
	QString presetPath(const QString &id) const;
	QString presetIndexPath() const;

	void ensureTicking(bool on);
	void startZoomIn();
	void startZoomOut();
	void resetState();
	void rebuildTriggersFromSettings();
	void installHooks();
	void uninstallHooks();
	bool needsKeyboardHook() const;
	bool needsMouseHook() const;
	void onTriggerDown();
	void onTriggerUp();
	void onAutoActivity(bool typing = false);
	void checkAutoIdle();
	/* Automatic mode: hotkeys that zoom in and hold, and zoom out and release. */
	void manualZoomIn();
	void manualZoomOut();
	void toggleFocusRuntime();
	void setFocusRuntime(bool on);
	void beginAutoZoom();
	void beginWheelZoomGesture();
	void adjustActiveZoomFromWheel(int direction, int steps = 1);
	bool usesWheelZoomGesture() const;
	bool wheelModifierHeld() const;
	void wheelZoom(int direction, int steps = 1);
	bool zoomedView() const;
	void startIdlePan();
	void fitScreenNow();
	void finishWheelZoomGesture();
	void toggleFollowMouseRuntime();
	bool triggerMatchesKeyboard(int vk) const;
	bool triggerMatchesMouse(unsigned int msg, unsigned short mouseData) const;
	bool modsMatch() const;

	bool getSelectedScreenRect(int &x, int &y, int &w, int &h) const;
	void enumerateTargetItemsInCurrentScene(std::vector<obs_sceneitem_t *> &items) const;

	bool getCursorPos(int &x, int &y) const;
	bool mapCursorToScenePixels(int cursorX, int cursorY, float &sx, float &sy, bool &cursorInside) const;
	/* Resting-layout bounds of the target items, cached briefly (main thread only). */
	mutable qint64 restBoundsMs = 0;
	mutable bool restBoundsValid = false;
	mutable vec2 restBoundsMin{};
	mutable vec2 restBoundsMax{};

	void captureOriginal(obs_sceneitem_t *item);
	void restoreOriginal(obs_sceneitem_t *item);
	void captureOriginalSceneItems(const std::vector<obs_sceneitem_t *> &items);
	void restoreOriginalSceneItems(const std::vector<obs_sceneitem_t *> &items);
	void restoreOriginalSceneItemsFromState();
	QString markerImagePath() const;
	void ensureMarkerSource();
	obs_sceneitem_t *ensureMarkerItem(obs_scene_t *scene);
	void hideMarkerInScene(obs_scene_t *scene);
	void rebuildMarkerImage();
	void ensureMarkerFilter();
	void applyMarkerOpacity(int opacity255);
	void updateMarkerAppearance();
	void updateMarkerPosition(obs_scene_t *scene, double x, double y, int opacity255, bool idle = false, double zoomK = 1.0);
	int markerImageSize() const;
	bool captureMarkerClickPosition();
	bool isMarkerFlashActive(qint64 nowMs) const;
	int currentMarkerOpacity(qint64 nowMs);
	void applyZoomToScene(double t);

	/* Focus spotlight (see updateFocus). Main thread only. */
	QString focusImagePath() const;
	double focusImagePad() const;
	void rebuildFocusImage(int canvasW, int canvasH);
	void ensureFocusFilter();
	void applyFocusOpacity(int opacity255);
	void ensureFocusSource();
	void updateFocusAppearance();
	obs_sceneitem_t *ensureFocusItem(obs_scene_t *scene, bool &created);
	void placeFocusBelowOverlays(obs_scene_t *scene, obs_sceneitem_t *item);
	void updateFocus(obs_scene_t *scene, double x, double y, int opacity255, bool idle = false,
			 int progress255 = -1);
	double focusIdleScale() const;
	int coneRadius(int radius) const;  /* spotlight radius after the light cone setting */
	int coneSoftness() const;          /* edge softness after the light cone setting */
	void hideFocusInScene(obs_scene_t *scene);
	void hideFocusInAllScenes();
	void updateIdleFocus(bool on);
	bool wantsIdleFocus() const { return focusEnabled && showFocusWhenNotZoomed && (wantsDim() || wantsBlur()); }
	void refreshIdleFocusTicking();

	/* Frame overlay (zoominator-frame.cpp). Main thread only. */
	void refreshFrame();
	void refreshCamRing();
	void captureCamItem(obs_scene_t *scene);
	void restoreCamItem();
	void hideFrameInAllScenes();

	/* Split loop. Frame-critical work runs on OBS's graphics thread via
	 * obs_add_tick_callback so the transform lands exactly once per rendered
	 * frame, at whatever output fps is configured - a free-running Qt timer
	 * beats against the render clock and drops or doubles updates.
	 *
	 * Qt GUI calls (QCursor::pos, QGuiApplication::screens) and the whole
	 * obs_frontend_* API are main-thread only, and saveSettings() does blocking
	 * file IO, so all of that stays on the Qt timer, which publishes an
	 * InputSnapshot for the graphics thread to consume. */
	static void videoTickCallback(void *param, float seconds);
	void videoTick(double seconds);
	void finishZoomOnMainThread();

	struct InputSnapshot {
		bool mapped = false;
		bool inside = false;
		float sceneX = 0.0f;
		float sceneY = 0.0f;
		obs_source_t *sceneRef = nullptr; /* strong ref owned by the snapshot */
	};

	/* Guards input, pending marker placement, and the captured click position
	 * while the video thread calculates the halo's transformed position. */
	std::mutex inputMutex;
	InputSnapshot input;
	bool pendingMarkerVisible = false;
	bool pendingFocusVisible = false;
	bool pendingFocusFresh = false; /* the graphics thread has published a focus sample */
	double pendingFocusX = 0.0;
	double pendingFocusY = 0.0;
	int pendingFocusOpacity = 0;
	double pendingMarkerX = 0.0;
	double pendingMarkerZ = 1.0;
	double pendingMarkerY = 0.0;
	bool tickCallbackAdded = false;
	std::atomic<bool> tickingWanted{false};
	std::atomic<bool> pendingFinish{false};

	QTimer tickTimer;
	QTimer frameTimer;
	bool zoomPressed = false;
	bool zoomLatched = false;
	qint64 autoLastActivityMs = 0;
	qint64 autoLastTypingMs = 0;
	qint64 autoLastClickMs = 0;
	bool zoomAdjustButtonHeld = false;
	bool zoomAdjustedDuringButtonHold = false;
	int wheelDeltaRemainder = 0;
	double wheelBaseZoom = 0.0;
	double panFollowX = 0.0;
	bool panHasPos = false;
	bool pendingFit = false;
	qint64 lastIdlePanTryMs = 0;
	/* Release-stored by the main thread once capture completes, acquire-loaded
	 * by the graphics thread; that ordering is what makes sceneItems safe to
	 * read there without holding a lock across the transform writes. */
	std::atomic<bool> zoomActive{false};

	double animT = 0.0;
	int lastTickDir = 0; /* graphics thread only: direction seen on the previous tick */
	double renderedZoomFactor = 1.0;
	std::atomic<int> animDir{0};

	bool followHasPos = false;
	float followX = 0.0f;
	float followY = 0.0f;
	bool lastCursorSampleValid = false;
	float lastCursorSampleX = 0.0f;
	float lastCursorSampleY = 0.0f;
	qint64 lastCursorMovementMs = 0;
	bool mouseTrackingIdle = false;

	bool targetHasPos = false;
	double tickDeltaSeconds = 1.0 / 60.0;
	qint64 lastTickMs = 0;
	qint64 lastTransformApplyMs = 0;
	float lastFollowAnchorX = 0.0f;
	float lastFollowAnchorY = 0.0f;
	bool lastFollowAnchorValid = false;
	float targetX = 0.0f;
	float targetY = 0.0f;

	bool markerClickHasPos = false;
	float markerClickX = 0.0f;
	float markerClickY = 0.0f;
	uint32_t markerAppearanceHash = 0;
	int markerCurrentOpacity = -1;
	obs_source_t *markerFilter = nullptr;
	qint64 markerClickFlashStartMs = 0;
	qint64 markerClickFlashHoldUntilMs = 0;
	qint64 markerClickFlashFadeOutEndMs = 0;

	struct OrigState {
		bool valid = false;
		vec2 pos{};
		vec2 scale{};
		float rot = 0.0f;
		uint32_t align = 0;
		obs_bounds_type boundsType = OBS_BOUNDS_NONE;
		uint32_t boundsAlign = 0;
		vec2 bounds{};
		obs_sceneitem_crop crop{};
		vec2 effectivePos{};
		vec2 effectiveScale{};
	};

	struct SceneItemState {
		obs_sceneitem_t *item = nullptr;
		OrigState orig;
		bool normalized = false;
		bool lastAppliedValid = false;
		vec2 lastAppliedPos{};
		vec2 lastAppliedScale{};
		/* External edit detection while resting at 1x (see onTick). */
		bool extSeen = false;
		vec2 extPos{};
		vec2 extScale{};
		/* Focus blur: the source the filter sits on (strong ref) and the
		 * filter itself (strong ref). Both null when blur is not in use. */
		obs_source_t *blurTarget = nullptr;
		obs_source_t *blurFilter = nullptr;
	};

	/* Focus blur plumbing. attach/detach are main thread only. */
	bool wantsBlur() const { return focusEnabled && focusStyle != FocusStyle::Dim; }
	bool wantsDim() const
	{
		return focusEnabled && (focusStyle != FocusStyle::Blur || blurUnavailable);
	}
	void attachBlurFilters();
	/* Blur while not zoomed: filters on the target sources, owned separately from
	 * sceneItems (which only exists while zooming). Main thread only. */
	struct IdleBlur {
		obs_sceneitem_t *item = nullptr;
		obs_source_t *target = nullptr; /* strong ref */
		obs_source_t *filter = nullptr; /* strong ref */
	};
	std::vector<IdleBlur> idleBlurs;

	/* 3D camera filters on the target sources. Main thread only. */
	struct TiltTarget {
		obs_sceneitem_t *item = nullptr;
		obs_source_t *target = nullptr; /* strong ref */
		obs_source_t *filter = nullptr; /* strong ref */
	};
	std::vector<TiltTarget> tiltTargets;
	double tiltYaw = 0.0, tiltPitch = 0.0; /* smoothed, radians */
	std::atomic<double> tiltYawA{0.0}, tiltPitchA{0.0}; /* the same, readable from the graphics thread */
	void applyTiltToIdlePoint(double &x, double &y) const;
	qint64 tiltLastMs = 0;
	qint64 tiltSyncMs = 0;
	void updateTilt();
	void detachTilt();
	void updateIdleBlur(bool on, bool cursorOk, float sx, float sy);
	void detachIdleBlurs();
	void detachBlurFilters();
	bool blurUnavailable = false;

	QString sceneItemKey(obs_sceneitem_t *item) const;
	OrigState readSceneItemTransform(obs_sceneitem_t *item) const;
	OrigState computeOrigState(obs_sceneitem_t *item) const;
	void accumulateItemBounds(const OrigState &orig, obs_source_t *src, vec2 &mn, vec2 &mx, bool &valid) const;
	bool computeTargetBounds(const std::vector<obs_sceneitem_t *> &items, vec2 &mn, vec2 &mx) const;
	void applySceneItemTransform(obs_sceneitem_t *item, const OrigState &state);
	void loadRecoveryMap(obs_data_t *data);
	void saveRecoveryMap(obs_data_t *data);
	void scheduleSettingsSave(int delayMs = 250);
	void restoreRecoveryIfNeeded();
	void markRecoveryActive();
	void clearRecoveryActive();
	void requestRecoveryRestore();
	static void frontendEventCallback(enum obs_frontend_event event, void *data);

	std::vector<SceneItemState> sceneItems;
	QHash<QString, OrigState> recoveryTransforms;
	bool pendingSettingsSave = false;
	bool shuttingDown = false;
	bool recoveryActive = false;
	bool restoringRecovery = false;
	bool sceneContentBoundsValid = false;
	vec2 sceneContentMin{};
	vec2 sceneContentMax{};

	QPointer<ZoominatorDialog> dialog;
	obs_source_t *markerSource = nullptr;
	obs_source_t *focusSource = nullptr;
	obs_source_t *focusFilter = nullptr;
	void publishDimSheet(); /* main thread, not zoomed: the screen's resting rectangle for the dim mask */
	int focusCurrentOpacity = -1;
	uint64_t focusAppearanceHash = 0;
	bool focusShown = false;
	obs_source_t *frameSource = nullptr;
	obs_source_t *camFilter = nullptr;
	obs_source_t *camFilterTarget = nullptr;
	bool camRingUnavailable = false;
	/* The webcam's scene item while a zoom runs, so it can be scaled with the zoom. */
	obs_sceneitem_t *camItem = nullptr;
	OrigState camOrig;
	bool camLastValid = false;
	vec2 camLastScale{};
	vec2 camLastPos{};
	/* A global key and the modifiers it needs, ready for the hook procs. */
	struct HotkeyBinding {
		int vk = 0;
		bool valid = false;
		bool ctrl = false;
		bool alt = false;
		bool shift = false;
		bool win = false;
	};
	static HotkeyBinding makeHotkeyBinding(const QString &sequence);
	HotkeyBinding manualZoomInBinding;
	HotkeyBinding manualZoomOutBinding;
	HotkeyBinding focusToggleBinding;
	obs_source_t *soundSrc[3] = {nullptr, nullptr, nullptr};
	QString soundSrcPath[3];
	QString soundSrcLoop[3];
	qint64 soundTypingLastMs = 0;
	bool soundTypingLooping = false;
	void typingLoopWatch();
	QString effectiveSoundFile(int slot) const;
	HotkeyBinding focusOnBinding;
	HotkeyBinding focusOffBinding;
	qint64 autoLastManualKeyMs = 0;
	/* Set by the Zoom Out hotkey: clicks and typing no longer start a zoom until
	 * the Zoom In hotkey is pressed (or the settings are applied again). */
	bool autoSuspended = false;

	int hotkeyVk = 0;
	bool hkValid = false;
	int followToggleHotkeyVk = 0;
	bool followToggleHkValid = false;
	bool followToggleModCtrl = false;
	bool followToggleModAlt = false;
	bool followToggleModShift = false;
	bool followToggleModWin = false;

#ifdef _WIN32
	static LRESULT CALLBACK kb_hook_proc(int nCode, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK mouse_hook_proc(int nCode, WPARAM wParam, LPARAM lParam);

	void *keyboardHook = nullptr;
	void *mouseHook = nullptr;
#elif defined(__APPLE__)
	static CGEventRef eventTapCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *refcon);
	CFMachPortRef eventTap = nullptr;
	CFRunLoopSourceRef runLoopSource = nullptr;
#elif defined(__linux__)
	_XDisplay *xiDisplay = nullptr;
	int xiOpcode = 0;
	QSocketNotifier *xiNotifier = nullptr;
	bool linuxWheelZoomGrabInstalled = false;
	void processXInput2Events();
#endif
};
