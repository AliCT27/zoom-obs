#include "zoominator-dialog.hpp"
#include "zoominator-controller.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.h>

extern "C" {
struct calldata;
struct signal_handler;
void signal_handler_connect(struct signal_handler *handler, const char *signal,
			    void (*callback)(void *, struct calldata *), void *data);
void signal_handler_disconnect(struct signal_handler *handler, const char *signal,
			       void (*callback)(void *, struct calldata *), void *data);
}

#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QColorDialog>
#include <QPen>
#include <QPainter>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QString>

#include <algorithm>

namespace {

static QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

namespace {
/* A colour swatch button. While it is disabled (its owning option is off) it keeps exactly the shape of the enabled
 * one (the grey look comes from the same style sheet, so size, border and rounded corners are identical) and gets
 * diagonal hatching on top, so it is obvious that it is off because of that option. */
class ColorButton final : public QPushButton {
public:
	explicit ColorButton(QWidget *parent = nullptr) : QPushButton(parent) {}

protected:
	void paintEvent(QPaintEvent *event) override
	{
		QPushButton::paintEvent(event);
		if (isEnabled())
			return;
		QPainter p(this);
		const QRect r = rect().adjusted(3, 3, -4, -4); /* stays inside the rounded border */
		p.setClipRect(r);
		p.setPen(QPen(QColor(112, 112, 122), 1));
		for (int x = r.left() - r.height(); x < r.right(); x += 7)
			p.drawLine(x, r.bottom(), x + r.height(), r.top());
	}
};
} // namespace

static QWidget *mkField(const QString &labelText, QWidget *input)
{
	auto *container = new QWidget;
	auto *v = new QVBoxLayout(container);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(4);
	auto *lbl = new QLabel(labelText, container);
	v->addWidget(lbl);
	v->addWidget(input);
	return container;
}

static QScrollArea *wrapScrollable(QWidget *page)
{
	auto *scroll = new QScrollArea;
	scroll->setWidget(page);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	return scroll;
}

static void addSection(QVBoxLayout *lay, const QString &title, bool firstSection = false)
{
	if (!firstSection) {
		auto *sep = new QFrame;
		sep->setFrameShape(QFrame::HLine);
		sep->setFrameShadow(QFrame::Sunken);
		lay->addSpacing(12);
		lay->addWidget(sep);
	}
	lay->addSpacing(14);
	auto *lbl = new QLabel(QStringLiteral("<b>%1</b>").arg(title));
	lay->addWidget(lbl);
	lay->addSpacing(10);
}

static void frontend_event_cb(enum obs_frontend_event, void *data)
{
	auto *dlg = static_cast<ZoominatorDialog *>(data);
	if (!dlg)
		return;
	QMetaObject::invokeMethod(dlg, "refreshLists", Qt::QueuedConnection);
}

static QString friendlySourceKind(const QString &kind)
{
	if (kind == "image_source")
		return T("SourceKind.Image");
	if (kind == "browser_source")
		return T("SourceKind.Browser");
	if (kind == "game_capture")
		return T("SourceKind.GameCapture");
	if (kind == "window_capture")
		return T("SourceKind.WindowCapture");
	if (kind == "monitor_capture" || kind == "display_capture" || kind == "screen_capture")
		return T("SourceKind.DisplayCapture");
	if (kind == "dshow_input" || kind == "av_capture_input" || kind == "video_capture_device")
		return T("SourceKind.VideoCapture");
	if (kind == "wasapi_input_capture" || kind == "coreaudio_input_capture")
		return T("SourceKind.AudioInput");
	if (kind == "wasapi_output_capture" || kind == "coreaudio_output_capture")
		return T("SourceKind.AudioOutput");
	if (kind == "scene")
		return T("SourceKind.Scene");
	if (kind == "group")
		return T("SourceKind.Group");
	if (kind == "text_gdiplus" || kind == "text_ft2_source")
		return T("SourceKind.Text");
	if (kind == "color_source")
		return T("SourceKind.Color");
	if (kind == "ffmpeg_source")
		return T("SourceKind.Media");
	if (kind == "vlc_source")
		return T("SourceKind.VlcMedia");
	if (kind == "slideshow")
		return T("SourceKind.ImageSlideshow");
	return kind;
}

} // namespace

ZoominatorDialog::ZoominatorDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(T("Zoominator"));
	setModal(false);

	buildUi();

	int width = 620;
	int height = 560;
	if (QScreen *screen = QGuiApplication::primaryScreen()) {
		const QRect avail = screen->availableGeometry();
		width = (std::min)(width, avail.width() - 40);
		height = (std::min)(height, avail.height() - 40);
	}
	resize(width, height);

	obs_frontend_add_event_callback(frontend_event_cb, this);

	auto *sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_create", &ZoominatorDialog::obsSourceChanged, this);
	signal_handler_connect(sh, "source_destroy", &ZoominatorDialog::obsSourceChanged, this);
	signal_handler_connect(sh, "source_rename", &ZoominatorDialog::obsSourceChanged, this);

	refreshLists();
	loadFromController();
}

void ZoominatorDialog::closeEvent(QCloseEvent *event)
{
	obs_frontend_remove_event_callback(frontend_event_cb, this);

	auto *sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_create", &ZoominatorDialog::obsSourceChanged, this);
	signal_handler_disconnect(sh, "source_destroy", &ZoominatorDialog::obsSourceChanged, this);
	signal_handler_disconnect(sh, "source_rename", &ZoominatorDialog::obsSourceChanged, this);

	applyToController();
	QDialog::closeEvent(event);
}

void ZoominatorDialog::obsSourceChanged(void *data, struct calldata *)
{
	auto *dlg = static_cast<ZoominatorDialog *>(data);
	if (dlg)
		QMetaObject::invokeMethod(dlg, "populateSourcesTab", Qt::QueuedConnection);
}

