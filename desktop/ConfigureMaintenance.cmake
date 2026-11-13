# Adapt the reviewed Canvas installer and update worker to this product.
# Keep the Canvas transaction, verification and shutdown implementation intact.
function(mu_maintenance_source name source)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" code)
    string(REPLACE "OnjLouis/sound-canvas-bridge" "OnjLouis/mu2026-hybrid" code "${code}")
    string(REPLACE "me.onj.soundcanvasbridge.manager" "me.onj.mu2000-pilot" code "${code}")
    string(REPLACE "me.onj.soundcanvasbridge.installer" "me.onj.mu2000.installer" code "${code}")
    string(REPLACE "me.onj.soundcanvasbridge.diskimage" "me.onj.mu2000.diskimage" code "${code}")
    string(REPLACE "Sound Canvas Bridge.component" "MU2000 Pilot.component" code "${code}")
    string(REPLACE "Sound Canvas Bridge.vst3" "MU2000 Pilot.vst3" code "${code}")
    string(REPLACE "Sound Canvas Bridge" "MU2000" code "${code}")
    string(REPLACE "SoundCanvasBridge" "MU2000" code "${code}")
    string(REPLACE "SoundCanvasMaintenance" "Mu2000Maintenance" code "${code}")
    string(REPLACE "OnjMusic.SoundCanvas" "OnjMusic.Mu2000" code "${code}")
    string(REPLACE "windows-v" "mu2000-v" code "${code}")
    string(REPLACE "SoundCanvas" "MU2000" code "${code}")
    if(name STREQUAL "Maintenance.swift")
        string(REPLACE "tag.hasPrefix(\"v\")" "tag.hasPrefix(\"mu2000-v\")" code "${code}")
        string(REPLACE "tag.dropFirst()" "tag.dropFirst(8)" code "${code}")
        string(REPLACE "/releases/download/v" "/releases/download/mu2000-v" code "${code}")
        string(REPLACE "\"tag_name\": \"v1.2.0\"" "\"tag_name\": \"mu2000-v1.2.0\"" code "${code}")
        string(REPLACE "\"me.onj.soundcanvasbridge\"" "\"me.onj.mu2000-pilot\"" code "${code}")
        string(REPLACE "-mac-universal.dmg" "-mac-arm64.dmg" code "${code}")
        string(REPLACE "[\"arm64\", \"x86_64\"]" "[\"arm64\"]" code "${code}")
        string(REPLACE "UniversalExecutable" "MacExecutable" code "${code}")
        string(REPLACE "universal DMG" "Apple Silicon DMG" code "${code}")
        string(REPLACE "versionParts(release.version) != nil," "let offered = versionParts(release.version), let installed = versionParts(current), installed.lexicographicallyPrecedes(offered)," code "${code}")
        string(REPLACE "static func prepare(_ release: BridgeRelease)" "static func prepare(_ release: BridgeRelease, current: String)" code "${code}")
        string(REPLACE "Maintenance.prepare(JSONDecoder().decode(BridgeRelease.self, from: data))" "Maintenance.prepare(JSONDecoder().decode(BridgeRelease.self, from: data), current: \"@PROJECT_VERSION@\")" code "${code}")
        string(REPLACE "Maintenance.prepare(JSONDecoder().decode(BridgeRelease.self, from: Data(args[1].utf8)))" "Maintenance.prepare(JSONDecoder().decode(BridgeRelease.self, from: Data(args[1].utf8)), current: \"@PROJECT_VERSION@\")" code "${code}")
    elseif(name STREQUAL "MaintenanceWindows.cs")
        string(REPLACE "Substring(9)" "Substring(8)" code "${code}")
        string(REPLACE "\"Application/MU2000Helper.exe\"," "" code "${code}")
        string(REPLACE "\"VST3/Contents/Resources/MU2000Helper.exe\"," "" code "${code}")
        # This VST3 has its original bundle filename, independent of product/app naming.
        string(REPLACE "Product+\".vst3\"" "\"MU2000 Pilot.vst3\"" code "${code}")
    endif()
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/maintenance/${name}" CONTENT "${code}" @ONLY)
endfunction()

mu_maintenance_source(Maintenance.swift "${CANVAS_CONTROLS}/Maintenance.swift")
mu_maintenance_source(InstallerInfo.plist "${CANVAS_CONTROLS}/../Resources/InstallerInfo.plist.in")
mu_maintenance_source(MaintenanceWindows.cs "${CANVAS_CONTROLS}/MaintenanceWindows.cs")
mu_maintenance_source(WindowsSetupBootstrap.cs "${CANVAS_CONTROLS}/WindowsSetupBootstrap.cs")
mu_maintenance_source(WindowsVersion.cs "${CANVAS_CONTROLS}/../Resources/WindowsVersion.cs.in")
mu_maintenance_source(WindowsInstaller.iss "${CANVAS_CONTROLS}/../Resources/WindowsInstaller.iss")

