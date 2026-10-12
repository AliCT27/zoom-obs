/* Hotkeys: turns the configured key sequences into virtual-key matches. */

#include "zoominator-internal.hpp"

int qtKeyToVk(int qtKey)
{
#if defined(__APPLE__)
	if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
		static const int map[] = {kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E, kVK_ANSI_F,
					  kVK_ANSI_G, kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J, kVK_ANSI_K, kVK_ANSI_L,
					  kVK_ANSI_M, kVK_ANSI_N, kVK_ANSI_O, kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R,
					  kVK_ANSI_S, kVK_ANSI_T, kVK_ANSI_U, kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X,
					  kVK_ANSI_Y, kVK_ANSI_Z};
		return map[qtKey - Qt::Key_A];
	}
	if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
		static const int map[] = {kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
					  kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9};
		return map[qtKey - Qt::Key_0];
	}
	if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F20) {
		static const int map[] = {kVK_F1,  kVK_F2,  kVK_F3,  kVK_F4,  kVK_F5,  kVK_F6,  kVK_F7,
					  kVK_F8,  kVK_F9,  kVK_F10, kVK_F11, kVK_F12, kVK_F13, kVK_F14,
					  kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20};
		return map[qtKey - Qt::Key_F1];
	}
	switch (qtKey) {
	case Qt::Key_Space:
		return kVK_Space;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		return kVK_Return;
	case Qt::Key_Escape:
		return kVK_Escape;
	case Qt::Key_Tab:
		return kVK_Tab;
	case Qt::Key_Backspace:
		return kVK_Delete;
	case Qt::Key_Delete:
		return kVK_ForwardDelete;
	case Qt::Key_Left:
		return kVK_LeftArrow;
	case Qt::Key_Right:
		return kVK_RightArrow;
	case Qt::Key_Up:
		return kVK_UpArrow;
	case Qt::Key_Down:
		return kVK_DownArrow;
	case Qt::Key_Home:
		return kVK_Home;
	case Qt::Key_End:
		return kVK_End;
	case Qt::Key_PageUp:
		return kVK_PageUp;
	case Qt::Key_PageDown:
		return kVK_PageDown;
	case Qt::Key_CapsLock:
		return kVK_CapsLock;
	case Qt::Key_Shift:
		return kVK_Shift;
	case Qt::Key_Control:
		return kVK_Control;
	case Qt::Key_Alt:
		return kVK_Option;
	case Qt::Key_Meta:
		return kVK_Command;
	case Qt::Key_Semicolon:
		return kVK_ANSI_Semicolon;
	case Qt::Key_Equal:
		return kVK_ANSI_Equal;
	case Qt::Key_Comma:
		return kVK_ANSI_Comma;
	case Qt::Key_Minus:
		return kVK_ANSI_Minus;
	case Qt::Key_Period:
		return kVK_ANSI_Period;
	case Qt::Key_Slash:
		return kVK_ANSI_Slash;
	case Qt::Key_QuoteLeft:
		return kVK_ANSI_Grave;
	case Qt::Key_BracketLeft:
		return kVK_ANSI_LeftBracket;
	case Qt::Key_Backslash:
		return kVK_ANSI_Backslash;
	case Qt::Key_BracketRight:
		return kVK_ANSI_RightBracket;
	default:
		break;
	}
	return 0;
