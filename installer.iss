#ifndef MyAppVersion
  #define MyAppVersion "dev"
#endif
#ifndef MyBuildConfig
  #define MyBuildConfig "Release"
#endif

[Setup]
AppId={{A7058F89-E9B7-46D9-BA2C-F3A3590F7790}
AppName=Marcador de Softball para OBS
AppVersion={#MyAppVersion}
AppPublisher=somefirenoodles
DefaultDirName={commonappdata}\obs-studio\plugins\softball-scoreboard
UsePreviousAppDir=no
DisableProgramGroupPage=yes
OutputDir=release
OutputBaseFilename=softball-scoreboard-{#MyAppVersion}-windows-x64
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
CloseApplications=yes
UninstallDisplayName=Marcador de Softball para OBS
WizardStyle=modern

[Files]
Source: "release\{#MyBuildConfig}\softball-scoreboard\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb"

[Messages]
WelcomeLabel1=Instalar Marcador de Softball para OBS
WelcomeLabel2=Cierra OBS antes de continuar. El marcador aparecerá en la lista de Fuentes al volver a abrirlo.
FinishedLabel=Instalación completada. Abre OBS y agrega la fuente "Marcador de Softball".
