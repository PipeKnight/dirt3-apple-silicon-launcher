import AppKit
import UniformTypeIdentifiers

private let gameIdentifier = "com.feralinteractive.dirt3"
private let runtimeIdentifier = "com.khesin.mac32"
private let gameFileName = "DiRT 3 Complete Edition.app"
private let steamIdentifier = "com.valvesoftware.steam"
private let steamGameDirectory = "Library/Application Support/Steam/steamapps/common/DiRT 3 Complete Edition"

private enum LaunchError: LocalizedError {
    case wrongApp, missingSteam, missingRuntime, logCreation

    var errorDescription: String? {
        switch self {
        case .wrongApp: "Choose the 32-bit DiRT 3 Complete Edition Mac application (.app)."
        case .missingSteam: "Open Steam and sign in, then click Play again."
        case .missingRuntime: "The compatibility runtime is missing. Replace this launcher with a complete copy."
        case .logCreation: "The launcher could not create its session log."
        }
    }
}

private func verifiedGame(at candidate: URL) throws -> URL {
    let game = candidate.resolvingSymlinksInPath().standardizedFileURL
    guard game.pathExtension.lowercased() == "app",
          let bundle = Bundle(url: game), bundle.bundleIdentifier == gameIdentifier,
          bundle.executableArchitectures?.contains(NSNumber(value: NSBundleExecutableArchitectureI386)) == true,
          let executable = bundle.executableURL, FileManager.default.isReadableFile(atPath: executable.path) else {
        throw LaunchError.wrongApp
    }
    return game
}

private func isSteamGame(_ game: URL) -> Bool {
    FileManager.default.isReadableFile(
        atPath: game.appendingPathComponent("Contents/Frameworks/libsteam_api.dylib").path
    )
}

@MainActor
private final class Launcher: NSObject, NSApplicationDelegate {
    private let window = NSWindow(
        contentRect: NSRect(x: 0, y: 0, width: 500, height: 275),
        styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false
    )
    private let path = NSTextField(labelWithString: "")
    private let status = NSTextField(wrappingLabelWithString: "")
    private let play = NSButton(title: "Play", target: nil, action: nil)
    private let choose = NSButton(title: "Choose Game…", target: nil, action: nil)
    private let logs = NSButton(title: "Show Logs", target: nil, action: nil)
    private let skipVideos = NSButton(checkboxWithTitle: "Skip startup videos", target: nil, action: nil)
    private let adaptiveTriggers = NSButton(checkboxWithTitle: "DualSense adaptive triggers", target: nil, action: nil)
    private var game: URL?
    private var process: Process?
    private var lastLog: URL?

