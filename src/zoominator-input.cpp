/* Input: global keyboard/mouse hooks for Windows, macOS and Linux, and their install/uninstall. */

#include "zoominator-internal.hpp"

static ZoominatorController *g_ctl = nullptr;

struct ModifierState {
	bool leftCtrl = false;
	bool rightCtrl = false;
	bool leftAlt = false;
	bool rightAlt = false;
	bool leftShift = false;
	bool rightShift = false;
	bool leftWin = false;
	bool rightWin = false;
};

static inline bool family_matches(bool wantAny, bool wantLeft, bool wantRight, bool haveLeft, bool haveRight)
{
	if (!wantAny && !wantLeft && !wantRight)
		return !haveLeft && !haveRight;
	if (wantLeft && !haveLeft)
		return false;
	if (wantRight && !haveRight)
		return false;
	if (!wantAny) {
		if (!wantLeft && haveLeft)
			return false;
		if (!wantRight && haveRight)
			return false;
	}
	return wantAny ? (haveLeft || haveRight) : true;
}

static ModifierState current_modifiers()
{
	ModifierState state;
#if defined(_WIN32)
	auto down = [](int vk) {
		return (GetAsyncKeyState(vk) & 0x8000) != 0;
	};
	state.leftCtrl = down(VK_LCONTROL);
	state.rightCtrl = down(VK_RCONTROL);
	state.leftAlt = down(VK_LMENU);
	state.rightAlt = down(VK_RMENU);
	state.leftShift = down(VK_LSHIFT);
	state.rightShift = down(VK_RSHIFT);
	state.leftWin = down(VK_LWIN);
	state.rightWin = down(VK_RWIN);
#elif defined(__APPLE__)
	auto down = [](int vk) {
		return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, (CGKeyCode)vk);
	};
	state.leftCtrl = down(kVK_Control);
	state.rightCtrl = down(kVK_RightControl);
	state.leftAlt = down(kVK_Option);
	state.rightAlt = down(kVK_RightOption);
	state.leftShift = down(kVK_Shift);
	state.rightShift = down(kVK_RightShift);
	state.leftWin = down(kVK_Command);
	state.rightWin = down(kVK_RightCommand);
#elif defined(__linux__)
	Display *dpy = XOpenDisplay(nullptr);
	if (dpy) {
		char keys[32];
		XQueryKeymap(dpy, keys);
		auto isDown = [&](KeySym sym) -> bool {
			KeyCode kc = XKeysymToKeycode(dpy, sym);
			if (kc == 0)
				return false;
			return (keys[kc / 8] & (1 << (kc % 8))) != 0;
		};
		state.leftCtrl = isDown(XK_Control_L);
		state.rightCtrl = isDown(XK_Control_R);
		state.leftAlt = isDown(XK_Alt_L);
		state.rightAlt = isDown(XK_Alt_R);
		state.leftShift = isDown(XK_Shift_L);
		state.rightShift = isDown(XK_Shift_R);
		state.leftWin = isDown(XK_Super_L);
		state.rightWin = isDown(XK_Super_R);
		XCloseDisplay(dpy);
	}
#endif
	return state;
}

static bool mods_current(bool wantCtrl, bool wantAlt, bool wantShift, bool wantWin, bool wantLeftCtrl,
			 bool wantRightCtrl, bool wantLeftAlt, bool wantRightAlt, bool wantLeftShift,
			 bool wantRightShift, bool wantLeftWin, bool wantRightWin)
{
	const ModifierState state = current_modifiers();
	if (!family_matches(wantCtrl, wantLeftCtrl, wantRightCtrl, state.leftCtrl, state.rightCtrl))
		return false;
	if (!family_matches(wantAlt, wantLeftAlt, wantRightAlt, state.leftAlt, state.rightAlt))
		return false;
	if (!family_matches(wantShift, wantLeftShift, wantRightShift, state.leftShift, state.rightShift))
		return false;
	if (!family_matches(wantWin, wantLeftWin, wantRightWin, state.leftWin, state.rightWin))
		return false;
	return true;
}

static bool mods_current(bool wantCtrl, bool wantAlt, bool wantShift, bool wantWin)
{
	return mods_current(wantCtrl, wantAlt, wantShift, wantWin, false, false, false, false, false, false, false,
			    false);
}

#ifdef _WIN32
static inline bool is_modifier_vk(int vk)
{
	switch (vk) {
	case VK_CONTROL:
	case VK_LCONTROL:
	case VK_RCONTROL:
	case VK_MENU:
	case VK_LMENU:
	case VK_RMENU:
	case VK_SHIFT:
	case VK_LSHIFT:
	case VK_RSHIFT:
	case VK_LWIN:
	case VK_RWIN:
		return true;
	default:
		return false;
	}
}

static inline bool is_wanted_modifier_vk(int vk, const ZoominatorController *ctl)
{
	if (!ctl)
		return false;
	if ((ctl->modCtrl && (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL)) ||
	    (ctl->modLeftCtrl && vk == VK_LCONTROL) || (ctl->modRightCtrl && vk == VK_RCONTROL))
		return true;
	if ((ctl->modAlt && (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU)) ||
	    (ctl->modLeftAlt && vk == VK_LMENU) || (ctl->modRightAlt && vk == VK_RMENU))
		return true;
	if ((ctl->modShift && (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT)) ||
	    (ctl->modLeftShift && vk == VK_LSHIFT) || (ctl->modRightShift && vk == VK_RSHIFT))
		return true;
	if ((ctl->modWin && (vk == VK_LWIN || vk == VK_RWIN)) || (ctl->modLeftWin && vk == VK_LWIN) ||
	    (ctl->modRightWin && vk == VK_RWIN))
		return true;
	return false;
}