#elif defined(__linux__)
	if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
		return XK_a + (qtKey - Qt::Key_A);
	if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
		return XK_0 + (qtKey - Qt::Key_0);
	if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
		return XK_F1 + (qtKey - Qt::Key_F1);

	switch (qtKey) {
	case Qt::Key_Space:
		return XK_space;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		return XK_Return;
	case Qt::Key_Escape:
		return XK_Escape;
	case Qt::Key_Tab:
		return XK_Tab;
	case Qt::Key_Backspace:
		return XK_BackSpace;
	case Qt::Key_Delete:
		return XK_Delete;
	case Qt::Key_Left:
		return XK_Left;
	case Qt::Key_Right:
		return XK_Right;
	case Qt::Key_Up:
		return XK_Up;
	case Qt::Key_Down:
		return XK_Down;
	case Qt::Key_Home:
		return XK_Home;
	case Qt::Key_End:
		return XK_End;
	case Qt::Key_PageUp:
		return XK_Page_Up;
	case Qt::Key_PageDown:
		return XK_Page_Down;
	case Qt::Key_Insert:
		return XK_Insert;
	case Qt::Key_Print:
		return XK_Print;
	case Qt::Key_Pause:
		return XK_Pause;
	case Qt::Key_CapsLock:
		return XK_Caps_Lock;
	case Qt::Key_Shift:
		return XK_Shift_L;
	case Qt::Key_Control:
		return XK_Control_L;
	case Qt::Key_Alt:
		return XK_Alt_L;
	case Qt::Key_Meta:
		return XK_Super_L;
	case Qt::Key_Semicolon:
		return XK_semicolon;
	case Qt::Key_Equal:
		return XK_equal;
	case Qt::Key_Comma:
		return XK_comma;
	case Qt::Key_Minus:
		return XK_minus;
	case Qt::Key_Period:
		return XK_period;
	case Qt::Key_Slash:
		return XK_slash;
	case Qt::Key_QuoteLeft:
		return XK_grave;
	case Qt::Key_BracketLeft:
		return XK_bracketleft;
	case Qt::Key_Backslash:
		return XK_backslash;
	case Qt::Key_BracketRight:
		return XK_bracketright;
	default:
		break;
	}

	if (qtKey >= 0x20 && qtKey <= 0x7E)
		return qtKey;

	return 0;
#else
	if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
		return 'A' + (qtKey - Qt::Key_A);
	if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
		return '0' + (qtKey - Qt::Key_0);
	if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
		return VK_F1 + (qtKey - Qt::Key_F1);

	switch (qtKey) {
	case Qt::Key_Space:
		return VK_SPACE;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		return VK_RETURN;
	case Qt::Key_Escape:
		return VK_ESCAPE;
	case Qt::Key_Tab:
		return VK_TAB;
	case Qt::Key_Backspace:
		return VK_BACK;
	case Qt::Key_Left:
		return VK_LEFT;
	case Qt::Key_Right:
		return VK_RIGHT;
	case Qt::Key_Up:
		return VK_UP;
	case Qt::Key_Down:
		return VK_DOWN;
	case Qt::Key_Insert:
		return VK_INSERT;
	case Qt::Key_Delete:
		return VK_DELETE;
	case Qt::Key_Home:
		return VK_HOME;
	case Qt::Key_End:
		return VK_END;
	case Qt::Key_PageUp:
		return VK_PRIOR;
	case Qt::Key_PageDown:
		return VK_NEXT;
	case Qt::Key_Print:
		return VK_SNAPSHOT;
	case Qt::Key_Pause:
		return VK_PAUSE;
	case Qt::Key_CapsLock:
		return VK_CAPITAL;
	case Qt::Key_Clear:
		return VK_CLEAR;
	case Qt::Key_Shift:
		return VK_SHIFT;
	case Qt::Key_Control:
		return VK_CONTROL;
	case Qt::Key_Alt:
		return VK_MENU;
	case Qt::Key_Meta:
		return VK_LWIN;
	case Qt::Key_Semicolon:
		return VK_OEM_1;
	case Qt::Key_Plus:
		return VK_OEM_PLUS;
	case Qt::Key_Comma:
		return VK_OEM_COMMA;
	case Qt::Key_Minus:
		return VK_OEM_MINUS;
	case Qt::Key_Period:
		return VK_OEM_PERIOD;
	case Qt::Key_Slash:
		return VK_OEM_2;
	case Qt::Key_QuoteLeft:
		return VK_OEM_3;
	case Qt::Key_BracketLeft:
		return VK_OEM_4;
	case Qt::Key_Backslash:
		return VK_OEM_5;
	case Qt::Key_BracketRight:
		return VK_OEM_6;

	default:
		break;
	}

	if (qtKey >= 0x20 && qtKey <= 0x7E)
		return qtKey;

	return 0;
#endif
}

ZoominatorController::HotkeyBinding ZoominatorController::makeHotkeyBinding(const QString &sequence)
{
	HotkeyBinding b;
	const QKeySequence seq(sequence);
	if (seq.isEmpty())
		return b;

	const QKeyCombination kc = seq[0];
	const auto mods = kc.keyboardModifiers();
	const int key = int(kc.key());
	/* A modifier on its own is not a usable key. */
	if (key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_AltGr || key == Qt::Key_Shift ||
	    key == Qt::Key_Meta || key == 0)
		return b;

	b.vk = qtKeyToVk(key);
	b.ctrl = mods.testFlag(Qt::ControlModifier);
	b.alt = mods.testFlag(Qt::AltModifier);
	b.shift = mods.testFlag(Qt::ShiftModifier);
	b.win = mods.testFlag(Qt::MetaModifier);
	b.valid = b.vk != 0;
	return b;
}

