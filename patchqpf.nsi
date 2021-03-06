; !cd bin

; TODO: Use NSIS Simple Service Plugin

Name PatchQPF
OutFile PatchQPF-install.exe
RequestExecutionLevel admin

InstallDir $PROGRAMFILES\PatchQPF

!define REG_EVENTLOG 'SYSTEM\CurrentControlSet\services\eventlog\Application\PatchQPF'
!define REG_UNINST 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\PatchQPF'

Page license
Page directory
Page instfiles

LicenseData LICENSE.rtf
LicenseBkColor /windows

Section "Install"
	ReadRegStr $0 HKLM ${REG_UNINST} UninstallString
	StrCmp "$0" "" 0 AlreadyInstalled
	IfFileExists "$INSTDIR\patchqpf.exe" AlreadyInstalled

	SetOutPath -
	WriteUninstaller $INSTDIR\uninst.exe

	File "patchqpf.exe" "pqpf014c.dll" "pqpf8664.dll" "LICENSE.rtf" "sysinfo.exe"

	WriteRegStr HKLM ${REG_EVENTLOG} EventMessageFile $SYSDIR\kernel32.dll
	WriteRegDWORD HKLM ${REG_EVENTLOG} TypesSupported 7

	; Why doesn't WriteUninstaller set this part up, anyway?
	WriteRegStr HKLM ${REG_UNINST} DisplayName PatchQPF
	WriteRegDWORD HKLM ${REG_UNINST} NoModify 1
	WriteRegDWORD HKLM ${REG_UNINST} NoRepair 1
	WriteRegStr HKLM ${REG_UNINST} UninstallString "$INSTDIR\uninst.exe"

	; TODO Better error handling here. Or just use the NSIS service library.
	ExecWait 'sc create PatchQPF binPath= "$INSTDIR\patchqpf.exe" start= auto'
	ExecWait 'sc description PatchQPF "Make Windows Update check for updates faster on certain types of old hardware."'
	ExecWait 'sc start PatchQPF'

	Return

AlreadyInstalled:
	MessageBox MB_ICONSTOP "PatchQPF is already installed. Uninstall it first from Control Panel -> Programs and Features, reboot, then try again."
	Quit
SectionEnd

Section "Uninstall"
	ExecWait 'sc stop PatchQPF'
	ExecWait 'sc delete PatchQPF'

	Delete /REBOOTOK "$INSTDIR\patchqpf.exe"
	Delete /REBOOTOK "$INSTDIR\pqpf014c.dll"
	Delete /REBOOTOK "$INSTDIR\pqpf8664.dll"
	Delete /REBOOTOK "$INSTDIR\LICENSE.rtf"
	Delete /REBOOTOK "$INSTDIR\sysinfo.exe"
	Delete /REBOOTOK "$INSTDIR\uninst.exe"
	RMDir /REBOOTOK "$INSTDIR"

	DeleteRegKey HKLM ${REG_EVENTLOG}
	DeleteRegKey HKLM ${REG_UNINST}

	MessageBox MB_ICONQUESTION|MB_YESNO "To finish uninstalling PatchQPF, you must reboot your computer. Would you like to reboot now?" IDNO NoReboot
	Reboot
NoReboot:
SectionEnd