static bool vk_matches(int pressedVk, int wantVk)
{
	if (pressedVk == wantVk)
		return true;

	if (wantVk >= '0' && wantVk <= '9') {
		const int d = wantVk - '0';
		return pressedVk == (VK_NUMPAD0 + d);
	}
	if (wantVk >= VK_NUMPAD0 && wantVk <= VK_NUMPAD9) {
		const int d = wantVk - VK_NUMPAD0;
		return pressedVk == ('0' + d);
	}

	return false;
}

static bool is_modifier_only_hotkey(int vk)
{
	switch (vk) {
	case VK_CONTROL:
	case VK_LCONTROL:
	case VK_RCONTROL:
	case VK_MENU:
	case VK_LMENU:
	case VK_RMENU:
	case VK_SHIFT:
	case VK_LSHIFT:
	case VK_RSHIFT:
	case VK_LWIN:
	case VK_RWIN:
		return true;
	default:
		return false;
	}
}

static inline bool shortcut_modifier_held()
{
	return (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000) ||
	       (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
}

/* Readable name of a key for the key display. Empty for keys that are not worth showing. */
static QString key_display_name(int vk)
{
	if (vk >= 'A' && vk <= 'Z')
		return QString(QChar((ushort)vk));
	if (vk >= '0' && vk <= '9')
		return QString(QChar((ushort)vk));
	if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
		return QStringLiteral("Num %1").arg(vk - VK_NUMPAD0);
	if (vk >= VK_F1 && vk <= VK_F24)
		return QStringLiteral("F%1").arg(vk - VK_F1 + 1);
	switch (vk) {
	case VK_RETURN: return QStringLiteral("Enter");
	case VK_SPACE: return QStringLiteral("Space");
	case VK_BACK: return QStringLiteral("Backspace");
	case VK_TAB: return QStringLiteral("Tab");
	case VK_ESCAPE: return QStringLiteral("Esc");
	case VK_DELETE: return QStringLiteral("Del");
	case VK_INSERT: return QStringLiteral("Ins");
	case VK_HOME: return QStringLiteral("Home");
	case VK_END: return QStringLiteral("End");
	case VK_PRIOR: return QStringLiteral("PgUp");
	case VK_NEXT: return QStringLiteral("PgDn");
	case VK_LEFT: return QStringLiteral("\u2190");
	case VK_UP: return QStringLiteral("\u2191");
	case VK_RIGHT: return QStringLiteral("\u2192");
	case VK_DOWN: return QStringLiteral("\u2193");
	case VK_SNAPSHOT: return QStringLiteral("PrtSc");
	case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
		return QString();
	default:
		break;
	}
	const UINT ch = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_CHAR) & 0x7FFFFFFF;
	if (ch >= 0x20 && ch != 0x7F)
		return QString(QChar((ushort)ch)).toUpper();
	return QString();
}

/* Called from the keyboard hook for every key press while the key display is on. Builds the label ("Ctrl + Shift + P")
 * and hands it to the main thread. Shortcuts (Ctrl, Alt or Win held) are always shown; plain keys only when asked. */
void ZoominatorController::handleKeyForDisplay(int vk)
{
	if (shuttingDown || is_modifier_vk(vk))
		return;
	const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
	const bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
	const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	const bool shortcut = ctrl || alt || win;
	if (!shortcut && !keyDisplayAll)
		return;
	const QString name = key_display_name(vk);
	if (name.isEmpty())
		return;

	QStringList parts;
	if (ctrl)
		parts << QStringLiteral("Ctrl");
	if (alt)
		parts << QStringLiteral("Alt");
	if (win)
		parts << QStringLiteral("Win");
	if (shift && shortcut)
		parts << QStringLiteral("Shift");
	parts << name;
	const QString label = parts.join(QStringLiteral(" + "));
	QMetaObject::invokeMethod(this, [this, label]() { showKeyLabel(label); }, Qt::QueuedConnection);
}

