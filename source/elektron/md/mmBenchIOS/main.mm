// MM Bench: runs the trigPressFirmwareTest Monomachine scenario (cold boot, Trig 1 press, hold, release, tail) on
// this device and shows the engine, the render hash and the real-time factor.
//
// Engine: JIT when StikDebug has attached (CS_DEBUGGED) and prepared an executable region, the interpreter otherwise.
// Firmware: the MM OS 1.32b image must be in this app's Documents folder (copied with devicectl or the Files app);
// it is never part of the app bundle. Output: on screen and in Documents/mmbench.log.

#import <UIKit/UIKit.h>

#include "stikjit.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jitcoderegion.h"
#include "mdLib/mdromloader.h"
#include "baseLib/filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int trigPressMain(int _argc, char** _argv);

namespace
{
	constexpr size_t g_regionSize = 16 * 1024 * 1024;		// MM's peak JIT code is 350 KB (doc/IOS_PORT_STATUS.md)
	// expected render hashes of this scenario, from the macOS builds (doc/ios-baselines, IOS_PORT_STATUS.md)
	const char* const g_expectedJit = "6adf0ea75100b8c3";
	const char* const g_expectedInterp = "0863f46b643b5555";

	NSString* documents()
	{
		return NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	}

	// the MM OS 1.32b image anywhere in Documents (or Documents/ROMs), recognised by md::RomLoader's fingerprint
	std::string findMmRom()
	{
		NSFileManager* fm = NSFileManager.defaultManager;
		NSDirectoryEnumerator* it = [fm enumeratorAtPath:documents()];
		for(NSString* rel in it)
		{
			NSString* full = [documents() stringByAppendingPathComponent:rel];
			NSDictionary* attrs = [fm attributesOfItemAtPath:full error:nil];
			if([attrs fileSize] != 8 * 1024 * 1024)
				continue;
			std::vector<uint8_t> data;
			if(baseLib::filesystem::readFile(data, full.UTF8String) && md::RomLoader::isRomForModel(data, md::MachineModel::Monomachine))
				return full.UTF8String;
		}
		return {};
	}

	std::string field(const std::string& _line, const char* _key)
	{
		const std::string k = std::string(" ") + _key + "=";
		const auto p = _line.find(k);
		if(p == std::string::npos)
			return {};
		const auto e = _line.find(' ', p + k.size());
		return _line.substr(p + k.size(), e == std::string::npos ? std::string::npos : e - p - k.size());
	}
}

@interface BenchViewController : UIViewController
@property(nonatomic, strong) UILabel* engine;
@property(nonatomic, strong) UILabel* result;
@property(nonatomic, strong) UITextView* text;
@property(nonatomic, strong) UIButton* runInterp;
@property(nonatomic, strong) UIButton* askJit;
@property(nonatomic) BOOL running;
@end

@implementation BenchViewController

- (void)say:(NSString*)_line
{
	dispatch_async(dispatch_get_main_queue(), ^{
		self.text.text = [self.text.text stringByAppendingFormat:@"%@\n", _line];
	});
}

- (void)viewDidLoad
{
	[super viewDidLoad];
	self.view.backgroundColor = UIColor.systemBackgroundColor;

	self.engine = [[UILabel alloc] init];
	self.engine.font = [UIFont boldSystemFontOfSize:30];
	self.engine.textAlignment = NSTextAlignmentCenter;
	self.result = [[UILabel alloc] init];
	self.result.font = [UIFont monospacedSystemFontOfSize:20 weight:UIFontWeightSemibold];
	self.result.numberOfLines = 0;
	self.result.textAlignment = NSTextAlignmentCenter;

	self.runInterp = [UIButton buttonWithType:UIButtonTypeSystem];
	[self.runInterp setTitle:@"Run on the interpreter" forState:UIControlStateNormal];
	[self.runInterp addTarget:self action:@selector(startInterpreter) forControlEvents:UIControlEventTouchUpInside];
	self.askJit = [UIButton buttonWithType:UIButtonTypeSystem];
	[self.askJit setTitle:@"Ask StikDebug to enable JIT" forState:UIControlStateNormal];
	[self.askJit addTarget:self action:@selector(openStikDebug) forControlEvents:UIControlEventTouchUpInside];

	self.text = [[UITextView alloc] init];
	self.text.editable = NO;
	self.text.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular];

	UIStackView* stack = [[UIStackView alloc] initWithArrangedSubviews:@[self.engine, self.result, self.askJit, self.runInterp, self.text]];
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

	UIDevice* d = UIDevice.currentDevice;
	[self say:[NSString stringWithFormat:@"%@, iOS %@", d.model, d.systemVersion]];

	if(stikIsDebugged())
	{
		[self startJit];
		return;
	}
	self.engine.text = @"Engine: not chosen";
	self.result.text = @"StikDebug is not attached.\nTap a button below.";
	// if StikDebug attaches while we wait (button below), start the JIT run by itself
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
		for(int i = 0; i < 600 && !self.running; ++i)
		{
			if(stikIsDebugged())
			{
				dispatch_async(dispatch_get_main_queue(), ^{ [self startJit]; });
				return;
			}
			sleep(1);
		}
	});
}

