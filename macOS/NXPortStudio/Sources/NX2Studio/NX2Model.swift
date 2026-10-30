// SPDX-License-Identifier: GPL-3.0-or-later
import AppKit
import Observation
import PortCore

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
}

private struct NX2Result: Decodable {
    let error: String?
    let workspace: String?
}

@MainActor @Observable
final class NX2Model {
    var gameURL: URL?
    var inspection: NX2Inspection?
    var workspaceURL: URL?
    var destination = "macOS"
    var isBusy = false
    var failure: String?
    var status = "Waiting for an extracted dump"
    @ObservationIgnored let runner = ProcessRunner()
    @ObservationIgnored private var task: Task<Void, Never>?

    var repositoryURL: URL {
        if let bundled = Bundle.main.resourceURL?.appendingPathComponent("nx2-engine"),
           FileManager.default.fileExists(atPath: bundled.appendingPathComponent("scripts/nx2.py").path) {
            return bundled
        }
        return URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
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
        let panel = NSOpenPanel()
        panel.title = nx2t("Choose the folder containing exefs and romfs")
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        guard panel.runModal() == .OK, let selected = panel.url else { return }
        gameURL = selected
        inspection = nil
        workspaceURL = nil
        failure = nil
        status = "Ready to inspect"
    }

    func inspect() {
        guard !isBusy, let gameURL else { return }
        begin(status: "Validating decrypted files") {
            let result = try await self.run(["inspect", gameURL.path, "--json"])
            self.inspection = try JSONDecoder().decode(NX2Inspection.self, from: Data(result.utf8))
            self.status = "Input validated · native export pending"
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
        begin(status: "Preparing workspace") {
            let result = try await self.run(["prepare", gameURL.path, "--output", output.path,
                                             "--platform", platform, "--json"])
            let response = try JSONDecoder().decode(NX2Result.self, from: Data(result.utf8))
            guard let path = response.workspace else {
                throw PortError.invalidConfiguration("The tool returned no workspace path.")
            }
            self.workspaceURL = URL(fileURLWithPath: path)
            self.status = "Workspace prepared · native export pending"
        }
    }

    func cancel() { task?.cancel(); runner.cancel() }

    private func begin(status: String, operation: @escaping @MainActor () async throws -> Void) {
        failure = nil
        isBusy = true
        self.status = status
        task = Task {
            defer { self.isBusy = false; self.task = nil }
            do { try await operation() }
            catch is CancellationError { self.status = "Cancelled" }
            catch { self.failure = error.localizedDescription; self.status = "Input needs attention" }
        }
    }

    private func run(_ arguments: [String]) async throws -> String {
        let command = ProcessCommand(executableURL: pythonURL,
                                     arguments: ["-B", repositoryURL.appendingPathComponent("scripts/nx2.py").path] + arguments,
                                     workingDirectoryURL: repositoryURL,
                                     environment: ["PYTHONDONTWRITEBYTECODE": "1"])
        let result = try await runner.run(command)
        guard result.exitCode == 0 else {
            let decoded = try? JSONDecoder().decode(NX2Result.self, from: Data(result.output.utf8))
            throw PortError.invalidConfiguration(decoded?.error ?? result.output)
        }
        return result.output
    }
}
