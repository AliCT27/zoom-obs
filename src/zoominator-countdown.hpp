#pragma once

/* The big 3-2-1 number shown on the user's monitor before a recording starts. It is click-through and never takes
 * focus. It is hidden before the recording starts, so it never needs to be kept out of the capture. */

#include <QWidget>

class ZoominatorCountdown final : public QWidget {
public:
	explicit ZoominatorCountdown(QWidget *parent = nullptr);

	/* Shows the number, centred on the monitor the mouse is on. */
	void showNumber(int number);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	int shown = 0;
};
