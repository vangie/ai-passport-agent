import XCTest

final class PassportLocalizationTests: XCTestCase {
    func testEnglishInterfaceAndDeviceLanguageAreIndependent() {
        let application = XCUIApplication()
        application.launchArguments = ["-AppleLanguages", "(en)", "-AppleLocale", "en_US",
                                       "--preview-networks", "success", "--preview-saved-networks"]
        application.launch()
        XCTAssertTrue(application.textFields["wifi.ssid"].waitForExistence(timeout: 10))
        XCTAssertTrue(application.staticTexts["Network name"].exists)
        XCTAssertTrue(application.buttons["wifi.search"].staticTexts["Scan"].exists)
        XCTAssertEqual(application.buttons["wifi.search"].label, "Scan for Wi-Fi networks")
        XCTAssertFalse(application.staticTexts["网络名称"].exists)
        let home = XCTAttachment(screenshot: application.screenshot())
        home.name = "English configuration"
        home.lifetime = .keepAlways
        add(home)
        application.buttons["wifi.search"].tap()
        XCTAssertTrue(application.navigationBars["Nearby Wi-Fi"].waitForExistence(timeout: 5))
        XCTAssertTrue(application.buttons["network.Test iPhone"].waitForExistence(timeout: 5))
        XCTAssertEqual(application.buttons["network.Test iPhone"].value as? String, "Personal Hotspot")
        application.navigationBars["Nearby Wi-Fi"].buttons["Cancel"].tap()
        let language = application.buttons["device.language"]
        for _ in 0..<5 {
            if language.isHittable { break }
            application.swipeUp()
        }
        XCTAssertTrue(language.isHittable)
        XCTAssertTrue(language.label.contains("Simplified Chinese"))
        let attachment = XCTAttachment(screenshot: application.screenshot())
        attachment.name = "English interface with Chinese device language"
        attachment.lifetime = .keepAlways
        add(attachment)
        application.terminate()
    }

    func testChineseInterfaceFollowsSystemLanguage() {
        let application = XCUIApplication()
        application.launchArguments = ["-AppleLanguages", "(zh-Hans)", "-AppleLocale", "zh_CN",
                                       "--preview-networks", "success"]
        application.launch()
        XCTAssertTrue(application.textFields["wifi.ssid"].waitForExistence(timeout: 10))
        XCTAssertTrue(application.staticTexts["网络名称"].exists)
        XCTAssertTrue(application.buttons["wifi.search"].staticTexts["搜索"].exists)
        XCTAssertFalse(application.staticTexts["Network name"].exists)
        application.terminate()
    }
}
