# Releasing SidStation ASID

The `Release` workflow (`.github/workflows/release.yml`) builds all three
platforms and attaches their artifacts to the GitHub release:

| Platform | Artifact                                               | Signed                    |
| -------- | ------------------------------------------------------ | ------------------------- |
| macOS    | `SidStation-ASID-x.y.z.dmg` (a `.pkg` inside a `.dmg`) | yes, signed and notarized |
| Windows  | `SidStation-ASID-x.y.z-setup.exe` (Inno Setup)         | no                        |
| Linux    | `SidStation-ASID-x.y.z-linux-x86_64.tar.gz`            | no                        |

macOS is a universal (arm64 + x86_64) build. Windows and Linux build the VST3 and
Standalone (AU is macOS only). The sections below cover the macOS signing setup;
Windows and Linux need no secrets.

## macOS

The `.pkg` installs:

The `.pkg` installs:

| Item                               | Destination                          |
| ---------------------------------- | ------------------------------------ |
| `SidStation ASID.component` (AU)   | `/Library/Audio/Plug-Ins/Components` |
| `SidStation ASID.vst3` (VST3)      | `/Library/Audio/Plug-Ins/VST3`       |
| `SidStation ASID.app` (Standalone) | `/Applications`                      |

## One-time setup

You need an Apple Developer account and two certificates from
[developer.apple.com](https://developer.apple.com/account/resources/certificates):

- **Developer ID Application** signs the plugin/app bundles and the DMG.
- **Developer ID Installer** signs the `.pkg`.

Export the **Application** certificate (with its private key) from Keychain
Access as a `.p12`, then add these as repository secrets
(Settings -> Secrets and variables -> Actions):

| Secret                     | What it is                                                        |
| -------------------------- | ----------------------------------------------------------------- |
| `MACOS_CERT_P12`           | base64 of the Application `.p12` (`base64 -i cert.p12 \| pbcopy`) |
| `MACOS_CERT_PASSWORD`      | the `.p12` password                                               |
| `MACOS_SIGN_IDENTITY`      | e.g. `Developer ID Application: Your Name (TEAMID)`               |
| `MACOS_INSTALLER_IDENTITY` | e.g. `Developer ID Installer: Your Name (TEAMID)`                 |
| `APPLE_ID`                 | your Apple-ID email (for notarization)                            |
| `APPLE_TEAM_ID`            | your 10-character team id                                         |
| `APPLE_APP_PASSWORD`       | an app-specific password from appleid.apple.com                   |

The installer certificate's private key must be in the same `.p12` (export both
certs together, or add the installer `.p12` too). If any secret is missing the
job still runs and produces an **unsigned** `.dmg` (handy for testing the
packaging, but it will not install cleanly on other machines).

## Cutting a release

1. Bump the version in the root `CMakeLists.txt` (`project(... VERSION x.y.z)`).
2. Commit and push.
3. On GitHub, draft a release with the tag `vx.y.z` (must match the CMake
   version, the workflow checks this) and write the notes.
4. Publish it. The workflow builds, signs, notarizes and uploads
   `SidStation-ASID-x.y.z.dmg` to the release.

To dry-run the packaging without a release, trigger the workflow manually
(Actions -> Release -> Run workflow). It uploads the `.dmg` as a build artifact.

## Building the installer locally

With the artefacts built (`cmake --build build` after configuring Release), you
can produce the `.pkg` by hand:

```sh
packaging/make-pkg.sh 0.1.0 build/asid/SidStationAsid_artefacts/Release SidStation-ASID-0.1.0.pkg
```

Set `INSTALLER_IDENTITY` first to sign it.

## Windows

The Windows job builds the VST3 and Standalone, then compiles
`packaging/windows/installer.iss` with Inno Setup into an unsigned
`SidStation-ASID-x.y.z-setup.exe`. The installer puts the VST3 in the shared
`Common Files\VST3` folder and the Standalone in `Program Files`. Because it is
unsigned, Windows SmartScreen shows a warning on first run (choose "More info"
then "Run anyway"). Adding code signing later means a `.pfx` certificate and a
signing step around ISCC.

## Linux

The Linux job builds the VST3 and Standalone on Ubuntu and packs them, with an
`INSTALL.txt` and the licence, into `SidStation-ASID-x.y.z-linux-x86_64.tar.gz`.
There is no installer: the user copies the `.vst3` into `~/.vst3` and runs the
Standalone, as described in `INSTALL.txt`.
