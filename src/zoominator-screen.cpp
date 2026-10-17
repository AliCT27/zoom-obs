/* Screen mapping: finds the captured monitor/window and maps the cursor into scene pixels. */

#include "zoominator-internal.hpp"

static QString make_screen_key_from_rect(int x, int y, int w, int h)
{
	return QStringLiteral("%1,%2,%3,%4").arg(x).arg(y).arg(w).arg(h);
}

void ZoominatorController::enumerateTargetItemsInCurrentScene(std::vector<obs_sceneitem_t *> &items) const
{
	items.clear();

	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	if (!sceneSource)
		return;

	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	obs_source_release(sceneSource);
	if (!scene)
		return;

	struct Ctx {
		std::vector<obs_sceneitem_t *> *items = nullptr;
		const QSet<QString> *included = nullptr;

		static bool is_marker_item(obs_sceneitem_t *item)
		{
			if (!item)
				return true;
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (!src)
				return true;
			if (src == ZoominatorController::instance().markerSource)
				return true;
			const char *srcName = obs_source_get_name(src);
			const char *srcId = obs_source_get_id(src);
			if (srcId && QString::fromUtf8(srcId) == QString::fromUtf8(kZoominatorMarkerSourceId))
				return true;
			return source_name_starts_with(srcName, kZoominatorMarkerSourceName) ||
			       source_name_starts_with(srcName, kZoominatorFocusSourceName);
		}

		static void enum_scene(obs_scene_t *scene, Ctx *ctx)
		{
			if (!scene || !ctx || !ctx->items)
				return;
			obs_scene_enum_items(
				scene,
				[](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
					auto *ctx = static_cast<Ctx *>(param);
					if (!ctx || !ctx->items || !item)
						return true;
					if (is_marker_item(item))
						return true;

					obs_source_t *src = obs_sceneitem_get_source(item);
					if (!src)
						return true;

					if (obs_scene_t *subScene = obs_scene_from_source(src)) {
						enum_scene(subScene, ctx);
						return true;
					}

					if (!ctx->included || ctx->included->isEmpty())
						return true;

					const char *srcName = obs_source_get_name(src);
					if (!srcName || !ctx->included->contains(QString::fromUtf8(srcName)))
						return true;

					ctx->items->push_back(item);
					return true;
				},
				ctx);
		}
	};

	Ctx ctx{&items, &includedSources};
	Ctx::enum_scene(scene, &ctx);
}

bool ZoominatorController::getSelectedScreenRect(int &x, int &y, int &w, int &h) const
{
	const auto screens = QGuiApplication::screens();
	for (auto *screen : screens) {
		if (!screen)
			continue;
		const QRect g = screen->geometry();
		const QString key = make_screen_key_from_rect(g.x(), g.y(), g.width(), g.height());
		if (screenKey.isEmpty() || key == screenKey) {
			x = g.x();
			y = g.y();
			w = g.width();
			h = g.height();
			return w > 0 && h > 0;
		}
	}
	return false;
}

bool ZoominatorController::getCursorPos(int &x, int &y) const
{
#if defined(__linux__)
	const QString platform = QGuiApplication::platformName();
	const bool nativeWayland = platform.startsWith(QStringLiteral("wayland"), Qt::CaseInsensitive);
	if (nativeWayland)
		return false;
#endif

	const QPoint p = QCursor::pos();
	x = p.x();
	y = p.y();
	return true;
}