set(maintenance "${CMAKE_CURRENT_BINARY_DIR}/maintenance")
mu_maintenance_source(MuMenuBarStatus.h "${CANVAS_CONTROLS}/MenuBarStatus.h")
target_include_directories(OnjMu2000_Standalone PRIVATE "${maintenance}")
if(APPLE)
    add_custom_command(OUTPUT "${maintenance}/Mu2000Maintenance"
        COMMAND xcrun swiftc -swift-version 5 -O -target arm64-apple-macosx12.0
            "${maintenance}/Maintenance.swift" -o "${maintenance}/Mu2000Maintenance"
        DEPENDS "${maintenance}/Maintenance.swift" VERBATIM)
    add_custom_target(Mu2000InstallerBundle
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${maintenance}/Install MU2000.app/Contents/MacOS"
        COMMAND "${CMAKE_COMMAND}" -E copy "${maintenance}/Mu2000Maintenance"
            "${maintenance}/Install MU2000.app/Contents/MacOS/Mu2000Maintenance"
        COMMAND "${CMAKE_COMMAND}" -E copy "${maintenance}/InstallerInfo.plist"
            "${maintenance}/Install MU2000.app/Contents/Info.plist"
        DEPENDS "${maintenance}/Mu2000Maintenance" VERBATIM)
    add_dependencies(OnjMu2000_Standalone Mu2000InstallerBundle)
    add_custom_target(Mu2000MacApplication
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:OnjMu2000_Standalone>/Contents/Resources"
        COMMAND "${CMAKE_COMMAND}" -E copy "${maintenance}/Mu2000Maintenance"
            "$<TARGET_BUNDLE_DIR:OnjMu2000_Standalone>/Contents/Resources/Mu2000Maintenance"
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:OnjMu2000_Standalone>"
        DEPENDS OnjMu2000_Standalone Mu2000InstallerBundle VERBATIM)
elseif(WIN32)
    set(MU_UPDATE_PUBLIC_KEY "" CACHE FILEPATH "Public RSA XML for MU2000 Windows updates")
    set(MU_INSTALLER_INPUT "" CACHE PATH "Directory containing payload.zip, manifest.json and manifest.sig")
    set(MU_CSC "$ENV{SystemRoot}/Microsoft.NET/Framework64/v4.0.30319/csc.exe" CACHE FILEPATH "C# compiler")
    if(MU_UPDATE_PUBLIC_KEY)
        file(READ "${MU_UPDATE_PUBLIC_KEY}" public_key)
        if(public_key MATCHES "<D>|<P>|<Q>|<DP>|<DQ>|<InverseQ>" OR NOT public_key MATCHES "<Modulus>[^<]+</Modulus><Exponent>[^<]+</Exponent>")
            message(FATAL_ERROR "Supply only the MU2000 public verification key")
        endif()
        set(csc_refs /r:System.dll /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll
            /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /r:Microsoft.CSharp.dll)
        file(TO_NATIVE_PATH "${maintenance}" maintenance_native)
        file(TO_NATIVE_PATH "${MU_UPDATE_PUBLIC_KEY}" key_native)
        set(csc_sources "${maintenance_native}\\MaintenanceWindows.cs" "${maintenance_native}\\WindowsVersion.cs")
        add_custom_command(OUTPUT "${maintenance}/Mu2000Maintenance.exe"
            COMMAND "${MU_CSC}" /nologo /target:winexe /platform:x64 /optimize+ /utf8output ${csc_refs}
                "/resource:${key_native},WindowsUpdateKey" "/out:${maintenance_native}\\Mu2000Maintenance.exe" ${csc_sources}
            DEPENDS ${csc_sources} "${MU_UPDATE_PUBLIC_KEY}" VERBATIM)
        add_custom_target(Mu2000MaintenanceBuild DEPENDS "${maintenance}/Mu2000Maintenance.exe")
        add_dependencies(OnjMu2000_Standalone Mu2000MaintenanceBuild)
        add_custom_target(Mu2000WindowsApplication
            COMMAND "${CMAKE_COMMAND}" -E copy "${maintenance}/Mu2000Maintenance.exe"
                "$<TARGET_FILE_DIR:OnjMu2000_Standalone>"
            DEPENDS OnjMu2000_Standalone Mu2000MaintenanceBuild VERBATIM)
        if(MU_INSTALLER_INPUT)
            find_program(MU_ISCC NAMES ISCC.exe ISCC REQUIRED)
            set(installer "MU2000-${PROJECT_VERSION}-windows-x64.exe")
            file(TO_NATIVE_PATH "${MU_INSTALLER_INPUT}" input_native)
            add_custom_target(Mu2000WindowsInstaller
                COMMAND "${MU_CSC}" /nologo /target:winexe /platform:x64 /optimize+ /utf8output ${csc_refs}
                    "/resource:${key_native},WindowsUpdateKey"
                    "/resource:${input_native}\\payload.zip,Payload" "/resource:${input_native}\\manifest.json,Manifest"
                    "/resource:${input_native}\\manifest.sig,Signature"
                    "/out:${maintenance_native}\\PayloadInstaller.exe" ${csc_sources}
                COMMAND "${MU_ISCC}" "/DBridgeVersion=${PROJECT_VERSION}"
                    "/DPayloadWorker=${maintenance}/PayloadInstaller.exe" "/DMaintenanceWorker=${maintenance}/Mu2000Maintenance.exe"
                    "/DInstallerOutput=${maintenance}/Wizard" "${maintenance}/WindowsInstaller.iss"
                COMMAND "${MU_CSC}" /nologo /target:winexe /platform:x64 /optimize+ /utf8output /r:System.dll /r:System.Windows.Forms.dll
                    "/resource:${maintenance_native}\\Wizard\\${installer},Wizard" "/out:${maintenance_native}\\${installer}"
                    "${maintenance_native}\\WindowsSetupBootstrap.cs" "${maintenance_native}\\WindowsVersion.cs"
                DEPENDS Mu2000MaintenanceBuild VERBATIM)
        endif()
    endif()
endif()
