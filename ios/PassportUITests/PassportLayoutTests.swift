import XCTest

final class PassportLayoutTests: XCTestCase {
    private var application: XCUIApplication!

    override func setUpWithError() throws {
        continueAfterFailure = false
        application = XCUIApplication()
        application.launchArguments = ["-AppleLanguages", "(zh-Hans)", "-AppleLocale", "zh_CN"]
        application.launch()
    }

    func testSaveScrollsWithForm() {
        let save = application.buttons["configuration.save"]
        XCTAssertTrue(application.textFields["wifi.ssid"].waitForExistence(timeout: 10))
        application.buttons["配对 OpenClaw"].tap()
        XCTAssertFalse(save.isHittable)
        capture("Configuration")
        scrollTo(save)
        XCTAssertTrue(save.isHittable)
        XCTAssertFalse(save.isEnabled)
        application.swipeDown()
        application.swipeDown()
        XCTAssertFalse(save.isHittable)
    }

    func testOnlyOneNetworkSearchEntry() {
        let search = application.buttons["wifi.search"]
        XCTAssertTrue(search.waitForExistence(timeout: 10))
        XCTAssertFalse(search.isEnabled)
        XCTAssertFalse(application.buttons["wifi.hotspot"].exists)
        XCTAssertFalse(application.buttons["个人热点"].exists)
        XCTAssertTrue(application.textFields["wifi.ssid"].isEnabled)
    }

    func testNetworkSearchIsCompactAndInlineWithName() {
        previewNetworks("success")
        let search = application.buttons["wifi.search"]
        let heading = application.staticTexts["网络名称"]
        let name = application.textFields["wifi.ssid"]
        XCTAssertTrue(search.waitForExistence(timeout: 10))
        XCTAssertTrue(search.isHittable && search.isEnabled)
        XCTAssertLessThan(search.frame.width, application.frame.width * 0.45)
        XCTAssertGreaterThanOrEqual(search.frame.height, 44)
        let title = search.staticTexts["搜索"]
        XCTAssertTrue(title.exists)
        XCTAssertGreaterThan(title.frame.width, title.frame.height)
        XCTAssertTrue(search.frame.contains(title.frame))
        XCTAssertEqual(search.frame.midY, heading.frame.midY, accuracy: 2)
        XCTAssertEqual(search.frame.midY, name.frame.midY, accuracy: 2)
        capture("Compact inline Wi-Fi search")
        search.tap()
        XCTAssertTrue(application.navigationBars["附近的 Wi-Fi"].waitForExistence(timeout: 5))
    }

    func testNetworksShareOneListWithDistinctIcons() {
        previewNetworks("success")
        searchNetworks()
        let hotspot = application.buttons["network.Test iPhone"]
        let router = application.buttons["network.Office Wi-Fi"]
        let misleadingName = application.buttons["network.iPhone-named router"]
        XCTAssertTrue(hotspot.waitForExistence(timeout: 5))
        XCTAssertTrue(router.exists && misleadingName.exists)
        XCTAssertEqual(hotspot.value as? String, "个人热点")
        XCTAssertEqual(router.value as? String, "Wi-Fi")
        XCTAssertEqual(misleadingName.value as? String, "Wi-Fi")
        XCTAssertTrue(hotspot.images["个人热点"].exists)
        XCTAssertTrue(router.images["Wi-Fi"].exists)
        capture("Unified Wi-Fi search")
    }

    func testNetworksAppearAndCanBeSelectedBeforeScanFinishes() {
        previewNetworks("progressive")
        searchNetworks()
        let hotspot = application.buttons["network.Test iPhone"]
        XCTAssertTrue(hotspot.waitForExistence(timeout: 5))
        XCTAssertTrue(application.descendants(matching: .any)["networks.scanning"].exists)
        XCTAssertTrue(hotspot.isEnabled)
        XCTAssertTrue(application.buttons["network.Office Wi-Fi"].waitForExistence(timeout: 5))
        XCTAssertTrue(application.descendants(matching: .any)["networks.scanning"].exists)
        capture("Live Wi-Fi scan")
        hotspot.tap()
        XCTAssertTrue(application.textFields["wifi.ssid"].waitForExistence(timeout: 5))
        XCTAssertEqual(application.textFields["wifi.ssid"].value as? String, "Test iPhone")
    }

