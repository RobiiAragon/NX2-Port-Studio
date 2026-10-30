// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "NXPortStudio",
    defaultLocalization: "es",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "NX2PortStudio", targets: ["NX2Studio"]),
        .executable(name: "NXPortStudio", targets: ["PortStudio"])
    ],
    targets: [
        .target(name: "CLANRelay", publicHeadersPath: "include"),
        .target(name: "PortCore", dependencies: ["CLANRelay"]),
        .executableTarget(name: "NX2Studio", dependencies: ["PortCore"], resources: [.process("Resources")]),
        .testTarget(name: "NX2StudioTests", dependencies: ["NX2Studio", "PortCore"]),
        .executableTarget(name: "PortStudio", dependencies: ["PortCore"], resources: [.copy("Resources")]),
        .testTarget(name: "PortCoreTests", dependencies: ["PortCore"]),
        .testTarget(name: "PortStudioTests", dependencies: ["PortStudio", "PortCore"])
    ],
    swiftLanguageModes: [.v6]
)
