// SPDX-License-Identifier: GPL-3.0-or-later
import AppKit
import Observation
import PortCore
import UniformTypeIdentifiers

struct NX2Inspection: Decodable, Sendable {
    struct Metadata: Decodable, Sendable {
        let program_id: String
        let platform: String
        let main_core: Int
    }
    struct Module: Decodable, Identifiable, Sendable {
        let name: String
        let build_id: String
        let file_bytes: Int64
        var id: String { name }
    }
    struct Target: Decodable, Sendable {
        let name: String
        let initial_target: Bool
        let gameplay_verified: Bool
    }
    struct Blocker: Decodable, Identifiable, Sendable {
        let id: String
        let detail: String
    }
    let metadata: Metadata
    let modules: [Module]
    let target: Target
    let blockers: [Blocker]
    let runtime_ready: Bool
    struct NativeExport: Decodable, Sendable {
        let available: Bool
        let reason: String?
        let name: String?
        let commit: String?
    }
    let native_export: NativeExport?
}

private struct NX2Result: Decodable {
    let error: String?
    let hint: String?
    let workspace: String?
    let report: NX2Inspection?
}

private struct NX2NativeResult: Decodable {
    let stage: String
    let artifact: String?
    let signature_verified: Bool?
}

struct NX2Resources: Decodable, Sendable {
    let catalog: String
    let firmware_version: String
    let source_platform: String
    let file_count: Int
    let switch2_compatibility: String
}

@MainActor @Observable
final class NX2Model {
    var gameURL: URL?
    var inspection: NX2Inspection?
    var workspaceURL: URL?
    var resourcesURL: URL?
    var resources: NX2Resources?
    var destination = "macOS"
    var isBusy = false
    var failure: String?
    var status = "Waiting for an extracted dump"
    var nativeEngineReady = false
    var exportedAppURL: URL?
    var exportProgress: Double?
    var gameLanguage = "es"
    @ObservationIgnored let runner: ProcessRunner
    @ObservationIgnored private let preferences: UserDefaults
    @ObservationIgnored private let executeCommand: @Sendable (ProcessCommand) async throws -> ProcessResult
    @ObservationIgnored private var task: Task<Void, Never>?
    @ObservationIgnored private let streamsOutput: Bool
    @ObservationIgnored private var operationID = UUID()

    init(preferences: UserDefaults = .standard, runner: ProcessRunner = ProcessRunner(),
         executor: (@Sendable (ProcessCommand) async throws -> ProcessResult)? = nil) {
        self.preferences = preferences
        self.runner = runner
        self.executeCommand = executor ?? { command in try await runner.run(command) }
        self.streamsOutput = executor == nil
        if let path = preferences.string(forKey: "nx2ResourcesPath") {
            self.resourcesURL = URL(fileURLWithPath: path)
        }
    }

    var repositoryURL: URL {
        if let bundled = Bundle.main.resourceURL?.appendingPathComponent("nx2-engine"),
           FileManager.default.fileExists(atPath: bundled.appendingPathComponent("scripts/nx2.py").path) {
            return bundled
        }
        #if DEBUG
        return URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        #else
        return Bundle.main.bundleURL.appendingPathComponent("Contents/Resources/nx2-engine")
        #endif
    }