static bool get_monitor_capture_selector(obs_source_t *src, QString &selector, int &monitorId, bool &hasId)
{
	selector.clear();
	monitorId = -1;
	hasId = false;

	if (!src)
		return false;

	obs_data_t *s = obs_source_get_settings(src);
	if (!s)
		return false;

	auto pick_str = [&](const char *key) -> const char * {
		const char *v = obs_data_get_string(s, key);
		return (v && *v) ? v : nullptr;
	};

	if (const char *v = pick_str("alt_id"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("monitor_device"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("display_device"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("device"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("monitor"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("monitor_id"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("id"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("setting_id"))
		selector = QString::fromUtf8(v);

	if (selector.isEmpty()) {
		monitorId = (int)obs_data_get_int(s, "monitor_id");
		if (monitorId != 0)
			hasId = true;

		if (!hasId) {
			monitorId = (int)obs_data_get_int(s, "monitor");
			if (monitorId != 0)
				hasId = true;
		}
		if (!hasId) {
			monitorId = (int)obs_data_get_int(s, "display");
			if (monitorId != 0)
				hasId = true;
		}
		if (!hasId) {
			monitorId = (int)obs_data_get_int(s, "screen");
			if (monitorId != 0)
				hasId = true;
		}
	}

	obs_data_release(s);
	return (!selector.isEmpty()) || hasId;
}

#ifdef _WIN32
struct MonitorInfoLite {
	QString device;
	RECT rc{};
};

static std::vector<MonitorInfoLite> enum_monitors()
{
	std::vector<MonitorInfoLite> out;

	auto cb = [](HMONITOR hMon, HDC, LPRECT, LPARAM lp) -> BOOL {
		auto *vec = reinterpret_cast<std::vector<MonitorInfoLite> *>(lp);
		MONITORINFOEXA mi{};
		mi.cbSize = sizeof(mi);
		if (!GetMonitorInfoA(hMon, &mi))
			return TRUE;
		MonitorInfoLite m;
		m.device = QString::fromLatin1(mi.szDevice);
		m.rc = mi.rcMonitor;
		vec->push_back(m);
		return TRUE;
	};

	EnumDisplayMonitors(nullptr, nullptr, cb, reinterpret_cast<LPARAM>(&out));
	return out;
}

#ifdef _WIN32
#include <vector>
#include <string>
#include <cwctype>
#include <windows.h>
#include <winuser.h>
#include <wingdi.h>

static bool wcs_ieq(const wchar_t *a, const wchar_t *b)
{
	if (!a || !b)
		return false;
	while (*a && *b) {
		if (towlower(*a) != towlower(*b))
			return false;
		++a;
		++b;
	}
	return *a == 0 && *b == 0;
}

static bool resolve_displayconfig_path_to_gdi(const QString &selector, QString &outGdi)
{
	outGdi.clear();
	if (selector.isEmpty())
		return false;

	const std::wstring want = selector.toStdWString();
	if (want.rfind(LR"(\\?\DISPLAY#)", 0) != 0)
		return false;

	UINT32 pathCount = 0, modeCount = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
		return false;

	std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
	    ERROR_SUCCESS)
		return false;

	for (UINT32 i = 0; i < pathCount; ++i) {
		const auto &p = paths[i];

		DISPLAYCONFIG_TARGET_DEVICE_NAME tdn{};
		tdn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
		tdn.header.size = sizeof(tdn);
		tdn.header.adapterId = p.targetInfo.adapterId;
		tdn.header.id = p.targetInfo.id;

		if (DisplayConfigGetDeviceInfo(&tdn.header) != ERROR_SUCCESS)
			continue;

		if (!wcs_ieq(tdn.monitorDevicePath, want.c_str()))
			continue;

		DISPLAYCONFIG_SOURCE_DEVICE_NAME sdn{};
		sdn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		sdn.header.size = sizeof(sdn);
		sdn.header.adapterId = p.sourceInfo.adapterId;
		sdn.header.id = p.sourceInfo.id;

		if (DisplayConfigGetDeviceInfo(&sdn.header) != ERROR_SUCCESS)
			return false;

		outGdi = QString::fromWCharArray(sdn.viewGdiDeviceName);
		return !outGdi.isEmpty();
	}

	return false;
}
#endif

static bool match_monitor_rect(obs_source_t *src, RECT &rcOut)
{
	QString selector;
	int monId = -1;
	bool hasId = false;
	if (!get_monitor_capture_selector(src, selector, monId, hasId))
		return false;

#ifdef _WIN32
	if (!selector.isEmpty()) {
		QString gdi;
		if (resolve_displayconfig_path_to_gdi(selector, gdi) && !gdi.isEmpty()) {
			selector = gdi;
		}
	}
#endif

	auto mons = enum_monitors();
	if (mons.empty())
		return false;

	if (!selector.isEmpty()) {
		for (auto &m : mons) {
			if (m.device == selector) {
				rcOut = m.rc;
				return true;
			}
			QRegularExpression re("DISPLAY(\\d+)");
			auto mw = re.match(selector);
			auto md = re.match(m.device);
			if (mw.hasMatch() && md.hasMatch() && mw.captured(1) == md.captured(1)) {
				rcOut = m.rc;
				return true;
			}
		}
	}

	if (hasId) {
		int idx = monId;
		if (idx >= 1 && idx <= (int)mons.size() && !selector.isEmpty() == false) {
		}
		if (idx >= 0 && idx < (int)mons.size()) {
			rcOut = mons[(size_t)idx].rc;
			return true;
		}
		if (idx >= 1 && idx <= (int)mons.size()) {
			rcOut = mons[(size_t)(idx - 1)].rc;
			return true;
		}
	}

	return false;
}

static bool parse_obs_window_selector(const QString &sel, QString &title, QString &clazz, QString &exe)
{
	title.clear();
	clazz.clear();
	exe.clear();
	if (sel.isEmpty())
		return false;

	QString s = sel;
	int last = s.lastIndexOf(':');
	if (last <= 0) {
		title = s;
		return true;
	}
	exe = s.mid(last + 1);
	s = s.left(last);

	int mid = s.lastIndexOf(':');
	if (mid <= 0) {
		title = s;
		return true;
	}
	clazz = s.mid(mid + 1);
	title = s.left(mid);
	return true;
}

static std::wstring to_w(const QString &s)
{
	return s.toStdWString();
}

static bool get_process_exe_name(DWORD pid, QString &out)
{
	out.clear();
	HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (!h)
		return false;
	wchar_t buf[MAX_PATH]{};
	if (GetModuleFileNameExW(h, nullptr, buf, MAX_PATH) == 0) {
		CloseHandle(h);
		return false;
	}
	CloseHandle(h);
	QString full = QString::fromWCharArray(buf);
	int slash = std::max(full.lastIndexOf('\\'), full.lastIndexOf('/'));
	out = (slash >= 0) ? full.mid(slash + 1) : full;
	return true;
}

struct WinFindCtx {
	QString wantTitle;
	QString wantClass;
	QString wantExe;
	HWND found = nullptr;
};

static BOOL CALLBACK enum_windows_cb(HWND hwnd, LPARAM lp)
{
	auto *ctx = reinterpret_cast<WinFindCtx *>(lp);
	if (!ctx)
		return TRUE;
	if (!IsWindowVisible(hwnd))
		return TRUE;

	wchar_t titleBuf[512]{};
	GetWindowTextW(hwnd, titleBuf, 512);
	QString title = QString::fromWCharArray(titleBuf);

	wchar_t classBuf[256]{};
	GetClassNameW(hwnd, classBuf, 256);
	QString clazz = QString::fromWCharArray(classBuf);

	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	QString exe;
	if (pid)
		get_process_exe_name(pid, exe);

	auto norm = [](QString s) {
		s = s.trimmed();
		return s;
	};

	const QString wt = norm(ctx->wantTitle);
	const QString wc = norm(ctx->wantClass);
	const QString we = norm(ctx->wantExe);

	bool ok = true;
	if (!we.isEmpty())
		ok = ok && exe.compare(we, Qt::CaseInsensitive) == 0;
	if (!wc.isEmpty())
		ok = ok && clazz.compare(wc, Qt::CaseInsensitive) == 0;

	if (!wt.isEmpty()) {
		ok = ok && title.contains(wt, Qt::CaseInsensitive);
	}

	if (ok) {
		ctx->found = hwnd;
		return FALSE;
	}
	return TRUE;
}

static bool match_window_rect_for_source(obs_source_t *src, RECT &rcOut)
{
	obs_data_t *s = obs_source_get_settings(src);
	if (!s)
		return false;
	const char *w = obs_data_get_string(s, "window");
	QString sel = (w && *w) ? QString::fromUtf8(w) : QString();
	obs_data_release(s);

	if (sel.isEmpty())
		return false;

	QString title, clazz, exe;
	parse_obs_window_selector(sel, title, clazz, exe);

	WinFindCtx ctx;
	ctx.wantTitle = title;
	ctx.wantClass = clazz;
	ctx.wantExe = exe;

	EnumWindows(enum_windows_cb, reinterpret_cast<LPARAM>(&ctx));
	if (!ctx.found)
		return false;

	RECT rc{};
	if (!GetWindowRect(ctx.found, &rc))
		return false;

	rcOut = rc;
	return true;
}
#endif

#ifdef __APPLE__
struct MonitorInfoLite {
	CGDirectDisplayID displayID;
	CGRect rc;
};

static std::vector<MonitorInfoLite> enum_monitors()
{
	std::vector<MonitorInfoLite> out;
	uint32_t count = 0;
	CGGetActiveDisplayList(0, nullptr, &count);
	if (count == 0)
		return out;
	std::vector<CGDirectDisplayID> displays(count);
	CGGetActiveDisplayList(count, displays.data(), &count);
	for (uint32_t i = 0; i < count; i++) {
		MonitorInfoLite m;
		m.displayID = displays[i];
		m.rc = CGDisplayBounds(displays[i]);
		out.push_back(m);
	}
	return out;
}

static bool match_monitor_rect(obs_source_t *src, CGRect &rcOut)
{
	auto mons = enum_monitors();
	if (mons.empty())
		return false;

	obs_data_t *s = obs_source_get_settings(src);
	if (s) {
		const char *uuid = obs_data_get_string(s, "display_uuid");
		if (uuid && *uuid) {
			for (auto &m : mons) {
				CFUUIDRef duuid = CGDisplayCreateUUIDFromDisplayID(m.displayID);
				if (duuid) {
					CFStringRef uuidStr = CFUUIDCreateString(kCFAllocatorDefault, duuid);
					CFRelease(duuid);
					if (uuidStr) {
						char buf[128];
						if (CFStringGetCString(uuidStr, buf, sizeof(buf),
								       kCFStringEncodingUTF8)) {
							if (strcasecmp(buf, uuid) == 0) {
								CFRelease(uuidStr);
								obs_data_release(s);
								rcOut = m.rc;
								return true;
							}
						}
						CFRelease(uuidStr);
					}
				}
			}
		}

		if (obs_data_has_user_value(s, "display")) {
			int idx = (int)obs_data_get_int(s, "display");
			if (idx >= 0 && idx < (int)mons.size()) {
				obs_data_release(s);
				rcOut = mons[(size_t)idx].rc;
				return true;
			}
		}

		obs_data_release(s);
	}

	QString selector;
	int monId = -1;
	bool hasId = false;
	if (get_monitor_capture_selector(src, selector, monId, hasId) && hasId) {
		CGDirectDisplayID wantId = (CGDirectDisplayID)monId;
		for (auto &m : mons) {
			if (m.displayID == wantId) {
				rcOut = m.rc;
				return true;
			}
		}
	}

	if (mons.size() == 1) {
		rcOut = mons[0].rc;
		return true;
	}

	return false;
}

static bool match_window_rect_for_source(obs_source_t *src, CGRect &rcOut)
{
	obs_data_t *s = obs_source_get_settings(src);
	if (!s)
		return false;

	int64_t windowID = obs_data_get_int(s, "window");
	if (windowID == 0)
		windowID = obs_data_get_int(s, "window_id");
	const char *ownerName = obs_data_get_string(s, "owner_name");
	obs_data_release(s);

	CFArrayRef windowList = CGWindowListCopyWindowInfo(
		kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
	if (!windowList)
		return false;

	bool found = false;
	CFIndex count = CFArrayGetCount(windowList);
	for (CFIndex i = 0; i < count && !found; i++) {
		CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(windowList, i);

		if (windowID != 0) {
			CFNumberRef numRef = (CFNumberRef)CFDictionaryGetValue(dict, kCGWindowNumber);
			if (numRef) {
				int64_t wid = 0;
				CFNumberGetValue(numRef, kCFNumberSInt64Type, &wid);
				if (wid == windowID) {
					CFDictionaryRef boundsDict =
						(CFDictionaryRef)CFDictionaryGetValue(dict, kCGWindowBounds);
					if (boundsDict && CGRectMakeWithDictionaryRepresentation(boundsDict, &rcOut))
						found = true;
				}
			}
			continue;
		}

		if (ownerName && *ownerName) {
			CFStringRef owner = (CFStringRef)CFDictionaryGetValue(dict, kCGWindowOwnerName);
			if (owner) {
				char buf[256];
				if (CFStringGetCString(owner, buf, sizeof(buf), kCFStringEncodingUTF8)) {
					if (strcmp(buf, ownerName) == 0) {
						CFDictionaryRef boundsDict =
							(CFDictionaryRef)CFDictionaryGetValue(dict, kCGWindowBounds);
						if (boundsDict &&
						    CGRectMakeWithDictionaryRepresentation(boundsDict, &rcOut))
							found = true;
					}
				}
			}
		}
	}

	CFRelease(windowList);
	return found;
}
#endif

#ifdef __linux__
struct MonitorInfoLite {
	QString name;
	int x, y, w, h;
};

static std::vector<MonitorInfoLite> enum_monitors()
{
	std::vector<MonitorInfoLite> out;
	Display *dpy = XOpenDisplay(nullptr);
	if (!dpy)
		return out;

	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);
	XRRScreenResources *res = XRRGetScreenResources(dpy, root);
	if (!res) {
		XCloseDisplay(dpy);
		return out;
	}

	for (int i = 0; i < res->noutput; i++) {
		XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
		if (!oi)
			continue;
		if (oi->connection != RR_Connected || oi->crtc == X11_None) {
			XRRFreeOutputInfo(oi);
			continue;
		}
		XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
		if (!ci) {
			XRRFreeOutputInfo(oi);
			continue;
		}
		MonitorInfoLite m;
		m.name = QString::fromLatin1(oi->name);
		m.x = (int)ci->x;
		m.y = (int)ci->y;
		m.w = (int)ci->width;
		m.h = (int)ci->height;
		out.push_back(m);
		XRRFreeCrtcInfo(ci);
		XRRFreeOutputInfo(oi);
	}

	XRRFreeScreenResources(res);
	XCloseDisplay(dpy);
	return out;
}

struct LinuxRect {
	int x, y, w, h;
};

static bool match_monitor_rect(obs_source_t *src, LinuxRect &rcOut)
{
	auto mons = enum_monitors();
	if (mons.empty())
		return false;

	QString selector;
	int monId = -1;
	bool hasId = false;
	get_monitor_capture_selector(src, selector, monId, hasId);

	if (!selector.isEmpty()) {
		for (auto &m : mons) {
			if (m.name == selector) {
				rcOut = {m.x, m.y, m.w, m.h};
				return true;
			}
		}
		QRegularExpression re("(\\d+)$");
		auto mw = re.match(selector);
		if (mw.hasMatch()) {
			int idx = mw.captured(1).toInt();
			if (idx >= 0 && idx < (int)mons.size()) {
				auto &m = mons[(size_t)idx];
				rcOut = {m.x, m.y, m.w, m.h};
				return true;
			}
		}
	}

	if (hasId) {
		if (monId >= 0 && monId < (int)mons.size()) {
			auto &m = mons[(size_t)monId];
			rcOut = {m.x, m.y, m.w, m.h};
			return true;
		}
		if (monId >= 1 && monId <= (int)mons.size()) {
			auto &m = mons[(size_t)(monId - 1)];
			rcOut = {m.x, m.y, m.w, m.h};
			return true;
		}
	}

	if (mons.size() == 1) {
		auto &m = mons[0];
		rcOut = {m.x, m.y, m.w, m.h};
		return true;
	}

	return false;
}

static bool parse_obs_window_selector_linux(const QString &sel, QString &title, QString &clazz, QString &exe)
{
	title.clear();
	clazz.clear();
	exe.clear();
	if (sel.isEmpty())
		return false;

	QString s = sel;
	int last = s.lastIndexOf(':');
	if (last <= 0) {
		title = s;
		return true;
	}
	exe = s.mid(last + 1);
	s = s.left(last);

	int mid = s.lastIndexOf(':');
	if (mid <= 0) {
		title = s;
		return true;
	}
	clazz = s.mid(mid + 1);
	title = s.left(mid);
	return true;
}

static bool match_window_rect_for_source(obs_source_t *src, LinuxRect &rcOut)
{
	obs_data_t *s = obs_source_get_settings(src);
	if (!s)
		return false;

	const char *w = obs_data_get_string(s, "window");
	QString sel = (w && *w) ? QString::fromUtf8(w) : QString();
	if (sel.isEmpty()) {
		w = obs_data_get_string(s, "capture_window");
		sel = (w && *w) ? QString::fromUtf8(w) : QString();
	}

	const char *windowName = obs_data_get_string(s, "window_name");
	QString wantName = (windowName && *windowName) ? QString::fromUtf8(windowName) : QString();

	int64_t windowId = obs_data_get_int(s, "window");
	obs_data_release(s);

	Display *dpy = XOpenDisplay(nullptr);
	if (!dpy)
		return false;

	if (windowId > 0) {
		XWindowAttributes attr;
		if (XGetWindowAttributes(dpy, (Window)windowId, &attr)) {
			int absX = 0, absY = 0;
			Window child;
			XTranslateCoordinates(dpy, (Window)windowId, DefaultRootWindow(dpy), 0, 0, &absX, &absY,
					      &child);
			XCloseDisplay(dpy);
			rcOut = {absX, absY, attr.width, attr.height};
			return true;
		}
	}

	QString title, clazz, exe;
	if (!sel.isEmpty())
		parse_obs_window_selector_linux(sel, title, clazz, exe);
	if (title.isEmpty() && !wantName.isEmpty())
		title = wantName;

	if (title.isEmpty() && clazz.isEmpty()) {
		XCloseDisplay(dpy);
		return false;
	}

	Window root = DefaultRootWindow(dpy);
	Atom netClientList = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
	bool found = false;

	if (netClientList != X11_None) {
		Atom type;
		int format;
		unsigned long nitems, bytesAfter;
		unsigned char *data = nullptr;
		if (XGetWindowProperty(dpy, root, netClientList, 0, ~0L, False, XA_WINDOW, &type, &format, &nitems,
				       &bytesAfter, &data) == Success &&
		    data) {
			Window *windows = (Window *)data;
			for (unsigned long i = 0; i < nitems && !found; i++) {
				Window win = windows[i];

				if (!clazz.isEmpty()) {
					XClassHint ch{};
					if (XGetClassHint(dpy, win, &ch)) {
						QString resName = ch.res_name ? QString::fromUtf8(ch.res_name)
									      : QString();
						QString resClass = ch.res_class ? QString::fromUtf8(ch.res_class)
										: QString();
						if (ch.res_name)
							XFree(ch.res_name);
						if (ch.res_class)
							XFree(ch.res_class);
						if (resName.compare(clazz, Qt::CaseInsensitive) != 0 &&
						    resClass.compare(clazz, Qt::CaseInsensitive) != 0)
							continue;
					} else {
						continue;
					}
				}

				if (!title.isEmpty()) {
					char *name = nullptr;
					if (XFetchName(dpy, win, &name) && name) {
						QString winTitle = QString::fromUtf8(name);
						XFree(name);
						if (!winTitle.contains(title, Qt::CaseInsensitive))
							continue;
					} else {
						continue;
					}
				}

				XWindowAttributes attr;
				if (XGetWindowAttributes(dpy, win, &attr)) {
					int absX = 0, absY = 0;
					Window child;
					XTranslateCoordinates(dpy, win, root, 0, 0, &absX, &absY, &child);
					rcOut = {absX, absY, attr.width, attr.height};
					found = true;
				}
			}
			XFree(data);
		}
	}

	XCloseDisplay(dpy);
	return found;
}
#endif

bool ZoominatorController::mapCursorToScenePixels(int cursorX, int cursorY, float &sx, float &sy,
						  bool &cursorInside) const
{
	cursorInside = false;
	sx = 0.f;
	sy = 0.f;

	int rx = 0, ry = 0, rw = 0, rh = 0;
	if (!getSelectedScreenRect(rx, ry, rw, rh) || rw <= 0 || rh <= 0)
		return false;

	cursorInside = !(cursorX < rx || cursorX >= rx + rw || cursorY < ry || cursorY >= ry + rh);
	const int clampedX = std::max(rx, std::min(cursorX, rx + rw - 1));
	const int clampedY = std::max(ry, std::min(cursorY, ry + rh - 1));
	const double relX = (clampedX - rx) / (double)rw;
	const double relY = (clampedY - ry) / (double)rh;

	obs_video_info ovi{};
	const bool haveVi = obs_get_video_info(&ovi);
	const double cw = haveVi ? (double)ovi.base_width : 1920.0;
	const double ch = haveVi ? (double)ovi.base_height : 1080.0;
	if (cw <= 0.0 || ch <= 0.0)
		return false;

	/* The cursor is mapped onto where the captured screen actually sits in the scene. While the
	 * zoom engine is running that is the bounds it captured; otherwise (e.g. a fitted screen
	 * on a vertical canvas, nothing zooming) the live layout is measured, so the spotlight and
	 * halo land on the cursor instead of assuming the capture fills the whole canvas. */
	vec2 bMin = sceneContentMin, bMax = sceneContentMax;
	bool bValid = sceneContentBoundsValid;
	if (centerCursorUntilEdge && !bValid) {
		const qint64 nowB = QDateTime::currentMSecsSinceEpoch();
		if (nowB - restBoundsMs >= 250) {
			restBoundsMs = nowB;
			std::vector<obs_sceneitem_t *> items;
			enumerateTargetItemsInCurrentScene(items);
			vec2 mn{}, mx{};
			restBoundsValid = !items.empty() && computeTargetBounds(items, mn, mx);
			restBoundsMin = mn;
			restBoundsMax = mx;
		}
		bMin = restBoundsMin;
		bMax = restBoundsMax;
		bValid = restBoundsValid;
	}
	if (centerCursorUntilEdge && bValid) {
		const double contentW = std::max(1.0, (double)bMax.x - (double)bMin.x);
		const double contentH = std::max(1.0, (double)bMax.y - (double)bMin.y);
		sx = (float)((double)bMin.x + relX * contentW);
		sy = (float)((double)bMin.y + relY * contentH);
	} else {
		sx = (float)(relX * cw);
		sy = (float)(relY * ch);
	}
	return true;
}

