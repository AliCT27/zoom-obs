/* Status badge on the user's monitor. See zoominator-hud.hpp. */

#include "zoominator-hud.hpp"

#include <util/base.h>


#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

ZoominatorHud::ZoominatorHud(QWidget *parent) : QWidget(parent)
{
	setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus |
		       Qt::WindowTransparentForInput);
	setAttribute(Qt::WA_TranslucentBackground, true);
	setAttribute(Qt::WA_ShowWithoutActivating, true);
	setAttribute(Qt::WA_TransparentForMouseEvents, true);
	setFocusPolicy(Qt::NoFocus);
}

bool ZoominatorHud::excludeFromCapture()
{
#ifdef _WIN32
	const HWND hwnd = reinterpret_cast<HWND>(winId());
	if (!hwnd)
		return false;
	/* Qt can recreate the native window, which drops the setting, so it is checked against the current one. */
	if (excludedWinId == (quintptr)hwnd) {
		DWORD now = 0;
		if (GetWindowDisplayAffinity(hwnd, &now) && now == WDA_EXCLUDEFROMCAPTURE)
			return true;
	}
	excludedWinId = 0;
	DWORD affinity = 0;
	const bool set = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE) != 0;
	const bool verified = set && GetWindowDisplayAffinity(hwnd, &affinity) && affinity == WDA_EXCLUDEFROMCAPTURE;
	if (verified) {
		excludedWinId = (quintptr)hwnd;
		static bool told = false;
		if (!told) {
			told = true;
			blog(LOG_INFO, "[Zoominator] Status badge hidden from capture (display affinity verified).");
		}
		return true;
	}
	static bool warned = false;
	if (!warned) {
		warned = true;
		blog(LOG_WARNING, "[Zoominator] Status badge cannot be hidden from capture on this system (error %lu); not shown.",
		     set ? 0ul : (unsigned long)GetLastError());
	}
	return false;
#else
	return false; /* no way to keep it out of the recording here */
#endif
}

bool ZoominatorHud::ensureExcluded()
{
	if (!excludeFromCapture()) {
		hide();
		return false;
	}
	return true;
}

void ZoominatorHud::place()
{
	QScreen *scr = QGuiApplication::primaryScreen();
	if (!scr)
		return;
	const QRect r = scr->availableGeometry();
	const int margin = 16;
	const int x = (cornerIdx == 1 || cornerIdx == 3) ? r.right() - width() - margin + 1 : r.left() + margin;
	const int y = (cornerIdx == 2 || cornerIdx == 3) ? r.bottom() - height() - margin + 1 : r.top() + margin;
	move(x, y);
}

bool ZoominatorHud::showInCorner(int corner)
{
	cornerIdx = corner < 0 || corner > 3 ? 0 : corner;
	/* The native window must exist before its capture affinity can be set. Hidden until it is excluded. */
	winId();
	if (!excludeFromCapture()) {
		hide();
		return false;
	}
	resize(sizeHint());
	place();
	if (!isVisible())
		show();
	/* Check again on the shown window: if showing created a new native window, the exclusion is redone. */
	return ensureExcluded();
}

void ZoominatorHud::hideBadge()
{
	hide();
}

void ZoominatorHud::setState(const State &state)
{
	if (state.zoomed == st.zoomed && state.direction == st.direction && state.modes == st.modes && state.flags == st.flags && state.recording == st.recording &&
	    qAbs(state.level - st.level) < 0.005)
		return;
	st = state;
	resize(sizeHint());
	place();
	update();
}