    func applicationDidFinishLaunching(_ notification: Notification) {
        window.title = "DiRT 3 Launcher"
        window.isReleasedWhenClosed = false
        let title = NSTextField(labelWithString: "DiRT 3 Complete Edition")
        title.font = .systemFont(ofSize: 25, weight: .semibold)
        let subtitle = NSTextField(labelWithString: "Play your installed Mac copy on Apple silicon.")
        subtitle.textColor = .secondaryLabelColor
        path.lineBreakMode = .byTruncatingMiddle
        path.isSelectable = true
        status.textColor = .secondaryLabelColor
        status.maximumNumberOfLines = 3

        configure(play, #selector(startGame))
        configure(choose, #selector(chooseGame))
        configure(logs, #selector(showLogs))
        skipVideos.target = self
        skipVideos.action = #selector(saveStartupVideoPreference)
        adaptiveTriggers.target = self
        adaptiveTriggers.action = #selector(saveAdaptiveTriggerPreference)
        adaptiveTriggers.toolTip = "Use the game's pedal and vehicle telemetry to add resistance to L2 and R2."
        UserDefaults.standard.register(defaults: ["skipStartupVideos": true])
        skipVideos.state = UserDefaults.standard.bool(forKey: "skipStartupVideos") ? .on : .off
        adaptiveTriggers.state = UserDefaults.standard.bool(forKey: "adaptiveTriggers") ? .on : .off
        play.keyEquivalent = "\r"
        logs.isEnabled = false
        let actions = NSStackView(views: [logs, NSView(), choose, play])
        actions.orientation = .horizontal
        actions.spacing = 8
        let fields = NSStackView(views: [title, subtitle, path, skipVideos, adaptiveTriggers, status, actions])
        fields.orientation = .vertical
        fields.alignment = .leading
        fields.spacing = 12
        fields.translatesAutoresizingMaskIntoConstraints = false
        let content = NSView()
        content.addSubview(fields)
        window.contentView = content
        NSLayoutConstraint.activate([
            fields.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 24),
            fields.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -24),
            fields.centerYAnchor.constraint(equalTo: content.centerYAnchor),
            path.widthAnchor.constraint(equalTo: fields.widthAnchor),
            status.widthAnchor.constraint(equalTo: fields.widthAnchor),
            actions.widthAnchor.constraint(equalTo: fields.widthAnchor),
        ])
        findGame()
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    private func configure(_ button: NSButton, _ action: Selector) {
        button.target = self
        button.action = action
        button.bezelStyle = .rounded
        button.setContentHuggingPriority(.required, for: .horizontal)
    }

    private func findGame() {
        let saved = UserDefaults.standard.string(forKey: "gamePath").map { URL(fileURLWithPath: $0) }
        let registered = NSWorkspace.shared.urlForApplication(withBundleIdentifier: gameIdentifier)
        let global = URL(fileURLWithPath: "/Applications").appendingPathComponent(gameFileName)
        let personal = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Applications").appendingPathComponent(gameFileName)
        let steam = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(steamGameDirectory).appendingPathComponent(gameFileName)
        let candidates = [saved, global, personal, steam, registered].compactMap { $0 }
        for candidate in candidates {
            if let found = try? verifiedGame(at: candidate) {
                select(found)
                return
            }
        }
        path.stringValue = "No supported copy selected"
        status.stringValue = "Choose the DiRT 3 application you downloaded."
        play.isEnabled = false
    }

    private func select(_ found: URL) {
        game = found
        UserDefaults.standard.set(found.path, forKey: "gamePath")
        path.stringValue = found.path
        path.toolTip = found.path
        status.stringValue = isSteamGame(found) ? "Steam edition. Keep Steam open while playing." : "Ready to play"
        play.isEnabled = true
    }

    @objc private func chooseGame() {
        let picker = NSOpenPanel()
        picker.title = "Choose DiRT 3 Complete Edition"
        picker.prompt = "Choose Game"
        picker.directoryURL = game?.deletingLastPathComponent() ?? URL(fileURLWithPath: "/Applications")
        picker.allowedContentTypes = [.applicationBundle]
        picker.canChooseDirectories = false
        picker.allowsMultipleSelection = false
        picker.beginSheetModal(for: window) { [weak self] result in
            guard let self, result == .OK, let candidate = picker.url else { return }
            do { self.select(try verifiedGame(at: candidate)) }
            catch { self.status.stringValue = error.localizedDescription }
        }
    }

    @objc private func saveStartupVideoPreference() {
        UserDefaults.standard.set(skipVideos.state == .on, forKey: "skipStartupVideos")
    }

    @objc private func saveAdaptiveTriggerPreference() {
        UserDefaults.standard.set(adaptiveTriggers.state == .on, forKey: "adaptiveTriggers")
    }

    @objc private func showLogs() {
        if let lastLog { NSWorkspace.shared.activateFileViewerSelecting([lastLog]) }
    }

    @objc private func startGame() {
        guard let game, process == nil else { return }
        if let active = NSRunningApplication.runningApplications(withBundleIdentifier: runtimeIdentifier).first {
            active.activate()
            status.stringValue = "A compatibility session is already running."
            return
        }
        do {
            let game = try verifiedGame(at: game)
            if isSteamGame(game), NSRunningApplication.runningApplications(withBundleIdentifier: steamIdentifier).isEmpty {
                throw LaunchError.missingSteam
            }
            let runtime = Bundle.main.bundleURL.appendingPathComponent("Contents/Helpers/Mac32.app/Contents/MacOS/mac32")
            guard FileManager.default.isExecutableFile(atPath: runtime.path) else { throw LaunchError.missingRuntime }
            let directory = try FileManager.default.url(for: .libraryDirectory, in: .userDomainMask,
                                                        appropriateFor: nil, create: false)
                .appendingPathComponent("Logs/DiRT 3 Launcher", isDirectory: true)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let log = directory.appendingPathComponent("\(UUID().uuidString).log")
            guard FileManager.default.createFile(atPath: log.path, contents: nil, attributes: [.posixPermissions: 0o600]) else {
                throw LaunchError.logCreation
            }
            lastLog = log
            logs.isEnabled = true
            let output = try FileHandle(forWritingTo: log)
            defer { try? output.close() }

            let child = Process()
            child.executableURL = runtime
            child.arguments = ["-v", game.path]
            var environment = ProcessInfo.processInfo.environment
            environment["MAC32_DIRT3_DIRECT"] = "1"
            environment["MAC32_DIRT3_SKIP_STARTUP_VIDEOS"] = skipVideos.state == .on ? "1" : "0"
            environment["MAC32_DIRT3_ADAPTIVE_TRIGGERS"] = adaptiveTriggers.state == .on ? "1" : "0"
            child.environment = environment
            child.currentDirectoryURL = game.deletingLastPathComponent()
            child.standardOutput = output
            child.standardError = output
            child.terminationHandler = { [weak self] child in
                let code = child.terminationStatus
                let signaled = child.terminationReason == .uncaughtSignal
                Task { @MainActor [weak self] in self?.didStop(code: code, signaled: signaled) }
            }
            try child.run()
            process = child
            play.isEnabled = false
            choose.isEnabled = false
            skipVideos.isEnabled = false
            adaptiveTriggers.isEnabled = false
            status.stringValue = "DiRT 3 is running."
        } catch {
            let failure = error as NSError
            let underlying = failure.userInfo[NSUnderlyingErrorKey] as? NSError
            if [failure, underlying].compactMap({ $0 }).contains(where: { $0.domain == NSPOSIXErrorDomain && $0.code == 86 }) {
                status.stringValue = "Rosetta is required to run the compatibility runtime."
            } else {
                status.stringValue = error.localizedDescription
            }
        }
    }

    private func didStop(code: Int32, signaled: Bool) {
        process = nil
        play.isEnabled = true
        choose.isEnabled = true
        skipVideos.isEnabled = true
        adaptiveTriggers.isEnabled = true
        if signaled {
            status.stringValue = "DiRT 3 stopped on signal \(code). See the session log."
        } else if code == 78 {
            status.stringValue = "No compatibility profile matched this DiRT 3 build. See the session log."
        } else {
            status.stringValue = code == 0 ? "Ready to play" : "DiRT 3 exited with status \(code). See the session log."
        }
    }
}

let app = NSApplication.shared
private let launcher = Launcher()
app.delegate = launcher
app.setActivationPolicy(.regular)
app.run()