void ZoominatorDialog::buildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(8);

	tabWidget = new QTabWidget(this);

	{
		auto *page = new QWidget;
		auto *lay = new QVBoxLayout(page);
		lay->setContentsMargins(20, 20, 20, 20);
		lay->setSpacing(0);

		cmbSource = new QComboBox(page);
		lay->addWidget(mkField(T("Dialog.TargetScreen"), cmbSource));
		lay->addSpacing(16);

		cmbMode = new QComboBox(page);
		cmbMode->addItem(T("Dialog.Mode.Hold"), "hold");
		cmbMode->addItem(T("Dialog.Mode.Toggle"), "toggle");
		rowBehaviorWidget = mkField(T("Dialog.Behavior"), cmbMode);
		lay->addWidget(rowBehaviorWidget);
		lay->addSpacing(16);

		auto *followHkRow = new QWidget(page);
		{
			auto *h = new QHBoxLayout(followHkRow);
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(6);
			editFollowToggleHotkey = new QKeySequenceEdit(followHkRow);
			btnClearFollowToggleHotkey = new QPushButton(T("Dialog.Clear"), followHkRow);
			btnClearFollowToggleHotkey->setToolTip(T("Dialog.ClearFollowToggleTooltip"));
			h->addWidget(editFollowToggleHotkey, 1);
			h->addWidget(btnClearFollowToggleHotkey);
		}
		lay->addWidget(mkField(T("Dialog.FollowToggleHotkey"), followHkRow));
		lay->addSpacing(16);

		lay->addStretch(1);
		lay->addSpacing(16);

		tabWidget->addTab(wrapScrollable(page), T("Dialog.Tab.Target"));
	}

	{
		auto *page = new QWidget;
		auto *lay = new QVBoxLayout(page);
		lay->setContentsMargins(20, 20, 20, 20);
		lay->setSpacing(0);

		cmbTrigger = new QComboBox(page);
		cmbTrigger->addItem(T("Dialog.Trigger.Keyboard"), "keyboard");
		cmbTrigger->addItem(T("Dialog.Trigger.MouseButton"), "mouse");
		cmbTrigger->addItem(T("Dialog.Trigger.Auto"), "auto");
		lay->addWidget(mkField(T("Dialog.TriggerType"), cmbTrigger));
		lay->addSpacing(16);

		rowHotkeyWidget = new QWidget(page);
		{
			auto *v = new QVBoxLayout(rowHotkeyWidget);
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(4);
			v->addWidget(new QLabel(T("Dialog.Hotkey"), rowHotkeyWidget));
			auto *h = new QHBoxLayout;
			h->setSpacing(6);
			editHotkey = new QKeySequenceEdit(rowHotkeyWidget);
			btnClearHotkey = new QPushButton(T("Dialog.Clear"), rowHotkeyWidget);
			btnClearHotkey->setToolTip(T("Dialog.ClearHotkeyTooltip"));
			h->addWidget(editHotkey, 1);
			h->addWidget(btnClearHotkey);
			v->addLayout(h);
		}
		lay->addWidget(rowHotkeyWidget);

		rowMouseWidget = new QWidget(page);
		{
			auto *v = new QVBoxLayout(rowMouseWidget);
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(4);
			v->addWidget(new QLabel(T("Dialog.MouseButton"), rowMouseWidget));
			cmbMouseBtn = new QComboBox(rowMouseWidget);
			cmbMouseBtn->addItem(T("Dialog.Mouse.Left"), "left");
			cmbMouseBtn->addItem(T("Dialog.Mouse.Right"), "right");
			cmbMouseBtn->addItem(T("Dialog.Mouse.Middle"), "middle");
			cmbMouseBtn->addItem("X1", "x1");
			cmbMouseBtn->addItem("X2", "x2");
			v->addWidget(cmbMouseBtn);
		}
		lay->addWidget(rowMouseWidget);
		lay->addSpacing(16);

		rowModifiersWidget = new QWidget(page);
		{
			auto *v = new QVBoxLayout(rowModifiersWidget);
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(6);
			v->addWidget(new QLabel(T("Dialog.RequiredModifiers"), rowModifiersWidget));
			auto *grid = new QGridLayout;
			grid->setHorizontalSpacing(10);
			grid->setVerticalSpacing(4);
			chkCtrl = new QCheckBox(T("Dialog.Mod.CtrlAny"), rowModifiersWidget);
			chkLeftCtrl = new QCheckBox(T("Dialog.Mod.LeftCtrl"), rowModifiersWidget);
			chkRightCtrl = new QCheckBox(T("Dialog.Mod.RightCtrl"), rowModifiersWidget);
			chkAlt = new QCheckBox(T("Dialog.Mod.AltAny"), rowModifiersWidget);
			chkLeftAlt = new QCheckBox(T("Dialog.Mod.LeftAlt"), rowModifiersWidget);
			chkRightAlt = new QCheckBox(T("Dialog.Mod.RightAlt"), rowModifiersWidget);
			chkShift = new QCheckBox(T("Dialog.Mod.ShiftAny"), rowModifiersWidget);
			chkLeftShift = new QCheckBox(T("Dialog.Mod.LeftShift"), rowModifiersWidget);
			chkRightShift = new QCheckBox(T("Dialog.Mod.RightShift"), rowModifiersWidget);
			chkWin = new QCheckBox(T("Dialog.Mod.MetaWinAny"), rowModifiersWidget);
			chkLeftWin = new QCheckBox(T("Dialog.Mod.LeftMetaWin"), rowModifiersWidget);
			chkRightWin = new QCheckBox(T("Dialog.Mod.RightMetaWin"), rowModifiersWidget);
			grid->addWidget(chkCtrl, 0, 0);
			grid->addWidget(chkLeftCtrl, 0, 1);
			grid->addWidget(chkRightCtrl, 0, 2);
			grid->addWidget(chkAlt, 1, 0);
			grid->addWidget(chkLeftAlt, 1, 1);
			grid->addWidget(chkRightAlt, 1, 2);
			grid->addWidget(chkShift, 2, 0);
			grid->addWidget(chkLeftShift, 2, 1);
			grid->addWidget(chkRightShift, 2, 2);
			grid->addWidget(chkWin, 3, 0);
			grid->addWidget(chkLeftWin, 3, 1);
			grid->addWidget(chkRightWin, 3, 2);
			grid->addWidget(new QLabel(T("Dialog.ModifierHelp"), rowModifiersWidget), 4, 0, 1, 3);
			grid->setColumnStretch(3, 1);
			v->addLayout(grid);
		}
		lay->addWidget(rowModifiersWidget);

		rowAutoWidget = new QWidget(page);
		{
			auto *v = new QVBoxLayout(rowAutoWidget);
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(8);
			auto *help = new QLabel(T("Dialog.AutoHelp"), rowAutoWidget);
			help->setWordWrap(true);
			v->addWidget(help);
			chkAutoClick = new QCheckBox(T("Dialog.AutoOnClick"), rowAutoWidget);
			chkAutoTyping = new QCheckBox(T("Dialog.AutoOnTyping"), rowAutoWidget);
			chkAutoTyping->setToolTip(T("Dialog.AutoOnTypingTooltip"));
			chkCaretFollow = new QCheckBox(T("Dialog.CaretFollow"), rowAutoWidget);
			chkCaretFollow->setToolTip(T("Dialog.CaretFollowTooltip"));
#ifndef _WIN32
			chkCaretFollow->setEnabled(false);
#endif
			spAutoHold = new QSpinBox(rowAutoWidget);
			spAutoHold->setRange(300, 60000);
			spAutoHold->setSingleStep(100);
			spAutoHold->setSuffix(T("Unit.Milliseconds"));
			spAutoHold->setToolTip(T("Dialog.AutoHoldTooltip"));
			v->addWidget(chkAutoClick);
			v->addWidget(chkAutoTyping);
			v->addWidget(chkCaretFollow);
			chkAutoZoomOut = new QCheckBox(T("Dialog.AutoZoomOut"), rowAutoWidget);
			chkAutoZoomOut->setToolTip(T("Dialog.AutoZoomOutTooltip"));
			v->addWidget(chkAutoZoomOut);
			v->addWidget(mkField(T("Dialog.AutoHold"), spAutoHold));
			connect(chkAutoZoomOut, &QCheckBox::toggled, spAutoHold, &QWidget::setEnabled);
#ifdef _WIN32
			chkCaretFollow->setEnabled(chkAutoTyping->isChecked());
			connect(chkAutoTyping, &QCheckBox::toggled, chkCaretFollow, &QWidget::setEnabled);
#endif

		}
		lay->addWidget(rowAutoWidget);

		rowManualKeysWidget = new QWidget(page);
		{
			auto *v = new QVBoxLayout(rowManualKeysWidget);
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(8);
			auto makeKeyRow = [&](QKeySequenceEdit *&edit, const char *label, const char *tip) {
				auto *row = new QWidget(rowManualKeysWidget);
				auto *h = new QHBoxLayout(row);
				h->setContentsMargins(0, 0, 0, 0);
				h->setSpacing(6);
				edit = new QKeySequenceEdit(row);
				edit->setToolTip(T(tip));
				auto *clear = new QPushButton(T("Dialog.Clear"), row);
				QKeySequenceEdit *e = edit;
				connect(clear, &QPushButton::clicked, this, [e]() { e->setKeySequence(QKeySequence()); });
				h->addWidget(edit, 1);
				h->addWidget(clear);
				v->addWidget(mkField(T(label), row));
			};
			makeKeyRow(editManualZoomIn, "Dialog.ManualZoomIn", "Dialog.ManualZoomInTooltip");
			makeKeyRow(editManualZoomOut, "Dialog.ManualZoomOut", "Dialog.ManualZoomOutTooltip");
		}
		lay->addSpacing(16);
		lay->addWidget(rowManualKeysWidget);

		lay->addStretch(1);
		tabWidget->addTab(wrapScrollable(page), T("Dialog.Tab.Trigger"));

		connect(cmbTrigger, &QComboBox::currentIndexChanged, this, [this](int) { updateTriggerRows(); });
	}

	{
		auto *page = new QWidget;
		auto *lay = new QVBoxLayout(page);
		lay->setContentsMargins(20, 8, 20, 20);
		lay->setSpacing(0);

		addSection(lay, T("Dialog.Section.Zoom"), true);

		spZoom = new QDoubleSpinBox(page);
		spZoom->setRange(0.0, 8.0);
		spZoom->setSingleStep(0.05);
		spZoom->setDecimals(2);
		spZoom->setToolTip(T("Dialog.ZoomFactorTooltip"));

		spIn = new QSpinBox(page);
		spIn->setRange(0, 5000);
		spIn->setSingleStep(10);
		spIn->setSuffix(T("Unit.Milliseconds"));

		spOut = new QSpinBox(page);
		spOut->setRange(0, 5000);
		spOut->setSingleStep(10);
		spOut->setSuffix(T("Unit.Milliseconds"));

		auto *zoomRow = new QHBoxLayout;
		zoomRow->setSpacing(12);
		zoomRow->addWidget(mkField(T("Dialog.ZoomFactor"), spZoom), 1);
		zoomRow->addWidget(mkField(T("Dialog.AnimateIn"), spIn), 1);
		zoomRow->addWidget(mkField(T("Dialog.AnimateOut"), spOut), 1);
		lay->addLayout(zoomRow);
		lay->addSpacing(12);

		cmbEasing = new QComboBox(page);
		cmbEasing->addItem(T("Dialog.Easing.Smooth"), "smooth");
		cmbEasing->addItem(T("Dialog.Easing.EaseOut"), "ease_out");
		cmbEasing->addItem(T("Dialog.Easing.Expo"), "expo");
		cmbEasing->addItem(T("Dialog.Easing.Punch"), "punch");
		cmbEasing->setToolTip(T("Dialog.EasingTooltip"));
		auto *easingRow = new QHBoxLayout;
		easingRow->setSpacing(12);
		easingRow->addWidget(mkField(T("Dialog.Easing"), cmbEasing), 1);
		easingRow->addStretch(2);
		lay->addLayout(easingRow);
		lay->addSpacing(12);

		chkWheelZoom = new QCheckBox(T("Dialog.WheelZoom"), page);
		chkWheelZoom->setToolTip(T("Dialog.WheelZoomTooltip"));
		lay->addWidget(chkWheelZoom);

		cmbWheelMod = new QComboBox(page);
		cmbWheelMod->addItem(T("Dialog.WheelMod.Alt"), "alt");
		cmbWheelMod->addItem(T("Dialog.WheelMod.Ctrl"), "ctrl");
		cmbWheelMod->addItem(T("Dialog.WheelMod.Shift"), "shift");
		spWheelStep = new QDoubleSpinBox(page);
		spWheelStep->setRange(0.05, 2.0);
		spWheelStep->setSingleStep(0.05);
		spWheelStep->setDecimals(2);
		spWheelMax = new QDoubleSpinBox(page);
		spWheelMax->setRange(1.5, 20.0);
		spWheelMax->setSingleStep(0.5);
		spWheelMax->setDecimals(1);
		spWheelExit = new QDoubleSpinBox(page);
		spWheelExit->setRange(0.0, 19.0);
		spWheelExit->setSpecialValueText(T("Dialog.WheelExitOff"));
		spWheelExit->setSingleStep(0.1);
		spWheelExit->setDecimals(1);
		spWheelExit->setToolTip(T("Dialog.WheelExitTooltip"));
		auto *wheelRow = new QHBoxLayout;
		wheelRow->setSpacing(12);
		wheelRow->addWidget(mkField(T("Dialog.WheelModifier"), cmbWheelMod), 1);
		wheelRow->addWidget(mkField(T("Dialog.WheelStep"), spWheelStep), 1);
		wheelRow->addWidget(mkField(T("Dialog.WheelMax"), spWheelMax), 1);
		wheelRow->addWidget(mkField(T("Dialog.WheelExit"), spWheelExit), 1);
		lay->addLayout(wheelRow);
		for (QWidget *w : {(QWidget *)cmbWheelMod, (QWidget *)spWheelStep, (QWidget *)spWheelMax,
				   (QWidget *)spWheelExit})
			connect(chkWheelZoom, &QCheckBox::toggled, w, &QWidget::setEnabled);
		lay->addSpacing(12);


		addSection(lay, T("Dialog.Section.MouseFollow"));

		cmbZoomAnchor = new QComboBox(page);
		cmbZoomAnchor->addItem(T("Dialog.ZoomAnchor.CursorStatic"), "cursor_static");
		cmbZoomAnchor->addItem(T("Dialog.ZoomAnchor.CursorFollow"), "cursor_follow");
		cmbZoomAnchor->setToolTip(T("Dialog.ZoomAnchorTooltip"));

		spFollowSpeed = new QDoubleSpinBox(page);
		spFollowSpeed->setRange(0.1, 40.0);
		spFollowSpeed->setSingleStep(0.5);
		spFollowSpeed->setDecimals(1);

		chkCenterFocus = new QCheckBox(T("Dialog.CenterFocus"), page);
		chkCenterFocus->setToolTip(T("Dialog.CenterFocusTooltip"));

		auto *followRow = new QHBoxLayout;
		followRow->setSpacing(12);

		followRow->addWidget(mkField(T("Dialog.ZoomAnchor"), cmbZoomAnchor), 1);
		followRow->addWidget(mkField(T("Dialog.SmoothingSpeed"), spFollowSpeed), 1);
		lay->addLayout(followRow);

		lay->addWidget(chkCenterFocus);

		/* Smoothing only means something while the anchor
		 * actually tracks the cursor. */
		auto syncFollowOnlyFields = [this]() {
			const bool follows = (cmbZoomAnchor->currentData().toString() == "cursor_follow");
			spFollowSpeed->setEnabled(follows);
		};
		connect(cmbZoomAnchor, &QComboBox::currentIndexChanged, this,
			[syncFollowOnlyFields](int) { syncFollowOnlyFields(); });
		syncFollowOnlyFields();

		addSection(lay, T("Dialog.Section.Canvas"));

		auto *coverHelp = new QLabel(T("Dialog.PortraitCoverHelp"), page);
		coverHelp->setWordWrap(true);
		lay->addWidget(coverHelp);
		chkPortraitCover = new QCheckBox(T("Dialog.PortraitCover"), page);
		chkPortraitCover->setToolTip(T("Dialog.PortraitCoverTooltip"));
		lay->addWidget(chkPortraitCover);

		chkIdleFollowX = new QCheckBox(T("Dialog.IdleFollowX"), page);
		chkIdleFollowX->setToolTip(T("Dialog.IdleFollowXTooltip"));
		lay->addWidget(chkIdleFollowX);

		addSection(lay, T("Dialog.Section.Tilt3d"));
		auto *tiltHelp = new QLabel(T("Dialog.Tilt3dHelp"), page);
		tiltHelp->setWordWrap(true);
		lay->addWidget(tiltHelp);
		chkTilt3d = new QCheckBox(T("Dialog.Tilt3d"), page);
		chkTilt3d->setToolTip(T("Dialog.Tilt3dTooltip"));
		lay->addWidget(chkTilt3d);
		spTiltX = new QDoubleSpinBox(page);
		spTiltX->setRange(0.0, 45.0);
		spTiltX->setSingleStep(1.0);
		spTiltX->setDecimals(1);
		spTiltX->setSuffix(QStringLiteral("°"));
		spTiltX->setToolTip(T("Dialog.Tilt3dXTooltip"));
		spTiltY = new QDoubleSpinBox(page);
		spTiltY->setRange(0.0, 45.0);
		spTiltY->setSingleStep(1.0);
		spTiltY->setDecimals(1);
		spTiltY->setSuffix(QStringLiteral("°"));
		spTiltY->setToolTip(T("Dialog.Tilt3dYTooltip"));
		spTiltPersp = new QDoubleSpinBox(page);
		spTiltPersp->setRange(0.8, 8.0);
		spTiltPersp->setSingleStep(0.2);
		spTiltPersp->setDecimals(1);
		spTiltPersp->setToolTip(T("Dialog.Tilt3dPerspTooltip"));
		auto *tiltRow = new QHBoxLayout;
		tiltRow->setSpacing(12);
		tiltRow->addWidget(mkField(T("Dialog.Tilt3dX"), spTiltX), 1);
		tiltRow->addWidget(mkField(T("Dialog.Tilt3dY"), spTiltY), 1);
		tiltRow->addWidget(mkField(T("Dialog.Tilt3dPersp"), spTiltPersp), 1);
		lay->addLayout(tiltRow);
		chkTiltInvert = new QCheckBox(T("Dialog.Tilt3dInvert"), page);
		chkTiltInvert->setToolTip(T("Dialog.Tilt3dInvertTooltip"));
		lay->addWidget(chkTiltInvert);
		chkTiltInZoom = new QCheckBox(T("Dialog.Tilt3dInZoom"), page);
		chkTiltInZoom->setToolTip(T("Dialog.Tilt3dInZoomTooltip"));
		lay->addWidget(chkTiltInZoom);
		for (QWidget *w : {(QWidget *)spTiltX, (QWidget *)spTiltY, (QWidget *)spTiltPersp, (QWidget *)chkTiltInvert,
				   (QWidget *)chkTiltInZoom})
			connect(chkTilt3d, &QCheckBox::toggled, w, &QWidget::setEnabled);

		addSection(lay, T("Dialog.Section.CursorHalo"));

		chkShowCursorMarker = new QCheckBox(T("Dialog.ShowCursorHalo"), page);
		chkShowCursorMarker->setToolTip(T("Dialog.ShowCursorHaloTooltip"));
		chkShowMarkerWhenNotZoomed = new QCheckBox(T("Dialog.ShowHaloWhenNotZoomed"), page);
		chkShowMarkerWhenNotZoomed->setToolTip(T("Dialog.ShowHaloWhenNotZoomedTooltip"));

		lay->addWidget(chkShowCursorMarker);
		lay->addSpacing(6);

		spMarkerSize = new QSpinBox(page);
		spMarkerSize->setRange(6, 256);
		spMarkerSize->setSingleStep(2);
		spMarkerSize->setSuffix(T("Unit.Pixels"));

		spMarkerThickness = new QSpinBox(page);
		spMarkerThickness->setRange(1, 64);
		spMarkerThickness->setSingleStep(1);
		spMarkerThickness->setSuffix(T("Unit.Pixels"));

		btnMarkerColor = new ColorButton(page);
		btnMarkerColor->setMinimumHeight(28);

		auto *haloRow = new QHBoxLayout;
		haloRow->setSpacing(12);
		haloRow->addWidget(mkField(T("Dialog.HaloSize"), spMarkerSize), 1);
		haloRow->addWidget(mkField(T("Dialog.RingThickness"), spMarkerThickness), 1);
		haloRow->addWidget(mkField(T("Dialog.Color"), btnMarkerColor), 1);
		lay->addLayout(haloRow);
		lay->addSpacing(10);

		lay->addWidget(chkShowMarkerWhenNotZoomed);
		lay->addSpacing(4);
		spMarkerIdleSize = new QSpinBox(page);
		spMarkerIdleSize->setRange(6, 256);
		spMarkerIdleSize->setSingleStep(2);
		spMarkerIdleSize->setSuffix(T("Unit.Pixels"));
		spMarkerIdleSize->setToolTip(T("Dialog.HaloIdleSizeTooltip"));
		spMarkerIdleSize->setEnabled(chkShowMarkerWhenNotZoomed->isChecked());
		auto *haloIdleRow = new QHBoxLayout;
		haloIdleRow->setSpacing(12);
		haloIdleRow->addWidget(mkField(T("Dialog.HaloIdleSize"), spMarkerIdleSize), 1);
		haloIdleRow->addStretch(2);
		lay->addLayout(haloIdleRow);
		lay->addSpacing(6);
		lay->addSpacing(4);
		spMarkerOffX = new QSpinBox(page);
		spMarkerOffX->setRange(-100, 100);
		spMarkerOffX->setSuffix(T("Unit.Pixels"));
		spMarkerOffX->setToolTip(T("Dialog.HaloOffsetTooltip"));
		spMarkerOffY = new QSpinBox(page);
		spMarkerOffY->setRange(-100, 100);
		spMarkerOffY->setSuffix(T("Unit.Pixels"));
		spMarkerOffY->setToolTip(T("Dialog.HaloOffsetTooltip"));
		btnMarkerOffReset = new QPushButton(T("Dialog.HaloOffsetReset"), page);
		btnMarkerOffReset->setToolTip(T("Dialog.HaloOffsetResetTooltip"));
		connect(btnMarkerOffReset, &QPushButton::clicked, this, [this]() {
			spMarkerOffX->setValue(6);
			spMarkerOffY->setValue(10);
		});
		auto *haloOffRow = new QHBoxLayout;
		haloOffRow->setSpacing(12);
		haloOffRow->addWidget(mkField(T("Dialog.HaloOffsetX"), spMarkerOffX), 1);
		haloOffRow->addWidget(mkField(T("Dialog.HaloOffsetY"), spMarkerOffY), 1);
		haloOffRow->addWidget(btnMarkerOffReset, 0, Qt::AlignBottom);
		lay->addLayout(haloOffRow);

		addSection(lay, T("Dialog.Section.Frame"));

		chkFrame = new QCheckBox(T("Dialog.FrameEnable"), page);
		chkFrame->setToolTip(T("Dialog.FrameEnableTooltip"));
		lay->addWidget(chkFrame);
		lay->addSpacing(6);

		btnFrameColor = new ColorButton(page);
		btnFrameColor->setMinimumHeight(28);
		btnFrameColor->setToolTip(T("Dialog.FrameColorTooltip"));
		btnFrameBg = new ColorButton(page);
		btnFrameBg->setMinimumHeight(28);
		btnFrameBg->setToolTip(T("Dialog.FrameBgTooltip"));

		auto *frameColorRow = new QHBoxLayout;
		frameColorRow->setSpacing(12);
		frameColorRow->addWidget(mkField(T("Dialog.FrameColor"), btnFrameColor), 1);
		frameColorRow->addWidget(mkField(T("Dialog.FrameBg"), btnFrameBg), 1);
		lay->addLayout(frameColorRow);

		cmbFrameBgMode = new QComboBox(page);
		cmbFrameBgMode->addItem(T("Dialog.FrameBgMode.Solid"), 0);
		cmbFrameBgMode->addItem(T("Dialog.FrameBgMode.Linear"), 1);
		cmbFrameBgMode->addItem(T("Dialog.FrameBgMode.Radial"), 2);
		cmbFrameBgMode->setToolTip(T("Dialog.FrameBgModeTooltip"));
		btnFrameBg2 = new ColorButton(page);
		btnFrameBg2->setMinimumHeight(28);
		btnFrameBg2->setToolTip(T("Dialog.FrameBg2Tooltip"));
		spFrameBgAngle = new QSpinBox(page);
		spFrameBgAngle->setRange(0, 360);
		spFrameBgAngle->setSingleStep(15);
		spFrameBgAngle->setSuffix(QString::fromUtf8("\xC2\xB0"));
		spFrameBgAngle->setToolTip(T("Dialog.FrameBgAngleTooltip"));

		auto *frameGradRow = new QHBoxLayout;
		frameGradRow->setSpacing(12);
		frameGradRow->addWidget(mkField(T("Dialog.FrameBgMode"), cmbFrameBgMode), 1);
		frameGradRow->addWidget(mkField(T("Dialog.FrameBg2"), btnFrameBg2), 1);
		frameGradRow->addWidget(mkField(T("Dialog.FrameBgAngle"), spFrameBgAngle), 1);
		lay->addLayout(frameGradRow);

		spFrameBorder = new QSpinBox(page);
		spFrameBorder->setRange(0, 80);
		spFrameBorder->setSuffix(T("Unit.Pixels"));
		spFrameBorder->setToolTip(T("Dialog.FrameBorderTooltip"));
		spFrameRadius = new QSpinBox(page);
		spFrameRadius->setRange(0, 400);
		spFrameRadius->setSingleStep(2);
		spFrameRadius->setSuffix(T("Unit.Pixels"));
		spFrameRadius->setToolTip(T("Dialog.FrameRadiusTooltip"));
		spFrameGlow = new QSpinBox(page);
		spFrameGlow->setRange(0, 200);
		spFrameGlow->setSingleStep(4);
		spFrameGlow->setSuffix(T("Unit.Pixels"));
		spFrameGlow->setToolTip(T("Dialog.FrameGlowTooltip"));
		spFrameGlowPct = new QSpinBox(page);
		spFrameGlowPct->setRange(0, 100);
		spFrameGlowPct->setSingleStep(5);
		spFrameGlowPct->setSuffix(T("Unit.Percent"));
		spFrameGlowPct->setToolTip(T("Dialog.FrameGlowStrengthTooltip"));

		auto *frameShapeRow = new QHBoxLayout;
		frameShapeRow->setSpacing(12);
		frameShapeRow->addWidget(mkField(T("Dialog.FrameBorder"), spFrameBorder), 1);
		frameShapeRow->addWidget(mkField(T("Dialog.FrameRadius"), spFrameRadius), 1);
		frameShapeRow->addWidget(mkField(T("Dialog.FrameGlow"), spFrameGlow), 1);
		frameShapeRow->addWidget(mkField(T("Dialog.FrameGlowStrength"), spFrameGlowPct), 1);
		lay->addLayout(frameShapeRow);

		chkFrameBgRainbow = new QCheckBox(T("Dialog.FrameBgRainbow"), page);
		chkFrameBgRainbow->setToolTip(T("Dialog.FrameRainbowTooltip"));
		chkFrameBorderRainbow = new QCheckBox(T("Dialog.FrameBorderRainbow"), page);
		chkFrameBorderRainbow->setToolTip(T("Dialog.FrameRainbowTooltip"));
		spFrameRainbowSec = new QSpinBox(page);
		spFrameRainbowSec->setRange(2, 120);
		spFrameRainbowSec->setSuffix(QStringLiteral(" s"));
		spFrameRainbowSec->setToolTip(T("Dialog.FrameRainbowSecTooltip"));
		auto *frameRainbowRow = new QHBoxLayout;
		frameRainbowRow->setSpacing(12);
		frameRainbowRow->addWidget(chkFrameBgRainbow, 1);
		frameRainbowRow->addWidget(chkFrameBorderRainbow, 1);
		frameRainbowRow->addWidget(mkField(T("Dialog.FrameRainbowSec"), spFrameRainbowSec), 1);
		lay->addLayout(frameRainbowRow);

		lay->addSpacing(10);

		auto syncFrameFields = [this]() {
			const bool on = chkFrame->isChecked();
			/* A rainbow replaces the colours it covers, so those fields are not used. */
			const bool bgColours = on && !chkFrameBgRainbow->isChecked();
			btnFrameColor->setEnabled(on && !chkFrameBorderRainbow->isChecked());
			btnFrameBg->setEnabled(bgColours);
			cmbFrameBgMode->setEnabled(bgColours);
			btnFrameBg2->setEnabled(bgColours && cmbFrameBgMode->currentData().toInt() != 0);
			spFrameBgAngle->setEnabled(bgColours && cmbFrameBgMode->currentData().toInt() == 1);
			spFrameBorder->setEnabled(on);
			spFrameRadius->setEnabled(on);
			spFrameGlow->setEnabled(on);
			spFrameGlowPct->setEnabled(on);
			chkFrameBgRainbow->setEnabled(on);
			chkFrameBorderRainbow->setEnabled(on);
			const bool anyRainbow = on && (chkFrameBgRainbow->isChecked() || chkFrameBorderRainbow->isChecked());
			spFrameRainbowSec->setEnabled(anyRainbow);
		};
		connect(chkFrame, &QCheckBox::toggled, this, [syncFrameFields](bool) { syncFrameFields(); });
		connect(chkFrameBgRainbow, &QCheckBox::toggled, this, [syncFrameFields](bool) { syncFrameFields(); });
		connect(chkFrameBorderRainbow, &QCheckBox::toggled, this, [syncFrameFields](bool) { syncFrameFields(); });
		connect(cmbFrameBgMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			[syncFrameFields](int) { syncFrameFields(); });
		syncFrameFields();

		addSection(lay, T("Dialog.Section.CamRing"));

		chkCamRing = new QCheckBox(T("Dialog.CamRingEnable"), page);
		chkCamRing->setToolTip(T("Dialog.CamRingEnableTooltip"));
		lay->addWidget(chkCamRing);
		lay->addSpacing(6);

		cmbCamSource = new QComboBox(page);
		cmbCamSource->setToolTip(T("Dialog.CamSourceTooltip"));
		btnCamColor = new ColorButton(page);
		btnCamColor->setMinimumHeight(28);
		btnCamColor->setToolTip(T("Dialog.CamColorTooltip"));

		auto *camTopRow = new QHBoxLayout;
		camTopRow->setSpacing(12);
		camTopRow->addWidget(mkField(T("Dialog.CamSource"), cmbCamSource), 2);
		camTopRow->addWidget(mkField(T("Dialog.CamColor"), btnCamColor), 1);
		lay->addLayout(camTopRow);

		spCamRing = new QSpinBox(page);
		spCamRing->setRange(0, 60);
		spCamRing->setSuffix(T("Unit.Pixels"));
		spCamRing->setToolTip(T("Dialog.CamRingTooltip"));
		spCamGlow = new QSpinBox(page);
		spCamGlow->setRange(0, 150);
		spCamGlow->setSingleStep(4);
		spCamGlow->setSuffix(T("Unit.Pixels"));
		spCamGlow->setToolTip(T("Dialog.CamGlowTooltip"));
		spCamGlowPct = new QSpinBox(page);
		spCamGlowPct->setRange(0, 100);
		spCamGlowPct->setSingleStep(5);
		spCamGlowPct->setSuffix(T("Unit.Percent"));
		spCamGlowPct->setToolTip(T("Dialog.CamGlowStrengthTooltip"));
		auto *camShapeRow = new QHBoxLayout;
		camShapeRow->setSpacing(12);
		camShapeRow->addWidget(mkField(T("Dialog.CamRing"), spCamRing), 1);
		camShapeRow->addWidget(mkField(T("Dialog.CamGlow"), spCamGlow), 1);
		camShapeRow->addWidget(mkField(T("Dialog.CamGlowStrength"), spCamGlowPct), 1);
		lay->addLayout(camShapeRow);

		chkCamRainbow = new QCheckBox(T("Dialog.CamRainbow"), page);
		chkCamRainbow->setToolTip(T("Dialog.FrameRainbowTooltip"));
		spCamRainbowSec = new QSpinBox(page);
		spCamRainbowSec->setRange(2, 120);
		spCamRainbowSec->setSuffix(QStringLiteral(" s"));
		spCamRainbowSec->setToolTip(T("Dialog.FrameRainbowSecTooltip"));
		auto *camRainbowRow = new QHBoxLayout;
		camRainbowRow->setSpacing(12);
		camRainbowRow->addWidget(chkCamRainbow, 1);
		camRainbowRow->addWidget(mkField(T("Dialog.FrameRainbowSec"), spCamRainbowSec), 1);
		lay->addLayout(camRainbowRow);
		lay->addSpacing(6);

		chkCamLayout = new QCheckBox(T("Dialog.CamLayout"), page);
		chkCamLayout->setToolTip(T("Dialog.CamLayoutTooltip"));
		lay->addWidget(chkCamLayout);

		auto mkPosSpin = [page](const QString &tip) {
			auto *sp = new QSpinBox(page);
			sp->setRange(-20000, 20000);
			sp->setSingleStep(10);
			sp->setSuffix(T("Unit.Pixels"));
			sp->setToolTip(tip);
			return sp;
		};
		auto mkSizeSpin = [page](const QString &tip) {
			auto *sp = new QSpinBox(page);
			sp->setRange(20, 8000);
			sp->setSingleStep(10);
			sp->setSuffix(T("Unit.Pixels"));
			sp->setToolTip(tip);
			return sp;
		};
		spCamIdleX = mkPosSpin(T("Dialog.CamLayoutXTooltip"));
		spCamIdleY = mkPosSpin(T("Dialog.CamLayoutYTooltip"));
		spCamIdleSize = mkSizeSpin(T("Dialog.CamLayoutSizeTooltip"));
		spCamZoomX = mkPosSpin(T("Dialog.CamLayoutXTooltip"));
		spCamZoomY = mkPosSpin(T("Dialog.CamLayoutYTooltip"));
		spCamZoomSize = mkSizeSpin(T("Dialog.CamLayoutSizeTooltip"));
		btnCamIdleTake = new QPushButton(T("Dialog.CamLayoutTake"), page);
		btnCamIdleTake->setCheckable(true);
		btnCamIdleTake->setToolTip(T("Dialog.CamLayoutTakeTooltip"));
		btnCamZoomTake = new QPushButton(T("Dialog.CamLayoutTake"), page);
		btnCamZoomTake->setCheckable(true);
		btnCamZoomTake->setToolTip(T("Dialog.CamLayoutTakeTooltip"));

		auto *idleRow = new QHBoxLayout;
		idleRow->setSpacing(12);
		idleRow->addWidget(mkField(T("Dialog.CamLayoutIdleX"), spCamIdleX), 1);
		idleRow->addWidget(mkField(T("Dialog.CamLayoutIdleY"), spCamIdleY), 1);
		idleRow->addWidget(mkField(T("Dialog.CamLayoutIdleSize"), spCamIdleSize), 1);
		idleRow->addWidget(mkField(" ", btnCamIdleTake), 1);
		lay->addLayout(idleRow);

		auto *camZoomedRow = new QHBoxLayout;
		camZoomedRow->setSpacing(12);
		camZoomedRow->addWidget(mkField(T("Dialog.CamLayoutZoomX"), spCamZoomX), 1);
		camZoomedRow->addWidget(mkField(T("Dialog.CamLayoutZoomY"), spCamZoomY), 1);
		camZoomedRow->addWidget(mkField(T("Dialog.CamLayoutZoomSize"), spCamZoomSize), 1);
		camZoomedRow->addWidget(mkField(" ", btnCamZoomTake), 1);
		lay->addLayout(camZoomedRow);
		lay->addSpacing(10);

		/* While one "Follow OBS" button is on, the numbers of its row track the
		 * webcam as it is dragged or resized in OBS. Only one row follows at a
		 * time, and it stops when the tab changes, the dialog hides or the
		 * layout is switched off. */
		auto *camFollowTimer = new QTimer(this);
		camFollowTimer->setInterval(200);
		auto stopFollow = [this, camFollowTimer]() {
			camFollowTimer->stop();
			btnCamIdleTake->setChecked(false);
			btnCamZoomTake->setChecked(false);
		};
		connect(camFollowTimer, &QTimer::timeout, this, [this, camFollowTimer, stopFollow]() {
			const bool idleRow = btnCamIdleTake->isChecked();
			if (!isVisible() || tabWidget->currentIndex() != camFollowTimer->property("tab").toInt() ||
			    !chkCamLayout->isChecked() || !chkCamRing->isChecked() ||
			    (!idleRow && !btnCamZoomTake->isChecked())) {
				stopFollow();
				return;
			}
			int px = 0, py = 0, ps = 0;
			if (!ZoominatorController::instance().readCamLayout(px, py, ps))
				return; /* not readable right now; keep the old numbers */
			QSpinBox *x = idleRow ? spCamIdleX : spCamZoomX;
			QSpinBox *y = idleRow ? spCamIdleY : spCamZoomY;
			QSpinBox *size = idleRow ? spCamIdleSize : spCamZoomSize;
			if (x->value() != px)
				x->setValue(px);
			if (y->value() != py)
				y->setValue(py);
			if (size->value() != ps)
				size->setValue(ps);
		});
		auto onFollowToggled = [this, camFollowTimer](QPushButton *self, QPushButton *other, bool on) {
			if (on) {
				other->setChecked(false);
				int px = 0, py = 0, ps = 0;
				if (!ZoominatorController::instance().readCamLayout(px, py, ps)) {
					lblStatus->setText(T("Dialog.CamLayoutTakeFailed"));
					self->setChecked(false);
					return;
				}
				lblStatus->setText(T("Dialog.CamLayoutFollowing"));
				camFollowTimer->setProperty("tab", tabWidget->currentIndex());
				camFollowTimer->start();
			} else if (!other->isChecked()) {
				camFollowTimer->stop();
			}
		};
		connect(btnCamIdleTake, &QPushButton::toggled, this,
			[=](bool on) { onFollowToggled(btnCamIdleTake, btnCamZoomTake, on); });
		connect(btnCamZoomTake, &QPushButton::toggled, this,
			[=](bool on) { onFollowToggled(btnCamZoomTake, btnCamIdleTake, on); });

		auto syncCamFields = [this]() {
			const bool on = chkCamRing->isChecked();
			const bool lay = on && chkCamLayout->isChecked();
			chkCamLayout->setEnabled(on);
			chkCamRainbow->setEnabled(on);
			spCamRainbowSec->setEnabled(on && chkCamRainbow->isChecked());
			if (!lay) {
				btnCamIdleTake->setChecked(false);
				btnCamZoomTake->setChecked(false);
			}
			for (QWidget *w : {(QWidget *)spCamIdleX, (QWidget *)spCamIdleY, (QWidget *)spCamIdleSize,
					   (QWidget *)spCamZoomX, (QWidget *)spCamZoomY, (QWidget *)spCamZoomSize,
					   (QWidget *)btnCamIdleTake, (QWidget *)btnCamZoomTake})
				w->setEnabled(lay);
			cmbCamSource->setEnabled(on);
			btnCamColor->setEnabled(on && !chkCamRainbow->isChecked());
			spCamRing->setEnabled(on);
			spCamGlow->setEnabled(on);
			spCamGlowPct->setEnabled(on);
		};
		connect(chkCamRing, &QCheckBox::toggled, this, [syncCamFields](bool) { syncCamFields(); });
		connect(chkCamLayout, &QCheckBox::toggled, this, [syncCamFields](bool) { syncCamFields(); });
		connect(chkCamRainbow, &QCheckBox::toggled, this, [syncCamFields](bool) { syncCamFields(); });
		syncCamFields();

		addSection(lay, T("Dialog.Section.Focus"));

		chkFocus = new QCheckBox(T("Dialog.FocusEnable"), page);
		chkFocus->setToolTip(T("Dialog.FocusEnableTooltip"));
		lay->addWidget(chkFocus);
		chkFocusWhenNotZoomed = new QCheckBox(T("Dialog.FocusWhenNotZoomed"), page);
		chkFocusWhenNotZoomed->setToolTip(T("Dialog.FocusWhenNotZoomedTooltip"));
		lay->addSpacing(6);

		{
			auto *row = new QWidget(page);
			auto *h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(6);
			editFocusToggle = new QKeySequenceEdit(row);
			editFocusToggle->setToolTip(T("Dialog.FocusToggleHotkeyTooltip"));
			auto *clear = new QPushButton(T("Dialog.Clear"), row);
			QKeySequenceEdit *e = editFocusToggle;
			connect(clear, &QPushButton::clicked, this, [e]() { e->setKeySequence(QKeySequence()); });
			h->addWidget(editFocusToggle, 1);
			h->addWidget(clear);
			lay->addWidget(mkField(T("Dialog.FocusToggleHotkey"), row));
			lay->addSpacing(6);
		}
		{
			auto *row = new QWidget(page);
			auto *h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(6);
			editFocusOn = new QKeySequenceEdit(row);
			editFocusOn->setToolTip(T("Dialog.FocusOnHotkeyTooltip"));
			auto *clear = new QPushButton(T("Dialog.Clear"), row);
			QKeySequenceEdit *e = editFocusOn;
			connect(clear, &QPushButton::clicked, this, [e]() { e->setKeySequence(QKeySequence()); });
			h->addWidget(editFocusOn, 1);
			h->addWidget(clear);
			lay->addWidget(mkField(T("Dialog.FocusOnHotkey"), row));
			lay->addSpacing(6);
		}
		{
			auto *row = new QWidget(page);
			auto *h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(6);
			editFocusOff = new QKeySequenceEdit(row);
			editFocusOff->setToolTip(T("Dialog.FocusOffHotkeyTooltip"));
			auto *clear = new QPushButton(T("Dialog.Clear"), row);
			QKeySequenceEdit *e = editFocusOff;
			connect(clear, &QPushButton::clicked, this, [e]() { e->setKeySequence(QKeySequence()); });
			h->addWidget(editFocusOff, 1);
			h->addWidget(clear);
			lay->addWidget(mkField(T("Dialog.FocusOffHotkey"), row));
			lay->addSpacing(6);
		}

		cmbFocusStyle = new QComboBox(page);
		cmbFocusStyle->addItem(T("Dialog.FocusStyle.Dim"), "dim");
		cmbFocusStyle->addItem(T("Dialog.FocusStyle.Blur"), "blur");
		cmbFocusStyle->addItem(T("Dialog.FocusStyle.BlurDim"), "blur_dim");
		cmbFocusStyle->setToolTip(T("Dialog.FocusStyleTooltip"));

		spFocusBlur = new QSpinBox(page);
		spFocusBlur->setRange(0, 60);
		spFocusBlur->setSingleStep(2);
		spFocusBlur->setSuffix(T("Unit.Pixels"));
		spFocusBlur->setToolTip(T("Dialog.FocusBlurTooltip"));

		auto *styleRow = new QHBoxLayout;
		styleRow->setSpacing(12);
		styleRow->addWidget(mkField(T("Dialog.FocusStyle"), cmbFocusStyle), 1);
		styleRow->addWidget(mkField(T("Dialog.FocusBlur"), spFocusBlur), 1);
		lay->addLayout(styleRow);

		spFocusDim = new QSpinBox(page);
		spFocusDim->setRange(0, 100);
		spFocusDim->setSingleStep(5);
		spFocusDim->setSuffix(QStringLiteral(" %"));
		spFocusDim->setToolTip(T("Dialog.FocusDimTooltip"));

		spFocusCone = new QSpinBox(page);
		spFocusCone->setRange(0, 100);
		spFocusCone->setSingleStep(5);
		spFocusCone->setSuffix(QStringLiteral(" %"));
		spFocusCone->setToolTip(T("Dialog.FocusConeTooltip"));

		auto *focusRow = new QHBoxLayout;
		focusRow->setSpacing(12);
		focusRow->addWidget(mkField(T("Dialog.FocusDim"), spFocusDim), 1);
		focusRow->addWidget(mkField(T("Dialog.FocusCone"), spFocusCone), 1);
		lay->addLayout(focusRow);
		lay->addSpacing(6);

		spFocusRadius = new QSpinBox(page);
		spFocusRadius->setRange(40, 2000);
		spFocusRadius->setSingleStep(10);
		spFocusRadius->setSuffix(T("Unit.Pixels"));
		spFocusRadius->setToolTip(T("Dialog.FocusRadiusTooltip"));
		spFocusIdleRadius = new QSpinBox(page);
		spFocusIdleRadius->setRange(40, 2000);
		spFocusIdleRadius->setSingleStep(10);
		spFocusIdleRadius->setSuffix(T("Unit.Pixels"));
		spFocusIdleRadius->setToolTip(T("Dialog.FocusIdleRadiusTooltip"));
		/* One under the other: the zoomed size, then "show when not zoomed" with its own size below it. */
		auto *focusSizeRow = new QHBoxLayout;
		focusSizeRow->setSpacing(12);
		focusSizeRow->addWidget(mkField(T("Dialog.FocusRadius"), spFocusRadius), 1);
		focusSizeRow->addStretch(1);
		lay->addLayout(focusSizeRow);
		lay->addSpacing(6);
		lay->addWidget(chkFocusWhenNotZoomed);
		lay->addSpacing(4);
		auto *focusIdleRow = new QHBoxLayout;
		focusIdleRow->setSpacing(12);
		focusIdleRow->addWidget(mkField(T("Dialog.FocusIdleRadius"), spFocusIdleRadius), 1);
		focusIdleRow->addStretch(1);
		lay->addLayout(focusIdleRow);
		lay->addSpacing(10);

		auto syncFocusFields = [this]() {
			const bool on = chkFocus->isChecked();
			const QString style = cmbFocusStyle->currentData().toString();
			chkFocusWhenNotZoomed->setEnabled(on);
			cmbFocusStyle->setEnabled(on);
			spFocusBlur->setEnabled(on && style != QLatin1String("dim"));
			spFocusDim->setEnabled(on && style != QLatin1String("blur"));
			spFocusIdleRadius->setEnabled(on && chkFocusWhenNotZoomed->isChecked());
			spFocusCone->setEnabled(on);
			spFocusRadius->setEnabled(on);
		};
		syncFocusFieldsFn = syncFocusFields;
		connect(chkFocus, &QCheckBox::toggled, this, [syncFocusFields](bool) { syncFocusFields(); });
		connect(chkFocusWhenNotZoomed, &QCheckBox::toggled, this, [syncFocusFields](bool) { syncFocusFields(); });
		connect(cmbFocusStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			[syncFocusFields](int) { syncFocusFields(); });
		syncFocusFields();

		addSection(lay, T("Dialog.Section.Countdown"));

		auto *cdHelp = new QLabel(T("Dialog.CountdownHelp"), page);
		cdHelp->setWordWrap(true);
		lay->addWidget(cdHelp);
		spCountdown = new QSpinBox(page);
		spCountdown->setRange(1, 10);
		spCountdown->setSuffix(QStringLiteral(" s"));
		spCountdown->setToolTip(T("Dialog.CountdownTooltip"));
		lay->addWidget(mkField(T("Dialog.Countdown"), spCountdown));
		chkCountdownDefault = new QCheckBox(T("Dialog.CountdownDefault"), page);
		chkCountdownDefault->setToolTip(T("Dialog.CountdownDefaultTooltip"));
		lay->addWidget(chkCountdownDefault);
		lay->addSpacing(10);

		addSection(lay, T("Dialog.Section.StatusHud"));

		auto *hudHelp = new QLabel(T("Dialog.StatusHudHelp"), page);
		hudHelp->setWordWrap(true);
		lay->addWidget(hudHelp);
		chkStatusHud = new QCheckBox(T("Dialog.StatusHud"), page);
		chkStatusHud->setToolTip(T("Dialog.StatusHudTooltip"));
		lay->addWidget(chkStatusHud);
		cmbStatusHudCorner = new QComboBox(page);
		cmbStatusHudCorner->addItem(T("Dialog.StatusHud.TopLeft"), 0);
		cmbStatusHudCorner->addItem(T("Dialog.StatusHud.TopRight"), 1);
		cmbStatusHudCorner->addItem(T("Dialog.StatusHud.BottomLeft"), 2);
		cmbStatusHudCorner->addItem(T("Dialog.StatusHud.BottomRight"), 3);
		spRecLimit = new QSpinBox(page);
		spRecLimit->setRange(0, 600);
		spRecLimit->setSingleStep(5);
		spRecLimit->setSuffix(QStringLiteral(" s"));
		spRecLimit->setSpecialValueText(T("Dialog.RecLimitOff"));
		spRecLimit->setToolTip(T("Dialog.RecLimitTooltip"));
		auto *hudRow = new QHBoxLayout;
		hudRow->setSpacing(12);
		hudRow->addWidget(mkField(T("Dialog.StatusHudCorner"), cmbStatusHudCorner), 1);
		hudRow->addWidget(mkField(T("Dialog.RecLimit"), spRecLimit), 1);
		lay->addLayout(hudRow);
		connect(chkStatusHud, &QCheckBox::toggled, this, [this](bool on) {
			cmbStatusHudCorner->setEnabled(on);
			spRecLimit->setEnabled(on);
		});
		lay->addSpacing(10);

		addSection(lay, T("Dialog.Section.Sound"));

		QWidget *soundRowWidget[3] = {nullptr, nullptr, nullptr};
		chkSoundEnabled = new QCheckBox(T("Dialog.SoundEnable"), page);
		chkSoundEnabled->setToolTip(T("Dialog.SoundEnableTooltip"));
		lay->addWidget(chkSoundEnabled);
		lay->addSpacing(6);

		{
			static const char *labels[3] = {"Dialog.SoundZoomIn", "Dialog.SoundZoomOut", "Dialog.SoundTyping"};
			for (int i = 0; i < 3; ++i) {
				auto *row = new QWidget(page);
				auto *h = new QHBoxLayout(row);
				h->setContentsMargins(0, 0, 0, 0);
				h->setSpacing(6);
				editSoundName[i] = new QLineEdit(row);
				editSoundName[i]->setReadOnly(true);
				auto *browse = new QPushButton(T("Dialog.SoundBrowse"), row);
				auto *def = new QPushButton(T("Dialog.SoundDefault"), row);
				def->setToolTip(T("Dialog.SoundDefaultTooltip"));
				auto *play = new QPushButton(T("Dialog.SoundPlay"), row);
				spSoundVolume[i] = new QSpinBox(row);
				spSoundVolume[i]->setRange(0, 100);
				spSoundVolume[i]->setSingleStep(5);
				spSoundVolume[i]->setSuffix(T("Unit.Percent"));
				spSoundVolume[i]->setToolTip(T("Dialog.SoundVolumeTooltip"));
				h->addWidget(editSoundName[i], 1);
				h->addWidget(browse);
				h->addWidget(def);
				h->addWidget(play);
				h->addWidget(spSoundVolume[i]);
				chkSoundOn[i] = new QCheckBox(T(labels[i]), page);
				chkSoundOn[i]->setToolTip(T("Dialog.SoundOnTooltip"));
				lay->addWidget(chkSoundOn[i]);
				lay->addWidget(row);
				lay->addSpacing(8);
				soundRowWidget[i] = row;

				connect(browse, &QPushButton::clicked, this, [this, i]() { browseSound(i); });
				connect(def, &QPushButton::clicked, this, [this, i]() { resetSound(i); });
				connect(play, &QPushButton::clicked, this, [this, i]() { playSoundPreview(i); });
			}
		}

		cmbSoundTypingMode = new QComboBox(page);
		cmbSoundTypingMode->addItem(T("Dialog.SoundTypingMode.Tick"), "tick");
		cmbSoundTypingMode->addItem(T("Dialog.SoundTypingMode.Loop"), "loop");
		cmbSoundTypingMode->setToolTip(T("Dialog.SoundTypingModeTooltip"));
		lay->addWidget(mkField(T("Dialog.SoundTypingMode"), cmbSoundTypingMode));
		/* The master switch greys everything below it; each sound's own switch
		 * greys that sound's controls. */
		auto syncSoundFields = [this, soundRowWidget]() {
			const bool master = chkSoundEnabled->isChecked();
			for (int i = 0; i < 3; ++i) {
				chkSoundOn[i]->setEnabled(master);
				soundRowWidget[i]->setEnabled(master && chkSoundOn[i]->isChecked());
			}
			cmbSoundTypingMode->setEnabled(master && chkSoundOn[2]->isChecked());
		};
		connect(chkSoundEnabled, &QCheckBox::toggled, this, [syncSoundFields](bool) { syncSoundFields(); });
		for (int i = 0; i < 3; ++i)
			connect(chkSoundOn[i], &QCheckBox::toggled, this, [syncSoundFields](bool) { syncSoundFields(); });
		syncSoundFields();
		lay->addSpacing(10);

		addSection(lay, T("Dialog.Section.CursorRing"));

		chkCursorRing = new QCheckBox(T("Dialog.CursorRing"), page);
		chkCursorRing->setToolTip(T("Dialog.CursorRingTooltip"));
		lay->addWidget(chkCursorRing);
		chkCursorRingIdle = new QCheckBox(T("Dialog.CursorRingIdle"), page);
		chkCursorRingIdle->setToolTip(T("Dialog.CursorRingIdleTooltip"));
		lay->addWidget(chkCursorRingIdle);

		spCursorRingSize = new QSpinBox(page);
		spCursorRingSize->setRange(20, 400);
		spCursorRingSize->setSingleStep(5);
		spCursorRingSize->setSuffix(T("Unit.Pixels"));
		spCursorRingSize->setToolTip(T("Dialog.CursorRingSizeTooltip"));
		spCursorRingPx = new QSpinBox(page);
		spCursorRingPx->setRange(1, 40);
		spCursorRingPx->setSuffix(T("Unit.Pixels"));
		spCursorRingPx->setToolTip(T("Dialog.CursorRingThicknessTooltip"));
		btnCursorRingColor = new ColorButton(page);
		btnCursorRingColor->setMinimumHeight(28);
		btnCursorRingColor->setToolTip(T("Dialog.CursorRingColorTooltip"));
		auto *cursorRingRow = new QHBoxLayout;
		cursorRingRow->setSpacing(12);
		cursorRingRow->addWidget(mkField(T("Dialog.CursorRingSize"), spCursorRingSize), 1);
		cursorRingRow->addWidget(mkField(T("Dialog.CursorRingThickness"), spCursorRingPx), 1);
		cursorRingRow->addWidget(mkField(T("Dialog.CursorRingColor"), btnCursorRingColor), 1);
		lay->addLayout(cursorRingRow);

		chkCursorRingRainbow = new QCheckBox(T("Dialog.CursorRingRainbow"), page);
		chkCursorRingRainbow->setToolTip(T("Dialog.FrameRainbowTooltip"));
		spCursorRingSec = new QSpinBox(page);
		spCursorRingSec->setRange(2, 120);
		spCursorRingSec->setSuffix(QStringLiteral(" s"));
		spCursorRingSec->setToolTip(T("Dialog.FrameRainbowSecTooltip"));
		auto *cursorRainbowRow = new QHBoxLayout;
		cursorRainbowRow->setSpacing(12);
		cursorRainbowRow->addWidget(chkCursorRingRainbow, 1);
		cursorRainbowRow->addWidget(mkField(T("Dialog.FrameRainbowSec"), spCursorRingSec), 1);
		lay->addLayout(cursorRainbowRow);

		auto syncCursorRingFields = [this]() {
			const bool on = chkCursorRing->isChecked();
			const bool rb = chkCursorRingRainbow->isChecked();
			chkCursorRingIdle->setEnabled(on);
			spCursorRingSize->setEnabled(on);
			spCursorRingPx->setEnabled(on);
			btnCursorRingColor->setEnabled(on && !rb);
			chkCursorRingRainbow->setEnabled(on);
			spCursorRingSec->setEnabled(on && rb);
		};
		connect(chkCursorRing, &QCheckBox::toggled, this, [syncCursorRingFields](bool) { syncCursorRingFields(); });
		connect(chkCursorRingRainbow, &QCheckBox::toggled, this, [syncCursorRingFields](bool) { syncCursorRingFields(); });
		syncCursorRingFields();
		lay->addSpacing(10);

		addSection(lay, T("Dialog.Section.KeyDisplay"));

		chkKeyDisplay = new QCheckBox(T("Dialog.KeyDisplay"), page);
		chkKeyDisplay->setToolTip(T("Dialog.KeyDisplayTooltip"));
		lay->addWidget(chkKeyDisplay);
		chkKeyDisplayAll = new QCheckBox(T("Dialog.KeyDisplayAll"), page);
		chkKeyDisplayAll->setToolTip(T("Dialog.KeyDisplayAllTooltip"));
		lay->addWidget(chkKeyDisplayAll);
		spKeyDisplaySize = new QSpinBox(page);
		spKeyDisplaySize->setRange(24, 160);
		spKeyDisplaySize->setSingleStep(4);
		spKeyDisplaySize->setSuffix(T("Unit.Pixels"));
		spKeyDisplaySize->setToolTip(T("Dialog.KeyDisplaySizeTooltip"));
		spKeyDisplayY = new QSpinBox(page);
		spKeyDisplayY->setRange(5, 95);
		spKeyDisplayY->setSuffix(T("Unit.Percent"));
		spKeyDisplayY->setToolTip(T("Dialog.KeyDisplayYTooltip"));
		auto *keyDisplayRow = new QHBoxLayout;
		keyDisplayRow->setSpacing(12);
		keyDisplayRow->addWidget(mkField(T("Dialog.KeyDisplaySize"), spKeyDisplaySize), 1);
		keyDisplayRow->addWidget(mkField(T("Dialog.KeyDisplayY"), spKeyDisplayY), 1);
		lay->addLayout(keyDisplayRow);
		connect(chkKeyDisplay, &QCheckBox::toggled, this, [this](bool on) {
			chkKeyDisplayAll->setEnabled(on);
			spKeyDisplaySize->setEnabled(on);
			spKeyDisplayY->setEnabled(on);
		});
		lay->addSpacing(10);

		addSection(lay, T("Dialog.Section.Guide"));

		chkGuide = new QCheckBox(T("Dialog.Guide"), page);
		chkGuide->setToolTip(T("Dialog.GuideTooltip"));
		lay->addWidget(chkGuide);
		auto mkGuideSpin = [page](int hi, const QString &tip) {
			auto *sp = new QSpinBox(page);
			sp->setRange(0, hi);
			sp->setSingleStep(5);
			sp->setSuffix(T("Unit.Pixels"));
			sp->setToolTip(tip);
			return sp;
		};
		spGuideTop = mkGuideSpin(1500, T("Dialog.GuideMarginTooltip"));
		spGuideBottom = mkGuideSpin(1500, T("Dialog.GuideMarginTooltip"));
		spGuideLeft = mkGuideSpin(500, T("Dialog.GuideMarginTooltip"));
		spGuideRight = mkGuideSpin(500, T("Dialog.GuideMarginTooltip"));
		auto *guideRow = new QHBoxLayout;
		guideRow->setSpacing(12);
		guideRow->addWidget(mkField(T("Dialog.GuideTop"), spGuideTop), 1);
		guideRow->addWidget(mkField(T("Dialog.GuideBottom"), spGuideBottom), 1);
		guideRow->addWidget(mkField(T("Dialog.GuideLeft"), spGuideLeft), 1);
		guideRow->addWidget(mkField(T("Dialog.GuideRight"), spGuideRight), 1);
		lay->addLayout(guideRow);
		connect(chkGuide, &QCheckBox::toggled, this, [this](bool on) {
			for (QWidget *w : {(QWidget *)spGuideTop, (QWidget *)spGuideBottom, (QWidget *)spGuideLeft, (QWidget *)spGuideRight})
				w->setEnabled(on);
		});
		lay->addSpacing(10);

		addSection(lay, T("Dialog.Section.Developer"));

		chkDebug = new QCheckBox(T("Dialog.EnableDebugLogging"), page);
		lay->addWidget(chkDebug);

		lay->addStretch(1);
		tabWidget->addTab(wrapScrollable(page), T("Dialog.Tab.Advanced"));
	}

	{
		auto *page = new QWidget;
		auto *lay = new QVBoxLayout(page);
		lay->setContentsMargins(20, 20, 20, 20);
		lay->setSpacing(10);

		auto *intro = new QLabel(T("Dialog.Presets.Intro"), page);
		intro->setWordWrap(true);
		lay->addWidget(intro);

		for (int i = 0; i < 2; ++i) {
			const bool horizontal = (i == 1);
			addSection(lay, T(horizontal ? "Dialog.Presets.Horizontal" : "Dialog.Presets.Vertical"), i == 0);
			presetListLay[i] = new QVBoxLayout;
			presetListLay[i]->setSpacing(6);
			lay->addLayout(presetListLay[i]);

			auto *btnAdd = new QPushButton(T("Dialog.Presets.Add"), page);
			btnAdd->setToolTip(T("Dialog.Presets.AddTooltip"));
			auto *addRow = new QHBoxLayout;
			addRow->addWidget(btnAdd);
			addRow->addStretch(1);
			lay->addLayout(addRow);
			connect(btnAdd, &QPushButton::clicked, this, [this, horizontal]() {
				applyToController();
				auto &ctl = ZoominatorController::instance();
				int count = 0;
				for (const auto &pi : ctl.listPresets())
					count += (pi.horizontal == horizontal) ? 1 : 0;
				const QString name = QString(T("Dialog.Presets.NewName")).arg(count + 1);
				if (ctl.addPreset(name, horizontal).isEmpty()) {
					lblStatus->setText(T("Dialog.PresetSaveFailed"));
					return;
				}
				rebuildPresetRows();
				lblStatus->setText(QString(T("Dialog.Presets.Added")).arg(name));
			});
		}
		rebuildPresetRows();
		addSection(lay, T("Dialog.Backup.Title"));
		auto *backupHelp = new QLabel(T("Dialog.Backup.Help"), page);
		backupHelp->setWordWrap(true);
		lay->addWidget(backupHelp);
		auto *btnExport = new QPushButton(T("Dialog.Backup.Export"), page);
		btnExport->setToolTip(T("Dialog.Backup.ExportTooltip"));
		auto *btnImport = new QPushButton(T("Dialog.Backup.Import"), page);
		btnImport->setToolTip(T("Dialog.Backup.ImportTooltip"));
		auto *backupRow = new QHBoxLayout;
		backupRow->setSpacing(8);
		backupRow->addWidget(btnExport);
		backupRow->addWidget(btnImport);
		backupRow->addStretch(1);
		lay->addLayout(backupRow);
		connect(btnExport, &QPushButton::clicked, this, [this]() {
			applyToController();
			const QString file = QFileDialog::getSaveFileName(
				this, T("Dialog.Backup.Export"),
				QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
					.filePath(QStringLiteral("zoominator-settings.json")),
				QStringLiteral("JSON (*.json)"));
			if (file.isEmpty())
				return;
			lblStatus->setText(ZoominatorController::instance().exportSettings(file)
						   ? QString(T("Dialog.Backup.Exported")).arg(file)
						   : QString(T("Dialog.Backup.Failed")));
		});
		connect(btnImport, &QPushButton::clicked, this, [this]() {
			const QString file = QFileDialog::getOpenFileName(
				this, T("Dialog.Backup.Import"),
				QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
				QStringLiteral("JSON (*.json)"));
			if (file.isEmpty())
				return;
			if (ZoominatorController::instance().importSettings(file)) {
				loadFromController();
				lblStatus->setText(T("Dialog.Backup.Imported"));
			} else {
				lblStatus->setText(T("Dialog.Backup.Failed"));
			}
		});
		lay->addStretch(1);

		tabWidget->addTab(wrapScrollable(page), T("Dialog.Tab.Presets"));
	}

	{
		auto *page = new QWidget;
		auto *lay = new QVBoxLayout(page);
		lay->setContentsMargins(20, 20, 20, 20);
		lay->setSpacing(10);

		auto *info = new QLabel(T("Dialog.SourcesHelp"), page);
		info->setWordWrap(true);
		lay->addWidget(info);

		lstSources = new QListWidget(page);
		lstSources->setAlternatingRowColors(true);
		lstSources->setSortingEnabled(false);
		lay->addWidget(lstSources, 1);

		tabWidget->addTab(page, T("Dialog.Tab.Sources"));
	}

	root->addWidget(tabWidget, 1);

	lblStatus = new QLabel(this);
	lblStatus->setWordWrap(true);
	lblStatus->setText(T("Dialog.StatusTip"));
	root->addWidget(lblStatus);

	auto *btnRow = new QHBoxLayout;
	btnRow->setSpacing(8);
	btnRefresh = new QPushButton(T("Dialog.RefreshLists"), this);
	btnApply = new QPushButton(T("Dialog.Apply"), this);
	btnTest = new QPushButton(T("Dialog.Test"), this);
	btnRow->addWidget(btnRefresh);
	btnRow->addStretch(1);
	btnRow->addWidget(btnTest);
	btnRow->addWidget(btnApply);
	root->addLayout(btnRow);

	connect(btnRefresh, &QPushButton::clicked, this, &ZoominatorDialog::refreshLists);
	connect(btnApply, &QPushButton::clicked, this, &ZoominatorDialog::applyToController);
	connect(btnTest, &QPushButton::clicked, this, &ZoominatorDialog::testZoom);
	connect(btnClearHotkey, &QPushButton::clicked, this, &ZoominatorDialog::clearHotkey);
	connect(btnClearFollowToggleHotkey, &QPushButton::clicked, this, &ZoominatorDialog::clearFollowToggleHotkey);
	connect(btnMarkerColor, &QPushButton::clicked, this, &ZoominatorDialog::chooseMarkerColor);
	connect(btnFrameColor, &QPushButton::clicked, this, [this]() { chooseFrameColor(btnFrameColor); });
	connect(btnFrameBg, &QPushButton::clicked, this, [this]() { chooseFrameColor(btnFrameBg); });
	connect(btnFrameBg2, &QPushButton::clicked, this, [this]() { chooseFrameColor(btnFrameBg2); });
	connect(btnCamColor, &QPushButton::clicked, this, [this]() { chooseFrameColor(btnCamColor); });
	connect(btnCursorRingColor, &QPushButton::clicked, this, [this]() { chooseFrameColor(btnCursorRingColor); });
	{
		auto syncHaloFields = [this]() {
			const bool on = chkShowCursorMarker->isChecked();
			spMarkerSize->setEnabled(on);
			spMarkerThickness->setEnabled(on);
			btnMarkerColor->setEnabled(on);
			chkShowMarkerWhenNotZoomed->setEnabled(on);
			spMarkerOffX->setEnabled(on);
			spMarkerOffY->setEnabled(on);
			btnMarkerOffReset->setEnabled(on);
			spMarkerIdleSize->setEnabled(on && chkShowMarkerWhenNotZoomed->isChecked());
		};
		connect(chkShowCursorMarker, &QCheckBox::toggled, this, [syncHaloFields](bool) { syncHaloFields(); });
		connect(chkShowMarkerWhenNotZoomed, &QCheckBox::toggled, this, [syncHaloFields](bool) { syncHaloFields(); });
		syncHaloFields();
	}
}

