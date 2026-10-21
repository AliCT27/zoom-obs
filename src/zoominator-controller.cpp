/* Core: lifecycle, zoom state machine, per-frame tick and the zoom transform. */

#include "zoominator-internal.hpp"
#include "zoominator-hud.hpp"

bool ZoominatorController::usesWheelZoomGesture() const
{
	/* The wheel-zoom gesture and its settings were removed; X2 toggle is a plain toggle now. */
	return false;
}

/* Modifier + wheel: every notch is one small step. From 1x the first notch
 * only zooms in by one step (it does not jump to the full zoom level), more
 * notches go deeper, wheel down steps back and zooms out fully at 1x. The
 * configured zoom level is put back once the zoom has ended. */
void ZoominatorController::wheelZoom(int direction, int steps)
{
	if (shuttingDown || direction == 0 || steps <= 0)
		return;

	const bool zoomedNow = zoomedView() && animDir.load(std::memory_order_relaxed) >= 0;
	const double step = std::max(0.01, wheelZoomInStep);
	const double maxZoom = std::max(1.1, wheelZoomMaximum);

	if (wheelBaseZoom <= 1.0)
		wheelBaseZoom = std::max(1.1, zoomFactor);

	if (direction > 0) {
		wheelOutAccum = 0.0;
		if (!zoomedNow) {
			zoomFactor = clampd(1.0 + step * steps, 1.05, maxZoom);
			renderedZoomFactor = zoomFactor;
			captureMarkerClickPosition();
			zoomLatched = false;
			manualZoomIn();
			return;
		}
		zoomFactor = clampd(zoomFactor + step * steps, 1.05, maxZoom);
		return;
	}

	if (!zoomedNow)
		return;
	const double next = zoomFactor - step * steps;
	wheelOutAccum += step * steps;
	if ((wheelZoomExit > 0.0 && wheelOutAccum >= wheelZoomExit - 1e-9) || next <= 1.0 + step * 0.5) {
		wheelOutAccum = 0.0;
		manualZoomOut();
		return;
	}
	zoomFactor = next;
}

/* True while the view is zoomed in or zooming in. With "follow mouse sideways"
 * the zoom engine also runs at rest (1x), which does not count. */
bool ZoominatorController::zoomedView() const
{
	if (!zoomActive.load(std::memory_order_acquire))
		return false;
	const int dir = animDir.load(std::memory_order_relaxed);
	return dir > 0 || (dir == 0 && animT > 0.0);
}

/* Enters the always-on 1x state used by "follow mouse sideways": the normal
 * setup in onTick() captures the items, then the transform only pans them. */
void ZoominatorController::startIdlePan()
{
	markRecoveryActive();
	lastTickMs = 0;
	animT = 0.0;
	animDir.store(0, std::memory_order_relaxed);
	panHasPos = false;
	tickingWanted.store(true, std::memory_order_release);
	ensureTicking(true);
}

void ZoominatorController::requestFitScreen()
{
	if (shuttingDown)
		return;
	/* The sideways pan keeps the engine running at rest and would put the old
	 * layout back, so it is switched off; the engine then winds down by itself. */
	idleFollowX = false;
	if (!zoomActive.load(std::memory_order_acquire) && !pendingFinish.load(std::memory_order_acquire)) {
		pendingFit = false;
		fitScreenNow();
		return;
	}
	pendingFit = true;
	lastFitSummary.clear();
	ensureTicking(true);
}

static QString canvasKey(int cw, int ch)
{
	return QStringLiteral("%1x%2").arg(cw).arg(ch);
}

bool ZoominatorController::canvasIs(int w, int h) const
{
	int cw = 0, ch = 0;
	get_canvas_size(cw, ch);
	return cw == w && ch == h;
}

bool ZoominatorController::hasSavedLayoutForCanvas() const
{
	int cw = 0, ch = 0;
	get_canvas_size(cw, ch);
	return savedLayouts.contains(canvasKey(cw, ch));
}

/* Remembers where the target items sit right now (the resting, unzoomed layout)
 * for the current canvas size. While the engine is running the items are being
 * moved by the zoom, so the positions it captured at rest are used instead. */
int ZoominatorController::saveCurrentLayout(int wantW, int wantH)
{
	std::vector<obs_sceneitem_t *> items;
	enumerateTargetItemsInCurrentScene(items);
	int cw = 0, ch = 0;
	get_canvas_size(cw, ch);
	if (cw <= 0 || ch <= 0)
		return 0;
	if (cw != wantW || ch != wantH)
		return -1;

	QVector<SavedItemLayout> list;
	for (obs_sceneitem_t *item : items) {
		obs_source_t *src = item ? obs_sceneitem_get_source(item) : nullptr;
		if (!src)
			continue;
		OrigState o;
		if (zoomActive.load(std::memory_order_acquire)) {
			for (const auto &st : sceneItems) {
				if (st.item == item && st.orig.valid)
					o = st.orig;
			}
		}
		if (!o.valid)
			o = computeOrigState(item);
		if (!o.valid)
			continue;
		SavedItemLayout l;
		l.name = QString::fromUtf8(obs_source_get_name(src));
		l.px = o.effectivePos.x;
		l.py = o.effectivePos.y;
		l.sx = o.effectiveScale.x;
		l.sy = o.effectiveScale.y;
		l.cropL = o.crop.left;
		l.cropT = o.crop.top;
		l.cropR = o.crop.right;
		l.cropB = o.crop.bottom;
		list.push_back(l);
	}
	if (list.isEmpty())
		return 0;
	savedLayouts.insert(canvasKey(cw, ch), list);
	scheduleSettingsSave();
	return (int)list.size();
}