LRESULT CALLBACK ZoominatorController::kb_hook_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION && g_ctl) {
		const auto *k = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
		const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
		const bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

		if (!k || !(down || up))
			return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);

		const int vk = (int)k->vkCode;

		if (down && g_ctl->keyDisplayEnabled)
			g_ctl->handleKeyForDisplay(vk);

		if (g_ctl->followToggleHkValid && down && g_ctl->followToggleHotkeyVk != 0 &&
		    vk_matches(vk, g_ctl->followToggleHotkeyVk) &&
		    mods_current(g_ctl->followToggleModCtrl, g_ctl->followToggleModAlt, g_ctl->followToggleModShift,
				 g_ctl->followToggleModWin)) {
			g_ctl->toggleFollowMouseRuntime();
		}

		if (down && g_ctl->manualZoomInBinding.valid && vk_matches(vk, g_ctl->manualZoomInBinding.vk) &&
		    mods_current(g_ctl->manualZoomInBinding.ctrl, g_ctl->manualZoomInBinding.alt,
				 g_ctl->manualZoomInBinding.shift, g_ctl->manualZoomInBinding.win)) {
			g_ctl->manualZoomIn();
		}
		if (down && g_ctl->manualZoomOutBinding.valid && vk_matches(vk, g_ctl->manualZoomOutBinding.vk) &&
		    mods_current(g_ctl->manualZoomOutBinding.ctrl, g_ctl->manualZoomOutBinding.alt,
				 g_ctl->manualZoomOutBinding.shift, g_ctl->manualZoomOutBinding.win)) {
			g_ctl->manualZoomOut();
		}
		if (down && g_ctl->focusToggleBinding.valid && vk_matches(vk, g_ctl->focusToggleBinding.vk) &&
		    mods_current(g_ctl->focusToggleBinding.ctrl, g_ctl->focusToggleBinding.alt,
				 g_ctl->focusToggleBinding.shift, g_ctl->focusToggleBinding.win)) {
			g_ctl->toggleFocusRuntime();
		}
		if (down && g_ctl->focusOnBinding.valid && vk_matches(vk, g_ctl->focusOnBinding.vk) &&
		    mods_current(g_ctl->focusOnBinding.ctrl, g_ctl->focusOnBinding.alt, g_ctl->focusOnBinding.shift, g_ctl->focusOnBinding.win)) {
			g_ctl->setFocusRuntime(true);
		}
		if (down && g_ctl->focusOffBinding.valid && vk_matches(vk, g_ctl->focusOffBinding.vk) &&
		    mods_current(g_ctl->focusOffBinding.ctrl, g_ctl->focusOffBinding.alt, g_ctl->focusOffBinding.shift, g_ctl->focusOffBinding.win)) {
			g_ctl->setFocusRuntime(false);
		}

		if (g_ctl->triggerType == "auto") {
			/* Plain typing keeps the automatic zoom alive. Shortcuts (Ctrl/Alt/Win held)
			 * are ignored so editor commands do not trigger it. */
			if (down && g_ctl->autoOnTyping && !is_modifier_vk(vk) && !shortcut_modifier_held())
				g_ctl->onAutoActivity(true);
			return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);
		}

		if (!(g_ctl->hkValid && g_ctl->triggerType == "keyboard"))
			return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);

		if (g_ctl->hotkeyVk == 0) {
			if (!is_modifier_vk(vk))
				return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);

			if (!is_wanted_modifier_vk(vk, g_ctl))
				return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);

			if (g_ctl->hotkeyMode == "toggle") {
				if (down)
					g_ctl->onTriggerDown();
			} else {
				if (down)
					g_ctl->onTriggerDown();
				if (up && g_ctl->zoomPressed)
					g_ctl->onTriggerUp();
			}

			return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);
		}

		if (vk_matches(vk, g_ctl->hotkeyVk)) {
			if (mods_current(g_ctl->modCtrl, g_ctl->modAlt, g_ctl->modShift, g_ctl->modWin,
					 g_ctl->modLeftCtrl, g_ctl->modRightCtrl, g_ctl->modLeftAlt, g_ctl->modRightAlt,
					 g_ctl->modLeftShift, g_ctl->modRightShift, g_ctl->modLeftWin,
					 g_ctl->modRightWin)) {
				if (g_ctl->hotkeyMode == "toggle") {
					if (down)
						g_ctl->onTriggerDown();
				} else {
					if (down)
						g_ctl->onTriggerDown();
					if (up)
						g_ctl->onTriggerUp();
				}
			}
		}
	}
	return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);
}

bool ZoominatorController::wheelModifierHeld() const
{
	const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
	const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	if (wheelZoomModifier == "ctrl")
		return ctrl && !alt && !shift;
	if (wheelZoomModifier == "shift")
		return shift && !ctrl && !alt;
	return alt && !ctrl && !shift;
}

LRESULT CALLBACK ZoominatorController::mouse_hook_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION && g_ctl) {
		const auto *m = reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
		if (!m)
			return CallNextHookEx((HHOOK)g_ctl->mouseHook, nCode, wParam, lParam);

		const bool down = (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN ||
				   wParam == WM_XBUTTONDOWN);
		const bool up = (wParam == WM_LBUTTONUP || wParam == WM_RBUTTONUP || wParam == WM_MBUTTONUP ||
				 wParam == WM_XBUTTONUP);
		const unsigned short mouseData = (unsigned short)HIWORD(m->mouseData);

		/* Modifier + mouse wheel zooms in and out. The hook swallows the wheel
		 * notches while the chosen modifier is held, so the window under the
		 * pointer does not scroll as well. */
		if (wParam == WM_MOUSEWHEEL && g_ctl->wheelZoomEnabled && g_ctl->wheelModifierHeld()) {
			const int delta = GET_WHEEL_DELTA_WPARAM(m->mouseData);
			g_ctl->wheelDeltaRemainder += delta;
			const int steps = g_ctl->wheelDeltaRemainder / WHEEL_DELTA;
			g_ctl->wheelDeltaRemainder %= WHEEL_DELTA;
			if (steps != 0)
				g_ctl->wheelZoom(steps > 0 ? 1 : -1, std::abs(steps));
			if (g_ctl->wheelZoomModifier == "alt") {
				/* A bare Alt press/release would otherwise focus the menu bar of the
				 * foreground app. An unassigned key press in between cancels that. */
				INPUT in[2] = {};
				in[0].type = INPUT_KEYBOARD;
				in[0].ki.wVk = 0xE8;
				in[1].type = INPUT_KEYBOARD;
				in[1].ki.wVk = 0xE8;
				in[1].ki.dwFlags = KEYEVENTF_KEYUP;
				SendInput(2, in, sizeof(INPUT));
			}
			return 1;
		}

		if (down)
			g_ctl->captureMarkerClickPosition();

		if (down && g_ctl->triggerType == "auto" && g_ctl->autoOnClick)
			g_ctl->onAutoActivity();

		if (g_ctl->triggerType == "mouse" && (down || up)) {
			if (mods_current(g_ctl->modCtrl, g_ctl->modAlt, g_ctl->modShift, g_ctl->modWin,
					 g_ctl->modLeftCtrl, g_ctl->modRightCtrl, g_ctl->modLeftAlt, g_ctl->modRightAlt,
					 g_ctl->modLeftShift, g_ctl->modRightShift, g_ctl->modLeftWin,
					 g_ctl->modRightWin)) {
				if (g_ctl->triggerMatchesMouse((unsigned int)wParam, mouseData)) {
					if (g_ctl->hotkeyMode == "toggle") {
						if (down)
							g_ctl->onTriggerDown();
					} else {
						if (down)
							g_ctl->onTriggerDown();
						if (up)
							g_ctl->onTriggerUp();
					}
				}
			}
		}
	}
	return CallNextHookEx((HHOOK)g_ctl->mouseHook, nCode, wParam, lParam);
}
#endif