void ZoominatorDialog::populateSourcesTab()
{
	if (!lstSources)
		return;

	auto &c = ZoominatorController::instance();

	QSet<QString> included;
	if (lstSources->count() > 0) {
		for (int i = 0; i < lstSources->count(); i++) {
			const QListWidgetItem *it = lstSources->item(i);
			if (it && it->checkState() == Qt::Checked)
				included.insert(it->data(Qt::UserRole).toString());
		}
	} else {
		included = c.includedSources;
	}

	lstSources->clear();

	struct Collector {
		QMap<QString, QString> *map = nullptr;

		static void visitScene(obs_scene_t *scene, Collector *col)
		{
			if (!scene || !col || !col->map)
				return;
			obs_scene_enum_items(
				scene,
				[](obs_scene_t *, obs_sceneitem_t *item, void *p) -> bool {
					auto *col2 = static_cast<Collector *>(p);
					if (!item || !col2 || !col2->map)
						return true;

					obs_source_t *src = obs_sceneitem_get_source(item);
					if (!src)
						return true;

					const char *name = obs_source_get_name(src);
					if (!name || !*name)
						return true;

					if (strcmp(name, "Zoominator Cursor Marker") == 0 || strcmp(name, "Zoominator Focus Dim") == 0)
						return true;

					const QString qname = QString::fromUtf8(name);
					if (!col2->map->contains(qname)) {
						const char *id = obs_source_get_id(src);
						col2->map->insert(qname, id ? QString::fromUtf8(id) : QString());
					}

					if (obs_scene_t *sub = obs_scene_from_source(src))
						Collector::visitScene(sub, col2);

					return true;
				},
				col);
		}
	};

	QMap<QString, QString> sourceMap;
	Collector collector{&sourceMap};

	obs_enum_scenes(
		[](void *p, obs_source_t *sceneSrc) -> bool {
			auto *col = static_cast<Collector *>(p);
			if (sceneSrc && col)
				if (obs_scene_t *scene = obs_scene_from_source(sceneSrc))
					Collector::visitScene(scene, col);
			return true;
		},
		&collector);

	QStringList names = sourceMap.keys();
	names.sort(Qt::CaseInsensitive);

	for (const QString &name : names) {
		const QString kind = friendlySourceKind(sourceMap.value(name));
		const QString label = kind.isEmpty() ? name : QStringLiteral("%1  [%2]").arg(name, kind);

		auto *litem = new QListWidgetItem(label, lstSources);
		litem->setData(Qt::UserRole, name);
		litem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		litem->setCheckState(included.contains(name) ? Qt::Checked : Qt::Unchecked);
	}

	/* Webcam source list: every video source in the collection. */
	if (cmbCamSource) {
		QString want = cmbCamSource->currentData().toString();
		if (want.isEmpty())
			want = cmbCamSource->property("savedName").toString();
		if (want.isEmpty())
			want = c.camSourceName;
		cmbCamSource->clear();
		cmbCamSource->addItem(T("Dialog.CamSourceNone"), QString());
		for (const QString &name : names) {
			const QString kind = friendlySourceKind(sourceMap.value(name));
			cmbCamSource->addItem(kind.isEmpty() ? name : QStringLiteral("%1  [%2]").arg(name, kind), name);
		}
		int idx = cmbCamSource->findData(want);
		if (idx < 0 && !want.isEmpty()) {
			cmbCamSource->addItem(want, want);
			idx = cmbCamSource->count() - 1;
		}
		cmbCamSource->setCurrentIndex(std::max(0, idx));
	}
}