QSize ZoominatorHud::sizeHint() const
{
	QFont fontBig = font();
	fontBig.setBold(true);
	fontBig.setPointSizeF(12.0);
	QFont fontSmall = font();
	fontSmall.setPointSizeF(9.0);
	const QFontMetrics fb(fontBig), fs(fontSmall);
	const int pad = 12;
	const int line1 = 20 + fb.horizontalAdvance(QStringLiteral("ZOOMING OUT")) + 24 + fb.horizontalAdvance(QStringLiteral("8.00x"));
	int line2 = fs.horizontalAdvance(st.modes);
	for (const auto &f : st.flags)
		line2 += 14 + 12 + fs.horizontalAdvance(f.first);
	const int line3 = 12 + 8 + fs.horizontalAdvance(QStringLiteral("NOT RECORDING"));
	int h = pad * 2 + fb.height();
	if (!st.modes.isEmpty() || !st.flags.isEmpty())
		h += 4 + fs.height();
	h += 4 + fs.height();
	return QSize(qMax(qMax(line1, line2), line3) + pad * 2, h);
}

void ZoominatorHud::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(15, 15, 18, 215));
	p.drawRoundedRect(rect(), 10, 10);

	QColor dot(130, 130, 140);
	QString word = QStringLiteral("NOT ZOOMED");
	if (st.direction > 0) {
		dot = QColor(255, 190, 40);
		word = QStringLiteral("ZOOMING IN");
	} else if (st.direction < 0) {
		dot = QColor(255, 190, 40);
		word = QStringLiteral("ZOOMING OUT");
	} else if (st.zoomed) {
		dot = QColor(60, 220, 110);
		word = QStringLiteral("ZOOMED");
	}

	QFont fontBig = font();
	fontBig.setBold(true);
	fontBig.setPointSizeF(12.0);
	QFont fontSmall = font();
	fontSmall.setPointSizeF(9.0);

	const int pad = 12;
	p.setBrush(dot);
	p.drawEllipse(QPoint(pad + 6, pad + QFontMetrics(fontBig).height() / 2), 6, 6);

	p.setPen(QColor(240, 240, 245));
	p.setFont(fontBig);
	p.drawText(QPoint(pad + 20, pad + QFontMetrics(fontBig).ascent()), word);
	const QString lvl = QStringLiteral("%1x").arg(st.level, 0, 'f', 2);
	p.drawText(QPoint(width() - pad - QFontMetrics(fontBig).horizontalAdvance(lvl), pad + QFontMetrics(fontBig).ascent()),
		   lvl);

	if (!st.modes.isEmpty() || !st.flags.isEmpty()) {
		const QFontMetrics fs(fontSmall);
		const int base = pad + QFontMetrics(fontBig).height() + 4 + fs.ascent();
		int x = pad;
		p.setFont(fontSmall);
		p.setPen(QColor(170, 170, 180));
		p.drawText(QPoint(x, base), st.modes);
		x += fs.horizontalAdvance(st.modes);
		for (const auto &f : st.flags) {
			x += 14;
			p.setPen(Qt::NoPen);
			p.setBrush(f.second ? QColor(60, 220, 110) : QColor(230, 70, 70));
			p.drawEllipse(QPoint(x + 4, base - fs.ascent() / 2 + 1), 4, 4);
			x += 12;
			p.setPen(QColor(210, 210, 218));
			p.drawText(QPoint(x, base), f.first);
			x += fs.horizontalAdvance(f.first);
		}
	}
	{
		const QFontMetrics fs(fontSmall);
		const bool hasRow2 = !st.modes.isEmpty() || !st.flags.isEmpty();
		const int row2 = hasRow2 ? 4 + fs.height() : 0;
		const int base = pad + QFontMetrics(fontBig).height() + row2 + 4 + fs.ascent();
		QColor c(120, 120, 130);
		QString label = QStringLiteral("NOT RECORDING");
		if (st.recording == 1) {
			c = QColor(235, 60, 60);
			label = QStringLiteral("RECORDING");
		} else if (st.recording == 2) {
			c = QColor(255, 190, 40);
			label = QStringLiteral("PAUSED");
		}
		p.setFont(fontSmall);
		p.setPen(Qt::NoPen);
		p.setBrush(c);
		p.drawEllipse(QPoint(pad + 4, base - fs.ascent() / 2 + 1), 4, 4);
		p.setPen(QColor(210, 210, 218));
		p.drawText(QPoint(pad + 16, base), label);
	}
}
