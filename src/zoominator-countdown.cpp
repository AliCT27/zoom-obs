/* Countdown before recording. See zoominator-countdown.hpp. The controller part starts the recording after the
 * countdown; it can be started from a hotkey (set in OBS Settings > Hotkeys) or from the Tools menu. */

#include "zoominator-countdown.hpp"
#include "zoominator-internal.hpp"

#include <QAbstractButton>
#include <QCursor>
#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QMouseEvent>
#include <QTimer>

ZoominatorCountdown::ZoominatorCountdown(QWidget *parent) : QWidget(parent)
{
	setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus |
		       Qt::WindowTransparentForInput);
	setAttribute(Qt::WA_TranslucentBackground, true);
	setAttribute(Qt::WA_ShowWithoutActivating, true);
	setAttribute(Qt::WA_TransparentForMouseEvents, true);
	setFocusPolicy(Qt::NoFocus);
	resize(220, 220);
}

void ZoominatorCountdown::showNumber(int number)
{
	shown = number;
	QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
	if (!screen)
		screen = QGuiApplication::primaryScreen();
	if (screen) {
		const QRect g = screen->geometry();
		move(g.center() - QPoint(width() / 2, height() / 2));
	}
	if (!isVisible())
		show();
	raise();
	update();
}

void ZoominatorCountdown::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(15, 15, 18, 215));
	p.drawEllipse(rect().adjusted(4, 4, -4, -4));
	QFont f = font();
	f.setBold(true);
	f.setPixelSize(130);
	p.setFont(f);
	p.setPen(QColor(240, 240, 245));
	p.drawText(rect(), Qt::AlignCenter, QString::number(shown));
}

/* Main thread. Starts the countdown, or cancels it when one is already running. Does nothing while recording. */
void ZoominatorController::startCountdownRecording()
{
	if (shuttingDown)
		return;
	if (countdownTimer.isActive()) {
		cancelCountdown();
		return;
	}
	if (obs_frontend_recording_active())
		return;

	countdownLeft = std::clamp(countdownSec, 1, 10);
	if (!countdownWidget)
		countdownWidget = new ZoominatorCountdown();
	countdownWidget->showNumber(countdownLeft);
	countdownTimer.start();
}

void ZoominatorController::cancelCountdown()
{
	countdownTimer.stop();
	if (countdownWidget)
		countdownWidget->hide();
}

void ZoominatorController::onCountdownTick()
{
	if (shuttingDown) {
		cancelCountdown();
		return;
	}
	--countdownLeft;
	if (countdownLeft > 0) {
		countdownWidget->showNumber(countdownLeft);
		return;
	}
	cancelCountdown();
	/* Let the number disappear from the screen before the first recorded frame. */
	QTimer::singleShot(250, this, [this]() {
		if (!shuttingDown && !obs_frontend_recording_active())
			obs_frontend_recording_start();
	});
}

namespace {
/* Sits on OBS's own Start Recording button. While the option is on, a click on it (when nothing is recording)
 * starts the countdown instead of the recording; the countdown then starts the recording itself. */
class RecordButtonFilter final : public QObject {
public:
	explicit RecordButtonFilter(ZoominatorController *c, QObject *parent) : QObject(parent), ctl(c) {}

	bool eventFilter(QObject *obj, QEvent *event) override
	{
		if (!ctl || !ctl->countdownOnDefaultRecord || obs_frontend_recording_active())
			return false;
		auto *button = qobject_cast<QAbstractButton *>(obj);
		if (!button)
			return false;
		switch (event->type()) {
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonDblClick:
			return static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton && button->isEnabled();
		case QEvent::MouseButtonRelease: {
			auto *me = static_cast<QMouseEvent *>(event);
			if (me->button() != Qt::LeftButton || !button->isEnabled())
				return false;
			if (button->rect().contains(me->position().toPoint()))
				ctl->startCountdownRecording();
			return true;
		}
		default:
			return false;
		}
	}

private:
	ZoominatorController *ctl;
};
} // namespace

/* Main thread. Finds OBS's Start Recording button and puts the filter on it. Retried by the caller until the
 * button exists. */
void ZoominatorController::installRecordButtonFilter()
{
	if (recordButtonFilterInstalled || shuttingDown)
		return;
	auto *mainWin = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!mainWin)
		return;
	auto *button = mainWin->findChild<QAbstractButton *>(QStringLiteral("recordButton"));
	if (!button)
		return;
	button->installEventFilter(new RecordButtonFilter(this, button));
	recordButtonFilterInstalled = true;
}

static void countdown_hotkey_cb(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	auto *ctl = static_cast<ZoominatorController *>(data);
	if (!pressed || !ctl)
		return;
	/* Called on OBS's hotkey thread. */
	QMetaObject::invokeMethod(ctl, [ctl]() { ctl->startCountdownRecording(); }, Qt::QueuedConnection);
}

static void countdown_save_cb(obs_data_t *saveData, bool saving, void *data)
{
	auto *ctl = static_cast<ZoominatorController *>(data);
	if (!ctl || ctl->countdownHotkeyId == OBS_INVALID_HOTKEY_ID)
		return;
	if (saving) {
		obs_data_array_t *arr = obs_hotkey_save(ctl->countdownHotkeyId);
		obs_data_set_array(saveData, "zoominator_countdown_hotkey", arr);
		obs_data_array_release(arr);
	} else {
		obs_data_array_t *arr = obs_data_get_array(saveData, "zoominator_countdown_hotkey");
		obs_hotkey_load(ctl->countdownHotkeyId, arr);
		obs_data_array_release(arr);
	}
}

void ZoominatorController::registerCountdownHotkey()
{
	if (countdownHotkeyId != OBS_INVALID_HOTKEY_ID)
		return;
	countdownHotkeyId = obs_hotkey_register_frontend("zoominator.countdown_record",
							 obs_module_text("Hotkey.StartCountdown"), countdown_hotkey_cb, this);
	obs_frontend_add_save_callback(countdown_save_cb, this);
}

void ZoominatorController::unregisterCountdownHotkey()
{
	cancelCountdown();
	delete countdownWidget;
	countdownWidget = nullptr;
	if (countdownHotkeyId == OBS_INVALID_HOTKEY_ID)
		return;
	obs_frontend_remove_save_callback(countdown_save_cb, this);
	obs_hotkey_unregister(countdownHotkeyId);
	countdownHotkeyId = OBS_INVALID_HOTKEY_ID;
}
