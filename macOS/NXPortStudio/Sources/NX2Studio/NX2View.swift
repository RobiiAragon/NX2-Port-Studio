// SPDX-License-Identifier: GPL-3.0-or-later
import AppKit
import SwiftUI

struct NX2View: View {
    @Bindable var model: NX2Model
    var body: some View {
        NavigationSplitView {
            VStack(alignment: .leading, spacing: 18) {
                Label("NX2 Port Studio", systemImage: "cpu")
                    .font(.title3.weight(.semibold))
                Text(nx2t("Switch 2 → Apple Silicon")).foregroundStyle(.secondary)
                Divider()
                Label(nx2t("Extracted game"), systemImage: "folder")
                Spacer()
                Text(nx2t("Experimental Switch 2 tools"))
                    .font(.callout).foregroundStyle(.secondary)
                Text(nx2t("Experimental development"))
                    .font(.caption).foregroundStyle(.secondary)
                Link(nx2t("Project source"), destination: URL(string: "https://github.com/RobiiAragon/NX2-Port-Studio")!)
            }
            .padding(20)
            .navigationSplitViewColumnWidth(min: 210, ideal: 230, max: 270)
        } detail: {
            VStack(alignment: .leading, spacing: 0) {
                HStack {
                    VStack(alignment: .leading, spacing: 6) {
                        Text(nx2t("Start with your dump")).font(.title2.weight(.semibold))
                        Text(nx2t("Import the decrypted files used by Eden NX2. No prod.keys needed."))
                            .foregroundStyle(.secondary)
                    }
                    Spacer()
                    if model.isBusy { ProgressView().controlSize(.small).accessibilityLabel(nx2t(model.status)) }
                }.padding(24)
                Divider()
                Form {
                    if let failure = model.failure {
                        Section {
                            Label(nx2t("Check the selected files"), systemImage: "exclamationmark.triangle")
                                .fontWeight(.semibold)
                            Text(failure).textSelection(.enabled)
                        }
                    }
                    Section {
                        Text(nx2t("Choose the game root or its exefs folder. A complete ExeFS and RomFS are required."))
                        if let url = model.gameURL {
                            Text(url.path).font(.callout.monospaced()).textSelection(.enabled)
                        }
                        HStack {
                            Button(nx2t(model.gameURL == nil ? "Choose game folder…" : "Change folder…")) { model.chooseGame() }
                                .disabled(model.isBusy)
                            Spacer()
                            Button(nx2t("Inspect dump")) { model.inspect() }
                                .buttonStyle(.borderedProminent)
                                .disabled(model.gameURL == nil || model.isBusy)
                                .keyboardShortcut(.return, modifiers: .command)
                        }
                    } header: { Text(nx2t("Decrypted input")) }
                    if let report = model.inspection {
                        Section {
                            LabeledContent(nx2t("Game"), value: report.target.name)
                            LabeledContent(nx2t("Program ID"), value: report.metadata.program_id)
                            LabeledContent(nx2t("Platform"), value: "Switch 2 · ARM64")
                            ForEach(report.modules) { module in
                                LabeledContent(module.name) {
                                    Text(module.build_id.prefix(16)).font(.callout.monospaced())
                                        .help(module.build_id).textSelection(.enabled)
                                }
                            }
                        } header: { Text(nx2t("Validated modules")) }
                        Section {
                            Picker(nx2t("Target platform"), selection: $model.destination) {
                                Text("Mac · arm64").tag("macOS")
                                Text("iPhone / iPad · arm64").tag("iOS")
                            }.disabled(model.isBusy)
                            Text(nx2t("Prepare a private report and build plan. Game files stay in their original folder."))
                            Button(nx2t("Prepare workspace…")) { model.prepare() }.disabled(model.isBusy)
                            if let url = model.workspaceURL {
                                Button(nx2t("Show workspace in Finder")) { NSWorkspace.shared.activateFileViewerSelecting([url]) }
                            }
                        } header: { Text(nx2t("Bring-up workspace")) }
                    }
                    Section {
                        Text(nx2t("Mii and font resources are optional candidates. Switch 2 compatibility remains unverified."))
                            .foregroundStyle(.secondary)
                        if let selected = model.resources {
                            LabeledContent(nx2t("Source firmware"), value: "\(selected.source_platform) · \(selected.firmware_version)")
                            LabeledContent(nx2t("Verified files"), value: String(selected.file_count))
                        }
                        if let url = model.resourcesURL {
                            Text(url.path).font(.caption.monospaced()).textSelection(.enabled)
                        }
                        HStack {
                            Button(nx2t("Choose resource library…")) { model.chooseResources() }
                            if model.resourcesURL != nil {
                                Button(nx2t("Remove selection")) { model.clearResources() }
                            }
                        }.disabled(model.isBusy)
                    } header: { Text(nx2t("Optional system resources")) }
                    Section {
                        Label(nx2t("Dump import and validation"), systemImage: "checkmark.circle")
                        Label(nx2t("Standalone Ampere translator · synthetic tests"), systemImage: "wrench.and.screwdriver")
                        Label(nx2t("Native game execution and NVN2 on Metal · pending"), systemImage: "clock")
                        Text(nx2t("This version prepares development workspaces. It does not yet export a playable Mac app or iPhone IPA."))
                            .foregroundStyle(.secondary)
                        Button(nx2t("Prepare validation tools…")) { model.prepareTools() }.disabled(model.isBusy)
                        Text(nx2t("Installs LZ4 in a separate NX2 environment for compressed modules."))
                            .font(.callout).foregroundStyle(.secondary)
                    } header: { Text(nx2t("Current capabilities")) }
                }
                .formStyle(.grouped)
                Divider()
                HStack {
                    Text(nx2t(model.status)).font(.callout).foregroundStyle(.secondary)
                    Spacer()
                    if model.isBusy { Button(nx2t("Cancel")) { model.cancel() } }
                }.padding(16)
            }
        }
    }
}