    func testSelectionUpdatesDraftWithoutSaving() {
        previewNetworks("success")
        searchNetworks()
        let hotspot = application.buttons["network.Test iPhone"]
        XCTAssertTrue(hotspot.waitForExistence(timeout: 5))
        hotspot.tap()
        let name = application.textFields["wifi.ssid"]
        XCTAssertTrue(name.waitForExistence(timeout: 5))
        XCTAssertEqual(name.value as? String, "Test iPhone")
        let password = application.secureTextFields["wifi.password"]
        password.tap()
        password.typeText("test-password")
        application.toolbars.buttons["完成"].tap()
        searchNetworks()
        XCTAssertTrue(hotspot.waitForExistence(timeout: 5))
        hotspot.tap()
        application.buttons["显示密码"].tap()
        XCTAssertEqual(application.textFields["wifi.password"].value as? String, "test-password")
        let discard = application.buttons["configuration.discard"]
        scrollTo(discard)
        discard.tap()
        application.swipeDown()
        application.swipeDown()
        XCTAssertEqual(name.value as? String, "Saved iPhone")
    }

    func testSearchCancellationPreservesDraft() {
        previewNetworks("success")
        let name = application.textFields["wifi.ssid"]
        XCTAssertTrue(name.waitForExistence(timeout: 10))
        name.tap()
        name.typeText(" edited")
        application.toolbars.buttons["完成"].tap()
        searchNetworks()
        application.navigationBars["附近的 Wi-Fi"].buttons["取消"].tap()
        XCTAssertEqual(name.value as? String, "Saved iPhone edited")
    }

    func testHotspotHintKeepsSearchAndDraft() {
        previewNetworks("success")
        let name = application.textFields["wifi.ssid"]
        XCTAssertTrue(name.waitForExistence(timeout: 10))
        name.tap()
        name.typeText(" edited")
        application.toolbars.buttons["完成"].tap()
        searchNetworks()
        let hint = application.staticTexts["networks.hotspotHint"]
        XCTAssertTrue(hint.waitForExistence(timeout: 5))
        XCTAssertTrue(hint.isHittable)
        XCTAssertEqual(hint.label, "如果找不到本机的 iPhone 热点，请先进入系统「设置 → 个人热点」页面，再返回重新扫描。")
        XCTAssertFalse(application.buttons["networks.hotspotHelp"].exists)
        XCTAssertFalse(application.buttons["hotspot.open.primary"].exists)
        XCTAssertTrue(application.buttons["network.Test iPhone"].waitForExistence(timeout: 5))
        capture("Personal hotspot discovery hint")
        application.navigationBars["附近的 Wi-Fi"].buttons["取消"].tap()
        XCTAssertEqual(name.value as? String, "Saved iPhone edited")
    }

    func testHotspotHintIsVisibleWhenScanIsEmpty() {
        previewNetworks("empty")
        searchNetworks()
        XCTAssertTrue(application.staticTexts["未发现网络"].waitForExistence(timeout: 5))
        let hint = application.staticTexts["networks.hotspotHint"]
        XCTAssertTrue(hint.isHittable)
        XCTAssertTrue(hint.label.contains("设置 → 个人热点"))
        XCTAssertFalse(application.buttons["networks.hotspotHelp"].exists)
    }

    func testSavedNetworkReusesPassword() {
        previewNetworks("success")
        let disclosure = application.buttons["已保存的网络, 1"]
        if disclosure.exists { disclosure.tap() }
        else { application.buttons["已保存的网络"].tap() }
        let saved = application.buttons.matching(NSPredicate(format: "label CONTAINS %@", "Saved iPhone")).firstMatch
        XCTAssertTrue(saved.waitForExistence(timeout: 5))
        saved.tap()
        XCTAssertEqual(application.secureTextFields["wifi.password"].value as? String, "留空沿用已存密码")
        XCTAssertFalse(application.buttons["configuration.discard"].exists)
    }

