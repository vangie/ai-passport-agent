[简体中文](ios-install.zh_CN.md) · **English**

# AI Passport for iPhone

The iOS App is retained as **source only**. New Releases no longer build or
package it, including source kits and Simulator ZIPs. Use the repository for local
development; there is no App Store or TestFlight distribution.

A Simulator app cannot be re-signed for a physical iPhone. Re-signing without
rebuilding requires a compatible device binary, valid certificate and provisioning
profile, matching entitlements and an installation tool. This release does not
distribute a device re-signing kit; use the source workflow below.

## Install on your iPhone

1. Clone or download the [repository](https://github.com/vangie/ai-passport-agent).
   Keep `ios/` and `shared/` together; no private signing data is included.
2. Install full Xcode with an iOS SDK and XcodeGen (`brew install xcodegen`).
   The app's minimum deployment target is iOS 17.0; your Xcode must support your
   phone's installed iOS version.
3. From the repository root, run:

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

Update the device with matching firmware using the web configuration studio first. In Passport
Settings, enable Bluetooth and select `AI Passport XXXX` in the iOS App.
Scan nearby 2.4 GHz networks from the device, enter a password and save. For a
saved network, an empty password reuses the device's saved password. Pair an
assistant using a limited OpenClaw setup code, never the administrator token.

For this iPhone's hotspot, turn on **Allow Others to Join** and **Maximize
Compatibility**. The SSID is the phone's name. Keep **Settings → Personal
Hotspot** open and the screen awake if the hotspot is missing from device scans.
iOS does not let this app read your hotspot password or silently enable it.

The Passport stores settings and connects directly to OpenClaw over Wi-Fi;
the iOS App is not a voice relay. Audio/gateway work temporarily disconnects
configuration Bluetooth to free memory. Just Works pairing encrypts traffic but
does not authenticate ownership or protect against active man-in-the-middle.

## Preview on a Mac

From the repository root, build and launch locally with full Xcode and a compatible
iPhone Simulator runtime:

```sh
make sim-iphone
```

The simulator cannot configure a real Passport via BLE. There is no prebuilt
Simulator download or complete-app CI build; use a physical iPhone for Bluetooth tests.

Full setup, NFC tips, plugin and networking instructions are in the
[project repository](https://github.com/vangie/ai-passport-agent).
