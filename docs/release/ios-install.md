[简体中文](ios-install.zh_CN.md) · **English**

# AI Passport for iPhone

This release provides an **iPhone source kit**, not an installable unsigned IPA.
The separate Simulator ZIP runs on a Mac only and cannot access real Bluetooth.
Neither artifact is an App Store or TestFlight distribution.

## Install on your iPhone

1. Unzip `ai-passport-openclaw-ios-source.zip`. It contains `ios/`, `shared/`,
   the license and these instructions, without private signing data or endpoints.
2. Install full Xcode with an iOS SDK and XcodeGen (`brew install xcodegen`).
   The app's minimum deployment target is iOS 17.0; your Xcode must support your
   phone's installed iOS version.
3. From the extracted folder, run:

   ```sh
   cd ios
   xcodegen generate
   open Passport.xcodeproj
   ```

4. Select the **Passport** target, open **Signing & Capabilities**, and choose
   your own Apple development team. If the bundle ID is unavailable to your
   team, use your own unique identifier. Connect and select your physical iPhone,
   then run the **Passport** scheme.
5. Follow Xcode/iOS instructions for trusting the developer and enabling
   Developer Mode where required. Free Personal Team signing can expire and
   require reinstalling from Xcode. No paid signing account is bundled.

These signing changes are local. Do not publish certificates, provisioning
profiles, account details or configured credentials. Downloading this ZIP alone
does not install anything on a phone.

## Configure a Passport

Update the device with matching firmware using the Mac app first. In Passport
Settings, enable Bluetooth and select `AI Passport XXXX` in the iPhone app.
Scan nearby 2.4 GHz networks from the device, enter a password and save. For a
saved network, an empty password reuses the device's saved password. Pair an
assistant using a limited OpenClaw setup code, never the administrator token.

For this iPhone's hotspot, turn on **Allow Others to Join** and **Maximize
Compatibility**. The SSID is the phone's name. Keep **Settings → Personal
Hotspot** open and the screen awake if the hotspot is missing from device scans.
iOS does not let this app read your hotspot password or silently enable it.

The Passport stores settings and connects directly to OpenClaw over Wi-Fi;
the iPhone app is not a voice relay. Audio/gateway work temporarily disconnects
configuration Bluetooth to free memory. Just Works pairing encrypts traffic but
does not authenticate ownership or protect against active man-in-the-middle.

## Preview on a Mac

Unzip `ai-passport-openclaw-ios-simulator.zip`, boot an iPhone simulator with a
compatible runtime in Xcode, then run:

```sh
xcrun simctl install booted /path/to/Passport.app
xcrun simctl launch booted ai.mirasim.passport
```

The simulator cannot configure a real Passport via BLE. The app is built for
the CI runner's simulator architecture/SDK, not a universal or backwards-compatible
simulator binary. Build from the source kit if your architecture/runtime differs.

Full setup, NFC tips, plugin and networking instructions are in the
[project repository](https://github.com/vangie/ai-passport-openclaw).
