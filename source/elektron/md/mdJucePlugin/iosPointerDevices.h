#pragma once

// iOS: whether a mouse or trackpad is connected (iosPointerDevices.mm). The MD/MM panel works like the hardware
// with fingers: FUNCTION is held with one finger while another presses a key. A single pointer cannot hold two keys,
// so the editor makes the printed FUNCTION labels clickable only while a pointing device is there.
//
// Message thread (which is the main thread on iOS) only.

#include <functional>

namespace tusPointer
{
	// GCMouse lists a mouse, or a trackpad/mouse pointer (UITouchTypeIndirectPointer, never Apple Pencil) has
	// hovered over the app since it last became active. GEARMULATOR_IOS_POINTER=0/1 forces the answer (tests).
	bool pointingDeviceConnected();

	// Called whenever pointingDeviceConnected() may have changed. One callback; an empty function removes it.
	void setChangedCallback(std::function<void()> _callback);
}
