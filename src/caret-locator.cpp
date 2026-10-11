#include "caret-locator.hpp"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>

#include <QCursor>
#include <QGuiApplication>
#include <QPoint>
#include <QScreen>

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

#ifdef _MSC_VER
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "user32.lib")
#endif

namespace zoominator_caret {

namespace {

using Clock = std::chrono::steady_clock;

int64_t nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

std::atomic<bool> g_running{false};
std::thread g_thread;
std::atomic<int64_t> g_lastTypingMs{0};
std::atomic<int64_t> g_foundMs{0};
std::atomic<LONG> g_x{0};
std::atomic<LONG> g_y{0};

template<class T> void releaseCom(T *&p)
{
	if (p) {
		p->Release();
		p = nullptr;
	}
}

/* The classic system caret of the focused thread. */
bool caretFromGuiThread(POINT &out)
{
	HWND fg = GetForegroundWindow();
	if (!fg)
		return false;
	const DWORD tid = GetWindowThreadProcessId(fg, nullptr);
	GUITHREADINFO gti = {};
	gti.cbSize = sizeof(gti);
	if (!GetGUIThreadInfo(tid, &gti) || !gti.hwndCaret)
		return false;
	const RECT &r = gti.rcCaret;
	if (r.bottom - r.top <= 0)
		return false;
	POINT p = {r.left, (r.top + r.bottom) / 2};
	if (!ClientToScreen(gti.hwndCaret, &p))
		return false;
	out = p;
	return true;
}

/* Reads the last rectangle of a UI Automation rectangle array (left, top,
 * width, height as doubles). */
bool lastRect(SAFEARRAY *rects, POINT &out)
{
	if (!rects)
		return false;
	bool ok = false;
	LONG lb = 0, ub = -1;
	if (SUCCEEDED(SafeArrayGetLBound(rects, 1, &lb)) && SUCCEEDED(SafeArrayGetUBound(rects, 1, &ub))) {
		const LONG count = (ub - lb + 1) / 4;
		double *data = nullptr;
		if (count > 0 && SUCCEEDED(SafeArrayAccessData(rects, reinterpret_cast<void **>(&data))) && data) {
			for (LONG i = count - 1; i >= 0; --i) {
				const double l = data[i * 4 + 0];
				const double t = data[i * 4 + 1];
				const double w = data[i * 4 + 2];
				const double h = data[i * 4 + 3];
				if (w <= 0.0 && h <= 0.0)
					continue;
				out.x = (LONG)std::lround(l);
				out.y = (LONG)std::lround(t + h * 0.5);
				ok = true;
				break;
			}
			SafeArrayUnaccessData(rects);
		}
	}
	return ok;
}

/* The caret (or selection end) of the focused element through its text pattern. */
bool caretFromUia(IUIAutomation *ua, POINT &out)
{
	IUIAutomationElement *el = nullptr;
	if (FAILED(ua->GetFocusedElement(&el)) || !el)
		return false;

	bool ok = false;
	IUnknown *unk = nullptr;
	if (SUCCEEDED(el->GetCurrentPattern(UIA_TextPatternId, &unk)) && unk) {
		IUIAutomationTextPattern *tp = nullptr;
		if (SUCCEEDED(unk->QueryInterface(__uuidof(IUIAutomationTextPattern), reinterpret_cast<void **>(&tp))) &&
		    tp) {
			IUIAutomationTextRangeArray *sel = nullptr;
			int len = 0;
			if (SUCCEEDED(tp->GetSelection(&sel)) && sel && SUCCEEDED(sel->get_Length(&len)) && len > 0) {
				IUIAutomationTextRange *range = nullptr;
				if (SUCCEEDED(sel->GetElement(len - 1, &range)) && range) {
					SAFEARRAY *rects = nullptr;
					if (SUCCEEDED(range->GetBoundingRectangles(&rects)) && rects) {
						ok = lastRect(rects, out);
						SafeArrayDestroy(rects);
						rects = nullptr;
					}
					if (!ok) {
						/* A collapsed caret often has no rectangle of its own;
						 * the character next to it does. */
						IUIAutomationTextRange *c = nullptr;
						if (SUCCEEDED(range->Clone(&c)) && c) {
							if (SUCCEEDED(c->ExpandToEnclosingUnit(TextUnit_Character)) &&
							    SUCCEEDED(c->GetBoundingRectangles(&rects)) && rects) {
								ok = lastRect(rects, out);
								SafeArrayDestroy(rects);
								rects = nullptr;
							}
							releaseCom(c);
						}
					}
					releaseCom(range);
				}
			}
			releaseCom(sel);
			releaseCom(tp);
		}
		releaseCom(unk);
	}
	releaseCom(el);
	return ok;
}

void workerMain()
{
	const bool comOk = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

	IUIAutomation *ua = nullptr;
	if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
				       reinterpret_cast<void **>(&ua))) &&
	    ua) {
		/* A hung target app must not stall this thread for seconds. */
		IUIAutomation2 *ua2 = nullptr;
		if (SUCCEEDED(ua->QueryInterface(__uuidof(IUIAutomation2), reinterpret_cast<void **>(&ua2))) && ua2) {
			ua2->put_ConnectionTimeout(300);
			ua2->put_TransactionTimeout(300);
			ua2->Release();
		}
	}

	while (g_running.load(std::memory_order_acquire)) {
		const int64_t now = nowMs();
		const bool typing = now - g_lastTypingMs.load(std::memory_order_relaxed) < 2500;
		if (!typing) {
			std::this_thread::sleep_for(std::chrono::milliseconds(120));
			continue;
		}

		POINT p = {};
		bool found = caretFromGuiThread(p);
		if (!found && ua)
			found = caretFromUia(ua, p);
		if (found) {
			g_x.store(p.x, std::memory_order_relaxed);
			g_y.store(p.y, std::memory_order_relaxed);
			g_foundMs.store(nowMs(), std::memory_order_release);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(60));
	}

	if (ua)
		ua->Release();
	if (comOk)
		CoUninitialize();
}

} // namespace