void ZoominatorController::fitScreenNow()
{
	std::vector<obs_sceneitem_t *> items;
	enumerateTargetItemsInCurrentScene(items);
	int cw = 0, ch = 0;
	get_canvas_size(cw, ch);
	if (cw <= 0 || ch <= 0)
		return;
	int fitted = 0;
	const QVector<SavedItemLayout> saved = savedLayouts.value(canvasKey(cw, ch));
	for (obs_sceneitem_t *item : items) {
		obs_source_t *src = item ? obs_sceneitem_get_source(item) : nullptr;
		if (!src)
			continue;
		if (!saved.isEmpty()) {
			const QString name = QString::fromUtf8(obs_source_get_name(src));
			const SavedItemLayout *hit = nullptr;
			for (const auto &l : saved) {
				if (l.name == name)
					hit = &l;
			}
			if (hit) {
				obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
				obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
				obs_sceneitem_set_rot(item, 0.0f);
				obs_sceneitem_crop sc2{};
				sc2.left = hit->cropL;
				sc2.top = hit->cropT;
				sc2.right = hit->cropR;
				sc2.bottom = hit->cropB;
				obs_sceneitem_set_crop(item, &sc2);
				vec2 scl{};
				scl.x = (float)hit->sx;
				scl.y = (float)hit->sy;
				obs_sceneitem_set_scale(item, &scl);
				vec2 p{};
				p.x = (float)hit->px;
				p.y = (float)hit->py;
				obs_sceneitem_set_pos(item, &p);
				lastFitSummary = QStringLiteral("saved layout for %1x%2: scale %3, pos (%4, %5)")
							 .arg(cw)
							 .arg(ch)
							 .arg(hit->sx, 0, 'f', 4)
							 .arg((int)hit->px)
							 .arg((int)hit->py);
				++fitted;
				continue;
			}
		}
		obs_sceneitem_crop crop{};
		obs_sceneitem_get_crop(item, &crop);
		const double w = (double)obs_source_get_width(src) - crop.left - crop.right;
		const double h = (double)obs_source_get_height(src) - crop.top - crop.bottom;
		if (w <= 0.0 || h <= 0.0)
			continue;
		/* Vertical canvas: fill the width. Horizontal canvas: show it all (contain). */
		const double s = (ch > cw) ? (double)cw / w : std::min((double)cw / w, (double)ch / h);
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
		obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		obs_sceneitem_set_rot(item, 0.0f);
		vec2 sc{};
		sc.x = sc.y = (float)s;
		obs_sceneitem_set_scale(item, &sc);
		vec2 pos{};
		pos.x = (float)(((double)cw - w * s) * 0.5);
		pos.y = (float)(((double)ch - h * s) * 0.5);
		obs_sceneitem_set_pos(item, &pos);
		lastFitSummary = QStringLiteral("%1x%2 -> scale %3, pos (%4, %5), canvas %6x%7")
					 .arg((int)w)
					 .arg((int)h)
					 .arg(s, 0, 'f', 4)
					 .arg((int)pos.x)
					 .arg((int)pos.y)
					 .arg(cw)
					 .arg(ch);
		++fitted;
	}
	if (fitted == 0)
		lastFitSummary.clear();
	if (debug)
		blog(LOG_INFO, "[Zoominator] Fitted %d item(s) to the canvas", fitted);
	refreshFrame();
}

void ZoominatorController::beginWheelZoomGesture()
{
	zoomAdjustButtonHeld = true;
	zoomAdjustedDuringButtonHold = false;
	wheelDeltaRemainder = 0;
	captureMarkerClickPosition();
}

void ZoominatorController::adjustActiveZoomFromWheel(int direction, int steps)
{
	if (!zoomAdjustButtonHeld)
		return;
	if (direction == 0 || steps <= 0)
		return;

	const bool wasActive = zoomActive.load(std::memory_order_acquire);
	const double delta = direction > 0 ? wheelZoomInStep * steps : -wheelZoomOutStep * steps;
	zoomFactor = clampd(zoomFactor + delta, wheelZoomMinimum, wheelZoomMaximum);
	zoomAdjustedDuringButtonHold = true;
	if (zoomFactor > 1.0) {
		if (!wasActive || animDir.load(std::memory_order_relaxed) < 0) {
			/* The existing entrance animation supplies the first transition.
			 * Later wheel notches use renderedZoomFactor's smoothing above. */
			if (!wasActive)
				renderedZoomFactor = zoomFactor;
			startZoomIn();
		}
	} else {
		zoomLatched = false;
		startZoomOut();
	}
	scheduleSettingsSave();
	emit settingsChanged();
}

void ZoominatorController::finishWheelZoomGesture()
{
	zoomAdjustButtonHeld = false;
	wheelDeltaRemainder = 0;
	if (zoomAdjustedDuringButtonHold)
		return;

	zoomFactor = 1.0;
	zoomLatched = false;
	startZoomOut();
	scheduleSettingsSave();
	emit settingsChanged();
}


/* Maps linear animation progress 0..1 to eased progress. Only Punch can leave
 * the 0..1 range (a small overshoot on the way in); callers that interpolate
 * positions rather than scale must clamp. */
/* Back-ease-out used by Punch. Rises past 1.0 (peak ~1.05 near t=0.636) and
 * settles back to exactly 1.0 at t=1. */
static inline double punch_curve(double t)
{
	const double c1 = 1.2;
	const double c3 = c1 + 1.0;
	const double u = t - 1.0;
	return 1.0 + c3 * u * u * u + c1 * u * u;
}

static constexpr double kPunchPeakT = 0.636;

/* Inverse of punch_curve on its rising branch [0, kPunchPeakT]. Values at or
 * above 1.0 map to the end of the animation. */
static inline double punch_curve_inverse(double v)
{
	if (v <= 0.0)
		return 0.0;
	if (v >= 1.0)
		return 1.0;
	double lo = 0.0, hi = kPunchPeakT;
	for (int i = 0; i < 40; ++i) {
		const double mid = 0.5 * (lo + hi);
		(punch_curve(mid) < v ? lo : hi) = mid;
	}
	return 0.5 * (lo + hi);
}

/* Inverse of smoothstep on [0,1]. */
static inline double smoothstep_inverse(double y)
{
	y = clampd(y, 0.0, 1.0);
	return 0.5 - std::sin(std::asin(1.0 - 2.0 * y) / 3.0);
}

static inline double apply_easing(ZoominatorController::ZoomEasing easing, double t, bool zoomingIn)
{
	t = clampd(t, 0.0, 1.0);
	switch (easing) {
	case ZoominatorController::ZoomEasing::EaseOut: {
		const double u = 1.0 - t;
		return 1.0 - u * u * u;
	}
	case ZoominatorController::ZoomEasing::Expo:
		if (t <= 0.0)
			return 0.0;
		if (t >= 1.0)
			return 1.0;
		return t < 0.5 ? std::pow(2.0, 20.0 * t - 10.0) * 0.5 : (2.0 - std::pow(2.0, -20.0 * t + 10.0)) * 0.5;
	case ZoominatorController::ZoomEasing::Punch:
		/* Overshoot only when zooming in; running it backwards would make the
		 * zoom-out bulge outwards first. */
		if (zoomingIn)
			return punch_curve(t);
		return smoothstep(t);
	case ZoominatorController::ZoomEasing::Smooth:
		break;
	}
	return smoothstep(t);
}


ZoominatorController &ZoominatorController::instance()
{
	static ZoominatorController inst;
	return inst;
}


ZoominatorController::ZoominatorController()
{
	tickTimer.setInterval(16);
	tickTimer.setTimerType(Qt::PreciseTimer);
	connect(&tickTimer, &QTimer::timeout, this, &ZoominatorController::onTick);
	hudTimer.setInterval(100);
	connect(&hudTimer, &QTimer::timeout, this, [this]() { refreshHud(); });
	hudTimer.start();
	frameTimer.setInterval(500);
	connect(&frameTimer, &QTimer::timeout, this, [this]() {
		refreshFrame();
		refreshCamRing();
	});
}