#ifdef __APPLE__
static inline bool is_modifier_vk(int vk)
{
	return vk == kVK_Control || vk == kVK_RightControl || vk == kVK_Option || vk == kVK_RightOption ||
	       vk == kVK_Shift || vk == kVK_RightShift || vk == kVK_Command || vk == kVK_RightCommand;
}

static inline bool is_wanted_modifier_vk(int vk, const ZoominatorController *ctl)
{
	if (!ctl)
		return false;
	if ((ctl->modCtrl && (vk == kVK_Control || vk == kVK_RightControl)) ||
	    (ctl->modLeftCtrl && vk == kVK_Control) || (ctl->modRightCtrl && vk == kVK_RightControl))
		return true;
	if ((ctl->modAlt && (vk == kVK_Option || vk == kVK_RightOption)) || (ctl->modLeftAlt && vk == kVK_Option) ||
	    (ctl->modRightAlt && vk == kVK_RightOption))
		return true;
	if ((ctl->modShift && (vk == kVK_Shift || vk == kVK_RightShift)) || (ctl->modLeftShift && vk == kVK_Shift) ||
	    (ctl->modRightShift && vk == kVK_RightShift))
		return true;
	if ((ctl->modWin && (vk == kVK_Command || vk == kVK_RightCommand)) || (ctl->modLeftWin && vk == kVK_Command) ||
	    (ctl->modRightWin && vk == kVK_RightCommand))
		return true;
	return false;
}

static bool vk_matches(int pressedVk, int wantVk)
{
	if (pressedVk == wantVk)
		return true;

	if (wantVk >= kVK_ANSI_Keypad0 && wantVk <= kVK_ANSI_Keypad9) {
		static const int numRow[] = {kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
					     kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9};
		return pressedVk == numRow[wantVk - kVK_ANSI_Keypad0];
	}
	return false;
}

static bool mac_mouse_button_matches(CGEventType type, int64_t buttonNumber, const QString &want)
{
	const bool isDown =
		(type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown || type == kCGEventOtherMouseDown);
	const bool isUp = (type == kCGEventLeftMouseUp || type == kCGEventRightMouseUp || type == kCGEventOtherMouseUp);
	if (!isDown && !isUp)
		return false;

	if (want == "left")
		return buttonNumber == 0;
	if (want == "right")
		return buttonNumber == 1;
	if (want == "middle")
		return buttonNumber == 2;
	if (want == "x1")
		return buttonNumber == 3;
	if (want == "x2")
		return buttonNumber == 4;
	return false;
}

