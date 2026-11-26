// JIT probe app: shows the probe's progress and its PASS/FAIL reason on screen, and writes them to
// Documents/jitprobe.log (Files app, or xcrun devicectl device copy from ... --domain-type appDataContainer).

#import <UIKit/UIKit.h>

#include "probe.h"

#include <string>

@interface ProbeViewController : UIViewController
@property(nonatomic, strong) UILabel* status;
@property(nonatomic, strong) UITextView* text;
@property(nonatomic, strong) NSMutableString* lines;
@end

static void probeLog(const char* _line, void* _ctx)
{
	ProbeViewController* vc = (__bridge ProbeViewController*)_ctx;
	NSString* line = [NSString stringWithUTF8String:_line];
	dispatch_async(dispatch_get_main_queue(), ^{
		[vc.lines appendFormat:@"%@\n", line];
		vc.text.text = vc.lines;
		if([line hasPrefix:@"PASS"]) { vc.status.text = @"PASS"; vc.status.textColor = UIColor.systemGreenColor; }
		else if([line hasPrefix:@"FAIL"]) { vc.status.text = @"FAIL"; vc.status.textColor = UIColor.systemRedColor; }
		NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
		[vc.lines writeToFile:[docs stringByAppendingPathComponent:@"jitprobe.log"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
	});
}

@implementation ProbeViewController

- (void)viewDidLoad
{
	[super viewDidLoad];
	self.view.backgroundColor = UIColor.systemBackgroundColor;
	self.lines = [NSMutableString string];

	self.status = [[UILabel alloc] init];
	self.status.text = @"RUNNING";
	self.status.font = [UIFont boldSystemFontOfSize:44];
	self.status.textAlignment = NSTextAlignmentCenter;

	UIButton* button = [UIButton buttonWithType:UIButtonTypeSystem];
	[button setTitle:@"Ask StikDebug to enable JIT" forState:UIControlStateNormal];
	button.titleLabel.font = [UIFont systemFontOfSize:20];
	[button addTarget:self action:@selector(openStikDebug) forControlEvents:UIControlEventTouchUpInside];

	self.text = [[UITextView alloc] init];
	self.text.editable = NO;
	self.text.font = [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightRegular];

	UIStackView* stack = [[UIStackView alloc] initWithArrangedSubviews:@[self.status, button, self.text]];
	stack.axis = UILayoutConstraintAxisVertical;
	stack.spacing = 12;
	stack.translatesAutoresizingMaskIntoConstraints = NO;
	[self.view addSubview:stack];
	UILayoutGuide* g = self.view.safeAreaLayoutGuide;
	[NSLayoutConstraint activateConstraints:@[
		[stack.topAnchor constraintEqualToAnchor:g.topAnchor constant:16],
		[stack.bottomAnchor constraintEqualToAnchor:g.bottomAnchor constant:-16],
		[stack.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:16],
		[stack.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-16]]];

	NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	NSString* progressPath = [docs stringByAppendingPathComponent:@"jitprobe-progress.txt"];

	// If the previous run died mid-step, say where: that is the result of that run
	NSString* previous = [NSString stringWithContentsOfFile:progressPath encoding:NSUTF8StringEncoding error:nil];
	if(previous.length && ![previous hasPrefix:@"finished"])
		[self.lines appendFormat:@"Previous run ended during: %@\n", previous];
	else if(previous.length)
		[self.lines appendFormat:@"Previous run: %@\n", previous];

	UIDevice* d = UIDevice.currentDevice;
	[self.lines appendFormat:@"%@ %@, iOS %@\n", d.model, d.name, d.systemVersion];
	self.text.text = self.lines;

	std::string path = progressPath.UTF8String;
	void* ctx = (__bridge void*)self;
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
		runJitProbe(&probeLog, ctx, path.c_str(), 600);
	});
}

- (void)openStikDebug
{
	// URL form from the StikDebug integration guide; universal.js is the script for TXM devices
	NSString* url = [NSString stringWithFormat:@"stikdebug://enable-jit?bundle-id=%@&pid=%d&script-name=universal.js",
		NSBundle.mainBundle.bundleIdentifier, getpid()];
	[UIApplication.sharedApplication openURL:[NSURL URLWithString:url] options:@{} completionHandler:^(BOOL ok) {
		if(!ok)
			probeLog("could not open StikDebug: is it installed? Enable JIT for JIT Probe from inside StikDebug instead.", (__bridge void*)self);
	}];
}

@end

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation SceneDelegate
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)connectionOptions
{
	self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
	self.window.rootViewController = [[ProbeViewController alloc] init];
	[self.window makeKeyAndVisible];
}
@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
	return YES;
}
@end

int main(int argc, char* argv[])
{
	@autoreleasepool
	{
		return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
	}
}
