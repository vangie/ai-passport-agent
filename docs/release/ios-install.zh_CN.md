[English](ios-install.md) · **简体中文**

# AI Passport iOS App

iOS App **仅保留源码**。新 Release 不再构建或打包，包括源码安装包和模拟器 ZIP。
需要时从仓库本地开发，不通过 App Store 或 TestFlight 分发。

模拟器 App 不能通过重签装到真实 iPhone。免构建重签需要兼容的真机二进制、
有效证书与描述文件、匹配的权限和安装工具。本版不提供真机重签套件，
请使用下方源码安装流程。

## 安装到自己的 iPhone

1. 克隆或下载[仓库](https://github.com/vangie/ai-passport-agent)，
   保持 `ios/` 与 `shared/` 同在仓库中；不包含私人签名资料。
2. 安装带 iOS SDK 的完整 Xcode 和 XcodeGen（`brew install xcodegen`）。
   App 最低支持 iOS 17.0；Xcode 也需要支持手机实际安装的 iOS 版本。
3. 在仓库根目录运行：

   ```sh
   cd ios
   xcodegen generate
   open Passport.xcodeproj
   ```

4. 选择 **Passport** target，在 **Signing & Capabilities** 中选择自己的 Apple 开发团队。
   如果该团队无法使用现有 Bundle ID，改为自己的唯一标识。
   连接并选中真实 iPhone，运行 **Passport** scheme。
5. 按 Xcode／iOS 提示信任开发者、开启开发者模式。
   免费 Personal Team 签名会过期，可能需要重新通过 Xcode 安装，包内不包含付费签名账号。

签名修改仅保留本地。不要公开证书、描述文件、账号或已配置凭据。
单纯下载 ZIP 不会在手机上安装应用。

## 配置 Passport

先使用网页配置工具给设备更新匹配固件，在 Passport 设置中打开蓝牙，
在 iOS App 选择 `AI Passport XXXX`。由设备扫描附近 2.4 GHz 网络，填写密码并保存。
已存网络留空密码可以沿用设备的密码。
助手使用 OpenClaw 受限配对码授权，不要填管理员 token。

本机热点需打开 **「允许其他人加入」** 和 **「最大兼容性」**，SSID 就是手机名称。
扫描找不到时，请停留在 **系统设置 → 个人热点** 并保持亮屏。
iOS 不允许此 App 读取热点密码或静默开启热点。

配置保存在 Passport，设备通过 Wi-Fi 直连 OpenClaw，iPhone 不转发语音。
音频／网关工作期间会临时断开配置蓝牙以释放内存。
Just Works 配对加密传输，但不验证所有权，也不防主动中间人攻击。

## 在 Mac 预览

安装完整 Xcode 和兼容的 iPhone 模拟器运行时，在仓库根目录本地构建并启动：

```sh
make sim-iphone
```

模拟器无法通过蓝牙配置真实 Passport，不再提供预构建模拟器下载或完整 App CI 构建；
测试蓝牙请使用真实 iPhone。

完整配置、NFC 技巧、插件和网络接入说明见
[项目仓库](https://github.com/vangie/ai-passport-agent)。