CGEventRef ZoominatorController::eventTapCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event,
						  void *refcon)
{
	(void)proxy;
	auto *ctl = static_cast<ZoominatorController *>(refcon);
	if (!ctl)
		return event;

	if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
		if (ctl->eventTap)
			CGEventTapEnable(ctl->eventTap, true);
		return event;
	}

	if (type == kCGEventKeyDown || type == kCGEventKeyUp) {
		const int keycode = (int)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
		const bool down = (type == kCGEventKeyDown);

		if (ctl->followToggleHkValid && down && ctl->followToggleHotkeyVk != 0 &&
		    vk_matches(keycode, ctl->followToggleHotkeyVk) &&
		    mods_current(ctl->followToggleModCtrl, ctl->followToggleModAlt, ctl->followToggleModShift,
				 ctl->followToggleModWin)) {
			ctl->toggleFollowMouseRuntime();
		}

		if (down && ctl->manualZoomInBinding.valid && vk_matches(keycode, ctl->manualZoomInBinding.vk) &&
		    mods_current(ctl->manualZoomInBinding.ctrl, ctl->manualZoomInBinding.alt,
				 ctl->manualZoomInBinding.shift, ctl->manualZoomInBinding.win)) {
			ctl->manualZoomIn();
		}
		if (down && ctl->manualZoomOutBinding.valid && vk_matches(keycode, ctl->manualZoomOutBinding.vk) &&
		    mods_current(ctl->manualZoomOutBinding.ctrl, ctl->manualZoomOutBinding.alt,
				 ctl->manualZoomOutBinding.shift, ctl->manualZoomOutBinding.win)) {
			ctl->manualZoomOut();
		}
		if (down && ctl->focusToggleBinding.valid && vk_matches(keycode, ctl->focusToggleBinding.vk) &&
		    mods_current(ctl->focusToggleBinding.ctrl, ctl->focusToggleBinding.alt,
				 ctl->focusToggleBinding.shift, ctl->focusToggleBinding.win)) {
			ctl->toggleFocusRuntime();
		}
		if (down && ctl->focusOnBinding.valid && vk_matches(keycode, ctl->focusOnBinding.vk) &&
		    mods_current(ctl->focusOnBinding.ctrl, ctl->focusOnBinding.alt, ctl->focusOnBinding.shift, ctl->focusOnBinding.win)) {
			ctl->setFocusRuntime(true);
		}
		if (down && ctl->focusOffBinding.valid && vk_matches(keycode, ctl->focusOffBinding.vk) &&
		    mods_current(ctl->focusOffBinding.ctrl, ctl->focusOffBinding.alt, ctl->focusOffBinding.shift, ctl->focusOffBinding.win)) {
			ctl->setFocusRuntime(false);
		}

		if (ctl->triggerType == "auto") {
			const CGEventFlags flags = CGEventGetFlags(event);
			const bool shortcut =
				(flags & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate)) != 0;
			if (down && ctl->autoOnTyping && !shortcut)
				ctl->onAutoActivity(true);
			return event;
		}
	}

	if (ctl->triggerType == "keyboard" && ctl->hkValid) {
		if (type == kCGEventKeyDown || type == kCGEventKeyUp) {
			const int keycode = (int)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
			const bool down = (type == kCGEventKeyDown);
			const bool up = (type == kCGEventKeyUp);

			if (ctl->hotkeyVk == 0) {
				return event;
			}

			if (vk_matches(keycode, ctl->hotkeyVk)) {
				if (mods_current(ctl->modCtrl, ctl->modAlt, ctl->modShift, ctl->modWin,
						 ctl->modLeftCtrl, ctl->modRightCtrl, ctl->modLeftAlt, ctl->modRightAlt,
						 ctl->modLeftShift, ctl->modRightShift, ctl->modLeftWin,
						 ctl->modRightWin)) {
					if (ctl->hotkeyMode == "toggle") {
						if (down)
							ctl->onTriggerDown();
					} else {
						if (down)
							ctl->onTriggerDown();
						if (up)
							ctl->onTriggerUp();
					}
				}
			}
			return event;
		}

		if (type == kCGEventFlagsChanged) {
			if (ctl->hotkeyVk == 0) {
				const bool matchNow = mods_current(ctl->modCtrl, ctl->modAlt, ctl->modShift,
								   ctl->modWin, ctl->modLeftCtrl, ctl->modRightCtrl,
								   ctl->modLeftAlt, ctl->modRightAlt, ctl->modLeftShift,
								   ctl->modRightShift, ctl->modLeftWin,
								   ctl->modRightWin);
				if (ctl->hotkeyMode == "toggle") {
					if (matchNow && !ctl->zoomPressed && !ctl->zoomLatched)
						ctl->onTriggerDown();
					else if (matchNow && ctl->zoomLatched)
						ctl->onTriggerDown();
				} else {
					if (matchNow && !ctl->zoomPressed)
						ctl->onTriggerDown();
					if (!matchNow && ctl->zoomPressed)
						ctl->onTriggerUp();
				}
			}
			return event;
		}
	}

	const bool isMouseDown =
		(type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown || type == kCGEventOtherMouseDown);
	const bool isMouseUp =
		(type == kCGEventLeftMouseUp || type == kCGEventRightMouseUp || type == kCGEventOtherMouseUp);

	if (isMouseDown)
		ctl->captureMarkerClickPosition();

	if (isMouseDown && ctl->triggerType == "auto" && ctl->autoOnClick)
		ctl->onAutoActivity();

	if (ctl->triggerType == "mouse") {
		if (isMouseDown || isMouseUp) {
			const int64_t btn = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
			if (mods_current(ctl->modCtrl, ctl->modAlt, ctl->modShift, ctl->modWin, ctl->modLeftCtrl,
					 ctl->modRightCtrl, ctl->modLeftAlt, ctl->modRightAlt, ctl->modLeftShift,
					 ctl->modRightShift, ctl->modLeftWin, ctl->modRightWin)) {
				if (mac_mouse_button_matches(type, btn, ctl->mouseButton)) {
					if (ctl->hotkeyMode == "toggle") {
						if (isMouseDown)
							ctl->onTriggerDown();
					} else {
						if (isMouseDown)
							ctl->onTriggerDown();
						if (isMouseUp)
							ctl->onTriggerUp();
					}
				}
			}
		}
	}

	return event;
}
#endif

#ifdef __linux__
static inline bool is_modifier_vk(int vk)
{
	return vk == XK_Control_L || vk == XK_Control_R || vk == XK_Alt_L || vk == XK_Alt_R || vk == XK_Shift_L ||
	       vk == XK_Shift_R || vk == XK_Super_L || vk == XK_Super_R;
}

