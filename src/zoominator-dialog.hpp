#pragma once

#include <functional>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QHBoxLayout;
class QKeySequenceEdit;
class QLineEdit;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QVBoxLayout;
struct calldata;

class ZoominatorDialog final : public QDialog {
	Q_OBJECT

public:
	explicit ZoominatorDialog(QWidget *parent = nullptr);

protected:
	void closeEvent(QCloseEvent *event) override;

private slots:
	void refreshLists();
	void applyToController();
	void browseSound(int slot);
	void resetSound(int slot);
	void playSoundPreview(int slot);
	void testZoom();
	void clearHotkey();
	void clearFollowToggleHotkey();
	void chooseMarkerColor();
	void chooseFrameColor(QPushButton *button);
	void setColorButton(QPushButton *button, uint32_t rgba);
	void populateSourcesTab();
	void applyPreset(const QString &id, bool horizontal);
	void savePreset(const QString &id, bool horizontal);
	void rebuildPresetRows();

private:
	void buildUi();
	void loadFromController();
	void populateSources();
	void updateMarkerColorButton(const QColor &color);
	void updateTriggerRows();

	static void obsSourceChanged(void *data, struct calldata *cd);

	QTabWidget *tabWidget = nullptr;

	QComboBox *cmbSource = nullptr;
	QComboBox *cmbMode = nullptr;
	QKeySequenceEdit *editFollowToggleHotkey = nullptr;
	QPushButton *btnClearFollowToggleHotkey = nullptr;
	QCheckBox *chkAutoZoomOut = nullptr;
	QKeySequenceEdit *editManualZoomIn = nullptr;
	QKeySequenceEdit *editManualZoomOut = nullptr;
	QKeySequenceEdit *editFocusToggle = nullptr;
	QKeySequenceEdit *editFocusOn = nullptr;
	QKeySequenceEdit *editFocusOff = nullptr;

	QComboBox *cmbTrigger = nullptr;
	QKeySequenceEdit *editHotkey = nullptr;
	QPushButton *btnClearHotkey = nullptr;
	QComboBox *cmbMouseBtn = nullptr;
	QCheckBox *chkCtrl = nullptr;
	QCheckBox *chkAlt = nullptr;
	QCheckBox *chkShift = nullptr;
	QCheckBox *chkWin = nullptr;
	QWidget *rowAutoWidget = nullptr;
	QWidget *rowManualKeysWidget = nullptr;
	QCheckBox *chkAutoClick = nullptr;
	QCheckBox *chkAutoTyping = nullptr;
	QCheckBox *chkCaretFollow = nullptr;
	QSpinBox *spAutoHold = nullptr;
	QComboBox *cmbEasing = nullptr;
	QCheckBox *chkLeftCtrl = nullptr;
	QCheckBox *chkRightCtrl = nullptr;
	QCheckBox *chkLeftAlt = nullptr;
	QCheckBox *chkRightAlt = nullptr;
	QCheckBox *chkLeftShift = nullptr;
	QCheckBox *chkRightShift = nullptr;
	QCheckBox *chkLeftWin = nullptr;
	QCheckBox *chkRightWin = nullptr;

