; Inno Setup script for SidStation ASID (Windows).
;
; Compiled by the release workflow, which passes the version and the paths:
;   ISCC /DAppVersion=1.0.0 /DSrcDir=<staging> /DOutDir=<dist> installer.iss
;
; The staging dir holds VST3\ (the .vst3 bundle), Standalone\ (the .exe) and
; LICENSE.txt. Installs the VST3 into the shared VST3 folder and the Standalone
; into Program Files.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SrcDir
  #define SrcDir "staging"
#endif
#ifndef OutDir
  #define OutDir "."
#endif

[Setup]
AppName=SidStation ASID
AppVersion={#AppVersion}
AppPublisher=DehliMusikk
AppPublisherURL=https://benjamindehli.github.io/sidstation-asid/
DefaultDirName={autopf}\SidStation ASID
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SourceDir={#SrcDir}
OutputDir={#OutDir}
OutputBaseFilename=SidStation-ASID-{#AppVersion}-setup
LicenseFile=LICENSE.txt
UninstallDisplayName=SidStation ASID {#AppVersion}
WizardStyle=modern

[Files]
Source: "VST3\SidStation ASID.vst3\*"; DestDir: "{commoncf64}\VST3\SidStation ASID.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "Standalone\SidStation ASID.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\SidStation ASID"; Filename: "{app}\SidStation ASID.exe"