ZoominatorController::~ZoominatorController()
{
	markerSource = nullptr;
}


void ZoominatorController::initialize()
{
	loadSettings();
	rebuildTriggersFromSettings();
	obs_frontend_add_event_callback(frontendEventCallback, this);
	obs_add_tick_callback(videoTickCallback, this);
	tickCallbackAdded = true;
	frameTimer.start();
	zoominator_caret::start();
	QTimer::singleShot(1500, this, [this]() { installHooks(); });
	QTimer::singleShot(3000, this, [this]() { requestRecoveryRestore(); });
	QTimer::singleShot(5000, this, [this]() { cleanup_legacy_marker_items_all_scenes(markerSource); });
}

void ZoominatorController::shutdown()
{
	shuttingDown = true;
	/* Must come first: obs_remove_tick_callback blocks until any in-flight tick
	 * returns, so after this the graphics thread can no longer touch our state
	 * while the teardown below dismantles it. */
	if (tickCallbackAdded) {
		obs_remove_tick_callback(videoTickCallback, this);
		tickCallbackAdded = false;
	}
	zoomActive.store(false, std::memory_order_release);
	tickingWanted.store(false, std::memory_order_release);
	obs_frontend_remove_event_callback(frontendEventCallback, this);
	frameTimer.stop();
	hudTimer.stop();
	if (hud) {
		delete hud;
		hud = nullptr;
	}
	uninstallHooks();
	zoominator_caret::stop();
	ensureTicking(false);
	detachBlurFilters();
	detachIdleBlurs();
	detachTilt();
	releaseSoundSources();
	zoominator_frame::clearRect();
	if (camFilter) {
		zoominator_camring::detach(camFilterTarget, camFilter);
		camFilter = nullptr;
	}
	if (camFilterTarget) {
		obs_source_release(camFilterTarget);
		camFilterTarget = nullptr;
	}

	obs_source_t *staleRef = nullptr;
	{
		std::lock_guard<std::mutex> lock(inputMutex);
		staleRef = input.sceneRef;
		input.sceneRef = nullptr;
	}
	if (staleRef)
		obs_source_release(staleRef);
	if (dialog)
		dialog->close();
}

void ZoominatorController::showDialog()
{
	if (!dialog) {
		dialog = new ZoominatorDialog(reinterpret_cast<QWidget *>(obs_frontend_get_main_window()));
		dialog->setAttribute(Qt::WA_DeleteOnClose, true);
		connect(dialog.data(), &QObject::destroyed, this, [this]() { dialog = nullptr; });
	}
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}


void ZoominatorController::rebuildRuntimeHooks()
{
	camRingUnavailable = false;
	refreshFrame();
	refreshCamRing();
	ensureSoundSources();
	rebuildTriggersFromSettings();
	uninstallHooks();
	installHooks();
	refreshIdleFocusTicking();
}

void ZoominatorController::ensureTicking(bool on)
{
	if (on) {
		if (!tickTimer.isActive())
			tickTimer.start();
	} else {
		if (tickTimer.isActive())
			tickTimer.stop();
	}
}

void ZoominatorController::startZoomIn()
{
	if (animDir.load(std::memory_order_relaxed) <= 0)
		playSound(0);
	markRecoveryActive();
	lastTickMs = 0;
	pendingFinish.store(false, std::memory_order_release);
	animDir.store(+1, std::memory_order_relaxed);
	tickingWanted.store(true, std::memory_order_release);
	ensureTicking(true);
	/* Sample immediately so the first rendered frame after the trigger already
	 * has a scene and a cursor position, rather than waiting for the Qt timer. */
	onTick();
}

void ZoominatorController::startZoomOut()
{
	if (animDir.load(std::memory_order_relaxed) >= 0 && zoomedView())
		playSound(1);
	lastTickMs = 0;
	animDir.store(-1, std::memory_order_relaxed);
	tickingWanted.store(true, std::memory_order_release);
	{
		std::lock_guard<std::mutex> lock(inputMutex);
		mk.flashStartMs = 0;
		mk.flashHoldUntilMs = 0;
		mk.flashFadeOutEndMs = 0;
		mk.clickHasPos = false;
	}
	ensureTicking(true);
}

void ZoominatorController::resetState()
{
	detachBlurFilters();
	detachIdleBlurs();
	zoomPressed = false;
	zoomLatched = false;
	zoomActive.store(false, std::memory_order_release);
	restoreCamItem();
	tickingWanted.store(false, std::memory_order_release);
	pendingFinish.store(false, std::memory_order_release);
	animT = 0.0;
	renderedZoomFactor = 1.0;
	hudZoom.store(1.0f, std::memory_order_relaxed);
	if (wheelBaseZoom > 1.0) {
		zoomFactor = wheelBaseZoom;
		wheelBaseZoom = 0.0;
	}
	animDir.store(0, std::memory_order_relaxed);
	followHasPos = false;
	panHasPos = false;
	lastCursorSampleValid = false;
	lastCursorMovementMs = 0;
	mouseTrackingIdle = false;
	targetHasPos = false;
	tickDeltaSeconds = 1.0 / 60.0;
	lastTickMs = 0;
	lastTransformApplyMs = 0;
	lastFollowAnchorValid = false;
	sceneItems.clear();
	sceneContentBoundsValid = false;
	sceneContentMin = {};
	sceneContentMax = {};
	mk.currentOpacity = -1;
	{
		std::lock_guard<std::mutex> lock(inputMutex);
		mk.flashStartMs = 0;
		mk.flashHoldUntilMs = 0;
		mk.flashFadeOutEndMs = 0;
		mk.clickHasPos = false;
	}
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	if (sceneSource) {
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		if (scene)
			hideMarkerInScene(scene);
		obs_source_release(sceneSource);
	}
}


