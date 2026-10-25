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
class ZoominatorHud;

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
	bool camRainbow = false; /* ring and glow cycle round the colour wheel */
	int camRainbowSec = 10;  /* seconds for one full turn */
	/* Webcam placed by numbers: centre and outer diameter in canvas pixels, for
	 * the resting view and for full zoom. */
	bool camLayoutEnabled = false;
	int camIdleX = 960;
	int camIdleY = 540;
	int camIdleSize = 300;
	int camZoomX = 960;
	int camZoomY = 540;
	int camZoomSize = 375;
	int frameBorderPx = 4;
	int frameRadiusPx = 24;
	int frameGlowPx = 80;
	int frameGlowPct = 70;
	bool frameBgRainbow = false;     /* background colours cycle round the colour wheel */
	bool frameBorderRainbow = false; /* border and glow colour cycle round the colour wheel */
	int frameRainbowSec = 10;        /* seconds for one full turn */
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
	bool showStatusHud = false; /* status badge on the user's own monitor, hidden from the recording */
	int statusHudCorner = 1;    /* 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right */
	bool guideEnabled = false; /* Reels safe-zone guide in the preview while not recording */
	int guideTopPx = 270;      /* margins for a 1080 x 1920 video */
	int guideBottomPx = 670;
	int guideLeftPx = 65;
	int guideRightPx = 65;
	bool cursorRingEnabled = false; /* a ring around the mouse cursor in the video */
	bool cursorRingIdle = true;     /* also while the view is not zoomed */
	int cursorRingSize = 90;        /* outer diameter at 1x, canvas pixels */
	int cursorRingPx = 6;           /* stroke thickness at 1x */
	uint32_t cursorRingColor = 0xFFFFFFFF;
	bool cursorRingRainbow = false;
	int cursorRingSec = 10;
	bool keyDisplayEnabled = false; /* pressed shortcuts shown as key caps in the video */
	bool keyDisplayAll = false;     /* also plain keys, not only shortcuts */
	int keyDisplaySize = 64;        /* key cap text height for a 1080-wide canvas */
	int keyDisplayYPct = 58;        /* vertical position, percent of the canvas height */
	double keyDisplaySec = 1.7;     /* seconds the key caps stay after the last key press, fade-out included */
	QList<int> keyDisplayIgnoreQt;  /* Qt key combinations (key + modifiers) that the key display never shows */
	static constexpr int kMaxIgnoredKeys = 32;
	std::atomic<int> keyIgnoreVk[kMaxIgnoredKeys]{}; /* the same keys as virtual-key codes, read by the keyboard hook */
	std::atomic<int> keyIgnoreMods[kMaxIgnoredKeys]{}; /* modifier bits of each entry: 1 Ctrl, 2 Alt, 4 Shift, 8 Win */
	std::atomic<int> keyIgnoreCount{0};
	bool countdownOnDefaultRecord = false; /* OBS's own Start Recording button also counts down first */
	int countdownSec = 3;       /* seconds counted down before a recording that is started with the countdown */
	int recLimitSec = 90;       /* the badge turns amber 10 s before this and red after it; 0 = no limit */
	bool showCursorMarker = false;
	bool showMarkerWhenNotZoomed = false;
	/* Click-halo runtime state (not user settings). Used by zoominator-marker.cpp and the tick in
	 * zoominator-controller.cpp. */
	struct MarkerRuntime {
		double zHist[3] = {1.0, 1.0, 1.0}; /* last zoom readings for the halo size (median of three) */
		int zCount = 0;
		std::atomic<bool> flashStartedZoomed{false}; /* a click halo that began while zoomed finishes as zoomed */
		bool clickHasPos = false;
		float clickX = 0.0f;
		float clickY = 0.0f;
		uint32_t appearanceHash = 0;
		int currentOpacity = -1;
		obs_source_t *filter = nullptr;
		qint64 flashStartMs = 0;
		int fadeInFrom = 0; /* opacity the new click's fade-in starts from (a halo still showing carries over) */
		qint64 flashHoldUntilMs = 0;
		qint64 flashFadeOutEndMs = 0;
	} mk;
	/* The halo is centred on the cursor arrow (not its tip) and grows with the zoom, so it stays around the arrow. */
	int markerOffsetX = 6;  /* where the arrow's middle is, from the reported cursor position, in capture pixels at 1x */
	int markerOffsetY = 10;
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
	void updateMarkerPosition(obs_scene_t *scene, double x, double y, int opacity255, double zoomK = 1.0);
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
	bool wantsCursorRingIdle() const { return cursorRingEnabled && cursorRingIdle && !shuttingDown; }
	void refreshCursorRing();
	void refreshKeyDisplay();
	void hideKeyDisplayInAllScenes();
	void showKeyLabel(const QString &label);
	void handleKeyForDisplay(int vk);
	void hideCursorRingInAllScenes();
	void updateCursorRingIdle();

	/* Frame overlay (zoominator-frame.cpp). Main thread only. */
	void refreshFrame();
	void refreshCamRing();
	void applyCamLayout(obs_scene_t *scene, double progress, bool force);
