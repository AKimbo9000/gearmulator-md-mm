// Pointing-device detection on iOS, see iosPointerDevices.h.
//
// Two sources of evidence, since neither alone is reliable:
// - GameController's GCMouse: the documented connect/disconnect API (iOS 14+). Users report that some iPadOS
//   versions do not list the Magic Keyboard trackpad.
// - UIKit: a hover recognizer on the app's window restricted to UITouchTypeIndirectPointer, which fires when a
//   trackpad or mouse pointer moves over the app and never for fingers or Apple Pencil hover. JUCE's own hover
//   recognizer cannot be used for this: it is not restricted and reports Pencil hover too.
// The UIKit evidence lasts until the app goes to the background or GCMouse reports a disconnect, and comes back as
// soon as the pointer moves again.
//
// Built without ARC, like the other .mm files of this target.

#include "iosPointerDevices.h"

#include "iosEngine.h"

#include <cstdlib>
#include <string>

#import <GameController/GameController.h>
#import <UIKit/UIKit.h>

namespace
{
	std::function<void()> g_callback;
	bool g_hoverSeen = false;
	bool g_lastReported = false;
	bool g_started = false;

	int forcedState()
	{
		if(const char* v = std::getenv("GEARMULATOR_IOS_POINTER"))
			return std::atoi(v) != 0 ? 1 : 0;
		return -1;
	}

	bool gcMouseListed()
	{
		if(@available(iOS 14.0, *))
			return GCMouse.mice.count > 0;
		return false;
	}

	void report(const char* _why)
	{
		const bool now = tusPointer::pointingDeviceConnected();
		if(now == g_lastReported)
			return;
		g_lastReported = now;
		tusEngine::log(std::string("pointer: ") + (now ? "connected" : "not connected") + " (" + _why + ", GCMouse " +
			std::to_string(gcMouseListed() ? 1 : 0) + ", hover " + (g_hoverSeen ? "1" : "0") + ")");
		if(g_callback)
			g_callback();
	}
}

@interface TusPointerWatcher : NSObject <UIGestureRecognizerDelegate>
@property (nonatomic, assign) UIWindow* window;
@property (nonatomic, retain) UIHoverGestureRecognizer* hover;
@end

@implementation TusPointerWatcher
- (void)onHover:(UIHoverGestureRecognizer*)_recognizer
{
	if(g_hoverSeen)
		return;
	g_hoverSeen = true;
	report("pointer hover");
}

// never interfere with JUCE's own recognizers or touches
- (BOOL)gestureRecognizer:(UIGestureRecognizer*)_a shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer*)_b
{
	return YES;
}

- (void)attachToKeyWindow
{
	UIWindow* target = nil;
	for(UIScene* scene in UIApplication.sharedApplication.connectedScenes)
	{
		if(![scene isKindOfClass:[UIWindowScene class]])
			continue;
		for(UIWindow* w in ((UIWindowScene*)scene).windows)
		{
			if(w.isKeyWindow || !target)
				target = w;
		}
	}
	if(!target || target == self.window)
		return;

	if(self.hover && self.window)
		[self.window removeGestureRecognizer:self.hover];

	UIHoverGestureRecognizer* hover = [[UIHoverGestureRecognizer alloc] initWithTarget:self action:@selector(onHover:)];
	hover.allowedTouchTypes = @[@(UITouchTypeIndirectPointer)];
	hover.cancelsTouchesInView = NO;
	hover.delaysTouchesBegan = NO;
	hover.delaysTouchesEnded = NO;
	hover.delegate = self;
	[target addGestureRecognizer:hover];
	self.hover = hover;
	[hover release];
	self.window = target;
}
@end

namespace
{
	TusPointerWatcher* g_watcher = nil;

	void start()
	{
		if(g_started)
			return;
		g_started = true;

		g_watcher = [[TusPointerWatcher alloc] init];
		[g_watcher attachToKeyWindow];

		NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
		NSOperationQueue* main = NSOperationQueue.mainQueue;
		if(@available(iOS 14.0, *))
		{
			[center addObserverForName:GCMouseDidConnectNotification object:nil queue:main usingBlock:^(NSNotification*)
			{
				report("GCMouse connected");
			}];
			[center addObserverForName:GCMouseDidDisconnectNotification object:nil queue:main usingBlock:^(NSNotification*)
			{
				g_hoverSeen = false;
				report("GCMouse disconnected");
			}];
		}
		[center addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:main usingBlock:^(NSNotification*)
		{
			[g_watcher attachToKeyWindow];
			report("became active");
		}];
		[center addObserverForName:UIApplicationDidEnterBackgroundNotification object:nil queue:main usingBlock:^(NSNotification*)
		{
			g_hoverSeen = false;
			report("background");
		}];
		[center addObserverForName:UIWindowDidBecomeKeyNotification object:nil queue:main usingBlock:^(NSNotification*)
		{
			[g_watcher attachToKeyWindow];
		}];

		g_lastReported = tusPointer::pointingDeviceConnected();
		tusEngine::log(std::string("pointer: ") + (g_lastReported ? "connected" : "not connected") + " at start (GCMouse " +
			std::to_string(gcMouseListed() ? 1 : 0) + (forcedState() >= 0 ? ", forced by GEARMULATOR_IOS_POINTER)" : ")"));
	}
}

namespace tusPointer
{
	bool pointingDeviceConnected()
	{
		const int forced = forcedState();
		if(forced >= 0)
			return forced != 0;
		return gcMouseListed() || g_hoverSeen;
	}

	void setChangedCallback(std::function<void()> _callback)
	{
		g_callback = std::move(_callback);
		start();
	}
}
