import Foundation
import XCTest
import PortCore
@testable import NX2Studio

@MainActor
final class NX2ModelTests: XCTestCase {
    private let valid = """
    {"metadata":{"program_id":"0400C3F00006E000","platform":"switch2","main_core":0},
     "modules":[{"name":"main","build_id":"synthetic","file_bytes":256}],
     "target":{"name":"Synthetic test","initial_target":true,"gameplay_verified":false},
     "blockers":[],"runtime_ready":false}
    """

    private func preferences() -> UserDefaults {
        UserDefaults(suiteName: "NX2ModelTests." + UUID().uuidString)!
    }

    func testFailedReinspectionRemovesPreviouslyValidatedStateAndShowsRecoveryHint() async throws {
        let model = NX2Model(preferences: preferences(), executor: { _ in
            ProcessResult(exitCode: 1, output: "{\"error\":\"Input changed\",\"hint\":\"Inspect it again\"}")
        })
        model.gameURL = URL(fileURLWithPath: "/synthetic-input")
        model.inspection = try JSONDecoder().decode(NX2Inspection.self, from: Data(valid.utf8))
        model.workspaceURL = URL(fileURLWithPath: "/previous-workspace")
        model.inspect()
        await model.waitForCurrentOperation()
        XCTAssertNil(model.inspection)
        XCTAssertNil(model.workspaceURL)
        XCTAssertTrue(model.failure?.contains("Inspect it again") == true)
        XCTAssertFalse(model.isBusy)
    }

    func testCancellationCannotPublishLateSuccessfulInspection() async {
        let response = valid
        let model = NX2Model(preferences: preferences(), executor: { _ in
            try? await Task.sleep(for: .milliseconds(20))
            return ProcessResult(exitCode: 0, output: response)
        })
        model.gameURL = URL(fileURLWithPath: "/synthetic-input")
        model.inspect()
        await Task.yield()
        model.cancel()
        await model.waitForCurrentOperation()
        XCTAssertNil(model.inspection)
        XCTAssertEqual(model.status, "Cancelled")
        XCTAssertFalse(model.isBusy)
    }

    func testResourceSelectionRequiresValidationAndUsesSeparatePreference() async {
        let saved = preferences()
        saved.set("/unrelated-studio-keys", forKey: "keysPath")
        let model = NX2Model(preferences: saved, executor: { command in
            XCTAssertTrue(command.arguments.contains("registered"))
            return ProcessResult(exitCode: 0, output: """
            {"catalog":"/synthetic-resources","firmware_version":"23.0.1","source_platform":"switch1",
             "file_count":13,"switch2_compatibility":"unverified"}
            """)
        })
        model.selectResources(URL(fileURLWithPath: "/synthetic-resources"))
        await model.waitForCurrentOperation()
        XCTAssertEqual(model.resources?.file_count, 13)
        XCTAssertEqual(model.resources?.switch2_compatibility, "unverified")
        XCTAssertEqual(saved.string(forKey: "nx2ResourcesPath"), "/synthetic-resources")
        model.clearResources()
        XCTAssertNil(model.resourcesURL)
        XCTAssertNil(saved.string(forKey: "nx2ResourcesPath"))
        XCTAssertEqual(saved.string(forKey: "keysPath"), "/unrelated-studio-keys")
    }

    private var nativeInput: String {
        valid.replacingOccurrences(of: "\"runtime_ready\":false", with:
            "\"runtime_ready\":false,\"native_export\":{\"available\":true,\"name\":\"Synthetic game\",\"commit\":\"synthetic\"}")
    }

    func testNativeExportRequiresVerifiedReceiptRatherThanExitStatusAlone() async throws {
        let model = NX2Model(preferences: preferences(), executor: { command in
            XCTAssertTrue(command.arguments.contains("export"))
            XCTAssertTrue(command.arguments.contains("--engine"))
            XCTAssertFalse(command.arguments.contains("-k"))
            return ProcessResult(exitCode: 0, output: "{\"stage\":\"progress\"}")
        })
        model.gameURL = URL(fileURLWithPath: "/synthetic-input")
        model.inspection = try JSONDecoder().decode(NX2Inspection.self, from: Data(nativeInput.utf8))
        model.nativeEngineReady = true
        model.exportMac(to: URL(fileURLWithPath: "/synthetic.app"))
        await model.waitForCurrentOperation()
        XCTAssertNil(model.exportedAppURL)
        XCTAssertTrue(model.failure?.contains("receipt") == true)
    }

    func testNativeExportSelectsTheFinalVerifiedArtifactAfterProgress() async throws {
        let model = NX2Model(preferences: preferences(), executor: { _ in
            ProcessResult(exitCode: 0, output: """
            {"ev":"progress","name":"Mac shaders","frac":0.8}
            {"stage":"native-exported","artifact":"/synthetic.app","signature_verified":true}
            """)
        })
        model.gameURL = URL(fileURLWithPath: "/synthetic-input")
        model.inspection = try JSONDecoder().decode(NX2Inspection.self, from: Data(nativeInput.utf8))
        model.nativeEngineReady = true
        model.exportMac(to: URL(fileURLWithPath: "/synthetic.app"))
        await model.waitForCurrentOperation()
        XCTAssertEqual(model.exportedAppURL?.path, "/synthetic.app")
        XCTAssertTrue(model.status.contains("gameplay needs testing"))
    }

    func testCancelledExportCannotPublishLateReceipt() async throws {
        let model = NX2Model(preferences: preferences(), executor: { _ in
            try? await Task.sleep(for: .milliseconds(20))
            return ProcessResult(exitCode: 0, output: "{\"stage\":\"native-exported\",\"artifact\":\"/synthetic.app\",\"signature_verified\":true}")
        })
        model.gameURL = URL(fileURLWithPath: "/synthetic-input")
        model.inspection = try JSONDecoder().decode(NX2Inspection.self, from: Data(nativeInput.utf8))
        model.nativeEngineReady = true
        model.exportMac(to: URL(fileURLWithPath: "/synthetic.app"))
        await Task.yield()
        model.cancel()
        await model.waitForCurrentOperation()
        XCTAssertNil(model.exportedAppURL)
        XCTAssertEqual(model.status, "Cancelled")
    }
}