void ZoominatorDialog::populateSources()
{
	const QString cur = cmbSource->currentData().toString();

	cmbSource->blockSignals(true);
	cmbSource->clear();
	cmbSource->addItem(T("Dialog.SelectScreen"), "");

	const auto screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); ++i) {
		auto *screen = screens[i];
		if (!screen)
			continue;
		const QRect g = screen->geometry();
		const QString key = QStringLiteral("%1,%2,%3,%4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
		const QString label = QStringLiteral("%1: %2×%3 @ (%4, %5)")
					      .arg(screen->name())
					      .arg(g.width())
					      .arg(g.height())
					      .arg(g.x())
					      .arg(g.y());
		cmbSource->addItem(label, key);
	}

	int idx = cmbSource->findData(cur);
	if (idx < 0)
		idx = cmbSource->findData(ZoominatorController::instance().screenKey);
	if (idx >= 0)
		cmbSource->setCurrentIndex(idx);

	cmbSource->blockSignals(false);
}

void ZoominatorDialog::refreshLists()
{
	populateSources();
	populateSourcesTab();
}

void ZoominatorDialog::loadFromController()
{
	loading = true;
	auto &c = ZoominatorController::instance();

	refreshLists();

	{
		int idx = cmbSource->findData(c.screenKey);
		if (idx >= 0)
			cmbSource->setCurrentIndex(idx);

		idx = cmbMode->findData(c.hotkeyMode);
		if (idx >= 0)
			cmbMode->setCurrentIndex(idx);

		editFollowToggleHotkey->setKeySequence(QKeySequence(c.followToggleHotkeySequence));
		editManualZoomIn->setKeySequence(QKeySequence(c.manualZoomInSequence));
		editManualZoomOut->setKeySequence(QKeySequence(c.manualZoomOutSequence));
		editFocusToggle->setKeySequence(QKeySequence(c.focusToggleSequence));
		editFocusOn->setKeySequence(QKeySequence(c.focusOnSequence));
		editFocusOff->setKeySequence(QKeySequence(c.focusOffSequence));
	}

	{
		int idx = cmbTrigger->findData(c.triggerType);
		if (idx >= 0)
			cmbTrigger->setCurrentIndex(idx);

		idx = cmbMouseBtn->findData(c.mouseButton);
		if (idx >= 0)
			cmbMouseBtn->setCurrentIndex(idx);

		chkCtrl->setChecked(c.modCtrl);
		chkLeftCtrl->setChecked(c.modLeftCtrl);
		chkAlt->setChecked(c.modAlt);
		chkLeftAlt->setChecked(c.modLeftAlt);
		chkShift->setChecked(c.modShift);
		chkLeftShift->setChecked(c.modLeftShift);
		chkWin->setChecked(c.modWin);
		chkLeftWin->setChecked(c.modLeftWin);
		chkRightCtrl->setChecked(c.modRightCtrl);
		chkRightAlt->setChecked(c.modRightAlt);
		chkRightShift->setChecked(c.modRightShift);
		chkRightWin->setChecked(c.modRightWin);

		editHotkey->setKeySequence(QKeySequence(c.hotkeySequence));

		chkFocus->setChecked(c.focusEnabled);
		chkFocusWhenNotZoomed->setChecked(c.showFocusWhenNotZoomed);
		spFocusDim->setValue(c.focusDimPct);
		spFocusIdleRadius->setValue(c.focusIdleRadius);
		spFocusRadius->setValue(c.focusRadius);
		spFocusCone->setValue(c.focusCone);
		spFocusBlur->setValue(c.focusBlurPx);
		const int styleIdx = cmbFocusStyle->findData(ZoominatorController::focusStyleToString(c.focusStyle));
		cmbFocusStyle->setCurrentIndex(styleIdx >= 0 ? styleIdx : 0);
		if (syncFocusFieldsFn)
			syncFocusFieldsFn();
		chkAutoClick->setChecked(c.autoOnClick);
		chkAutoTyping->setChecked(c.autoOnTyping);
		chkCaretFollow->setChecked(c.caretFollow);
		chkAutoZoomOut->setChecked(c.autoZoomOutEnabled);
		spAutoHold->setEnabled(c.autoZoomOutEnabled);
		spAutoHold->setValue(c.autoHoldMs);
		updateTriggerRows();
	}

	{
		spZoom->setValue(c.zoomFactor);
		spIn->setValue(c.animInMs);
		spOut->setValue(c.animOutMs);
		const int easingIdx = cmbEasing->findData(ZoominatorController::zoomEasingToString(c.easing));
		cmbEasing->setCurrentIndex(easingIdx >= 0 ? easingIdx : 0);
		chkWheelZoom->setChecked(c.wheelZoomEnabled);
		cmbWheelMod->setCurrentIndex(std::max(0, cmbWheelMod->findData(c.wheelZoomModifier)));
		spWheelStep->setValue(c.wheelZoomInStep);
		spWheelMax->setValue(c.wheelZoomMaximum);
		spWheelExit->setValue(c.wheelZoomExit);
		cmbWheelMod->setEnabled(c.wheelZoomEnabled);
		spWheelStep->setEnabled(c.wheelZoomEnabled);
		spWheelMax->setEnabled(c.wheelZoomEnabled);
		spWheelExit->setEnabled(c.wheelZoomEnabled);
		const int anchorIdx =
			cmbZoomAnchor->findData(ZoominatorController::zoomAnchorModeToString(c.zoomAnchor));
		cmbZoomAnchor->setCurrentIndex(anchorIdx >= 0 ? anchorIdx : 1);
		spFollowSpeed->setValue(c.followSpeed);
		chkCenterFocus->setChecked(c.centerFocus);
		chkPortraitCover->setChecked(c.portraitCover);
		chkIdleFollowX->setChecked(c.idleFollowX);
		chkTilt3d->setChecked(c.tilt3dEnabled);
		spTiltX->setValue(c.tilt3dMaxX);
		spTiltY->setValue(c.tilt3dMaxY);
		spTiltPersp->setValue(c.tilt3dPerspective);
		chkTiltInvert->setChecked(c.tilt3dInvert);
		chkTiltInZoom->setChecked(c.tilt3dInZoom);
		for (QWidget *w : {(QWidget *)spTiltX, (QWidget *)spTiltY, (QWidget *)spTiltPersp, (QWidget *)chkTiltInvert,
				   (QWidget *)chkTiltInZoom})
			w->setEnabled(c.tilt3dEnabled);
		chkShowCursorMarker->setChecked(c.showCursorMarker);
		chkShowMarkerWhenNotZoomed->setChecked(c.showMarkerWhenNotZoomed);
		spMarkerSize->setEnabled(c.showCursorMarker);
		spMarkerThickness->setEnabled(c.showCursorMarker);
		btnMarkerColor->setEnabled(c.showCursorMarker);
		chkShowMarkerWhenNotZoomed->setEnabled(c.showCursorMarker);
		spMarkerIdleSize->setEnabled(c.showCursorMarker && c.showMarkerWhenNotZoomed);
		spMarkerSize->setValue(c.markerSize);
		spMarkerIdleSize->setValue(c.markerIdleSize);
		spMarkerOffX->setValue(c.markerOffsetX);
		spMarkerOffY->setValue(c.markerOffsetY);
		spMarkerOffX->setEnabled(c.showCursorMarker);
		spMarkerOffY->setEnabled(c.showCursorMarker);
		btnMarkerOffReset->setEnabled(c.showCursorMarker);
		spMarkerThickness->setValue(c.markerThickness);
		updateMarkerColorButton(QColor::fromRgba(c.markerColor));
		chkFrame->setChecked(c.frameEnabled);
		setColorButton(btnFrameColor, c.frameColor);
		setColorButton(btnFrameBg, c.frameBgColor);
		setColorButton(btnFrameBg2, c.frameBgColor2);
		cmbFrameBgMode->setCurrentIndex(std::clamp(c.frameBgMode, 0, 2));
		spFrameBgAngle->setValue(c.frameBgAngle);
		spFrameBorder->setValue(c.frameBorderPx);
		spFrameRadius->setValue(c.frameRadiusPx);
		spFrameGlow->setValue(c.frameGlowPx);
		spFrameGlowPct->setValue(c.frameGlowPct);
		btnFrameColor->setEnabled(c.frameEnabled && !c.frameBorderRainbow);
		btnFrameBg->setEnabled(c.frameEnabled && !c.frameBgRainbow);
		cmbFrameBgMode->setEnabled(c.frameEnabled && !c.frameBgRainbow);
		btnFrameBg2->setEnabled(c.frameEnabled && !c.frameBgRainbow && c.frameBgMode != 0);
		spFrameBgAngle->setEnabled(c.frameEnabled && !c.frameBgRainbow && c.frameBgMode == 1);
		spFrameBorder->setEnabled(c.frameEnabled);
		spFrameRadius->setEnabled(c.frameEnabled);
		spFrameGlow->setEnabled(c.frameEnabled);
		spFrameGlowPct->setEnabled(c.frameEnabled);
		chkFrameBgRainbow->setChecked(c.frameBgRainbow);
		chkFrameBorderRainbow->setChecked(c.frameBorderRainbow);
		spFrameRainbowSec->setValue(c.frameRainbowSec);
		chkFrameBgRainbow->setEnabled(c.frameEnabled);
		chkFrameBorderRainbow->setEnabled(c.frameEnabled);
		spFrameRainbowSec->setEnabled(c.frameEnabled && (c.frameBgRainbow || c.frameBorderRainbow));
		chkCamRing->setChecked(c.camRingEnabled);
		setColorButton(btnCamColor, c.camRingColor);
		spCamRing->setValue(c.camRingPx);
		spCamGlow->setValue(c.camGlowPx);
		spCamGlowPct->setValue(c.camGlowPct);
		chkCamLayout->setChecked(c.camLayoutEnabled);
		chkCamRainbow->setChecked(c.camRainbow);
		spCamRainbowSec->setValue(c.camRainbowSec);
		chkCamRainbow->setEnabled(c.camRingEnabled);
		spCamRainbowSec->setEnabled(c.camRingEnabled && c.camRainbow);
		spCamIdleX->setValue(c.camIdleX);
		spCamIdleY->setValue(c.camIdleY);
		spCamIdleSize->setValue(c.camIdleSize);
		spCamZoomX->setValue(c.camZoomX);
		spCamZoomY->setValue(c.camZoomY);
		spCamZoomSize->setValue(c.camZoomSize);
		cmbCamSource->setProperty("savedName", c.camSourceName);
		cmbCamSource->setEnabled(c.camRingEnabled);
		btnCamColor->setEnabled(c.camRingEnabled && !c.camRainbow);
		spCamRing->setEnabled(c.camRingEnabled);
		spCamGlow->setEnabled(c.camRingEnabled);
		spCamGlowPct->setEnabled(c.camRingEnabled);
		chkCamLayout->setEnabled(c.camRingEnabled);
		for (QWidget *w : {(QWidget *)spCamIdleX, (QWidget *)spCamIdleY, (QWidget *)spCamIdleSize,
				   (QWidget *)spCamZoomX, (QWidget *)spCamZoomY, (QWidget *)spCamZoomSize,
				   (QWidget *)btnCamIdleTake, (QWidget *)btnCamZoomTake})
			w->setEnabled(c.camRingEnabled && c.camLayoutEnabled);
		chkDebug->setChecked(c.debug);
		spCountdown->setValue(c.countdownSec);
		chkCountdownDefault->setChecked(c.countdownOnDefaultRecord);
		chkCursorRing->setChecked(c.cursorRingEnabled);
		chkCursorRingIdle->setChecked(c.cursorRingIdle);
		spCursorRingSize->setValue(c.cursorRingSize);
		spCursorRingPx->setValue(c.cursorRingPx);
		setColorButton(btnCursorRingColor, c.cursorRingColor);
		chkCursorRingRainbow->setChecked(c.cursorRingRainbow);
		spCursorRingSec->setValue(c.cursorRingSec);
		chkCursorRingIdle->setEnabled(c.cursorRingEnabled);
		spCursorRingSize->setEnabled(c.cursorRingEnabled);
		spCursorRingPx->setEnabled(c.cursorRingEnabled);
		btnCursorRingColor->setEnabled(c.cursorRingEnabled && !c.cursorRingRainbow);
		chkCursorRingRainbow->setEnabled(c.cursorRingEnabled);
		spCursorRingSec->setEnabled(c.cursorRingEnabled && c.cursorRingRainbow);
		chkKeyDisplay->setChecked(c.keyDisplayEnabled);
		chkKeyDisplayAll->setChecked(c.keyDisplayAll);
		spKeyDisplaySize->setValue(c.keyDisplaySize);
		spKeyDisplayY->setValue(c.keyDisplayYPct);
		chkKeyDisplayAll->setEnabled(c.keyDisplayEnabled);
		spKeyDisplaySize->setEnabled(c.keyDisplayEnabled);
		spKeyDisplayY->setEnabled(c.keyDisplayEnabled);
		chkGuide->setChecked(c.guideEnabled);
		spGuideTop->setValue(c.guideTopPx);
		spGuideBottom->setValue(c.guideBottomPx);
		spGuideLeft->setValue(c.guideLeftPx);
		spGuideRight->setValue(c.guideRightPx);
		for (QWidget *w : {(QWidget *)spGuideTop, (QWidget *)spGuideBottom, (QWidget *)spGuideLeft, (QWidget *)spGuideRight})
			w->setEnabled(c.guideEnabled);
		chkStatusHud->setChecked(c.showStatusHud);
		cmbStatusHudCorner->setCurrentIndex(std::clamp(c.statusHudCorner, 0, 3));
		cmbStatusHudCorner->setEnabled(c.showStatusHud);
		spRecLimit->setValue(c.recLimitSec);
		spRecLimit->setEnabled(c.showStatusHud);
		chkSoundEnabled->setChecked(c.soundEnabled);
		for (int i = 0; i < 3; ++i) {
			chkSoundOn[i]->setChecked(c.soundOn[i]);
			spSoundVolume[i]->setValue(c.soundVolume[i]);
			editSoundName[i]->setText(c.soundFile[i].isEmpty() ? T("Dialog.SoundBuiltIn") : c.soundName[i]);
		}
		{
			const int mi = cmbSoundTypingMode->findData(c.soundTypingMode);
			cmbSoundTypingMode->setCurrentIndex(mi >= 0 ? mi : 0);
		}
	}

	if (lstSources)
		lstSources->clear();
	populateSourcesTab();

	loading = false;
}

