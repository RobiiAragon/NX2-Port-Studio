// SPDX-License-Identifier: GPL-3.0-or-later
import AppKit
import SwiftUI

@main
struct NX2App: App {
    @State private var model = NX2Model()
    var body: some Scene {
        Window("NX2 Port Studio", id: "nx2-main") {
            NX2View(model: model)
                .frame(minWidth: 800, minHeight: 580)
                .onReceive(NotificationCenter.default.publisher(for: NSApplication.willTerminateNotification)) { _ in
                    model.runner.cancelImmediately()
                }
        }
        .defaultSize(width: 1040, height: 720)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button(nx2t("Open extracted game…")) { model.chooseGame() }
                    .keyboardShortcut("o").disabled(model.isBusy)
            }
        }
    }
}

func nx2t(_ key: String) -> String {
    // The initial NX2 desktop release uses Spanish, independently of Studio 1's preferences.
    guard let url = Bundle.module.url(forResource: "es", withExtension: "lproj"),
          let bundle = Bundle(url: url) else { return key }
    return bundle.localizedString(forKey: key, value: key, table: nil)
}