static inline bool is_wanted_modifier_vk(int vk, const ZoominatorController *ctl)
{
	if (!ctl)
		return false;
	if ((ctl->modCtrl && (vk == XK_Control_L || vk == XK_Control_R)) || (ctl->modLeftCtrl && vk == XK_Control_L) ||
	    (ctl->modRightCtrl && vk == XK_Control_R))
		return true;
	if ((ctl->modAlt && (vk == XK_Alt_L || vk == XK_Alt_R)) || (ctl->modLeftAlt && vk == XK_Alt_L) ||
	    (ctl->modRightAlt && vk == XK_Alt_R))
		return true;
	if ((ctl->modShift && (vk == XK_Shift_L || vk == XK_Shift_R)) || (ctl->modLeftShift && vk == XK_Shift_L) ||
	    (ctl->modRightShift && vk == XK_Shift_R))
		return true;
	if ((ctl->modWin && (vk == XK_Super_L || vk == XK_Super_R)) || (ctl->modLeftWin && vk == XK_Super_L) ||
	    (ctl->modRightWin && vk == XK_Super_R))
		return true;
	return false;
}

static bool vk_matches(int pressedVk, int wantVk)
{
	if (pressedVk == wantVk)
		return true;

	if (wantVk >= XK_a && wantVk <= XK_z)
		return pressedVk == (wantVk - XK_a + XK_A);
	if (wantVk >= XK_A && wantVk <= XK_Z)
		return pressedVk == (wantVk - XK_A + XK_a);

	if (wantVk >= XK_KP_0 && wantVk <= XK_KP_9)
		return pressedVk == (XK_0 + (wantVk - XK_KP_0));
	if (wantVk >= XK_0 && wantVk <= XK_9)
		return pressedVk == (XK_KP_0 + (wantVk - XK_0));

	return false;
}

static bool linux_button_matches(int button, const QString &want)
{
	if (want == "left")
		return button == 1;
	if (want == "middle")
		return button == 2;
	if (want == "right")
		return button == 3;
	if (want == "x1")
		return button == 8;
	if (want == "x2")
		return button == 9;
	return false;
}

void ZoominatorController::processXInput2Events()
{
	if (!xiDisplay)
		return;

	while (XPending(xiDisplay)) {
		XEvent ev;
		XNextEvent(xiDisplay, &ev);

		if (linuxWheelZoomGrabInstalled && (ev.type == ButtonPress || ev.type == ButtonRelease)) {
			const int button = ev.xbutton.button;
			const bool down = ev.type == ButtonPress;
			if (button == 9) {
				if (down) {
					beginWheelZoomGesture();
				} else {
					finishWheelZoomGesture();
				}
			} else if (down && (button == 4 || button == 5)) {
				adjustActiveZoomFromWheel(button == 4 ? 1 : -1);
			}
			continue;
		}

		if (ev.xcookie.type != GenericEvent || ev.xcookie.extension != xiOpcode)
			continue;
		if (!XGetEventData(xiDisplay, &ev.xcookie))
			continue;

		const int evtype = ev.xcookie.evtype;

		if (evtype == XI_RawKeyPress || evtype == XI_RawKeyRelease) {
			XIRawEvent *raw = (XIRawEvent *)ev.xcookie.data;
			const int keycode = raw->detail;
			KeySym sym = XkbKeycodeToKeysym(xiDisplay, keycode, 0, 0);
			const bool down = (evtype == XI_RawKeyPress);

			if (followToggleHkValid && down && followToggleHotkeyVk != 0 &&
			    vk_matches((int)sym, followToggleHotkeyVk) &&
			    mods_current(followToggleModCtrl, followToggleModAlt, followToggleModShift,
					 followToggleModWin)) {
				toggleFollowMouseRuntime();
			}

			if (down && manualZoomInBinding.valid && vk_matches((int)sym, manualZoomInBinding.vk) &&
			    mods_current(manualZoomInBinding.ctrl, manualZoomInBinding.alt, manualZoomInBinding.shift,
					 manualZoomInBinding.win)) {
				manualZoomIn();
			}
			if (down && manualZoomOutBinding.valid && vk_matches((int)sym, manualZoomOutBinding.vk) &&
			    mods_current(manualZoomOutBinding.ctrl, manualZoomOutBinding.alt, manualZoomOutBinding.shift,
					 manualZoomOutBinding.win)) {
				manualZoomOut();
			}
			if (down && focusToggleBinding.valid && vk_matches((int)sym, focusToggleBinding.vk) &&
			    mods_current(focusToggleBinding.ctrl, focusToggleBinding.alt, focusToggleBinding.shift,
					 focusToggleBinding.win)) {
				toggleFocusRuntime();
			}
			if (down && focusOnBinding.valid && vk_matches((int)sym, focusOnBinding.vk) &&
			    mods_current(focusOnBinding.ctrl, focusOnBinding.alt, focusOnBinding.shift, focusOnBinding.win)) {
				setFocusRuntime(true);
			}
			if (down && focusOffBinding.valid && vk_matches((int)sym, focusOffBinding.vk) &&
			    mods_current(focusOffBinding.ctrl, focusOffBinding.alt, focusOffBinding.shift, focusOffBinding.win)) {
				setFocusRuntime(false);
			}

			if (triggerType == "auto" && down && autoOnTyping && !is_modifier_vk((int)sym)) {
				const ModifierState ms = current_modifiers();
				const bool shortcut = ms.leftCtrl || ms.rightCtrl || ms.leftAlt || ms.rightAlt ||
						      ms.leftWin || ms.rightWin;
				if (!shortcut)
					onAutoActivity();
			}

			if (hkValid && triggerType == "keyboard") {
				if (hotkeyVk == 0) {
					if (is_modifier_vk((int)sym) && is_wanted_modifier_vk((int)sym, this)) {
						const bool matchNow = mods_current(modCtrl, modAlt, modShift, modWin,
										   modLeftCtrl, modRightCtrl,
										   modLeftAlt, modRightAlt,
										   modLeftShift, modRightShift,
										   modLeftWin, modRightWin);
						if (hotkeyMode == "toggle") {
							if (down && matchNow)
								onTriggerDown();
						} else {
							if (down && matchNow)
								onTriggerDown();
							if (!down && zoomPressed && !matchNow)
								onTriggerUp();
						}
					}
				} else if (vk_matches((int)sym, hotkeyVk)) {
					if (mods_current(modCtrl, modAlt, modShift, modWin, modLeftCtrl, modRightCtrl,
							 modLeftAlt, modRightAlt, modLeftShift, modRightShift,
							 modLeftWin, modRightWin)) {
						if (hotkeyMode == "toggle") {
							if (down)
								onTriggerDown();
						} else {
							if (down)
								onTriggerDown();
							if (!down)
								onTriggerUp();
						}
					}
				}
			}
		} else if (evtype == XI_RawButtonPress || evtype == XI_RawButtonRelease) {
			XIRawEvent *raw = (XIRawEvent *)ev.xcookie.data;
			const int button = raw->detail;
			const bool down = (evtype == XI_RawButtonPress);
			const bool up = (evtype == XI_RawButtonRelease);

			/* The passive X11 grab below supplies these as ordinary button
			 * events while Mouse5 is held. Ignoring their raw copies prevents
			 * a wheel notch from being applied twice or without Mouse5. */
			if (linuxWheelZoomGrabInstalled && (button == 4 || button == 5 || button == 9)) {
				XFreeEventData(xiDisplay, &ev.xcookie);
				continue;
			}

			if (down)
				captureMarkerClickPosition();

			if (down && triggerType == "auto" && autoOnClick && button >= 1 && button <= 3)
				onAutoActivity();

			if (button >= 4 && button <= 7) {
				XFreeEventData(xiDisplay, &ev.xcookie);
				continue;
			}

			if (triggerType == "mouse" && (down || up)) {
				if (mods_current(modCtrl, modAlt, modShift, modWin, modLeftCtrl, modRightCtrl,
						 modLeftAlt, modRightAlt, modLeftShift, modRightShift, modLeftWin,
						 modRightWin)) {
					if (linux_button_matches(button, mouseButton)) {
						if (hotkeyMode == "toggle") {
							if (down)
								onTriggerDown();
						} else {
							zoomAdjustButtonHeld = down;
							if (down)
								onTriggerDown();
							if (up)
								onTriggerUp();
						}
					}
				}
			}
		}

		XFreeEventData(xiDisplay, &ev.xcookie);
	}
}
#endif