void ZoominatorController::applyZoomToScene(double t)
{
	if (sceneItems.empty())
		return;

	/* Scene and cursor come from the snapshot the Qt timer publishes; both
	 * obs_frontend_get_current_scene() and QCursor::pos() are main-thread only.
	 * Take our own ref under the lock so the main thread cannot release the
	 * scene out from under us mid-frame. */
	obs_source_t *sceneSource = nullptr;
	float snapX = 0.0f, snapY = 0.0f;
	bool snapMapped = false, snapInside = false;
	{
		std::lock_guard<std::mutex> lock(inputMutex);
		if (input.sceneRef)
			sceneSource = obs_source_get_ref(input.sceneRef);
		snapMapped = input.mapped;
		snapInside = input.inside;
		snapX = input.sceneX;
		snapY = input.sceneY;
	}

	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (!scene) {
		if (sceneSource)
			obs_source_release(sceneSource);
		return;
	}

	/* Released at every exit below; applyZoomToScene has several. */
	struct SceneRefGuard {
		obs_source_t *ref;
		~SceneRefGuard()
		{
			if (ref)
				obs_source_release(ref);
		}
	} sceneRefGuard{sceneSource};

	std::vector<obs_sceneitem_t *> liveItems;
	collect_live_scene_items(scene, liveItems);
	auto isLiveItem = [&liveItems](obs_sceneitem_t *item) {
		return item && std::find(liveItems.begin(), liveItems.end(), item) != liveItems.end();
	};

	const double tt = apply_easing(easing, t, animDir.load(std::memory_order_relaxed) >= 0);
	/* Position blends must stay inside 0..1 even when the scale overshoots. */
	const double ttA = clampd(tt, 0.0, 1.0);

	obs_video_info ovi{};
	const bool haveVi = obs_get_video_info(&ovi);
	const double cw = haveVi ? (double)ovi.base_width : 1920.0;
	const double ch = haveVi ? (double)ovi.base_height : 1080.0;

	/* Portrait cover: on a canvas taller than wide, a landscape capture is
	 * letterboxed. While active, add whatever extra scale is needed for the
	 * captured content to fill the canvas, so the view behaves like a camera
	 * panning across the capture instead of leaving gaps above and below. */
	double coverBase = 1.0;
	if (portraitCover && ch > cw && sceneContentBoundsValid) {
		const double contentW = std::max(1.0, (double)sceneContentMax.x - (double)sceneContentMin.x);
		const double contentH = std::max(1.0, (double)sceneContentMax.y - (double)sceneContentMin.y);
		coverBase = std::max(1.0, std::max(cw / contentW, ch / contentH));
	}
	const bool coverActive = coverBase > 1.0001;

	const double zBase = (renderedZoomFactor <= 1.0) ? 1.0 : renderedZoomFactor;
	const double zTarget = zBase * coverBase;
	hudZoom.store((float)(1.0 + (zBase - 1.0) * tt), std::memory_order_relaxed);
	const double z = 1.0 + (zTarget - 1.0) * tt;
	const double centerX = cw * 0.5;
	const double centerY = ch * 0.5;

	float fx = (float)centerX;
	float fy = (float)centerY;
	float anchorX = (float)centerX;
	float anchorY = (float)centerY;
	float markerSceneX = fx;
	float markerSceneY = fy;
	bool markerHasPoint = false;

	const float mx = snapX;
	const float my = snapY;
	const bool mapped = snapMapped;

	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
	if (mapped) {
		const double sampleDx = (double)mx - (double)lastCursorSampleX;
		const double sampleDy = (double)my - (double)lastCursorSampleY;
		const bool cursorMoved = !lastCursorSampleValid || sampleDx * sampleDx + sampleDy * sampleDy >= 0.25;
		if (cursorMoved) {
			lastCursorSampleX = mx;
			lastCursorSampleY = my;
			lastCursorSampleValid = true;
			lastCursorMovementMs = nowMs;
			mouseTrackingIdle = false;
		} else if (mouseIdleTimeoutMs > 0 && lastCursorMovementMs > 0 &&
			   nowMs - lastCursorMovementMs >= mouseIdleTimeoutMs) {
			mouseTrackingIdle = true;
		}
	}

	const double marginRatio = (double)edgeOverflowMarginPct / 100.0;
	const bool allowOverflow = edgeOverflowMarginPct > 0;
	/* Re-centre the view on the cursor when the margin is enabled or when cover
	 * mode is zooming far past the canvas; the clamp below still keeps the
	 * capture covering the canvas. */
	const bool centerAnchor = allowOverflow || coverActive || centerFocus;

	if (zoomAnchor == ZoomAnchorMode::CursorFollow && followMouseRuntimeEnabled && !mouseTrackingIdle) {
		targetHasPos = false;
		if (mapped) {
			if (!followHasPos) {
				followX = mx;
				followY = my;
				followHasPos = true;
			} else {
				const double dx = (double)mx - (double)followX;
				const double dy = (double)my - (double)followY;
				const double dist = std::sqrt(dx * dx + dy * dy);

				const double effectiveSpeed = clampd(followSpeed, 0.25, 30.0);
				if (dist > 0.01) {
					const double response =
						1.0 - std::exp(-(2.0 + effectiveSpeed * 1.35) * tickDeltaSeconds);
					const double step = clampd(response, 0.01, 0.92);
					followX = (float)((double)followX + dx * step);
					followY = (float)((double)followY + dy * step);
				}
			}
			fx = followX;
			fy = followY;
			if (centerAnchor) {
				anchorX = (float)((double)followX + ((double)centerX - (double)followX) * ttA);
				anchorY = (float)((double)followY + ((double)centerY - (double)followY) * ttA);
			} else {
				/* Scale around the tracked pointer itself. Anchoring at the canvas
				 * centre makes the first zoom frame pull content sideways before
				 * follow tracking catches up, especially when the pointer starts
				 * near an edge. */
				anchorX = followX;
				anchorY = followY;
			}
		} else if (followHasPos) {
			fx = followX;
			fy = followY;
			if (centerAnchor) {
				anchorX = (float)((double)followX + ((double)centerX - (double)followX) * ttA);
				anchorY = (float)((double)followY + ((double)centerY - (double)followY) * ttA);
			} else {
				anchorX = followX;
				anchorY = followY;
			}
		}
	} else {
		if (!targetHasPos) {
			if (followHasPos) {
				/* Follow mode frozen mid-zoom by the runtime toggle or the
				 * idle timeout: hold the view where the cursor last led it. */
				targetX = followX;
				targetY = followY;
			} else if (mapped) {
				/* CursorStatic: anchor on wherever the cursor was when the
				 * zoom was triggered, then stay put. */
				targetX = mx;
				targetY = my;
			} else {
				targetX = (float)centerX;
				targetY = (float)centerY;
			}
			targetHasPos = true;
		}
		fx = targetX;
		fy = targetY;
		/* The anchor slides from the focal point to the canvas centre with the zoom, so at rest (no
		 * zoom) the transform is the identity. Anchoring at the centre from the start shifts the
		 * whole capture by (centre - focal) while idle - e.g. after Follow mouse (Alt+C) froze the
		 * focal point - and only the edge clamp hides it. */
		anchorX = (float)((double)targetX + ((double)centerX - (double)targetX) * ttA);
		anchorY = (float)((double)targetY + ((double)centerY - (double)targetY) * ttA);
	}

	if (showCursorMarker) {
		std::lock_guard<std::mutex> lock(inputMutex);
		if (mk.clickHasPos) {
			markerSceneX = mk.clickX;
			markerSceneY = mk.clickY;
			markerHasPoint = true;
		}
	}

	const double baseMinX = sceneContentBoundsValid ? (double)sceneContentMin.x : 0.0;
	const double baseMinY = sceneContentBoundsValid ? (double)sceneContentMin.y : 0.0;
	const double baseMaxX = sceneContentBoundsValid ? (double)sceneContentMax.x : cw;
	const double baseMaxY = sceneContentBoundsValid ? (double)sceneContentMax.y : ch;
	const double refMinX = (double)anchorX + (baseMinX - (double)fx) * z;
	const double refMaxX = (double)anchorX + (baseMaxX - (double)fx) * z;
	const double refMinY = (double)anchorY + (baseMinY - (double)fy) * z;
	const double refMaxY = (double)anchorY + (baseMaxY - (double)fy) * z;
	const double minOffsetX = cw - refMaxX;
	const double maxOffsetX = -refMinX;
	const double minOffsetY = ch - refMaxY;
	const double maxOffsetY = -refMinY;

	const double overflowLimitX = cw * marginRatio;
	const double overflowLimitY = ch * marginRatio;

	const double minOffsetXClamped = minOffsetX - overflowLimitX;
	const double maxOffsetXClamped = maxOffsetX + overflowLimitX;
	const double minOffsetYClamped = minOffsetY - overflowLimitY;
	const double maxOffsetYClamped = maxOffsetY + overflowLimitY;

	double offsetX = 0.0;
	double offsetY = 0.0;

	if (minOffsetXClamped <= maxOffsetXClamped) {
		offsetX = clampd(0.0, minOffsetXClamped, maxOffsetXClamped);
	} else {
		offsetX = (minOffsetXClamped + maxOffsetXClamped) * 0.5;
	}

	if (minOffsetYClamped <= maxOffsetYClamped) {
		offsetY = clampd(0.0, minOffsetYClamped, maxOffsetYClamped);
	} else {
		offsetY = (minOffsetYClamped + maxOffsetYClamped) * 0.5;
	}

	/* Centre-focus: no edge clamp at all, so the focus point stays in the middle
	 * of the canvas. Near the capture edges this shows empty canvas. */
	if (centerFocus) {
		offsetX = 0.0;
		offsetY = 0.0;
	}

	/* Follow mouse sideways: pan the capture so the cursor stays in the middle
	 * horizontally (clamped so the capture still covers the canvas). The pan fades
	 * out as the zoom comes in; with the cursor centred the two blend seamlessly. */
	if (idleFollowX) {
		double panFocal = 0.0;
		bool havePan = false;
		if (zoomAnchor == ZoomAnchorMode::CursorFollow) {
			/* Same focal point the zoom uses, so the pan fades into the zoom
			 * seamlessly and "Follow mouse" off (Alt+C) freezes the view. */
			panFocal = fx;
			havePan = true;
		} else {
			if (mapped && animT <= 0.0) {
				if (!panHasPos) {
					panFollowX = mx;
					panHasPos = true;
				} else {
					const double speed = clampd(followSpeed, 0.25, 30.0);
					const double resp = clampd(1.0 - std::exp(-(2.0 + speed * 1.35) * tickDeltaSeconds), 0.01, 0.92);
					panFollowX += ((double)mx - panFollowX) * resp;
				}
			}
			panFocal = panFollowX;
			havePan = panHasPos;
		}
		if (havePan && baseMaxX - baseMinX > cw + 1.0) {
			const double panX = clampd(centerX - panFocal, cw - baseMaxX, -baseMinX);
			offsetX += panX * (1.0 - ttA);
		}
	}

	const qint64 nowApplyMs = nowMs;
	const bool steadyFollow = zoomAnchor == ZoomAnchorMode::CursorFollow && followMouseRuntimeEnabled &&
				  !mouseTrackingIdle && animDir.load(std::memory_order_relaxed) == 0 && animT >= 0.999;
	if (steadyFollow) {
		const float dx = fx - lastFollowAnchorX;
		const float dy = fy - lastFollowAnchorY;
		const bool followMovedEnough = !lastFollowAnchorValid || ((dx * dx + dy * dy) >= 1.0f);
		if (!followMovedEnough && nowApplyMs - lastTransformApplyMs < 16) {
			std::lock_guard<std::mutex> lock(inputMutex);
			pendingMarkerVisible = showCursorMarker && markerHasPoint;
			pendingMarkerZ = z;
			pendingMarkerZ = z;
		pendingMarkerX = (double)anchorX + ((double)markerSceneX - (double)fx) * z + offsetX;
			pendingMarkerY = (double)anchorY + ((double)markerSceneY - (double)fy) * z + offsetY;
				pendingFocusVisible = focusEnabled && mapped && snapInside;
			pendingFocusFresh = true;
			pendingFocusX = (double)anchorX + ((double)mx - (double)fx) * z + offsetX;
			pendingFocusY = (double)anchorY + ((double)my - (double)fy) * z + offsetY;
				pendingFocusOpacity = (int)std::lround(ttA * 255.0);
			return;
		}
	}

	lastTransformApplyMs = nowApplyMs;
	lastFollowAnchorX = fx;
	lastFollowAnchorY = fy;
	lastFollowAnchorValid = true;

	const uint32_t topLeftAlign = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	for (auto &state : sceneItems) {
		if (!state.item || !state.orig.valid || !isLiveItem(state.item))
			continue;

		if (!state.normalized) {
			if (obs_sceneitem_get_bounds_type(state.item) != OBS_BOUNDS_NONE)
				obs_sceneitem_set_bounds_type(state.item, OBS_BOUNDS_NONE);
			if (obs_sceneitem_get_alignment(state.item) != topLeftAlign)
				obs_sceneitem_set_alignment(state.item, topLeftAlign);
			state.normalized = true;
		}

		vec2 sc{};
		sc.x = state.orig.effectiveScale.x * (float)z;
		sc.y = state.orig.effectiveScale.y * (float)z;

		vec2 pos{};
		pos.x = (float)((double)anchorX + ((double)state.orig.effectivePos.x - (double)fx) * z + offsetX);
		pos.y = (float)((double)anchorY + ((double)state.orig.effectivePos.y - (double)fy) * z + offsetY);

		if (!state.lastAppliedValid || !nearly_equal_vec2(state.lastAppliedScale, sc, kScaleEpsilon)) {
			obs_sceneitem_set_scale(state.item, &sc);
			state.lastAppliedScale = sc;
		}

		if (!state.lastAppliedValid || !nearly_equal_vec2(state.lastAppliedPos, pos)) {
			obs_sceneitem_set_pos(state.item, &pos);
			state.lastAppliedPos = pos;
		}

		state.lastAppliedValid = true;

		if (state.blurFilter) {
			/* The cursor is in unzoomed scene coordinates, so mapping it through the
			 * item's original transform gives the source pixel under it directly;
			 * only sizes depend on the current zoom. Rotation is ignored. */
			const float osx = std::max(state.orig.effectiveScale.x, 0.0001f);
			const float osy = std::max(state.orig.effectiveScale.y, 0.0001f);
			const float cxSrc = (mx - state.orig.effectivePos.x) / osx + (float)state.orig.crop.left;
			const float cySrc = (my - state.orig.effectivePos.y) / osy + (float)state.orig.crop.top;
			const float shown = std::max(0.0001f, (osx + osy) * 0.5f * (float)z);
			const float blurSrc = std::min(12.0f, (float)focusBlurPx / shown);
			const bool on = focusEnabled && mapped && snapInside;
			if (showFocusWhenNotZoomed) {
				/* Stays fully on and only the radius eases between the two sizes. */
				const double idleS = focusIdleScale();
				const double rad = (double)coneRadius(focusIdleRadius) + ((double)coneRadius(focusRadius) - (double)coneRadius(focusIdleRadius)) * ttA;
				const double softK = idleS + (1.0 - idleS) * ttA;
				zoominator_blur::setParams(state.blurFilter, cxSrc, cySrc, (float)rad / shown,
							   (float)(coneSoftness() * softK) / shown, blurSrc,
							   on ? 1.0f : 0.0f);
			} else {
				zoominator_blur::setParams(state.blurFilter, cxSrc, cySrc, (float)coneRadius(focusRadius) / shown,
							   (float)coneSoftness() / shown, blurSrc, on ? (float)ttA : 0.0f);
			}
		}
	}

	/* Webcam: grows or shrinks around its own centre with the zoom. */
	if (camItem && isLiveItem(camItem)) {
		obs_source_t *camSrc = obs_sceneitem_get_source(camItem);
		const double kk = 1.0 + ((double)camZoomPct / 100.0 - 1.0) * ttA;
		const float rw = std::max(1.0f, (float)obs_source_get_width(camSrc) - (float)camOrig.crop.left -
							(float)camOrig.crop.right) *
				 camOrig.effectiveScale.x;
		const float rh = std::max(1.0f, (float)obs_source_get_height(camSrc) - (float)camOrig.crop.top -
							(float)camOrig.crop.bottom) *
				 camOrig.effectiveScale.y;
		vec2 csc{};
		csc.x = camOrig.effectiveScale.x * (float)kk;
		csc.y = camOrig.effectiveScale.y * (float)kk;
		vec2 cpos{};
		cpos.x = camOrig.effectivePos.x + rw * 0.5f - rw * (float)kk * 0.5f;
		cpos.y = camOrig.effectivePos.y + rh * 0.5f - rh * (float)kk * 0.5f;
		if (!camLastValid) {
			obs_sceneitem_set_bounds_type(camItem, OBS_BOUNDS_NONE);
			obs_sceneitem_set_alignment(camItem, topLeftAlign);
		}
		if (!camLastValid || !nearly_equal_vec2(camLastScale, csc, kScaleEpsilon)) {
			obs_sceneitem_set_scale(camItem, &csc);
			camLastScale = csc;
		}
		if (!camLastValid || !nearly_equal_vec2(camLastPos, cpos)) {
			obs_sceneitem_set_pos(camItem, &cpos);
			camLastPos = cpos;
		}
		camLastValid = true;
	}

	/* Frame: the screen's rectangle as it appears right now. */
	if (frameEnabled || tilt3dEnabled || focusEnabled) {
		vec2 fmn{}, fmx{};
		bool fvalid = false;
		for (auto &state : sceneItems) {
			if (!state.item || !state.orig.valid || !state.lastAppliedValid || !isLiveItem(state.item))
				continue;
			if (!obs_sceneitem_visible(state.item))
				continue;
			OrigState moved = state.orig;
			moved.effectivePos = state.lastAppliedPos;
			moved.effectiveScale = state.lastAppliedScale;
			accumulateItemBounds(moved, obs_sceneitem_get_source(state.item), fmn, fmx, fvalid);
		}
		if (fvalid && focusEnabled)
			zoominator_dimmask::setSheet(fmn.x, fmn.y, fmx.x, fmx.y);
		if (fvalid && frameEnabled)
			zoominator_frame::setRect(fmn.x, fmn.y, fmx.x, fmx.y);
		if (fvalid && tilt3dEnabled)
			zoominator_tilt::setSheet(fmn.x, fmn.y, fmx.x, fmx.y);
	}

	/* Marker placement is published rather than applied: creating the marker
	 * source, rebuilding its image and setting filter values all mutate the
	 * scene graph, which must not happen from the graphics tick. The Qt timer
	 * picks this up. A cursor dot does not need frame-perfect timing; the zoom
	 * transform above does, which is the whole point of this split. */
	{
		std::lock_guard<std::mutex> lock(inputMutex);
		pendingMarkerVisible = showCursorMarker && markerHasPoint;
		pendingMarkerX = (double)anchorX + ((double)markerSceneX - (double)fx) * z + offsetX;
		pendingMarkerY = (double)anchorY + ((double)markerSceneY - (double)fy) * z + offsetY;
		pendingFocusVisible = focusEnabled && mapped && snapInside;
		pendingFocusX = (double)anchorX + ((double)mx - (double)fx) * z + offsetX;
		pendingFocusY = (double)anchorY + ((double)my - (double)fy) * z + offsetY;
		pendingFocusOpacity = (int)std::lround(ttA * 255.0);
	}
}