void ZoominatorDialog::applyToController()
{
	if (loading)
		return;

	auto &c = ZoominatorController::instance();

	c.screenKey = cmbSource->currentData().toString();
	c.hotkeyMode = cmbMode->currentData().toString();
	c.followToggleHotkeySequence = editFollowToggleHotkey->keySequence().toString(QKeySequence::NativeText);
	c.manualZoomInSequence = editManualZoomIn->keySequence().toString(QKeySequence::NativeText);
	c.manualZoomOutSequence = editManualZoomOut->keySequence().toString(QKeySequence::NativeText);
	c.focusToggleSequence = editFocusToggle->keySequence().toString(QKeySequence::NativeText);
	c.focusOnSequence = editFocusOn->keySequence().toString(QKeySequence::NativeText);
	c.focusOffSequence = editFocusOff->keySequence().toString(QKeySequence::NativeText);

	c.triggerType = cmbTrigger->currentData().toString();
	c.mouseButton = cmbMouseBtn->currentData().toString();

	c.modCtrl = chkCtrl->isChecked();
	c.modLeftCtrl = chkLeftCtrl->isChecked();
	c.modAlt = chkAlt->isChecked();
	c.modLeftAlt = chkLeftAlt->isChecked();
	c.modShift = chkShift->isChecked();
	c.modLeftShift = chkLeftShift->isChecked();
	c.modWin = chkWin->isChecked();
	c.modLeftWin = chkLeftWin->isChecked();
	c.modRightCtrl = chkRightCtrl->isChecked();
	c.modRightAlt = chkRightAlt->isChecked();
	c.modRightShift = chkRightShift->isChecked();
	c.modRightWin = chkRightWin->isChecked();

	c.hotkeySequence.clear();

	c.zoomFactor = spZoom->value();
	c.animInMs = spIn->value();
	c.animOutMs = spOut->value();
	c.easing = ZoominatorController::zoomEasingFromString(cmbEasing->currentData().toString());
	c.wheelZoomEnabled = chkWheelZoom->isChecked();
	c.wheelZoomModifier = cmbWheelMod->currentData().toString();
	c.wheelZoomInStep = spWheelStep->value();
	c.wheelZoomOutStep = spWheelStep->value();
	c.wheelZoomMaximum = spWheelMax->value();
	c.wheelZoomExit = spWheelExit->value();
	c.focusEnabled = chkFocus->isChecked();
	c.showFocusWhenNotZoomed = chkFocusWhenNotZoomed->isChecked();
	c.focusDimPct = spFocusDim->value();
	c.focusBlurPx = spFocusBlur->value();
	c.focusStyle = ZoominatorController::focusStyleFromString(cmbFocusStyle->currentData().toString());
	c.focusIdleRadius = spFocusIdleRadius->value();
	c.focusCone = spFocusCone->value();
	c.focusRadius = spFocusRadius->value();
	c.focusSoftness = ZoominatorController::beamSoftnessForCone(c.focusCone, c.focusRadius);
	c.autoOnClick = chkAutoClick->isChecked();
	c.autoOnTyping = chkAutoTyping->isChecked();
	c.caretFollow = chkCaretFollow->isChecked();
	c.autoZoomOutEnabled = chkAutoZoomOut->isChecked();
	c.autoHoldMs = spAutoHold->value();
	c.zoomAnchor = ZoominatorController::zoomAnchorModeFromString(cmbZoomAnchor->currentData().toString());
	c.followSpeed = spFollowSpeed->value();
	/* Fixed: no longer offered in the dialog. */
	c.centerCursorUntilEdge = true;
	c.edgeOverflowMarginPct = 0;
	c.centerFocus = chkCenterFocus->isChecked();
	c.mouseIdleTimeoutMs = 0;
	c.portraitCover = chkPortraitCover->isChecked();
	c.idleFollowX = chkIdleFollowX->isChecked();
	c.tilt3dEnabled = chkTilt3d->isChecked();
	c.tilt3dMaxX = spTiltX->value();
	c.tilt3dMaxY = spTiltY->value();
	c.tilt3dPerspective = spTiltPersp->value();
	c.tilt3dInvert = chkTiltInvert->isChecked();
	c.tilt3dInZoom = chkTiltInZoom->isChecked();
	c.showCursorMarker = chkShowCursorMarker->isChecked();
	c.showMarkerWhenNotZoomed = chkShowMarkerWhenNotZoomed->isChecked();
	c.markerOnlyOnClick = true;
	c.markerSize = spMarkerSize->value();
	c.markerIdleSize = spMarkerIdleSize->value();
	c.markerOffsetX = spMarkerOffX->value();
	c.markerOffsetY = spMarkerOffY->value();
	c.markerThickness = spMarkerThickness->value();
	c.frameEnabled = chkFrame->isChecked();
	c.frameColor = (uint32_t)btnFrameColor->property("rgba").toUInt();
	c.frameBgColor = (uint32_t)btnFrameBg->property("rgba").toUInt();
	c.frameBgColor2 = (uint32_t)btnFrameBg2->property("rgba").toUInt();
	c.frameBgMode = cmbFrameBgMode->currentData().toInt();
	c.frameBgAngle = spFrameBgAngle->value();
	c.frameBorderPx = spFrameBorder->value();
	c.frameRadiusPx = spFrameRadius->value();
	c.frameGlowPx = spFrameGlow->value();
	c.frameGlowPct = spFrameGlowPct->value();
	c.frameBgRainbow = chkFrameBgRainbow->isChecked();
	c.frameBorderRainbow = chkFrameBorderRainbow->isChecked();
	c.frameRainbowSec = spFrameRainbowSec->value();
	c.camRingEnabled = chkCamRing->isChecked();
	c.camSourceName = cmbCamSource->currentData().toString();
	c.camRingColor = (uint32_t)btnCamColor->property("rgba").toUInt();
	c.camRingPx = spCamRing->value();
	c.camGlowPx = spCamGlow->value();
	c.camGlowPct = spCamGlowPct->value();
	c.camLayoutEnabled = chkCamLayout->isChecked();
	c.camRainbow = chkCamRainbow->isChecked();
	c.camRainbowSec = spCamRainbowSec->value();
	c.camIdleX = spCamIdleX->value();
	c.camIdleY = spCamIdleY->value();
	c.camIdleSize = spCamIdleSize->value();
	c.camZoomX = spCamZoomX->value();
	c.camZoomY = spCamZoomY->value();
	c.camZoomSize = spCamZoomSize->value();
	if (btnMarkerColor)
		c.markerColor = (uint32_t)btnMarkerColor->property("markerRgba").toUInt();
	c.debug = chkDebug->isChecked();
	c.countdownSec = spCountdown->value();
	c.countdownOnDefaultRecord = chkCountdownDefault->isChecked();
	c.cursorRingEnabled = chkCursorRing->isChecked();
	c.cursorRingIdle = chkCursorRingIdle->isChecked();
	c.cursorRingSize = spCursorRingSize->value();
	c.cursorRingPx = spCursorRingPx->value();
	c.cursorRingColor = (uint32_t)btnCursorRingColor->property("rgba").toUInt();
	c.cursorRingRainbow = chkCursorRingRainbow->isChecked();
	c.cursorRingSec = spCursorRingSec->value();
	c.keyDisplayEnabled = chkKeyDisplay->isChecked();
	c.keyDisplayAll = chkKeyDisplayAll->isChecked();
	c.keyDisplaySize = spKeyDisplaySize->value();
	c.keyDisplayYPct = spKeyDisplayY->value();
	c.guideEnabled = chkGuide->isChecked();
	c.guideTopPx = spGuideTop->value();
	c.guideBottomPx = spGuideBottom->value();
	c.guideLeftPx = spGuideLeft->value();
	c.guideRightPx = spGuideRight->value();
	c.showStatusHud = chkStatusHud->isChecked();
	c.statusHudCorner = cmbStatusHudCorner->currentData().toInt();
	c.recLimitSec = spRecLimit->value();
	c.soundEnabled = chkSoundEnabled->isChecked();
	for (int i = 0; i < 3; ++i) {
		c.soundVolume[i] = spSoundVolume[i]->value();
		c.soundOn[i] = chkSoundOn[i]->isChecked();
	}
	c.soundTypingMode = cmbSoundTypingMode->currentData().toString();

	c.includedSources.clear();
	if (lstSources) {
		for (int i = 0; i < lstSources->count(); i++) {
			const QListWidgetItem *it = lstSources->item(i);
			if (it && it->checkState() == Qt::Checked)
				c.includedSources.insert(it->data(Qt::UserRole).toString());
		}
	}

	c.saveSettings();
	c.rebuildRuntimeHooks();
	lblStatus->setText(T("Dialog.SettingsApplied"));
}

