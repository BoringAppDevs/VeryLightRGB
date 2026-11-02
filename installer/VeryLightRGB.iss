; SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AppVersion
  #define AppVersion "1.2.0"
#endif
#define ProjectRoot ".."
#define Payload ProjectRoot + "\artifacts\VeryLightRGB"

[Setup]
AppId={{A38BD459-F630-4A2D-94F0-A2CA40ED3271}
AppName=Very Light RGB
AppVersion={#AppVersion}
AppPublisher=Very Light RGB contributors
DefaultDirName={localappdata}\Programs\VeryLightRGB
DefaultGroupName=Very Light RGB
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DisableProgramGroupPage=yes
LicenseFile={#ProjectRoot}\LICENSE
SetupIconFile={#ProjectRoot}\native\assets\app.ico
UninstallDisplayIcon={app}\VeryLightRGB.exe
OutputDir={#ProjectRoot}\artifacts
OutputBaseFilename=VeryLightRGB-{#AppVersion}-Setup-win-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
Uninstallable=yes
ChangesAssociations=no
ShowLanguageDialog=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[Tasks]
Name: "startup"; Description: "{cm:StartupTask}"; Flags: checkedonce
Name: "desktopicon"; Description: "{cm:DesktopTask}"; Flags: unchecked

[CustomMessages]
english.StartupTask=Start Very Light RGB when I sign in
ukrainian.StartupTask=Запускати Very Light RGB після входу у Windows
english.DesktopTask=Create a desktop shortcut
ukrainian.DesktopTask=Створити ярлик на робочому столі
english.LaunchApp=Launch Very Light RGB
ukrainian.LaunchApp=Запустити Very Light RGB
english.CloseApp=Close the running Very Light RGB copy and retry. No files have been replaced.
ukrainian.CloseApp=Закрий запущену копію Very Light RGB і повтори. Файли ще не замінено.
english.DriverCheckFailed=The installed native driver bundle failed its runtime check. See the installation log.
ukrainian.DriverCheckFailed=Встановлений комплект драйверів не пройшов перевірку. Переглянь журнал встановлення.
english.StartupFailed=Could not register the startup task.
ukrainian.StartupFailed=Не вдалося створити задачу автозапуску.
english.RemoveStartupFailed=Could not remove this installation's startup task.
ukrainian.RemoveStartupFailed=Не вдалося видалити задачу автозапуску цієї копії.
english.CloseUninstall=Close Very Light RGB and retry uninstalling.
ukrainian.CloseUninstall=Закрий Very Light RGB і повтори видалення.

[Files]
Source: "{#Payload}\VeryLightRGB.exe"; Flags: dontcopy; DestName: "maintenance.exe"
Source: "{#Payload}\VeryLightRGB.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\COPYRIGHT.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\SOURCE.zip"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\json.LICENSE.MIT"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\LLVM-RUNTIME-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\native-drivers\*"; DestDir: "{app}\native-drivers"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Very Light RGB"; Filename: "{app}\VeryLightRGB.exe"; Parameters: "--menu"
Name: "{autodesktop}\Very Light RGB"; Filename: "{app}\VeryLightRGB.exe"; Parameters: "--menu"; Tasks: desktopicon

[Run]
; Inno Setup checks postinstall entries by default unless the unchecked flag is set.
Filename: "{app}\VeryLightRGB.exe"; Parameters: "--menu"; Description: "{cm:LaunchApp}"; Flags: postinstall nowait skipifsilent; Check: ShouldLaunch

[Code]
var
  StartupPreferenceKnown: Boolean;
  StartupPreferenceEnabled: Boolean;
  StartupPreferenceApplied: Boolean;

procedure ApplyStartupPreference;
begin
  if StartupPreferenceKnown and not StartupPreferenceApplied then begin
    if StartupPreferenceEnabled then WizardSelectTasks('startup')
    else WizardSelectTasks('!startup');
    StartupPreferenceApplied := True;
    Log('Applied current app startup preference: ' + IntToStr(Ord(StartupPreferenceEnabled)));
  end;
end;

procedure InitializeWizard;
var ResultCode: Integer; State: AnsiString; Path: String;
begin
  { Preserve changes made in the app after the previous Setup run. Explicit
    unattended /TASKS or /MERGETASKS arguments take precedence. }
  if FileExists(ExpandConstant('{localappdata}\VeryLightRGB\settings.json')) and
     (ExpandConstant('{param:TASKS|__unset__}') = '__unset__') and
     (ExpandConstant('{param:MERGETASKS|__unset__}') = '__unset__') then begin
    ExtractTemporaryFile('maintenance.exe');
    Path := ExpandConstant('{tmp}\startup-state.json');
    if Exec(ExpandConstant('{tmp}\maintenance.exe'), '--startup-info --output "' + Path + '"',
      '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0) and
      LoadStringFromFile(Path, State) then begin
      StartupPreferenceKnown := True;
      StartupPreferenceEnabled := Pos('"StartupEnabled": true', String(State)) > 0;
      Log('Read current app startup preference: ' + IntToStr(Ord(StartupPreferenceEnabled)));
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  { Task defaults are populated after InitializeWizard. Apply once, then respect
    changes made by the user on the task page. Silent installs use PrepareToInstall. }
  if (CurPageID = wpSelectTasks) or (CurPageID = wpReady) then ApplyStartupPreference;
end;

function RunApp(const Arguments: String): Boolean;
var ResultCode: Integer;
begin
  Result := Exec(ExpandConstant('{app}\VeryLightRGB.exe'), Arguments, '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

function ShouldLaunch: Boolean;
begin
  Result := ExpandConstant('{param:NOLAUNCH|0}') <> '1';
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var ResultCode: Integer;
begin
  ApplyStartupPreference;
  ExtractTemporaryFile('maintenance.exe');
  if not Exec(ExpandConstant('{tmp}\maintenance.exe'),
    '--installer-stop --target-exe "' + ExpandConstant('{app}\VeryLightRGB.exe') + '"',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
    Result := CustomMessage('CloseApp')
  else Result := '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    if not RunApp('--installer-check') then
      RaiseException(CustomMessage('DriverCheckFailed'));
    if WizardIsTaskSelected('startup') then begin
      if not RunApp('--enable-startup') then RaiseException(CustomMessage('StartupFailed'));
    end else if not RunApp('--installer-remove-startup') then
      RaiseException(CustomMessage('RemoveStartupFailed'));
  end;
end;

function InitializeUninstall: Boolean;
begin
  Result := RunApp('--installer-stop');
  if Result then Result := RunApp('--installer-remove-startup');
  if not Result then MsgBox(CustomMessage('CloseUninstall'), mbError, MB_OK);
  { User settings under LocalAppData\VeryLightRGB are deliberately preserved. }
end;