/* Main-thread half of the loop. Samples everything the graphics thread is not
 * allowed to touch and publishes it. Deliberately does no animation work. */
void ZoominatorController::onTick()
{
	if (pendingFinish.exchange(false)) {
		finishZoomOnMainThread();
		return;
	}

	checkAutoIdle();
	updateTilt();

	if (pendingFit && !zoomActive.load(std::memory_order_acquire) && !pendingFinish.load(std::memory_order_acquire)) {
		pendingFit = false;
		fitScreenNow();
	}

	if (idleFollowX && !shuttingDown && !tickingWanted.load(std::memory_order_acquire) &&
	    !zoomActive.load(std::memory_order_acquire)) {
		const qint64 nowTry = QDateTime::currentMSecsSinceEpoch();
		if (nowTry - lastIdlePanTryMs >= 500) {
			lastIdlePanTryMs = nowTry;
			startIdlePan();
		}
	}

	const bool zoomTickWanted = tickingWanted.load(std::memory_order_acquire);
	const bool idleFocus = !zoomTickWanted && wantsIdleFocus();
	if (idleFocus || (!zoomTickWanted && (focusRt.shown || !idleBlurs.empty())))
		updateIdleFocus(idleFocus);
	if (!zoomTickWanted && !mk.clickHasPos) {
		if (!idleFocus && !idleFollowX && !tilt3dEnabled)
			ensureTicking(false);
		return;
	}
	if (!zoomTickWanted && !showMarkerWhenNotZoomed) {
		std::lock_guard<std::mutex> lock(inputMutex);
		mk.clickHasPos = false;
		mk.flashStartMs = 0;
		mk.flashHoldUntilMs = 0;
		mk.flashFadeOutEndMs = 0;
		if (!idleFocus && !idleFollowX && !tilt3dEnabled)
			ensureTicking(false);
		return;
	}

	/* A click halo has its own short lifetime and must keep rendering even when
	 * no zoom animation is active. Its captured position is already in canvas
	 * coordinates, so this path can display and fade it entirely on the main
	 * thread without starting or mutating zoom state. */
	if (!zoomTickWanted) {
		const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
		obs_source_t *sceneSource = obs_frontend_get_current_scene();
		if (obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr) {
			const int opacity = currentMarkerOpacity(nowMs);
			if (showCursorMarker && opacity > 0)
				{
				double hx = mk.clickX, hy = mk.clickY;
				applyTiltToIdlePoint(hx, hy);
				updateMarkerPosition(scene, hx, hy, opacity, 1.0);
			}
			else
				hideMarkerInScene(scene);
		}
		if (sceneSource)
			obs_source_release(sceneSource);

		if (!isMarkerFlashActive(nowMs)) {
			mk.clickHasPos = false;
			mk.flashStartMs = 0;
			mk.flashHoldUntilMs = 0;
			mk.flashFadeOutEndMs = 0;
			if (!idleFocus && !idleFollowX && !tilt3dEnabled)
				ensureTicking(false);
		}
		return;
	}

	/* While the engine only idles at 1x (sideways follow) the items are expected to sit exactly
	 * where it last put them. If one was moved or resized from outside (Fit screen, Edit
	 * Transform, dragging in the preview) the captured original layout is stale and the engine
	 * would drag the item back to it - so the new layout is adopted as the original. The change
	 * has to be seen twice in a row, unchanged, to rule out racing the graphics thread. */
	const qint64 extNowMs = QDateTime::currentMSecsSinceEpoch();
	if (zoomActive.load(std::memory_order_acquire) && !pendingFinish.load(std::memory_order_acquire) &&
	    animDir.load(std::memory_order_relaxed) == 0 && animT <= 0.0 && !sceneItems.empty() &&
	    extNowMs - lastExtCheckMs >= 150) {
		/* Not every tick: this walks the scene under its lock, which the idle pan must not wait on. */
		lastExtCheckMs = extNowMs;
		bool adopt = false;
		std::vector<obs_sceneitem_t *> liveNow;
		if (obs_source_t *cs = obs_frontend_get_current_scene()) {
			if (obs_scene_t *csc = obs_scene_from_source(cs))
				collect_live_scene_items(csc, liveNow);
			obs_source_release(cs);
		}
		for (auto &st : sceneItems) {
			if (!st.item || !st.lastAppliedValid ||
			    std::find(liveNow.begin(), liveNow.end(), st.item) == liveNow.end())
				continue;
			vec2 lp{}, ls{};
			obs_sceneitem_get_pos(st.item, &lp);
			obs_sceneitem_get_scale(st.item, &ls);
			const bool differs = !nearly_equal_vec2(lp, st.lastAppliedPos) ||
					     !nearly_equal_vec2(ls, st.lastAppliedScale, kScaleEpsilon);
			if (!differs) {
				st.extSeen = false;
				continue;
			}
			if (st.extSeen && nearly_equal_vec2(lp, st.extPos) && nearly_equal_vec2(ls, st.extScale, kScaleEpsilon))
				adopt = true;
			st.extSeen = true;
			st.extPos = lp;
			st.extScale = ls;
		}
		if (adopt) {
			detachBlurFilters();
			detachIdleBlurs();
			zoomActive.store(false, std::memory_order_release);
			sceneItems.clear();
			sceneContentBoundsValid = false;
			panHasPos = false;
		}
	}

	if (!zoomActive.load(std::memory_order_acquire)) {
		std::vector<obs_sceneitem_t *> items;
		enumerateTargetItemsInCurrentScene(items);
		if (items.empty()) {
			if (debug)
				blog(LOG_WARNING, "[Zoominator] No movable scene items in current scene.");
			tickingWanted.store(false, std::memory_order_release);
			if (!idleFollowX && !tilt3dEnabled)
				ensureTicking(false);
			resetState();
			return;
		}

		/* Runs before zoomActive is published, so the graphics thread cannot be
		 * reading sceneItems while this mutates it. captureOriginalSceneItems
		 * may also saveSettings(), which does file IO - another reason it has
		 * to stay off the render path. */
		detachIdleBlurs();
		detachBlurFilters();
		captureOriginalSceneItems(items);
		if (sceneItems.empty())
			return;
		if (obs_source_t *capScene = obs_frontend_get_current_scene()) {
			captureCamItem(obs_scene_from_source(capScene));
			obs_source_release(capScene);
		}
		attachBlurFilters();
	}

	obs_source_t *sceneSource = obs_frontend_get_current_scene();

	/* All marker scene-graph mutation happens here, on the main thread, using
	 * the placement the graphics tick published. */
	if (obs_scene_t *sc = sceneSource ? obs_scene_from_source(sceneSource) : nullptr) {
		bool markerVisible = false;
		double markerX = 0.0, markerY = 0.0, markerZ = 1.0;
		bool focusVisible = false;
		double focusX = 0.0, focusY = 0.0;
		int focusOpacity = 0;
		bool focusFresh = false;
		{
			std::lock_guard<std::mutex> lock(inputMutex);
			markerVisible = pendingMarkerVisible;
			markerX = pendingMarkerX;
			markerY = pendingMarkerY;
			markerZ = pendingMarkerZ;
			focusVisible = pendingFocusVisible;
			focusX = pendingFocusX;
			focusY = pendingFocusY;
			focusOpacity = pendingFocusOpacity;
			focusFresh = pendingFocusFresh;
		}

		/* With "show focus when not zoomed" the spotlight stays fully visible while
		 * zooming and only its radius eases; otherwise it fades with the zoom. */
		applyTiltToIdlePoint(focusX, focusY);
		applyTiltToIdlePoint(markerX, markerY);
		const bool keepIdle = showFocusWhenNotZoomed && wantsDim();
		if (wantsDim() && focusVisible && (focusOpacity > 0 || keepIdle)) {
			updateFocus(sc, focusX, focusY, keepIdle ? 255 : focusOpacity, false, focusOpacity);
		} else if (keepIdle && !focusFresh) {
			/* The zoom just started and has not produced a sample yet: leave the
			 * spotlight where it is instead of blinking it off. */
		} else if (focusRt.shown) {
			hideFocusInScene(sc);
			focusRt.shown = false;
		}

		/* The engine also runs at 1x (sideways follow), which is not zoomed: there the halo obeys "Show halo
		 * when not zoomed" and uses the not-zoomed size. */
		const bool zoomedNow = zoomedView() || animT > 0.001 || (mk.flashStartedZoomed.load(std::memory_order_relaxed) &&
					   isMarkerFlashActive(QDateTime::currentMSecsSinceEpoch()));
		if (showCursorMarker && markerVisible && (zoomedNow || showMarkerWhenNotZoomed)) {
			const int opacity = currentMarkerOpacity(QDateTime::currentMSecsSinceEpoch());
			if (opacity > 0)
				updateMarkerPosition(sc, markerX, markerY, opacity, std::max(1.0, markerZ));
			else
				hideMarkerInScene(sc);
		} else if (showCursorMarker) {
			hideMarkerInScene(sc);
		}
	}

	int cx = 0, cy = 0;
	float sx = 0.0f, sy = 0.0f;
	bool insideNow = false;
	/* While typing in auto mode, aim at the text caret instead of the mouse so the
	 * view follows what is being written. A click or mouse-driven zoom, or an app
	 * that does not expose its caret, leaves the mouse in charge. */
	bool haveAim = false;
	if (caretFollow && triggerType == "auto") {
		const qint64 nowAimMs = QDateTime::currentMSecsSinceEpoch();
		if (autoLastTypingMs > 0 && nowAimMs - autoLastTypingMs < 1500 && autoLastTypingMs >= autoLastClickMs)
			haveAim = zoominator_caret::latestLogical(cx, cy, 600);
	}
	if (!haveAim)
		haveAim = getCursorPos(cx, cy);
	const bool mappedNow = haveAim && mapCursorToScenePixels(cx, cy, sx, sy, insideNow);

	obs_source_t *previousRef = nullptr;
	{
		std::lock_guard<std::mutex> lock(inputMutex);
		previousRef = input.sceneRef;
		input.sceneRef = sceneSource; /* ownership transferred */
		input.mapped = mappedNow;
		input.inside = insideNow;
		input.sceneX = sx;
		input.sceneY = sy;
	}
	if (previousRef)
		obs_source_release(previousRef);

	/* Published last: the graphics thread may start reading sceneItems the
	 * instant this lands, and by now it has a scene and a cursor sample. */
	zoomActive.store(true, std::memory_order_release);
}