void ZoominatorDialog::updateTriggerRows()
{
	const QString type = cmbTrigger->currentData().toString();
	const bool isMouse = (type == "mouse");
	const bool isAuto = (type == "auto");
	/* The single combined Hotkey is gone: Keyboard and Auto use the separate
	 * Zoom In / Zoom Out keys. Hold/Toggle only matters for the mouse button. */
	rowHotkeyWidget->setVisible(false);
	rowBehaviorWidget->setVisible(isMouse);
	rowMouseWidget->setVisible(isMouse);
	rowModifiersWidget->setVisible(isMouse);
	rowAutoWidget->setVisible(isAuto);
}

/* Presets: "vertical" is for a 1080x1920 canvas, "horizontal" for 1920x1080. Save keeps every
 * setting plus (when the OBS canvas has that size) where the screen sits in it. Restore brings
 * the settings back and, when the canvas matches, the screen layout (or the automatic fit). */
/* Rebuilds the preset rows of both sections from the saved list. */
void ZoominatorDialog::rebuildPresetRows()
{
	auto &ctl = ZoominatorController::instance();
	const QVector<ZoominatorController::PresetInfo> list = ctl.listPresets();
	for (int i = 0; i < 2; ++i) {
		QVBoxLayout *vl = presetListLay[i];
		if (!vl)
			continue;
		while (QLayoutItem *it = vl->takeAt(0)) {
			if (QLayout *cl = it->layout()) {
				while (QLayoutItem *ci = cl->takeAt(0)) {
					delete ci->widget();
					delete ci;
				}
			}
			delete it->widget();
			delete it;
		}
		const bool horizontal = (i == 1);
		QWidget *host = vl->parentWidget();
		for (const auto &pi : list) {
			if (pi.horizontal != horizontal)
				continue;
			const QString id = pi.id;
			auto *edit = new QLineEdit(pi.name, host);
			edit->setPlaceholderText(T(horizontal ? "Dialog.Presets.Horizontal" : "Dialog.Presets.Vertical"));
			edit->setToolTip(T("Dialog.Presets.NameTooltip"));
			edit->setMinimumWidth(180);
			const bool isMain = (id == QLatin1String("v1") || id == QLatin1String("h1"));
			if (isMain) {
				/* The main preset keeps its fixed name and cannot be deleted. */
				edit->setText(T(horizontal ? "Dialog.Presets.Horizontal" : "Dialog.Presets.Vertical"));
				edit->setEnabled(false); /* greyed out: no editing, no text selection */
			}
			auto *bRestore = new QPushButton(T("Dialog.Presets.Restore"), host);
			bRestore->setToolTip(T("Dialog.Presets.RestoreTooltip"));
			auto *bSave = new QPushButton(T("Dialog.Presets.Save"), host);
			bSave->setToolTip(T("Dialog.Presets.SaveTooltip"));
			auto *bFit = new QPushButton(T("Dialog.Presets.Fit"), host);
			bFit->setToolTip(T("Dialog.Presets.FitTooltip"));
			auto *bDel = new QPushButton(T("Dialog.Presets.Delete"), host);
			bDel->setToolTip(T(isMain ? "Dialog.Presets.DeleteMainTooltip" : "Dialog.Presets.DeleteTooltip"));
			bDel->setEnabled(!isMain);
			auto *row = new QHBoxLayout;
			row->setSpacing(8);
			row->addWidget(edit, 1);
			row->addWidget(bRestore);
			row->addWidget(bSave);
			row->addWidget(bFit);
			row->addWidget(bDel);
			vl->addLayout(row);

			if (!isMain)
				connect(edit, &QLineEdit::editingFinished, this,
					[id, edit]() { ZoominatorController::instance().renamePreset(id, edit->text()); });
			connect(bRestore, &QPushButton::clicked, this, [this, id, horizontal]() { applyPreset(id, horizontal); });
			connect(bSave, &QPushButton::clicked, this, [this, id, horizontal, edit, isMain]() {
				if (!isMain)
					ZoominatorController::instance().renamePreset(id, edit->text());
				savePreset(id, horizontal);
			});
			connect(bFit, &QPushButton::clicked, this, [this, horizontal]() {
				auto &c = ZoominatorController::instance();
				const int w = horizontal ? 1920 : 1080;
				const int h = horizontal ? 1080 : 1920;
				if (!c.canvasIs(w, h)) {
					lblStatus->setText(QString(T("Dialog.Presets.WrongCanvas")).arg(w).arg(h));
					return;
				}
				c.requestFitScreen();
				chkIdleFollowX->setChecked(false);
				lblStatus->setText(T("Dialog.FitScreenDone"));
			});
			connect(bDel, &QPushButton::clicked, this, [this, id, edit, horizontal]() {
				QString nm = edit->text().trimmed();
				if (nm.isEmpty())
					nm = T(horizontal ? "Dialog.Presets.Horizontal" : "Dialog.Presets.Vertical");
				if (QMessageBox::question(this, T("Dialog.Presets.Delete"),
							  QString(T("Dialog.Presets.DeleteConfirm")).arg(nm)) != QMessageBox::Yes)
					return;
				ZoominatorController::instance().removePreset(id);
				rebuildPresetRows();
				lblStatus->setText(QString(T("Dialog.Presets.Deleted")).arg(nm));
			});
		}
	}
}