void start()
{
	bool expected = false;
	if (!g_running.compare_exchange_strong(expected, true))
		return;
	g_thread = std::thread(workerMain);
}

void stop()
{
	if (!g_running.exchange(false))
		return;
	if (g_thread.joinable())
		g_thread.join();
}

void noteTyping()
{
	g_lastTypingMs.store(nowMs(), std::memory_order_relaxed);
}

bool latestLogical(int &x, int &y, int maxAgeMs)
{
	const int64_t found = g_foundMs.load(std::memory_order_acquire);
	if (found == 0 || nowMs() - found > maxAgeMs)
		return false;

	/* Win32 and UI Automation report physical pixels; Qt's cursor position is in
	 * logical pixels. Express the caret relative to the mouse, which is known in
	 * both systems at once, and scale the difference. */
	POINT cp = {};
	if (!GetCursorPos(&cp))
		return false;
	const QPoint lp = QCursor::pos();
	const QScreen *screen = QGuiApplication::screenAt(lp);
	const double dpr = screen ? screen->devicePixelRatio() : 1.0;
	const double scale = dpr > 0.0 ? dpr : 1.0;

	x = lp.x() + (int)std::lround(((double)g_x.load(std::memory_order_relaxed) - (double)cp.x) / scale);
	y = lp.y() + (int)std::lround(((double)g_y.load(std::memory_order_relaxed) - (double)cp.y) / scale);
	return true;
}

} // namespace zoominator_caret

#else // !_WIN32

namespace zoominator_caret {
void start() {}
void stop() {}
void noteTyping() {}
bool latestLogical(int &, int &, int)
{
	return false;
}
} // namespace zoominator_caret

#endif
