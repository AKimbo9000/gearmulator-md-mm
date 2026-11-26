# Headless Monomachine benchmark for iOS: runs the trigPressFirmwareTest scenario on the JIT when StikDebug has
# enabled it and on the interpreter otherwise, and shows engine, render hash and real-time factor.
# Needs DSP56K_RUNTIME_ENGINE=ON. The MM ROM is copied into the app's Documents folder, never bundled.
set(MMBENCH_TEAM "" CACHE STRING "Apple Developer team ID for development signing of mmBenchIOS")

add_executable(mmBenchIOS MACOSX_BUNDLE main.mm stikjit.cpp ../mdLibTest/trigPressFirmwareTest.cpp)
target_compile_definitions(mmBenchIOS PRIVATE TRIGPRESS_NO_MAIN=1)
target_link_libraries(mmBenchIOS PRIVATE mdLib "-framework UIKit" "-framework Foundation")
set_target_properties(mmBenchIOS PROPERTIES
	OUTPUT_NAME "MMBench"
	MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
	MACOSX_BUNDLE_GUI_IDENTIFIER "com.aki.gearmulator.bench"
	MACOSX_BUNDLE_BUNDLE_NAME "MM Bench"
	XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.aki.gearmulator.bench"
	XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${MMBENCH_TEAM}"
	XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
	XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
	XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
	XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "17.0"
)
