#pragma once

/* A small always-on-top status badge on the user's own monitor (zoomed or not, how much, Focus on or off, recording state).
 * It is click-through, never takes focus, and on Windows is excluded from screen capture so it does not show
 * up in the recording. If it cannot be excluded from capture it is not shown at all. */

#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

class ZoominatorHud final : public QWidget {
public:
	struct State {
		bool zoomed = false;
		int direction = 0; /* 1 zooming in, -1 zooming out, 0 steady */
		double level = 1.0;
		QList<QPair<QString, bool>> flags; /* feature name, on or off (green or red light) */
		int recording = 0;                 /* 0 not recording, 1 recording, 2 recording paused */
		int recElapsedSec = 0;             /* recording time so far, pauses not counted */
		int recLimitSec = 0;               /* time limit to warn about, 0 = none */
	};

	explicit ZoominatorHud(QWidget *parent = nullptr);

	/* corner: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right. Returns false when the badge cannot be
	 * kept out of the recording (then it stays hidden). */
	bool showInCorner(int corner);
	void hideBadge();
	int corner() const { return cornerIdx; }
	/* Re-checks (and if needed re-applies) the capture exclusion; hides the badge when it cannot be guaranteed. */
	bool ensureExcluded();
	void setState(const State &state);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	void place();
	bool excludeFromCapture();

	State st;
	int cornerIdx = 0;
	quintptr excludedWinId = 0; /* the native window the exclusion was verified on */
};