/* Graphics-thread half. Called once per rendered frame with the real frame
 * delta, so motion is correct at any output frame rate. */
void ZoominatorController::videoTickCallback(void *param, float seconds)
{
	auto *self = static_cast<ZoominatorController *>(param);
	if (self)
		self->videoTick((double)seconds);
}

void ZoominatorController::videoTick(double seconds)
{
	if (!zoomActive.load(std::memory_order_acquire) || shuttingDown)
		return;

	/* OBS hands us the true frame delta. Clamp only to survive a stalled
	 * frame, not to impose a rate of our own. */
	tickDeltaSeconds = clampd(seconds, 1.0 / 480.0, 1.0 / 20.0);

	const int dir = animDir.load(std::memory_order_relaxed);
	/* Wheel notches change the target immediately, but the rendered scale
	 * approaches it exponentially. This keeps consecutive notches fluid and
	 * remains frame-rate independent. The normal zoom-out animation owns the
	 * path back to 1x, so do not also shrink the target during that animation. */
	if (dir >= 0 && zoomFactor > 1.0) {
		const double response = 1.0 - std::exp(-kWheelZoomSmoothingRate * tickDeltaSeconds);
		renderedZoomFactor += (zoomFactor - renderedZoomFactor) * response;
		if (std::fabs(zoomFactor - renderedZoomFactor) < 0.0001)
			renderedZoomFactor = zoomFactor;
	}
	/* Punch uses a different curve in than out. If the direction flips while
	 * the animation is in flight (click during a zoom-out, or releasing the
	 * trigger during a zoom-in) re-map animT so the eased value is unchanged,
	 * otherwise the view jumps by up to ~40% of the zoom. */
	if (easing == ZoomEasing::Punch && dir != 0 && lastTickDir != 0 && dir != lastTickDir && animT > 0.0 &&
	    animT < 1.0) {
		if (dir > 0)
			animT = punch_curve_inverse(smoothstep(animT));
		else
			animT = smoothstep_inverse(punch_curve(animT));
	}
	if (dir != 0)
		lastTickDir = dir;

	const int dur = (dir >= 0) ? animInMs : animOutMs;
	animT += (double)dir * (tickDeltaSeconds * 1000.0) / (double)std::max(1, dur);

	if (animT >= 1.0) {
		animT = 1.0;
		animDir.store(0, std::memory_order_relaxed);
	}
	if (animT <= 0.0) {
		animT = 0.0;
		animDir.store(0, std::memory_order_relaxed);
		targetHasPos = false;
		if (dir < 0 && wheelBaseZoom > 1.0) {
			/* Wheel zoom ended: put the configured zoom level back. */
			zoomFactor = wheelBaseZoom;
			wheelBaseZoom = 0.0;
		}
	}

	if (animT == 0.0 && animDir.load(std::memory_order_relaxed) == 0 && !idleFollowX) {
		/* Stop touching shared state here, then let the main thread do the
		 * restore, the settings write and the reset. */
		zoomActive.store(false, std::memory_order_release);
		pendingFinish.store(true, std::memory_order_release);
		return;
	}

	applyZoomToScene(animT);
}