- (void)openStikDebug
{
	NSString* url = [NSString stringWithFormat:@"stikdebug://enable-jit?bundle-id=%@&pid=%d&script-name=universal.js",
		NSBundle.mainBundle.bundleIdentifier, getpid()];
	[UIApplication.sharedApplication openURL:[NSURL URLWithString:url] options:@{} completionHandler:^(BOOL ok) {
		if(!ok)
			[self say:@"could not open StikDebug: enable JIT for MM Bench from inside StikDebug instead"];
	}];
}

- (void)startJit
{
	if(self.running)
		return;
	void* rx = nullptr;
	void* rw = nullptr;
	std::string reason;
	if(stikAcquireRegion(g_regionSize, rx, rw, reason) && dsp56k::JitCodeRegion::install(rx, rw, g_regionSize))
	{
		dsp56k::g_runtimeUseJIT = true;
		[self say:[NSString stringWithFormat:@"JIT region %p (writable alias %p), 16 MiB", rx, rw]];
		[self runWithEngineName:@"JIT (StikDebug)" expected:g_expectedJit];
		return;
	}
	[self say:[NSString stringWithFormat:@"JIT not available: %s - using the interpreter", reason.c_str()]];
	[self startInterpreter];
}

- (void)startInterpreter
{
	if(self.running)
		return;
	dsp56k::g_runtimeUseJIT = false;
	[self runWithEngineName:@"Interpreter" expected:g_expectedInterp];
}

- (void)runWithEngineName:(NSString*)_name expected:(const char*)_expected
{
	self.running = YES;
	self.runInterp.enabled = NO;
	self.askJit.enabled = NO;
	self.engine.text = [@"Engine: " stringByAppendingString:_name];
	self.result.text = @"running (37 s of emulated time)...";
	std::string expected = _expected;

	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
		const std::string rom = findMmRom();
		if(rom.empty())
		{
			dispatch_async(dispatch_get_main_queue(), ^{
				self.result.text = @"FAIL: no MM OS 1.32b ROM in Documents.\nCopy it with the Files app or devicectl.";
			});
			return;
		}
		setenv("GEARMULATOR_MM_FIRMWARE_BIN", rom.c_str(), 1);

		// the scenario prints its result line to stdout: send stdout to Documents/mmbench.log
		const std::string log = std::string(documents().UTF8String) + "/mmbench.log";
		std::fflush(stdout);
		std::freopen(log.c_str(), "w", stdout);
		std::freopen((log + ".err").c_str(), "w", stderr);

		char a0[] = "trigPress", a1[] = "mm";
		char* argv[] = {a0, a1, nullptr};
		const int rc = trigPressMain(2, argv);
		std::fflush(stdout);

		std::string render;
		if(FILE* f = std::fopen(log.c_str(), "r"))
		{
			char line[4096];
			while(std::fgets(line, sizeof(line), f))
				if(std::string(line).rfind("RENDER", 0) == 0)
					render = line;
			std::fclose(f);
		}
		const std::string hash = field(render, "hash");
		const std::string rtf = field(render, "rtf");
		const std::string engine = field(render, "engine");
		const std::string peak = field(render, "jit_region_peak_kb");
		const bool match = !hash.empty() && hash == expected;

		NSString* summary = [NSString stringWithFormat:@"%s\nhash %s\n%s (expected %s)\nrtf %sx%s\nexit %d",
			match ? "PASS" : "FAIL", hash.c_str(), match ? "MATCH" : "DIFFERENT", expected.c_str(), rtf.c_str(),
			engine == "jit" ? (" , JIT code " + peak + " KB").c_str() : "", rc];
		dispatch_async(dispatch_get_main_queue(), ^{
			self.result.text = summary;
			self.result.textColor = match ? UIColor.systemGreenColor : UIColor.systemRedColor;
			[self say:[NSString stringWithUTF8String:render.c_str()]];
		});
	});
}

@end

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation SceneDelegate
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)connectionOptions
{
	self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
	self.window.rootViewController = [[BenchViewController alloc] init];
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
