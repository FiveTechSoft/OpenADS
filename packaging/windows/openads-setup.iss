; ============================================================================
;  openads-setup.iss — Inno Setup script for the OpenADS Windows installer.
;
;  Gives ex-Advantage users the familiar clicky setup.exe: it asks for the
;  data directory, wire port, Studio console and whether to start
;  automatically, then writes an openads.ini and (optionally) registers the
;  Windows service — exactly the same outcome as `openads_serverd --setup`
;  on the console, so there is no duplicated logic, just a GUI front door.
;
;  Build it with Inno Setup's compiler (ISCC.exe), pointing at a staging
;  folder that holds the built binaries (e.g. an extracted release zip):
;
;      iscc /DSrcDir=..\..\dist\openads-1.4.0-windows-x64 ^
;           /DAppVer=1.4.0 packaging\windows\openads-setup.iss
;
;  See README.md in this folder. Inno Setup is Windows-only; this is the
;  GUI shell over the cross-platform wizard, not a replacement for it.
; ============================================================================

#ifndef AppVer
  #define AppVer "0.0.0-dev"
#endif
#ifndef SrcDir
  #define SrcDir "staging"
#endif

[Setup]
AppName=OpenADS
AppVersion={#AppVer}
AppVerName=OpenADS {#AppVer}
AppPublisher=OpenADS
DefaultDirName={autopf}\OpenADS
DefaultGroupName=OpenADS
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=openads-{#AppVer}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Service registration + Program Files install need elevation.
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#SrcDir}\LICENSE

[Files]
Source: "{#SrcDir}\ace64.dll";            DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\openace64.dll";        DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SrcDir}\openads_serverd.exe";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\openads_bench.exe";    DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SrcDir}\openads-studio.bat";   DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SrcDir}\openads.ini.sample";   DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SrcDir}\QUICKSTART.md";        DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SrcDir}\README.md";            DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SrcDir}\LICENSE";              DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SrcDir}\NOTICE";               DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SrcDir}\lib\*";                DestDir: "{app}\lib"; Flags: recursesubdirs ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\OpenADS Studio (admin console)"; Filename: "{app}\openads-studio.bat"; WorkingDir: "{app}"
Name: "{group}\Quick start";                    Filename: "{app}\QUICKSTART.md"
Name: "{group}\Uninstall OpenADS";              Filename: "{uninstallexe}"

[UninstallRun]
; Stop the service synchronously first so its .exe isn't locked when the
; uninstaller deletes files, then deregister it.
Filename: "net.exe"; Parameters: "stop openads_serverd"; Flags: runhidden; RunOnceId: "StopOpenadsSvc"
Filename: "{app}\openads_serverd.exe"; Parameters: "--uninstall-service"; Flags: runhidden; RunOnceId: "DelOpenadsSvc"

[Code]
var
  CfgPage: TInputQueryWizardPage;
  OptPage: TInputOptionWizardPage;

procedure InitializeWizard;
begin
  CfgPage := CreateInputQueryPage(wpSelectDir,
    'OpenADS server settings',
    'Configure the database server',
    'Accept the defaults or adjust them. These are written to openads.ini, ' +
    'which the server reads on every start.');
  CfgPage.Add('Data directory (where tables live):', False);
  CfgPage.Add('Wire (TCP) port clients connect to:', False);
  CfgPage.Add('Studio web console port:', False);
  CfgPage.Add('Studio admin user (leave blank for an open console):', False);
  CfgPage.Add('Studio admin password:', True);
  // Default the data dir under ProgramData, not Program Files: a service
  // needs write access to where the tables live, and Program Files is
  // read-only for non-elevated processes / subject to UAC virtualisation.
  // (Use line comments here: a brace comment can't contain the constant
  // braces the explanation refers to — Inno's { } comments don't nest.)
  CfgPage.Values[0] := ExpandConstant('{commonappdata}\OpenADS\data');
  CfgPage.Values[1] := '6262';
  CfgPage.Values[2] := '6263';

  OptPage := CreateInputOptionPage(CfgPage.ID,
    'Options', 'Choose how OpenADS runs',
    'These can be changed later by editing openads.ini or re-running setup.',
    False, False);
  OptPage.Add('Enable the Studio web console (browser admin — the ARC replacement)');
  OptPage.Add('Start OpenADS automatically at boot (install as a Windows service)');
  OptPage.Values[0] := True;
  OptPage.Values[1] := True;