void ZoominatorDialog::savePreset(const QString &id, bool horizontal)
{
	applyToController();
	auto &c = ZoominatorController::instance();
	const int w = horizontal ? 1920 : 1080;
	const int h = horizontal ? 1080 : 1920;
	QString name = T(horizontal ? "Dialog.Presets.Horizontal" : "Dialog.Presets.Vertical");
	for (const auto &pi : c.listPresets()) {
		if (pi.id == id && !pi.name.isEmpty())
			name = pi.name;
	}
	if (!c.saveUserPreset(id)) {
		lblStatus->setText(T("Dialog.PresetSaveFailed"));
		return;
	}
	QString msg = QString(T("Dialog.Presets.Saved")).arg(name);
	if (c.canvasIs(w, h)) {
		if (c.saveCurrentLayout(w, h) > 0)
			msg += QStringLiteral(" ") + QString(T("Dialog.Presets.SavedLayout"));
	} else {
		msg += QStringLiteral(" ") + QString(T("Dialog.Presets.LayoutSkipped")).arg(w).arg(h);
	}
	lblStatus->setText(msg);
}

void ZoominatorDialog::applyPreset(const QString &id, bool horizontal)
{
	auto &ctl = ZoominatorController::instance();
	const int w = horizontal ? 1920 : 1080;
	const int h = horizontal ? 1080 : 1920;
	const bool canvasOk = ctl.canvasIs(w, h);
	QString note;
	if (canvasOk) {
		ctl.requestFitScreen(); /* saved layout for this canvas, or the automatic fit */
	} else {
		note = QStringLiteral(" ") + QString(T("Dialog.Presets.LayoutSkipped")).arg(w).arg(h);
	}

	if (ctl.hasUserPreset(id) && ctl.applyUserPreset(id)) {
		loadFromController();
		lblStatus->setText(QString(T("Dialog.Presets.Restored")) + note);
		return;
	}

	const int autoIdx = cmbTrigger->findData("auto");
	if (autoIdx >= 0)
		cmbTrigger->setCurrentIndex(autoIdx);
	chkAutoClick->setChecked(true);
	chkAutoTyping->setChecked(true);
	chkCaretFollow->setChecked(true);
	chkAutoZoomOut->setChecked(true);
	spAutoHold->setValue(2500);

	chkIdleFollowX->setChecked(false);
	spIn->setValue(260);
	spOut->setValue(420);
	cmbEasing->setCurrentIndex(std::max(0, cmbEasing->findData("punch")));
	cmbZoomAnchor->setCurrentIndex(std::max(0, cmbZoomAnchor->findData("cursor_follow")));
	spFollowSpeed->setValue(6.0);
	chkShowCursorMarker->setChecked(true);
	if (horizontal) {
		spZoom->setValue(2.0);
		chkPortraitCover->setChecked(false);
		chkCenterFocus->setChecked(false); /* stay inside the screen at its edges */
	} else {
		spZoom->setValue(1.0); /* cover mode already fills the canvas; >1 pushes in further */
		chkPortraitCover->setChecked(true);
	}

	applyToController();
	lblStatus->setText(QString(T(horizontal ? "Dialog.Presets.AppliedHorizontal" : "Dialog.Presets.AppliedVertical")) + note);
}