void ZoominatorController::rebuildTriggersFromSettings()
{
	autoSuspended = false;
	manualZoomInBinding = makeHotkeyBinding(manualZoomInSequence);
	manualZoomOutBinding = makeHotkeyBinding(manualZoomOutSequence);
	hkValid = false;
	hotkeyVk = 0;
	followToggleHkValid = false;
	followToggleHotkeyVk = 0;
	followToggleModCtrl = false;
	followToggleModAlt = false;
	followToggleModShift = false;
	followToggleModWin = false;

	QKeySequence followSeq(followToggleHotkeySequence);
	if (!followSeq.isEmpty()) {
		const QKeyCombination kc = followSeq[0];
		const auto mods = kc.keyboardModifiers();
		const int key = int(kc.key());
		followToggleHotkeyVk = qtKeyToVk(key);
		followToggleModCtrl = mods.testFlag(Qt::ControlModifier);
		followToggleModAlt = mods.testFlag(Qt::AltModifier);
		followToggleModShift = mods.testFlag(Qt::ShiftModifier);
		followToggleModWin = mods.testFlag(Qt::MetaModifier);

#ifdef _WIN32
		const bool keyIsModifier = (followToggleHotkeyVk == VK_CONTROL || followToggleHotkeyVk == VK_LCONTROL ||
					    followToggleHotkeyVk == VK_RCONTROL || followToggleHotkeyVk == VK_MENU ||
					    followToggleHotkeyVk == VK_LMENU || followToggleHotkeyVk == VK_RMENU ||
					    followToggleHotkeyVk == VK_SHIFT || followToggleHotkeyVk == VK_LSHIFT ||
					    followToggleHotkeyVk == VK_RSHIFT || followToggleHotkeyVk == VK_LWIN ||
					    followToggleHotkeyVk == VK_RWIN);
#elif defined(__APPLE__)
		const bool keyIsModifier =
			(followToggleHotkeyVk == kVK_Control || followToggleHotkeyVk == kVK_RightControl ||
			 followToggleHotkeyVk == kVK_Option || followToggleHotkeyVk == kVK_RightOption ||
			 followToggleHotkeyVk == kVK_Shift || followToggleHotkeyVk == kVK_RightShift ||
			 followToggleHotkeyVk == kVK_Command || followToggleHotkeyVk == kVK_RightCommand);
#elif defined(__linux__)
		const bool keyIsModifier = (followToggleHotkeyVk == XK_Control_L ||
					    followToggleHotkeyVk == XK_Control_R || followToggleHotkeyVk == XK_Alt_L ||
					    followToggleHotkeyVk == XK_Alt_R || followToggleHotkeyVk == XK_Shift_L ||
					    followToggleHotkeyVk == XK_Shift_R || followToggleHotkeyVk == XK_Super_L ||
					    followToggleHotkeyVk == XK_Super_R);
#else
		const bool keyIsModifier = false;
#endif

		if (keyIsModifier && mods == Qt::NoModifier) {
#ifdef _WIN32
			followToggleModCtrl = (followToggleHotkeyVk == VK_CONTROL ||
					       followToggleHotkeyVk == VK_LCONTROL ||
					       followToggleHotkeyVk == VK_RCONTROL);
			followToggleModAlt = (followToggleHotkeyVk == VK_MENU || followToggleHotkeyVk == VK_LMENU ||
					      followToggleHotkeyVk == VK_RMENU);
			followToggleModShift = (followToggleHotkeyVk == VK_SHIFT || followToggleHotkeyVk == VK_LSHIFT ||
						followToggleHotkeyVk == VK_RSHIFT);
			followToggleModWin = (followToggleHotkeyVk == VK_LWIN || followToggleHotkeyVk == VK_RWIN);
#elif defined(__APPLE__)
			followToggleModCtrl =
				(followToggleHotkeyVk == kVK_Control || followToggleHotkeyVk == kVK_RightControl);
			followToggleModAlt =
				(followToggleHotkeyVk == kVK_Option || followToggleHotkeyVk == kVK_RightOption);
			followToggleModShift =
				(followToggleHotkeyVk == kVK_Shift || followToggleHotkeyVk == kVK_RightShift);
			followToggleModWin =
				(followToggleHotkeyVk == kVK_Command || followToggleHotkeyVk == kVK_RightCommand);
#elif defined(__linux__)
			followToggleModCtrl =
				(followToggleHotkeyVk == XK_Control_L || followToggleHotkeyVk == XK_Control_R);
			followToggleModAlt = (followToggleHotkeyVk == XK_Alt_L || followToggleHotkeyVk == XK_Alt_R);
			followToggleModShift =
				(followToggleHotkeyVk == XK_Shift_L || followToggleHotkeyVk == XK_Shift_R);
			followToggleModWin = (followToggleHotkeyVk == XK_Super_L || followToggleHotkeyVk == XK_Super_R);
#else
			followToggleHotkeyVk = 0;
#endif
		}

		followToggleHkValid = (followToggleHotkeyVk != 0) &&
				      (followToggleModCtrl || followToggleModAlt || followToggleModShift ||
				       followToggleModWin || key != 0);
	}

	if (triggerType != "mouse" && triggerType != "auto")
		triggerType = "keyboard";

	if (triggerType == "keyboard") {
		QKeySequence seq(hotkeySequence);
		if (seq.isEmpty()) {
			hotkeyVk = 0;
			hkValid = (modCtrl || modAlt || modShift || modWin || modLeftCtrl || modRightCtrl ||
				   modLeftAlt || modRightAlt || modLeftShift || modRightShift || modLeftWin ||
				   modRightWin);
			if (debug)
				blog(LOG_INFO,
				     "[Zoominator] Hotkey empty; using modifier-only trigger (ctrl=%d alt=%d shift=%d win=%d valid=%d)",
				     modCtrl ? 1 : 0, modAlt ? 1 : 0, modShift ? 1 : 0, modWin ? 1 : 0,
				     hkValid ? 1 : 0);
		} else {
			const QKeyCombination kc = seq[0];
			const auto mods = kc.keyboardModifiers();
			const int key = int(kc.key());
			hotkeyVk = qtKeyToVk(key);

			const bool noMods = (mods == Qt::NoModifier);
#ifdef _WIN32
			const bool keyIsModifier =
				(hotkeyVk == VK_CONTROL || hotkeyVk == VK_LCONTROL || hotkeyVk == VK_RCONTROL ||
				 hotkeyVk == VK_MENU || hotkeyVk == VK_LMENU || hotkeyVk == VK_RMENU ||
				 hotkeyVk == VK_SHIFT || hotkeyVk == VK_LSHIFT || hotkeyVk == VK_RSHIFT ||
				 hotkeyVk == VK_LWIN || hotkeyVk == VK_RWIN);
#elif defined(__APPLE__)
			const bool keyIsModifier = (hotkeyVk == kVK_Control || hotkeyVk == kVK_RightControl ||
						    hotkeyVk == kVK_Option || hotkeyVk == kVK_RightOption ||
						    hotkeyVk == kVK_Shift || hotkeyVk == kVK_RightShift ||
						    hotkeyVk == kVK_Command || hotkeyVk == kVK_RightCommand);
#elif defined(__linux__)
			const bool keyIsModifier = (hotkeyVk == XK_Control_L || hotkeyVk == XK_Control_R ||
						    hotkeyVk == XK_Alt_L || hotkeyVk == XK_Alt_R ||
						    hotkeyVk == XK_Shift_L || hotkeyVk == XK_Shift_R ||
						    hotkeyVk == XK_Super_L || hotkeyVk == XK_Super_R);
#else
			const bool keyIsModifier = (key == Qt::Key_Control || key == Qt::Key_Shift ||
						    key == Qt::Key_Alt || key == Qt::Key_Meta);
#endif
			if (noMods && keyIsModifier) {
#ifdef _WIN32
				modCtrl =
					(hotkeyVk == VK_CONTROL || hotkeyVk == VK_LCONTROL || hotkeyVk == VK_RCONTROL);
				modAlt = (hotkeyVk == VK_MENU || hotkeyVk == VK_LMENU || hotkeyVk == VK_RMENU);
				modShift = (hotkeyVk == VK_SHIFT || hotkeyVk == VK_LSHIFT || hotkeyVk == VK_RSHIFT);
				modWin = (hotkeyVk == VK_LWIN || hotkeyVk == VK_RWIN);
				modLeftCtrl = (hotkeyVk == VK_LCONTROL);
				modRightCtrl = (hotkeyVk == VK_RCONTROL);
				modLeftAlt = (hotkeyVk == VK_LMENU);
				modRightAlt = (hotkeyVk == VK_RMENU);
				modLeftShift = (hotkeyVk == VK_LSHIFT);
				modRightShift = (hotkeyVk == VK_RSHIFT);
				modLeftWin = (hotkeyVk == VK_LWIN);
				modRightWin = (hotkeyVk == VK_RWIN);
#elif defined(__APPLE__)
				modCtrl = (hotkeyVk == kVK_Control || hotkeyVk == kVK_RightControl);
				modAlt = (hotkeyVk == kVK_Option || hotkeyVk == kVK_RightOption);
				modShift = (hotkeyVk == kVK_Shift || hotkeyVk == kVK_RightShift);
				modWin = (hotkeyVk == kVK_Command || hotkeyVk == kVK_RightCommand);
				modLeftCtrl = (hotkeyVk == kVK_Control);
				modRightCtrl = (hotkeyVk == kVK_RightControl);
				modLeftAlt = (hotkeyVk == kVK_Option);
				modRightAlt = (hotkeyVk == kVK_RightOption);
				modLeftShift = (hotkeyVk == kVK_Shift);
				modRightShift = (hotkeyVk == kVK_RightShift);
				modLeftWin = (hotkeyVk == kVK_Command);
				modRightWin = (hotkeyVk == kVK_RightCommand);
#elif defined(__linux__)
				modCtrl = (hotkeyVk == XK_Control_L || hotkeyVk == XK_Control_R);
				modAlt = (hotkeyVk == XK_Alt_L || hotkeyVk == XK_Alt_R);
				modShift = (hotkeyVk == XK_Shift_L || hotkeyVk == XK_Shift_R);
				modWin = (hotkeyVk == XK_Super_L || hotkeyVk == XK_Super_R);
				modLeftCtrl = (hotkeyVk == XK_Control_L);
				modRightCtrl = (hotkeyVk == XK_Control_R);
				modLeftAlt = (hotkeyVk == XK_Alt_L);
				modRightAlt = (hotkeyVk == XK_Alt_R);
				modLeftShift = (hotkeyVk == XK_Shift_L);
				modRightShift = (hotkeyVk == XK_Shift_R);
				modLeftWin = (hotkeyVk == XK_Super_L);
				modRightWin = (hotkeyVk == XK_Super_R);
#else
				modCtrl = (key == Qt::Key_Control);
				modAlt = (key == Qt::Key_Alt);
				modShift = (key == Qt::Key_Shift);
				modWin = (key == Qt::Key_Meta);
				modLeftCtrl = false;
				modRightCtrl = false;
				modLeftAlt = false;
				modRightAlt = false;
				modLeftShift = false;
				modRightShift = false;
				modLeftWin = false;
				modRightWin = false;
#endif
				hotkeyVk = 0;
				hkValid = (modCtrl || modAlt || modShift || modWin || modLeftCtrl || modRightCtrl ||
					   modLeftAlt || modRightAlt || modLeftShift || modRightShift || modLeftWin ||
					   modRightWin);
				if (debug)
					blog(LOG_INFO,
					     "[Zoominator] Single-modifier hotkey parsed; using modifier-only trigger (ctrl=%d alt=%d shift=%d win=%d valid=%d)",
					     modCtrl ? 1 : 0, modAlt ? 1 : 0, modShift ? 1 : 0, modWin ? 1 : 0,
					     hkValid ? 1 : 0);
			} else {
				modCtrl = mods.testFlag(Qt::ControlModifier);
				modAlt = mods.testFlag(Qt::AltModifier);
				modShift = mods.testFlag(Qt::ShiftModifier);
				modWin = mods.testFlag(Qt::MetaModifier);
				modLeftCtrl = false;
				modRightCtrl = false;
				modLeftAlt = false;
				modRightAlt = false;
				modLeftShift = false;
				modRightShift = false;
				modLeftWin = false;
				modRightWin = false;

				hkValid = (hotkeyVk != 0);
				if (debug)
					blog(LOG_INFO,
					     "[Zoominator] Hotkey parsed: '%s' vk=%d ctrl=%d alt=%d shift=%d win=%d valid=%d",
					     hotkeySequence.toUtf8().constData(), hotkeyVk, modCtrl ? 1 : 0,
					     modAlt ? 1 : 0, modShift ? 1 : 0, modWin ? 1 : 0, hkValid ? 1 : 0);
			}
		}
	} else {
		hkValid = true;
	}
}