	QDoubleSpinBox *spZoom = nullptr;
	QSpinBox *spIn = nullptr;
	QSpinBox *spOut = nullptr;
	QComboBox *cmbZoomAnchor = nullptr;
	QDoubleSpinBox *spFollowSpeed = nullptr;
	QCheckBox *chkCenterFocus = nullptr;
	QCheckBox *chkPortraitCover = nullptr;
	QCheckBox *chkShowCursorMarker = nullptr;
	QCheckBox *chkShowMarkerWhenNotZoomed = nullptr;
	QSpinBox *spMarkerSize = nullptr;
	QSpinBox *spMarkerIdleSize = nullptr;
	QSpinBox *spMarkerOffX = nullptr;
	QSpinBox *spMarkerOffY = nullptr;
	QPushButton *btnMarkerOffReset = nullptr;
	QCheckBox *chkWheelZoom = nullptr;
	QCheckBox *chkIdleFollowX = nullptr;
	QCheckBox *chkTilt3d = nullptr;
	QCheckBox *chkTiltInvert = nullptr;
	QCheckBox *chkTiltInZoom = nullptr;
	QDoubleSpinBox *spTiltX = nullptr;
	QDoubleSpinBox *spTiltY = nullptr;
	QDoubleSpinBox *spTiltPersp = nullptr;
	QVBoxLayout *presetListLay[2] = {nullptr, nullptr};
	QComboBox *cmbWheelMod = nullptr;
	QDoubleSpinBox *spWheelStep = nullptr;
	QDoubleSpinBox *spWheelMax = nullptr;
	QDoubleSpinBox *spWheelExit = nullptr;
	QSpinBox *spMarkerThickness = nullptr;
	QPushButton *btnMarkerColor = nullptr;
	QCheckBox *chkFrame = nullptr;
	QPushButton *btnFrameColor = nullptr;
	QPushButton *btnFrameBg = nullptr;
	QPushButton *btnFrameBg2 = nullptr;
	QCheckBox *chkCamRing = nullptr;
	QComboBox *cmbCamSource = nullptr;
	QPushButton *btnCamColor = nullptr;
	QSpinBox *spCamRing = nullptr;
	QCheckBox *chkCamRainbow = nullptr;
	QSpinBox *spCamRainbowSec = nullptr;
	QSpinBox *spCamGlow = nullptr;
	QSpinBox *spCamGlowPct = nullptr;
	QCheckBox *chkCamLayout = nullptr;
	QSpinBox *spCamIdleX = nullptr;
	QSpinBox *spCamIdleY = nullptr;
	QSpinBox *spCamIdleSize = nullptr;
	QSpinBox *spCamZoomX = nullptr;
	QSpinBox *spCamZoomY = nullptr;
	QSpinBox *spCamZoomSize = nullptr;
	QPushButton *btnCamIdleTake = nullptr;
	QPushButton *btnCamZoomTake = nullptr;
	QComboBox *cmbFrameBgMode = nullptr;
	QSpinBox *spFrameBgAngle = nullptr;
	QSpinBox *spFrameBorder = nullptr;
	QSpinBox *spFrameRadius = nullptr;
	QSpinBox *spFrameGlow = nullptr;
	QSpinBox *spFrameGlowPct = nullptr;
	QCheckBox *chkFrameBgRainbow = nullptr;
	QCheckBox *chkFrameBorderRainbow = nullptr;
	QSpinBox *spFrameRainbowSec = nullptr;
	QCheckBox *chkFocus = nullptr;
	QCheckBox *chkFocusWhenNotZoomed = nullptr;
	QSpinBox *spFocusRadius = nullptr;
	QSpinBox *spFocusIdleRadius = nullptr;
	QComboBox *cmbFocusStyle = nullptr;
	std::function<void()> syncFocusFieldsFn;
	QSpinBox *spFocusBlur = nullptr;
	QSpinBox *spFocusDim = nullptr;
	QSpinBox *spFocusCone = nullptr;
	QCheckBox *chkDebug = nullptr;
	QSpinBox *spCountdown = nullptr;
	QCheckBox *chkCountdownDefault = nullptr;
	QCheckBox *chkCursorRing = nullptr;
	QCheckBox *chkCursorRingIdle = nullptr;
	QSpinBox *spCursorRingSize = nullptr;
	QSpinBox *spCursorRingPx = nullptr;
	QPushButton *btnCursorRingColor = nullptr;
	QCheckBox *chkCursorRingRainbow = nullptr;
	QSpinBox *spCursorRingSec = nullptr;
	QCheckBox *chkKeyDisplay = nullptr;
	QCheckBox *chkKeyDisplayAll = nullptr;
	QSpinBox *spKeyDisplaySize = nullptr;
	QSpinBox *spKeyDisplayY = nullptr;
	QDoubleSpinBox *spKeyDisplaySec = nullptr;
	QPushButton *btnKeyDisplayReset = nullptr;
	QPushButton *btnKeyIgnoreAdd = nullptr;
	QHBoxLayout *keyIgnoreChips = nullptr;
	QList<int> keyIgnoreQt;
	void rebuildKeyIgnoreChips();
	QCheckBox *chkGuide = nullptr;
	QSpinBox *spGuideTop = nullptr;
	QSpinBox *spGuideBottom = nullptr;
	QSpinBox *spGuideLeft = nullptr;
	QSpinBox *spGuideRight = nullptr;
	QCheckBox *chkStatusHud = nullptr;
	QComboBox *cmbStatusHudCorner = nullptr;
	QSpinBox *spRecLimit = nullptr;
	QCheckBox *chkSoundEnabled = nullptr;
	QCheckBox *chkSoundOn[3] = {nullptr, nullptr, nullptr};
	QLineEdit *editSoundName[3] = {nullptr, nullptr, nullptr};
	QSpinBox *spSoundVolume[3] = {nullptr, nullptr, nullptr};
	QComboBox *cmbSoundTypingMode = nullptr;

	QListWidget *lstSources = nullptr;

	QLabel *lblStatus = nullptr;
	QPushButton *btnRefresh = nullptr;
	QPushButton *btnApply = nullptr;
	QPushButton *btnTest = nullptr;

	QWidget *rowHotkeyWidget = nullptr;
	QWidget *rowBehaviorWidget = nullptr;
	QWidget *rowMouseWidget = nullptr;
	QWidget *rowModifiersWidget = nullptr;

	bool loading = false;
};
