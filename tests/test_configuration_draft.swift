import Foundation

@main struct ConfigurationDraftTests {
    static func main() {
        var network = ConfigurationDraft(WiFiConfiguration(ssid: "Office", password: "cached-password"))
        precondition(!network.isDirty, "A cached password loaded with the device baseline is not an edit")
        network.value.password = "changed-password"
        precondition(network.isDirty)
        network.receive(WiFiConfiguration(ssid: "Office", password: "cached-password"))
        precondition(network.value.password == "changed-password" && network.isDirty,
                     "Polling must not clear an unsaved password")
        network.discard()
        precondition(network.value.password == "cached-password" && !network.isDirty)
        network.value = WiFiConfiguration(ssid: "Cafe", password: "draft-password")
        network.receive(WiFiConfiguration(ssid: "Home", password: "stored-password"))
        precondition(network.value.ssid == "Cafe" && network.isDirty)
        network.discard()
        precondition(network.value.ssid == "Home" && !network.isDirty,
                     "Discard restores the latest device configuration without a write")
        network.value.password = "new-password"
        let written = network.value
        network.accept(written)
        precondition(!network.isDirty && network.value.password == "new-password",
                     "A successful save keeps all fields filled")
        network.value.password = "next-edit"
        network.accept(written)
        precondition(network.isDirty && network.value.password == "next-edit",
                     "Acknowledgements must not overwrite edits made after submission")
        network.receive(WiFiConfiguration(ssid: "Home", password: "stored-password"))
        precondition(network.isDirty && network.value.password == "next-edit",
                     "Reload after a failed or partial save preserves the draft")

        var selection = ConfigurationDraft(1)
        selection.value = 2
        selection.receive(1)
        precondition(selection.isDirty && selection.value == 2)
        selection.discard()
        precondition(selection.value == 1 && !selection.isDirty)
        selection.receive(2)
        precondition(selection.value == 2 && !selection.isDirty)

        var pairing = ConfigurationDraft("")
        pairing.value = "single-use-code"
        precondition(pairing.isDirty)
        pairing.accept(pairing.value)
        precondition(!pairing.isDirty && !pairing.value.isEmpty)
        pairing.value = "replacement-code"
        pairing.discard()
        precondition(pairing.value == "single-use-code" && !pairing.isDirty,
                     "Discard must not accidentally resubmit an already saved pairing code")

        let storedRoute = ProxySettings(enabled: true, host: "proxy.example.net", port: 443, hasSecret: true)
        var route = ConfigurationDraft(RouteForm(storedRoute))
        precondition(!route.isDirty && route.value.settings.valid)
        route.value.secret = "replacement-secret"
        let acknowledged = route.value.acknowledged()
        route.value = acknowledged
        route.accept(acknowledged)
        route.receive(route.saved.refreshed(storedRoute))
        precondition(!route.isDirty && route.value.secret == "replacement-secret",
                     "A secret-redacted device reload does not erase a successfully saved local field")
        route.value.enabled = false
        route.receive(route.saved.refreshed(storedRoute))
        precondition(route.isDirty && !route.value.enabled)
        route.discard()
        precondition(route.value.enabled && !route.isDirty)
        let movedRoute = ProxySettings(enabled: true, host: "new.example.net", port: 8443, hasSecret: true)
        route.receive(route.saved.refreshed(movedRoute))
        precondition(route.value.host == "new.example.net" && route.value.secret.isEmpty && !route.isDirty)

        var drafts = [1: ConfigurationDraft(RouteForm(storedRoute)), 2: ConfigurationDraft(RouteForm())]
        drafts[1]!.value.host = "draft.example.net"
        drafts[2]!.receive(RouteForm())
        precondition(drafts[1]!.isDirty && !drafts[2]!.isDirty,
                     "Switching assistants keeps their unsaved routes independent")
        var language = ConfigurationDraft("zh")
        var volume = ConfigurationDraft(70)
        language.value = "en"
        volume.value = 90
        language.receive("zh")
        volume.receive(70)
        precondition(language.isDirty && language.value == "en" && volume.isDirty && volume.value == 90,
                     "Preferences remain drafts across polling and failed writes")
        language.discard()
        volume.discard()
        precondition(language.value == "zh" && volume.value == 70 && !language.isDirty && !volume.isDirty)
        volume.value = 85
        volume.accept(85)
        volume.receive(85)
        precondition(volume.value == 85 && !volume.isDirty, "Saving preferences preserves the displayed value")

        let newRoute = RouteForm(storedRoute)
        let edits = [AgentConfigurationEdit(key: "new", name: "New-Agent", code: "new-code", route: newRoute),
                     AgentConfigurationEdit(key: "saved:Other", name: "Other", code: nil, route: RouteForm())]
        let targetWiFi = WiFiConfiguration(ssid: "Cafe", password: "draft-password")
        let plan = ConfigurationSave.plan(agents: edits, selection: "New-Agent", language: "en", volume: 85, wifi: targetWiFi)
        precondition(plan.steps == [.pairing(key: "new", name: "New-Agent", code: "new-code"),
                                    .route(name: "New-Agent", value: newRoute),
                                    .route(name: "Other", value: RouteForm()), .selection(name: "New-Agent"),
                                    .language("en"), .volume(85), .wifi(targetWiFi)],
                     "Global save includes every agent draft, with activation after pairing and Wi-Fi last")
        var queue = plan
        precondition(!queue.readBack(revision: 1), "Unacknowledged commands cannot advance")
        precondition(queue.acknowledge(revision: 10))
        precondition(!queue.acknowledge(revision: 10), "Duplicate acknowledgements cannot advance twice")
        precondition(queue.acknowledgedCount == 1 && queue.completedCount == 0)
        precondition(!queue.readBack(revision: 10), "A stale readback cannot dispatch the next write")
        precondition(queue.readBack(revision: 11) && queue.completedCount == 1)
        precondition(queue.current == plan.steps[1])
        precondition(!queue.readBack(revision: 12), "A second read cannot skip an unacknowledged step")
        for revision in 12...17 {
            precondition(queue.acknowledge(revision: UInt(revision)))
            precondition(queue.readBack(revision: UInt(revision + 1)))
        }
        precondition(queue.current == nil && queue.completedCount == plan.steps.count)
        precondition(!queue.acknowledge(revision: 20))

        pairing.value = "single-use-code-2"
        var partial = ConfigurationSave.plan(agents: [AgentConfigurationEdit(key: "saved:Other", name: "Other",
            code: pairing.value, route: newRoute)], selection: nil, language: nil, volume: nil, wifi: nil)
        precondition(partial.acknowledge(revision: 20))
        pairing.accept(pairing.value)
        precondition(partial.readBack(revision: 21))
        precondition(partial.acknowledgedCount == 1 && partial.current == .route(name: "Other", value: newRoute))
        let retry = ConfigurationSave.plan(agents: [AgentConfigurationEdit(key: "saved:Other", name: "Other",
            code: pairing.isDirty ? pairing.value : nil, route: newRoute)], selection: nil, language: nil, volume: nil, wifi: nil)
        precondition(retry.steps == [.route(name: "Other", value: newRoute)],
                     "A partial-save retry does not resubmit an acknowledged single-use code")
        let preferencesOnly = ConfigurationSave.plan(agents: [], selection: nil, language: "en", volume: 80, wifi: nil)
        precondition(preferencesOnly.steps == [.language("en"), .volume(80)],
                     "Changing language or volume must not require Wi-Fi or pairing input")
        precondition(WiFiListLayout.height(rowCount: 2, expanded: false) == 98)
        precondition(WiFiListLayout.height(rowCount: 4, expanded: false) == 198)
        precondition(WiFiListLayout.height(rowCount: 20, expanded: true) == 298)
        precondition(WiFiListLayout.height(rowCount: 0, expanded: false) == 68)
        print("Configuration drafts, global save sequencing, partial retry, preference drafts and adaptive Wi-Fi layout PASS")
    }
}
