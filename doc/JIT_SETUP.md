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