bool ZoominatorController::modsMatch() const
{
	return mods_current(modCtrl, modAlt, modShift, modWin, modLeftCtrl, modRightCtrl, modLeftAlt, modRightAlt,
			    modLeftShift, modRightShift, modLeftWin, modRightWin);
}

bool ZoominatorController::triggerMatchesMouse(unsigned int msg, unsigned short mouseData) const
{
#ifdef _WIN32
	const QString b = mouseButton;
	if (b == "left")
		return msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP;
	if (b == "right")
		return msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP;
	if (b == "middle")
		return msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP;
	if (b == "x1")
		return (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) && mouseData == XBUTTON1;
	if (b == "x2")
		return (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) && mouseData == XBUTTON2;
	return false;
#elif defined(__linux__)
	(void)mouseData;
	return linux_button_matches((int)msg, mouseButton);
#else
	(void)msg;
	(void)mouseData;
	return false;
#endif
}


bool ZoominatorController::needsKeyboardHook() const
{
	return triggerType == "keyboard" || followToggleHkValid || manualZoomInBinding.valid ||
	       manualZoomOutBinding.valid || focusToggleBinding.valid || focusOnBinding.valid || focusOffBinding.valid ||
	       (triggerType == "auto" && autoOnTyping) || keyDisplayEnabled;
}

bool ZoominatorController::needsMouseHook() const
{
	return triggerType == "mouse" || showCursorMarker || wheelZoomEnabled || (triggerType == "auto" && autoOnClick);
}

void ZoominatorController::installHooks()
{
	if (!needsKeyboardHook() && !needsMouseHook())
		return;

#ifdef _WIN32
	g_ctl = this;

	if (needsKeyboardHook() && !keyboardHook) {
		keyboardHook = (void *)SetWindowsHookExW(WH_KEYBOARD_LL, kb_hook_proc, GetModuleHandleW(nullptr), 0);
	}
	if (needsMouseHook() && !mouseHook) {
		mouseHook = (void *)SetWindowsHookExW(WH_MOUSE_LL, mouse_hook_proc, GetModuleHandleW(nullptr), 0);
	}
#elif defined(__APPLE__)
	g_ctl = this;

	if (!eventTap) {
		CGEventMask mask = 0;
		if (needsKeyboardHook()) {
			mask |= CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) |
				CGEventMaskBit(kCGEventFlagsChanged);
		}
		if (needsMouseHook()) {
			mask |= CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp) |
				CGEventMaskBit(kCGEventRightMouseDown) | CGEventMaskBit(kCGEventRightMouseUp) |
				CGEventMaskBit(kCGEventOtherMouseDown) | CGEventMaskBit(kCGEventOtherMouseUp);
		}
		eventTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly,
					    mask, eventTapCallback, this);
		if (eventTap) {
			runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
			CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopCommonModes);
			CGEventTapEnable(eventTap, true);
		} else {
			blog(LOG_WARNING,
			     "[Zoominator] Failed to create CGEventTap. "
			     "Grant Accessibility permission to OBS in System Settings > Privacy & Security > Accessibility.");
		}
	}