    func testSavedNetworksAndWiFiFieldsUseCompactRows() {
        application.terminate()
        application.launchArguments += ["--preview-networks", "success", "--preview-saved-networks"]
        application.launch()
        let disclosure = application.buttons.matching(NSPredicate(format: "label BEGINSWITH %@", "已保存的网络")).firstMatch
        XCTAssertTrue(disclosure.waitForExistence(timeout: 5))
        disclosure.tap()
        let first = application.buttons["wifi.saved.0"]
        let second = application.buttons["wifi.saved.1"]
        let third = application.buttons["wifi.saved.2"]
        XCTAssertTrue(first.waitForExistence(timeout: 5))
        capture("Compact Wi-Fi with saved networks")
        for row in [first, second, third] {
            XCTAssertEqual(row.frame.height, 44, accuracy: 0.5)
        }
        XCTAssertLessThanOrEqual(second.frame.minY - first.frame.minY, 48)
        XCTAssertLessThanOrEqual(third.frame.minY - second.frame.minY, 48)
        let actions = application.buttons["Saved iPhone 的操作"]
        XCTAssertGreaterThanOrEqual(actions.frame.width, 44)
        XCTAssertGreaterThanOrEqual(actions.frame.height, 44)
        let name = application.textFields["wifi.ssid"]
        let password = application.secureTextFields["wifi.password"]
        XCTAssertLessThanOrEqual(password.frame.midY - name.frame.midY, 53)
        second.tap()
        XCTAssertEqual(name.value as? String, "Office Wi-Fi")
        XCTAssertEqual(password.value as? String, "留空沿用已存密码")
        actions.tap()
        XCTAssertTrue(application.buttons["忘记网络"].waitForExistence(timeout: 5))
    }

    func testEmptyScanCanRescanOrReturnToManualInput() {
        previewNetworks("empty")
        searchNetworks()
        XCTAssertTrue(application.staticTexts["未发现网络"].waitForExistence(timeout: 5))
        capture("No networks")
        let rescan = application.buttons["networks.rescan"]
        XCTAssertTrue(rescan.isEnabled)
        rescan.tap()
        XCTAssertTrue(application.staticTexts["未发现网络"].waitForExistence(timeout: 5))
        application.navigationBars["附近的 Wi-Fi"].buttons["取消"].tap()
        XCTAssertTrue(application.textFields["wifi.ssid"].isEnabled)
    }

    func testFailedScanPreservesManualInput() {
        previewNetworks("failure")
        searchNetworks()
        XCTAssertTrue(application.staticTexts["扫描未完成"].waitForExistence(timeout: 5))
        XCTAssertFalse(application.buttons["networks.rescan"].isEnabled)
        application.navigationBars["附近的 Wi-Fi"].buttons["取消"].tap()
        XCTAssertTrue(application.textFields["wifi.ssid"].isEnabled)
        XCTAssertEqual(application.textFields["wifi.ssid"].value as? String, "Saved iPhone")
    }

    func testLargeTextAllowsSearchAndSaveNavigation() {
        application.terminate()
        application.launchArguments += ["--preview-networks", "success", "-UIPreferredContentSizeCategoryName", "UICTContentSizeCategoryAccessibilityXXXL"]
        application.launch()
        searchNetworks()
        XCTAssertTrue(application.buttons["network.Test iPhone"].waitForExistence(timeout: 5))
        capture("Accessible Wi-Fi search")
        application.navigationBars["附近的 Wi-Fi"].buttons["取消"].tap()
        scrollTo(application.buttons["configuration.save"])
        XCTAssertTrue(application.buttons["configuration.save"].isHittable)
    }

    private func previewNetworks(_ result: String) {
        application.terminate()
        application.launchArguments += ["--preview-networks", result]
        application.launch()
    }

    private func searchNetworks() {
        let search = application.buttons["wifi.search"]
        scrollTo(search)
        XCTAssertTrue(search.waitForExistence(timeout: 5))
        search.tap()
        XCTAssertTrue(application.navigationBars["附近的 Wi-Fi"].waitForExistence(timeout: 5))
    }

    private func capture(_ name: String) {
        let attachment = XCTAttachment(screenshot: application.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func scrollTo(_ element: XCUIElement) {
        for _ in 0..<12 {
            let declinePasswordSave = application.buttons["Not Now"]
            if declinePasswordSave.exists { declinePasswordSave.tap() }
            if element.isHittable { return }
            application.swipeUp()
        }
        XCTFail("Could not scroll to \(element.identifier)")
    }
}
