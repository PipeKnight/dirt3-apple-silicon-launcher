import AppKit
import CryptoKit
import UniformTypeIdentifiers

private let gameIdentifier = "com.feralinteractive.dirt3"
private let runtimeIdentifier = "com.khesin.mac32"
private let supportedExecutable = "0415a9ecbaccdc25752a327c7b8a0dbd62435a308d0a95652d4a286e6a579014"
private let gameFileName = "DiRT 3 Complete Edition.app"

private enum LaunchError: LocalizedError {
    case wrongApp, unsupportedBuild, missingRuntime, logCreation

    var errorDescription: String? {
        switch self {
        case .wrongApp: "Choose the original DiRT 3 Complete Edition Mac application from Feral Interactive."
        case .unsupportedBuild: "This build of DiRT 3 is unsupported. This launcher requires Feral Mac version 1.0, build 85186.17625.4091."
        case .missingRuntime: "The compatibility runtime is missing. Replace this launcher with a complete copy."
        case .logCreation: "The launcher could not create its session log."
        }
    }
}

private func verifiedGame(at candidate: URL) throws -> URL {
    let game = candidate.resolvingSymlinksInPath().standardizedFileURL
    guard game.pathExtension.lowercased() == "app",
          let bundle = Bundle(url: game), bundle.bundleIdentifier == gameIdentifier,
          let executable = bundle.executableURL, FileManager.default.isReadableFile(atPath: executable.path) else {
        throw LaunchError.wrongApp
    }
    let input = try FileHandle(forReadingFrom: executable)
    defer { try? input.close() }
    var digest = SHA256()
    while let block = try input.read(upToCount: 1024 * 1024), !block.isEmpty {
        digest.update(data: block)
    }
    let fingerprint = digest.finalize().map { String(format: "%02x", $0) }.joined()
    guard fingerprint == supportedExecutable else { throw LaunchError.unsupportedBuild }
    return game
}

@MainActor
private final class Launcher: NSObject, NSApplicationDelegate {
    private let window = NSWindow(
        contentRect: NSRect(x: 0, y: 0, width: 500, height: 245),
        styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false
    )
    private let path = NSTextField(labelWithString: "")
    private let status = NSTextField(wrappingLabelWithString: "")
    private let play = NSButton(title: "Play", target: nil, action: nil)
    private let choose = NSButton(title: "Choose Game…", target: nil, action: nil)
    private let logs = NSButton(title: "Show Logs", target: nil, action: nil)
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
        play.keyEquivalent = "\r"
        logs.isEnabled = false
        let actions = NSStackView(views: [logs, NSView(), choose, play])
        actions.orientation = .horizontal
        actions.spacing = 8
        let fields = NSStackView(views: [title, subtitle, path, status, actions])
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
        let candidates = [saved, registered, global, personal].compactMap { $0 }
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
        status.stringValue = "Ready to play"
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
        if signaled {
            status.stringValue = "DiRT 3 stopped on signal \(code). See the session log."
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