#elif defined(__linux__)
	g_ctl = this;

	const QString platform = QGuiApplication::platformName();
	if (platform.startsWith(QStringLiteral("wayland"), Qt::CaseInsensitive)) {
		blog(LOG_WARNING,
		     "[Zoominator] Native Wayland session detected. XInput2 hooks and global cursor tracking are disabled. "
		     "The Global Shortcuts portal can provide hotkeys, but Wayland has no passive global cursor-position "
		     "portal; full tracking requires a compositor-supported input-capture workflow.");
		return;
	}

	if (!xiDisplay) {
		xiDisplay = XOpenDisplay(nullptr);
		if (!xiDisplay) {
			blog(LOG_WARNING, "[Zoominator] Failed to open X11 display for input hooks.");
			return;
		}

		int event, error;
		if (!XQueryExtension(xiDisplay, "XInputExtension", &xiOpcode, &event, &error)) {
			blog(LOG_WARNING, "[Zoominator] XInput2 extension not available.");
			XCloseDisplay(xiDisplay);
			xiDisplay = nullptr;
			return;
		}

		int major = 2, minor = 0;
		if (XIQueryVersion(xiDisplay, &major, &minor) != Success) {
			blog(LOG_WARNING, "[Zoominator] XInput2 version query failed.");
			XCloseDisplay(xiDisplay);
			xiDisplay = nullptr;
			return;
		}

		unsigned char mask_bits[(XI_LASTEVENT + 7) / 8] = {};
		XIEventMask evmask;
		evmask.deviceid = XIAllMasterDevices;
		evmask.mask_len = sizeof(mask_bits);
		evmask.mask = mask_bits;
		if (needsKeyboardHook()) {
			XISetMask(mask_bits, XI_RawKeyPress);
			XISetMask(mask_bits, XI_RawKeyRelease);
		}
		if (needsMouseHook()) {
			XISetMask(mask_bits, XI_RawButtonPress);
			XISetMask(mask_bits, XI_RawButtonRelease);
		}
		XISelectEvents(xiDisplay, DefaultRootWindow(xiDisplay), &evmask, 1);

		if (usesWheelZoomGesture()) {
			/* A passive grab becomes active only while Mouse5 is held. During
			 * that short interval wheel events come exclusively to this plugin,
			 * so the application below the cursor does not scroll. X11 releases
			 * the grab on Mouse5 release or if this display connection closes. */
			XGrabButton(xiDisplay, 9, AnyModifier, DefaultRootWindow(xiDisplay), False,
				    ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, (Window)X11_None,
				    (Cursor)X11_None);
			XSync(xiDisplay, False);
			linuxWheelZoomGrabInstalled = true;
			blog(LOG_INFO, "[Zoominator] Mouse5 + wheel zoom grab installed.");
		}
		XFlush(xiDisplay);

		int fd = ConnectionNumber(xiDisplay);
		xiNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
		connect(xiNotifier, &QSocketNotifier::activated, this, &ZoominatorController::processXInput2Events);

		blog(LOG_INFO, "[Zoominator] XInput2 global input hooks installed (XI %d.%d).", major, minor);
	}
#endif
}

void ZoominatorController::uninstallHooks()
{
#ifdef _WIN32
	if (keyboardHook) {
		UnhookWindowsHookEx((HHOOK)keyboardHook);
		keyboardHook = nullptr;
	}
	if (mouseHook) {
		UnhookWindowsHookEx((HHOOK)mouseHook);
		mouseHook = nullptr;
	}
	zoomAdjustButtonHeld = false;
	zoomAdjustedDuringButtonHold = false;
	wheelDeltaRemainder = 0;
	g_ctl = nullptr;
#elif defined(__APPLE__)
	if (runLoopSource) {
		CFRunLoopRemoveSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopCommonModes);
		CFRelease(runLoopSource);
		runLoopSource = nullptr;
	}
	if (eventTap) {
		CGEventTapEnable(eventTap, false);
		CFRelease(eventTap);
		eventTap = nullptr;
	}
	g_ctl = nullptr;
#elif defined(__linux__)
	if (xiNotifier) {
		delete xiNotifier;
		xiNotifier = nullptr;
	}
	if (xiDisplay) {
		if (linuxWheelZoomGrabInstalled) {
			XUngrabButton(xiDisplay, 9, AnyModifier, DefaultRootWindow(xiDisplay));
			XUngrabPointer(xiDisplay, CurrentTime);
			XSync(xiDisplay, False);
			linuxWheelZoomGrabInstalled = false;
		}
		XCloseDisplay(xiDisplay);
		xiDisplay = nullptr;
	}
	zoomAdjustButtonHeld = false;
	zoomAdjustedDuringButtonHold = false;
	wheelDeltaRemainder = 0;
	g_ctl = nullptr;
#endif
}

#if !defined(_WIN32)
/* The key display is only implemented for Windows. */
void ZoominatorController::handleKeyForDisplay(int) {}
#endif
