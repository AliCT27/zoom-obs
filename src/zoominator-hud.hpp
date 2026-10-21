#pragma once

/* A small always-on-top status badge on the user's own monitor (zoomed or not, how much, which modes are on).
 * It is click-through, never takes focus, and on Windows is excluded from screen capture so it does not show
 * up in the recording. If it cannot be excluded from capture it is not shown at all. */

#include <QString>
#include <QWidget>

class ZoominatorHud final : public QWidget {
public:
	struct State {
		bool zoomed = false;
		int direction = 0; /* 1 zooming in, -1 zooming out, 0 steady */
		double level = 1.0;
		QString modes; /* e.g. "Auto  3D  Focus" */
	};

	explicit ZoominatorHud(QWidget *parent = nullptr);

	/* corner: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right. Returns false when the badge cannot be
	 * kept out of the recording (then it stays hidden). */
	bool showInCorner(int corner);
	void hideBadge();
	void setState(const State &state);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	void place();
	bool excludeFromCapture();

	State st;
	int cornerIdx = 0;
	bool excluded = false;
};