end;

function StudioEnabled: Boolean;
begin
  Result := OptPage.Values[0];
end;

function ServiceWanted: Boolean;
begin
  Result := OptPage.Values[1];
end;

{ Validate the numeric ports before leaving the settings page. }
function NextButtonClick(CurPageID: Integer): Boolean;
var
  p: Integer;
begin
  Result := True;
  if CurPageID = CfgPage.ID then
  begin
    p := StrToIntDef(CfgPage.Values[1], -1);
    if (p < 0) or (p > 65535) then
    begin
      MsgBox('The wire port must be a number between 0 and 65535.',
             mbError, MB_OK);
      Result := False;
      Exit;
    end;
    p := StrToIntDef(CfgPage.Values[2], -1);
    if (p < 0) or (p > 65535) then
    begin
      MsgBox('The Studio port must be a number between 0 and 65535.',
             mbError, MB_OK);
      Result := False;
      Exit;
    end;
    { Wire port and Studio port must differ, or the server can't bind both. }
    if Trim(CfgPage.Values[1]) = Trim(CfgPage.Values[2]) then
    begin
      MsgBox('The wire port and the Studio port must be different.',
             mbError, MB_OK);
      Result := False;
    end;
  end;
end;

{ Build the openads.ini text from the wizard answers. }
function BuildIni: String;
var
  nl, s, user: String;
begin
  nl := #13#10;
  s := '# openads.ini — written by the OpenADS installer.' + nl;
  s := s + '# Read by:  openads_serverd --config openads.ini' + nl + nl;
  s := s + '[server]' + nl;
  s := s + 'host = 0.0.0.0' + nl;
  s := s + 'port = ' + CfgPage.Values[1] + nl;
  s := s + 'backlog = 16' + nl;
  s := s + 'data = ' + CfgPage.Values[0] + nl;
  if StudioEnabled then
  begin
    s := s + 'http_port = ' + CfgPage.Values[2] + nl;
    user := Trim(CfgPage.Values[3]);
    if user <> '' then
      s := s + 'http_user = ' + user + ':' + CfgPage.Values[4] + nl;
  end;
  Result := s;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  iniPath, dataDir: String;
  rc: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    { Make sure the chosen data directory exists. }
    dataDir := CfgPage.Values[0];
    if dataDir <> '' then
      ForceDirectories(dataDir);

    { Write the config next to the binaries. }
    iniPath := ExpandConstant('{app}\openads.ini');
    SaveStringToFile(iniPath, BuildIni, False);

    { Register the auto-start service, pointing it at the config we wrote.
      Same as the console wizard's "start automatically" answer. }
    if ServiceWanted then
    begin
      if not Exec(ExpandConstant('{app}\openads_serverd.exe'),
                  '--install-service --config "' + iniPath + '"',
                  ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, rc) then
        MsgBox('Could not launch the service installer.', mbError, MB_OK)
      else if rc <> 0 then
        MsgBox('Service registration returned code ' + IntToStr(rc) +
               '. You can register it later with:'#13#10 +
               '  openads_serverd --install-service --config "' + iniPath + '"',
               mbInformation, MB_OK)
      else
        { Registered as auto-start; bring it up now so the user doesn't have
          to reboot or start it by hand. }
        Exec('net.exe', 'start openads_serverd', '',
             SW_HIDE, ewWaitUntilTerminated, rc);
    end;
  end;
end;
