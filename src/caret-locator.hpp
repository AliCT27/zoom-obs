#pragma once

/* Finds where the text caret (the blinking typing cursor) is on screen so the
 * zoom can follow what is being typed instead of the mouse. Windows only; on
 * other systems every call is a no-op and latestLogical() returns false.
 *
 * A background thread asks the focused application for its caret while the
 * user is typing: first through the classic system caret (Notepad, most native
 * edit boxes), then through UI Automation text patterns (Word, browsers with
 * accessibility on, many modern apps). Apps that draw their own caret without
 * exposing it (some editors and games) simply report nothing, and the caller
 * keeps following the mouse. */
namespace zoominator_caret {

void start();
void stop();

/* Call whenever a typing key is pressed; wakes the worker up. Any thread. */
void noteTyping();

/* Last caret position in the same logical coordinates as QCursor::pos(), if one
 * was found no longer than maxAgeMs ago. Main thread. */
bool latestLogical(int &x, int &y, int maxAgeMs);

} // namespace zoominator_caret
