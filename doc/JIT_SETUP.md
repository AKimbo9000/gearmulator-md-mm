# JIT on iPad and iPhone: setup

The Monomachine app uses the DSP JIT when StikDebug has enabled it, and the interpreter otherwise. The JIT
probe (`tools/jitprobe`, **JIT Probe** on the home screen) tests the JIT route on its own: it shows **PASS** or
**FAIL** with the reason on screen.

**Devices:**
- iPad Pro 13" M4, iPadOS 27.0
- iPhone 17 Pro Max, iOS 27.2.1

Both are TXM devices, both have Developer Mode on, and their IDs are in `CLAUDE.local.md`.

## Already done from the Mac (2026-11-26)

- **JIT Probe** (`com.aki.gearmulator.jitprobe`), development-signed with `get-task-allow`: installed on both devices. It opened, and is waiting for a debugger.
- **StikDebug**, built from source and signed with our team as `com.aki.stikdebug`: installed on both devices.
  - **Source:** https://github.com/StikDebug/StikDebug, commit `63713ea` (2026-11-20).
  - **One local change:** the unused `com.apple.security.application-groups` entitlement (`group.com.stik.stikdebug`, another team's group) was removed. No code refers to it.
  - **Build output:** `build/stikdebug-src`; rebuild with the command in the next section.
- **iloader.app** (the pairing tool StikDebug's guide recommends) is already installed on this Mac.

## One time, by you

1. **Install LocalDevVPN from the App Store** on both devices: https://apps.apple.com/us/app/localdevvpn/id6755608044 (Coxson Engineering, free). Open it once and allow the VPN configuration when iOS asks.
2. **Give StikDebug a pairing file, on each device.** Use either method.
   - **On the device** (iOS/iPadOS 27 or later, which both are):
     1. In StikDebug, go to **Settings → Pair a Device → Pair iPhone or iPad**.
     2. Follow the prompts. StikDebug saves the file itself. [PAIRING.md, method 1]
   - **From this Mac with iloader:**
     1. Connect the device by USB and unlock it.
     2. Open **iloader** and click **Manage Pairing File**.
     3. Click **Place** next to StikDebug, and wait for "Pairing file placed successfully!" [PAIRING.md, method 2]
3. **Mount the developer disk image:**
   1. With Wi-Fi on and LocalDevVPN connected, open StikDebug once.
   2. Force-quit it, then open it again. [SideStore JIT docs]
   3. Repeat this after every reboot of the device.
4. **Run the probe once on each device** (see the next section), and tell me the PASS/FAIL line, or let me read `Documents/jitprobe.log` with the device connected.

The pairing file stops working if the device's passcode is changed or reset [PAIRING.md]. The SideStore docs say it can also expire after an iOS update or "at random times". Then repeat step 2.

## Every time you want JIT (no Mac, no cable)

1. Turn on **LocalDevVPN**. Wi-Fi must be on; on cellular only, connect the VPN and then turn on Airplane Mode [S1].
2. Open the app (JIT Probe now, the Monomachine app later).
3. Enable JIT for it, either way:
   - In StikDebug: **Enable JIT** → choose the app; or
   - In the app: tap **Ask StikDebug to enable JIT**. This opens `stikdebug://enable-jit?bundle-id=…&pid=…&script-name=universal.js`, the form given in StikDebug's integration guide [S1]; the README does not document it.
4. The probe should then show **PASS**. If it shows **FAIL**, the reason line says which step failed:
   - **"no debugger attached"**: StikDebug did not attach. Check the VPN, the pairing file and the disk-image mount.
   - **"TXM prepare not serviced"**: StikDebug attached without its `universal.js` script, or this iOS version is not supported yet.
   - **"region is not executable"** or **"generated code … wrong value"**: the protocol ran but the memory did not behave. Report it.
   - **"Previous run ended during: step …"**: the last run was killed at that step.

## Rebuilding and reinstalling (Mac, optional)

```bash
cmake -G Xcode -S tools/jitprobe -B build/jitprobe-ios -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DJITPROBE_TEAM=<team id from CLAUDE.local.md>
```
```bash
cmake --build build/jitprobe-ios --config Release -- -sdk iphoneos -allowProvisioningUpdates
```
```bash
xcrun devicectl device install app --device <UDID> build/jitprobe-ios/Release-iphoneos/JITProbe.app
```
StikDebug: `xcodebuild -project build/stikdebug-src/StikDebug.xcodeproj -scheme StikDebug -configuration Release -sdk iphoneos -allowProvisioningUpdates DEVELOPMENT_TEAM=<team id> PRODUCT_BUNDLE_IDENTIFIER=com.aki.stikdebug CODE_SIGN_STYLE=Automatic build`

A development profile from the paid team lasts a year; reinstalling from the Mac renews it.

## Sources

- [S1] StikDebug integration guide, https://github.com/StikDebug/StikJIT/blob/main/INTEGRATION.md
- StikDebug README and PAIRING.md, https://github.com/StikDebug/StikDebug
- SideStore JIT and pairing-file docs, https://docs.sidestore.io/docs/advanced/jit and https://docs.sidestore.io/docs/advanced/pairing-file
- LocalDevVPN, https://apps.apple.com/us/app/localdevvpn/id6755608044

## MM Bench: Monomachine on the device, JIT or interpreter

**MM Bench** (`com.aki.gearmulator.bench`) is installed on both devices. The MM OS 1.32b ROM is already in its Documents folder; the app bundle contains none.
- It runs the same scenario as `trigPressFirmwareTest mm` on the Mac: cold boot, Trig 1 press, 2 s hold, release, 10 s tail, 37 s of emulated time in all.
- It then shows the engine, the render hash and the real-time factor (rtf).

**Running it with JIT (no Mac, no cable):**
1. Turn on LocalDevVPN.
2. Open **MM Bench**.
3. Either tap **Ask StikDebug to enable JIT**, or in StikDebug use **Enable JIT** and choose **MM Bench**.
4. When StikDebug has attached, MM Bench starts by itself and shows **Engine: JIT (StikDebug)**. The run takes roughly 15–25 s.
5. Read the result:
   - `PASS … MATCH (expected 6adf0ea75100b8c3)` means the device JIT reproduced the desktop JIT bit for bit.
   - The `rtf` line is the speed: above 1.0 means faster than real time.

**Running it on the interpreter:** open MM Bench without StikDebug and tap **Run on the interpreter**. It takes about 1–2 minutes (the interpreter runs at about 0.5x on these devices). The expected hash is `0863f46b643b5555`.

**Each run also writes `Documents/mmbench.log`.** You can read it in the Files app (On My iPad/iPhone → MM Bench), or I can pull it with the device connected.

Run it once per engine on each device and tell me the four result lines, or connect the devices and I'll read the logs.

## Gearmulator MM with JIT from a Mac (cable, no StikDebug)

For development and testing, the Mac can do StikDebug's job. Xcode's LLDB attaches through the device's debugserver and services the app's `brk #0xf00d` request the way `universal.js` does.

```bash
GEARMULATOR_MM_AUTOPLAY_SECONDS=12 tools/jitprobe/lldb_jit_run.sh <hardware udid> <coredevice uuid> <out dir> 45
```

- **Device ids:** both are in `CLAUDE.local.md`. LLDB names devices by the CoreDevice UUID; `devicectl` accepts the hardware UDID.
- **Requirements:** the device unlocked (otherwise `RequestDenied`), and the LLDB from Xcode 27 (`device select` / `device process attach`).
- **What the script does:** installs the app, launches it stopped (`devicectl … --start-stopped`), attaches LLDB, services the request (`lldb_jit_service.py`), detaches, and after the wait pulls `realtime.log` and `engine.log` into `<out dir>`.
- **How the request is serviced:**
  1. advance pc;
  2. allocate the RX region with `_M<size>,rx`;
  3. touch every 16 KiB page through the debugger;
  4. write x0 with a gdb-remote `P` packet;
  5. detach on x16 = 0.
- **The page touch is required.** Without it, the first JIT block died with `EXC_BAD_ACCESS` code 0x32 (`KERN_CODESIGN_ERROR`) inside the region. It stands in for StikDebug's native `prepare_memory_region`, which `universal.js` calls but does not contain. It takes about 40 s for the 16 MiB region, and the app waits at the breakpoint meanwhile.
- **Why `P` packets:** LLDB's own register writes fail while the app is still in its load-time initialisers.
- **Result on the iPad Pro 13 (M4):** `engine=JIT … serviced after 1 attempt(s)`, rtf 2.26, 187.5 callbacks/s.

**JIT after launch.** Gearmulator MM also switches to the JIT on its own when a debugger attaches after launch: StikDebug's Enable JIT on the running app, or Settings › Relaunch with JIT. `Documents/engine.log` records each step.
