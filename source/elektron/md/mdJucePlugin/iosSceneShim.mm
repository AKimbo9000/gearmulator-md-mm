// iOS 27 traps apps that have not adopted the scene lifecycle (_UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption).
// JUCE 7 creates plain UIWindows from the app delegate. This shim adopts scenes without changing JUCE:
//  - Info.plist names TusSceneDelegate in UIApplicationSceneManifest (juce.cmake, PLIST_TO_MERGE on iOS)
//  - windows created before the scene connects are remembered and attached when it does
//  - windows created afterwards are attached as they are created (an initWithFrame: override on UIWindow only)

#import <UIKit/UIKit.h>
#import <objc/runtime.h>

static UIWindowScene* g_tusScene = nil;
static NSHashTable<UIWindow*>* g_tusEarlyWindows = nil;
static IMP g_tusInheritedInitWithFrame = nullptr;

static id tusWindowInitWithFrame(UIWindow* self, SEL _cmd, CGRect _frame)
{
	UIWindow* w = ((UIWindow* (*)(id, SEL, CGRect))g_tusInheritedInitWithFrame)(self, _cmd, _frame);
	if(!w)
		return w;
	if(g_tusScene)
		w.windowScene = g_tusScene;
	else
		[g_tusEarlyWindows addObject:w];
	return w;
}

@interface TusSceneDelegate : UIResponder <UIWindowSceneDelegate>
@end

@implementation TusSceneDelegate
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)connectionOptions
{
	g_tusScene = (UIWindowScene*)scene;
	UIWindow* first = nil;
	for(UIWindow* w in g_tusEarlyWindows.allObjects)
	{
		w.windowScene = g_tusScene;
		w.frame = g_tusScene.coordinateSpace.bounds;
		if(!first && !w.hidden)
			first = w;
	}
	[g_tusEarlyWindows removeAllObjects];
	[first makeKeyAndVisible];
}
@end

__attribute__((constructor)) static void tusInstallSceneShim()
{
	g_tusEarlyWindows = [NSHashTable weakObjectsHashTable];
	Class cls = [UIWindow class];
	SEL sel = @selector(initWithFrame:);
	Method inherited = class_getInstanceMethod(cls, sel);
	g_tusInheritedInitWithFrame = method_getImplementation(inherited);
	// add the override to UIWindow itself; if UIWindow already defines one, replace only that one
	if(!class_addMethod(cls, sel, (IMP)tusWindowInitWithFrame, method_getTypeEncoding(inherited)))
		g_tusInheritedInitWithFrame = method_setImplementation(class_getInstanceMethod(cls, sel), (IMP)tusWindowInitWithFrame);
}
