# JIT probe: a minimal iOS app that checks whether StikDebug's TXM JIT protocol works on this device.
# Separate from the emulator on purpose. Build (device, development signing):
#
#   cmake -G Xcode -S tools/jitprobe -B build/jitprobe-ios -DCMAKE_SYSTEM_NAME=iOS \
#         -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DJITPROBE_TEAM=<team id>
#   cmake --build build/jitprobe-ios --config Release -- -sdk iphoneos -allowProvisioningUpdates
#
cmake_minimum_required(VERSION 3.20)
project(jitprobe LANGUAGES C CXX OBJC OBJCXX)

set(JITPROBE_TEAM "" CACHE STRING "Apple Developer team ID for automatic development signing")
set(JITPROBE_BUNDLE_ID "com.aki.gearmulator.jitprobe" CACHE STRING "Bundle identifier")

add_executable(jitprobe MACOSX_BUNDLE main.mm probe.cpp)
target_compile_features(jitprobe PRIVATE cxx_std_17)
target_link_libraries(jitprobe PRIVATE "-framework UIKit" "-framework Foundation")

set_target_properties(jitprobe PROPERTIES
	OUTPUT_NAME "JITProbe"
	MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
	MACOSX_BUNDLE_GUI_IDENTIFIER "${JITPROBE_BUNDLE_ID}"
	MACOSX_BUNDLE_BUNDLE_NAME "JIT Probe"
	MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
	MACOSX_BUNDLE_BUNDLE_VERSION "1"
	XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${JITPROBE_BUNDLE_ID}"
	XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${JITPROBE_TEAM}"
	XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
	XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
	# Development signing carries get-task-allow, the one entitlement StikDebug needs
	XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
	XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "17.0"
	XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
)
