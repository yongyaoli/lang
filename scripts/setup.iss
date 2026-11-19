; L 语言 Windows 安装包 Inno Setup 脚本
; 用于生成免依赖、可直接双击安装且自动配置 PATH 的独立 exe 安装程序

#define MyAppName "L 语言"
#define MyAppEnglishName "L-Language"
#define MyAppVersion "0.1.11.202611191141"
#define MyAppPublisher "L Language Project"
#define MyAppExeName "l.exe"

[Setup]
; 唯一 GUID 标识符
AppId={{D37F67B5-8F21-4E83-9990-AEB9C187B576}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\lang
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=lang-setup-windows-x86_64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
UninstallDisplayIcon={app}\{#MyAppExeName}
ChangesEnvironment=yes
; 安装程序自身的 L 图标
SetupIconFile=..\assets\l.ico

[Languages]
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; Description: "将 L 语言添加到当前用户的 PATH 环境变量 (推荐，安装后可全局运行 'l')"; Flags: checkedonce
Name: "vscodeext"; Description: "自动安装 VS Code/CodeBuddy 编辑器扩展 (语法高亮 + .l 文件 L 图标, 推荐)"; Flags: checkedonce
Name: "assocl"; Description: "关联 .l 文件 (资源管理器显示 L 图标, 双击直接运行脚本)"; Flags: checkedonce

[Files]
Source: "..\l.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\src\stdlib\*"; DestDir: "{app}\stdlib"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\INSTALL.md"; DestDir: "{app}"; Flags: ignoreversion
; VS Code 扩展载荷 (语法高亮 + L 文件图标主题), 供安装后自动注册
Source: "..\editors\vscode\*"; DestDir: "{app}\editors\vscode"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\scripts\install_vscode_ext.ps1"; DestDir: "{app}\scripts"; Flags: ignoreversion

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Tasks: addtopath; Check: NeedsAddPath(ExpandConstant('{app}'))
; .l 文件关联: 图标取自 l.exe 内嵌资源, 双击直接运行脚本
Root: HKCU; Subkey: "Software\Classes\.l"; ValueType: string; ValueName: ""; ValueData: "L.LanguageScript"; Tasks: assocl; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\L.LanguageScript"; ValueType: string; ValueName: ""; ValueData: "L 语言脚本"; Tasks: assocl; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\L.LanguageScript\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\l.exe,0"; Tasks: assocl; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\L.LanguageScript\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\l.exe"" ""%1"""; Tasks: assocl; Flags: uninsdeletekey

[Code]
// 检查当前 PATH 中是否已有该目录，避免重复添加
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + UpperCase(Param) + ';', ';' + UpperCase(OrigPath) + ';') = 0;
end;

// 安装完成后: 自动把 VS Code/CodeBuddy 扩展注册到本机各编辑器并启用 L 文件图标主题
procedure CurStepChanged(CurSetupStep: TSetupStep);
var
  ResultCode: Integer;
  PsScript: string;
begin
  if (CurSetupStep = ssPostInstall) and WizardIsTaskSelected('vscodeext') then
  begin
    PsScript := ExpandConstant('{app}\scripts\install_vscode_ext.ps1');
    if FileExists(PsScript) then
      Exec('powershell.exe',
           '-NoProfile -ExecutionPolicy Bypass -File "' + PsScript + '"',
           '', SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode);
  end;
end;

// 卸载后自动从 PATH 中移除安装路径
procedure RemovePath(Param: string);
var
  OrigPath: string;
  P: Integer;
begin
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
  begin
    P := Pos(';' + UpperCase(Param) + ';', ';' + UpperCase(OrigPath) + ';');
    if P > 0 then
    begin
      Delete(OrigPath, P, Length(Param) + 1);
      if (Length(OrigPath) > 0) and (OrigPath[1] = ';') then
        Delete(OrigPath, 1, 1);
      RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath);
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    RemovePath(ExpandConstant('{app}'));
  end;
end;