    private var environmentURL: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
            .appendingPathComponent("NX2 Port Studio/venv")
    }

    private var pythonURL: URL {
        let managed = environmentURL.appendingPathComponent("bin/python3")
        if FileManager.default.isExecutableFile(atPath: managed.path) { return managed }
        let development = repositoryURL.appendingPathComponent(".nx2-build/venv/bin/python3")
        if FileManager.default.isExecutableFile(atPath: development.path) { return development }
        return ToolEnvironment.defaultPythonURL
    }

    private var nativeEngineURL: URL {
        let manifest = repositoryURL.appendingPathComponent("nx2/native_runtime.json")
        let data = (try? Data(contentsOf: manifest)) ?? Data()
        let json = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
        let commit = json?["commit"] as? String ?? "unavailable"
        return environmentURL.deletingLastPathComponent().appendingPathComponent("native-engines/" + commit)
    }

    func prepareNativeEngine() {
        guard !isBusy else { return }
        nativeEngineReady = false
        begin(status: "Preparing Mac export tools") {
            let fm = FileManager.default
            try fm.createDirectory(at: self.environmentURL.deletingLastPathComponent(), withIntermediateDirectories: true)
            if !fm.isExecutableFile(atPath: self.environmentURL.appendingPathComponent("bin/python3").path) {
                let created = try await self.executeCommand(ProcessCommand(executableURL: ToolEnvironment.defaultPythonURL,
                    arguments: ["-m", "venv", self.environmentURL.path]))
                guard created.exitCode == 0 else { throw PortError.processFailed(created.exitCode, created.output) }
            }
            let installed = try await self.executeCommand(ProcessCommand(
                executableURL: self.environmentURL.appendingPathComponent("bin/python3"),
                arguments: ["-m", "pip", "install", "--disable-pip-version-check", "-r",
                            self.repositoryURL.appendingPathComponent("requirements-nx2-runtime.txt").path]))
            guard installed.exitCode == 0 else { throw PortError.processFailed(installed.exitCode, installed.output) }
            try fm.createDirectory(at: self.nativeEngineURL.deletingLastPathComponent(), withIntermediateDirectories: true)
            let output = try await self.run(["native", "prepare", self.nativeEngineURL.path, "--json"])
            let response = try JSONDecoder().decode(NX2NativeResult.self, from: Data(output.utf8))
            guard response.stage == "native-engine-ready" else { throw PortError.invalidConfiguration("The native engine was not verified.") }
            self.nativeEngineReady = true
            self.status = "Mac export tools ready"
        }
    }

    func chooseMacExport() {
        guard !isBusy, nativeEngineReady, inspection?.native_export?.available == true else { return }
        let panel = NSSavePanel()
        panel.title = nx2t("Save the native Mac app")
        panel.directoryURL = URL(fileURLWithPath: "/Applications", isDirectory: true)
        panel.nameFieldStringValue = (inspection?.native_export?.name ?? "NX2 Game") + " — NX2.app"
        panel.allowedContentTypes = [.application]
        guard panel.runModal() == .OK, let output = panel.url else { return }
        exportMac(to: output)
    }

    func exportMac(to output: URL) {
        guard !isBusy, nativeEngineReady, inspection?.native_export?.available == true, let gameURL else { return }
        exportedAppURL = nil
        exportProgress = nil
        begin(status: "Building the native Mac app") {
            let result = try await self.run(["native", "export", gameURL.path, "--engine", self.nativeEngineURL.path,
                                            "--output", output.path, "--language", self.gameLanguage, "--json"], streaming: true)
            // The native compiler emits real progress events before its final receipt.
            let line = result.split(separator: "\n").last(where: { $0.contains("\"native-exported\"") })
            guard let line else { throw PortError.invalidConfiguration("The build returned no verified export receipt.") }
            let receipt = try JSONDecoder().decode(NX2NativeResult.self, from: Data(line.utf8))
            guard receipt.stage == "native-exported", receipt.signature_verified == true,
                  receipt.artifact == output.path else { throw PortError.invalidConfiguration("The Mac export was not verified.") }
            self.exportedAppURL = output
            self.exportProgress = 1
            self.status = "Native Mac app created · gameplay needs testing"
        }
    }

    func prepareTools() {
        guard !isBusy else { return }
        begin(status: "Preparing validation tools") {
            let fm = FileManager.default
            try fm.createDirectory(at: self.environmentURL.deletingLastPathComponent(), withIntermediateDirectories: true)
            if !fm.isExecutableFile(atPath: self.environmentURL.appendingPathComponent("bin/python3").path) {
                let create = try await self.runner.run(ProcessCommand(executableURL: ToolEnvironment.defaultPythonURL,
                                                                       arguments: ["-m", "venv", self.environmentURL.path]))
                guard create.exitCode == 0 else { throw PortError.processFailed(create.exitCode, create.output) }
            }
            let install = try await self.runner.run(ProcessCommand(executableURL: self.environmentURL.appendingPathComponent("bin/python3"),
                arguments: ["-m", "pip", "install", "--disable-pip-version-check", "-r",
                            self.repositoryURL.appendingPathComponent("requirements-nx2.txt").path]))
            guard install.exitCode == 0 else { throw PortError.processFailed(install.exitCode, install.output) }
            self.status = "Validation tools ready"
        }
    }

    func chooseGame() {
        guard !isBusy else { return }
        let panel = NSOpenPanel()
        panel.title = nx2t("Choose the folder containing exefs and romfs")
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        guard panel.runModal() == .OK, let selected = panel.url else { return }
        selectGame(selected)
    }

    func selectGame(_ selected: URL) {
        guard !isBusy else { return }
        gameURL = selected
        inspection = nil
        workspaceURL = nil
        exportedAppURL = nil
        failure = nil
        status = "Ready to inspect"
    }

    func inspect() {
        guard !isBusy, let gameURL else { return }
        inspection = nil
        workspaceURL = nil
        begin(status: "Validating decrypted files") {
            let result = try await self.run(["inspect", gameURL.path, "--json"])
            self.inspection = try JSONDecoder().decode(NX2Inspection.self, from: Data(result.utf8))
            self.status = self.inspection?.native_export?.available == true ? "Input validated · Mac export profile available" : "Input validated · native export pending"
        }
    }

    func prepare() {
        guard !isBusy, inspection != nil, let gameURL else { return }
        let panel = NSOpenPanel()
        panel.title = nx2t("Choose where to create the private workspace")
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        guard panel.runModal() == .OK, let parent = panel.url else { return }
        let name = (inspection?.metadata.program_id ?? "game") + "-nx2-" + UUID().uuidString.prefix(8)
        let output = parent.appendingPathComponent(name, isDirectory: true)
        let platform = destination
        let selectedResources = resourcesURL
        workspaceURL = nil
        inspection = nil
        begin(status: "Preparing workspace") {
            var arguments = ["prepare", gameURL.path, "--output", output.path, "--platform", platform, "--json"]
            if let selectedResources { arguments += ["--resources", selectedResources.path] }
            let result = try await self.run(arguments)
            let response = try JSONDecoder().decode(NX2Result.self, from: Data(result.utf8))
            guard let path = response.workspace else {
                throw PortError.invalidConfiguration("The tool returned no workspace path.")
            }
            self.workspaceURL = URL(fileURLWithPath: path)
            self.inspection = response.report
            self.status = "Workspace prepared · native export pending"
        }
    }

    func chooseResources() {
        guard !isBusy else { return }
        let panel = NSOpenPanel()
        panel.title = nx2t("Choose a registered NX2 resource library")
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        if let resourcesURL { panel.directoryURL = resourcesURL.deletingLastPathComponent() }
        guard panel.runModal() == .OK, let selected = panel.url else { return }
        selectResources(selected)
    }

    func selectResources(_ selected: URL) {
        guard !isBusy else { return }
        resources = nil
        resourcesURL = nil
        preferences.removeObject(forKey: "nx2ResourcesPath")
        begin(status: "Checking optional resources") {
            let output = try await self.run(["resources", "registered", selected.path, "--json"])
            self.resources = try JSONDecoder().decode(NX2Resources.self, from: Data(output.utf8))
            self.resourcesURL = selected
            self.preferences.set(selected.path, forKey: "nx2ResourcesPath")
            self.status = "Resource candidates verified · game compatibility pending"
        }
    }

    func clearResources() {
        guard !isBusy else { return }
        resources = nil; resourcesURL = nil
        preferences.removeObject(forKey: "nx2ResourcesPath")
    }

    func waitForCurrentOperation() async { await task?.value }

    func cancel() { task?.cancel(); runner.cancel() }

    private func begin(status: String, operation: @escaping @MainActor () async throws -> Void) {
        operationID = UUID()
        failure = nil
        isBusy = true
        self.status = status
        task = Task {
            defer { self.isBusy = false; self.task = nil }
            do { try Task.checkCancellation(); try await operation() }
            catch is CancellationError { self.status = "Cancelled" }
            catch { self.failure = error.localizedDescription; self.status = "Input needs attention" }
        }
    }

    private func run(_ arguments: [String], streaming: Bool = false) async throws -> String {
        let command = ProcessCommand(executableURL: pythonURL,
                                     arguments: ["-B", repositoryURL.appendingPathComponent("scripts/nx2.py").path] + arguments,
                                     workingDirectoryURL: repositoryURL,
                                     environment: ["PYTHONDONTWRITEBYTECODE": "1"])
        let result: ProcessResult
        if streaming && streamsOutput {
            let token = operationID
            result = try await runner.run(command) { [weak self] line in
                guard let data = line.data(using: .utf8),
                      let event = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
                      let name = event["name"] as? String,
                      ["progress", "step", "stage"].contains(event["ev"] as? String ?? "") else { return }
                let step = event["step"] as? String ?? ""
                let fraction = event["frac"] as? Double
                Task { @MainActor in
                    guard let self, self.isBusy, self.operationID == token, self.task?.isCancelled == false else { return }
                    self.status = step.isEmpty ? name : name + " · " + step
                    if let fraction, fraction.isFinite { self.exportProgress = min(1, max(0, fraction)) }
                }
            }
        } else {
            result = try await executeCommand(command)
        }
        try Task.checkCancellation()
        guard result.exitCode == 0 else {
            let decoded = result.output.split(separator: "\n").reversed().compactMap {
                try? JSONDecoder().decode(NX2Result.self, from: Data($0.utf8))
            }.first(where: { $0.error != nil })
            throw PortError.invalidConfiguration([decoded?.error ?? result.output, decoded?.hint].compactMap { $0 }.joined(separator: "\n"))
        }
        return result.output
    }
}