static const char *kSoundBase[3] = {"zoom_in", "zoom_out", "typing"};

static void removeSoundFiles(const QString &dir, int slot)
{
	QDir d(dir);
	const QStringList old = d.entryList(QStringList() << (QString::fromLatin1(kSoundBase[slot]) + ".*"), QDir::Files);
	for (const QString &f : old)
		QFile::remove(d.filePath(f));
}

/* Picks a sound file, copies it into the plugin's config folder (replacing the
 * previous one) and uses the copy, so deleting the original is harmless. */
void ZoominatorDialog::browseSound(int slot)
{
	auto &c = ZoominatorController::instance();
	/* Let go of the current file first; Windows will not replace a file in use. */
	c.releaseSoundSources();

	const QString start = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
	const QString picked = QFileDialog::getOpenFileName(
		this, T("Dialog.SoundBrowseTitle"), start,
		T("Dialog.SoundFilter") + QStringLiteral(" (*.wav *.mp3 *.ogg *.flac *.m4a *.aac);;*.*"));
	if (picked.isEmpty()) {
		c.ensureSoundSources();
		return;
	}

	const QString dir = c.soundDir();
	if (dir.isEmpty()) {
		lblStatus->setText(T("Dialog.SoundCopyFailed"));
		c.ensureSoundSources();
		return;
	}
	removeSoundFiles(dir, slot);
	const QString dest = QDir(dir).filePath(QString::fromLatin1(kSoundBase[slot]) + QStringLiteral(".") +
						QFileInfo(picked).suffix().toLower());
	if (!QFile::copy(picked, dest)) {
		c.soundFile[slot].clear();
		c.soundName[slot].clear();
		editSoundName[slot]->setText(T("Dialog.SoundBuiltIn"));
		lblStatus->setText(T("Dialog.SoundCopyFailed"));
	} else {
		c.soundFile[slot] = dest;
		c.soundName[slot] = QFileInfo(picked).fileName();
		editSoundName[slot]->setText(c.soundName[slot]);
		lblStatus->setText(T("Dialog.SoundCopied"));
	}
	c.saveSettings();
	c.ensureSoundSources();
}

void ZoominatorDialog::resetSound(int slot)
{
	auto &c = ZoominatorController::instance();
	c.releaseSoundSources();
	const QString dir = c.soundDir();
	if (!dir.isEmpty())
		removeSoundFiles(dir, slot);
	c.soundFile[slot].clear();
	c.soundName[slot].clear();
	editSoundName[slot]->setText(T("Dialog.SoundBuiltIn"));
	c.saveSettings();
	c.ensureSoundSources();
}

void ZoominatorDialog::playSoundPreview(int slot)
{
	auto &c = ZoominatorController::instance();
	c.soundVolume[slot] = spSoundVolume[slot]->value();
	c.soundTypingMode = cmbSoundTypingMode->currentData().toString();
	c.previewSound(slot);
}

void ZoominatorDialog::testZoom()
{
	applyToController();
	lblStatus->setText(T("Dialog.TestStatus"));
}

void ZoominatorDialog::clearHotkey()
{
	editHotkey->setKeySequence(QKeySequence());
}

void ZoominatorDialog::clearFollowToggleHotkey()
{
	editFollowToggleHotkey->setKeySequence(QKeySequence());
}

void ZoominatorDialog::updateMarkerColorButton(const QColor &color)
{
	if (!btnMarkerColor)
		return;
	const QColor c = color.isValid() ? color : QColor(255, 0, 0);
	btnMarkerColor->setProperty("markerRgba", c.rgba());
	btnMarkerColor->setText(QStringLiteral("%1 / %2 / %3").arg(c.red()).arg(c.green()).arg(c.blue()));
	btnMarkerColor->setStyleSheet(QStringLiteral("QPushButton { background:%1; color:%2;"
						     " border:1px solid #6e6e78; padding:4px 8px; }"
						     "QPushButton:disabled { background:#3a3a40; color:#9a9aa2;"
						     " border:1px solid #6e6e78; padding:4px 8px; }")
					      .arg(c.name(QColor::HexArgb))
					      .arg(c.lightness() < 128 ? "#ffffff" : "#000000"));
}

void ZoominatorDialog::setColorButton(QPushButton *button, uint32_t rgba)
{
	if (!button)
		return;
	const QColor c = QColor::fromRgba(rgba);
	button->setProperty("rgba", c.rgba());
	button->setText(c.name(QColor::HexRgb).toUpper());
	button->setStyleSheet(QStringLiteral("QPushButton { background:%1; color:%2;"
					     " border:1px solid #6e6e78; padding:4px 8px; }"
					     "QPushButton:disabled { background:#3a3a40; color:#9a9aa2;"
					     " border:1px solid #6e6e78; padding:4px 8px; }")
				      .arg(c.name(QColor::HexRgb))
				      .arg(c.lightness() < 128 ? "#ffffff" : "#000000"));
}

void ZoominatorDialog::chooseFrameColor(QPushButton *button)
{
	if (!button)
		return;
	const QColor picked = QColorDialog::getColor(QColor::fromRgba(button->property("rgba").toUInt()), this,
						     T("Dialog.PickFrameColor"));
	if (picked.isValid()) {
		QColor opaque = picked;
		opaque.setAlpha(255);
		setColorButton(button, opaque.rgba());
	}
}

void ZoominatorDialog::chooseMarkerColor()
{
	const uint rgba = btnMarkerColor ? btnMarkerColor->property("markerRgba").toUInt() : QColor(255, 0, 0).rgba();
	const QColor picked = QColorDialog::getColor(QColor::fromRgba(rgba), this, T("Dialog.PickCursorHaloColor"),
						     QColorDialog::ShowAlphaChannel);
	if (picked.isValid())
		updateMarkerColorButton(picked);
}