public:
	/* Reads the webcam's current place in the scene (centre and outer diameter, in
	 * canvas pixels) for the dialog's "Take from OBS" buttons. Main thread. */
	bool readCamLayout(int &x, int &y, int &size);
private:
	void hideFrameInAllScenes();
	void refreshGuide();
public:
	/* Countdown before recording (zoominator-countdown.cpp). Main thread only, except the hotkey id. */
	void startCountdownRecording();
	obs_hotkey_id countdownHotkeyId = OBS_INVALID_HOTKEY_ID;
private:
	void cancelCountdown();
	void installRecordButtonFilter();
	bool recordButtonFilterInstalled = false;
	void onCountdownTick();
	void registerCountdownHotkey();
	void unregisterCountdownHotkey();
	void hideGuideInAllScenes();

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
	bool pendingCursorVisible = false; /* cursor ring: published by the graphics thread, applied by onTick */
	double pendingCursorX = 0.0;
	double pendingCursorY = 0.0;
	double pendingCursorZ = 1.0;
	double pendingMarkerX = 0.0;
	double pendingMarkerZ = 1.0;
	double pendingMarkerY = 0.0;
	bool tickCallbackAdded = false;
	std::atomic<bool> tickingWanted{false};
	std::atomic<bool> pendingFinish{false};

	QTimer tickTimer;
	/* Recording time for the badge, kept from the frontend events (main thread). */
	qint64 recAccumMs = 0;
	qint64 recSegStartMs = 0;
	bool recRunning = false;
	QTimer hudTimer;
	ZoominatorHud *hud = nullptr;
	std::atomic<float> hudZoom{1.0f}; /* the zoom you asked for, as currently shown (1 = not zoomed) */
	void refreshHud();
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
	/* Focus spotlight runtime state (not user settings). Main thread only. */
	struct FocusRuntime {
		obs_source_t *source = nullptr;
		obs_source_t *filter = nullptr;
		int currentOpacity = -1;
		uint64_t appearanceHash = 0;
		bool shown = false;
	} focusRt;
	void publishDimSheet(); /* main thread, not zoomed: the screen's resting rectangle for the dim mask */
	obs_source_t *frameSource = nullptr;
	obs_source_t *guideSource = nullptr;
	obs_source_t *cursorFxSource = nullptr;
	bool cursorFxShown = false;
	obs_source_t *keysSource = nullptr;
	bool keysShown = false;
	/* How many of the overlay items (guide, cursor ring, key display) are in the scene; they share the top of it. */
	int overlayCount() const { return (guideShown ? 1 : 0) + (cursorFxShown ? 1 : 0) + (keysShown ? 1 : 0); }
	class ZoominatorCountdown *countdownWidget = nullptr;
	QTimer countdownTimer;
	int countdownLeft = 0;
	bool guideShown = false;
	obs_source_t *camFilter = nullptr;
	obs_source_t *camFilterTarget = nullptr;
	bool camRingUnavailable = false;
	/* Webcam layout: zoom progress last applied (0..1) and what was applied last,
	 * so the 500 ms refresh only re-applies after a real change and never fights a
	 * drag in OBS. */
	double camProgress = 0.0;
	obs_sceneitem_t *camLayoutItem = nullptr;
	int camLayoutKey[7] = {};
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