void ZoominatorController::finishZoomOnMainThread()
{
	restoringRecovery = true;
	restoreOriginalSceneItemsFromState();
	restoringRecovery = false;
	clearRecoveryActive();
	tickingWanted.store(false, std::memory_order_release);
	ensureTicking(false);
	resetState();
	if (!wantsIdleFocus())
		hideFocusInAllScenes();
	refreshFrame();
	refreshIdleFocusTicking();
	if (wantsIdleFocus())
		updateIdleFocus(true); /* no gap between the zoom's spotlight and the idle one */
	{
		std::lock_guard<std::mutex> lock(inputMutex);
		pendingFocusVisible = false;
		pendingFocusFresh = false;
		pendingFocusOpacity = 0;
	}

	obs_source_t *staleRef = nullptr;
	{
		std::lock_guard<std::mutex> lock(inputMutex);
		staleRef = input.sceneRef;
		input.sceneRef = nullptr;
		input.mapped = false;
	}
	if (staleRef)
		obs_source_release(staleRef);
}


void ZoominatorController::refreshHud()
{
	if (shuttingDown)
		return;
	if (!showStatusHud) {
		if (hud)
			hud->hideBadge();
		return;
	}
	if (!hud)
		hud = new ZoominatorHud();

	ZoominatorHud::State st;
	st.zoomed = zoomedView();
	st.direction = zoomActive.load(std::memory_order_acquire) ? animDir.load(std::memory_order_relaxed) : 0;
	st.level = st.zoomed ? (double)hudZoom.load(std::memory_order_relaxed) : 1.0;
	if (st.level < 1.0)
		st.level = 1.0;

	st.flags << qMakePair(QStringLiteral("Focus"), focusEnabled);
	st.recording = obs_frontend_recording_active() ? (obs_frontend_recording_paused() ? 2 : 1) : 0;

	hud->setState(st);
	if (!hud->isVisible() || hud->corner() != statusHudCorner)
		hud->showInCorner(statusHudCorner);
	else
		hud->ensureExcluded();
}
