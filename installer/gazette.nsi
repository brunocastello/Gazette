; Gazette installer.
;
; Built by NSIS from Linux in CI, as Gateway's is, so it is produced the same
; way as everything else here rather than assembled by hand on a Windows box.
;
;   makensis -DVERSION=0.2.0 -DSRC=../build-win installer/gazette.nsi
;
; The output is Setup.exe: named for what it does rather than for the program
; it carries, which is what the era's installers were called and what a
; floppy labelled "Gazette" wants on it.
;
; The default directory is C:\Gazette rather than Program Files, for
; Gateway's reason: Program Files is not an 8.3 name, and Gazette is one
; executable that keeps its settings (Gazette.ini) and its article cache
; (Cache) beside itself -- a short path at the root, which this era's
; utilities used, and which a user can find and back up.

; ANSI, not Unicode: the Unicode stub will not load on 95, 98 or Me. Before
; anything that writes to the header, or NSIS refuses to change charset.
Unicode false

!include "MUI2.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef SRC
  !define SRC "../build-win"
!endif

Name "Gazette ${VERSION}"
OutFile "Setup.exe"
InstallDir "C:\Gazette"
InstallDirRegKey HKLM "Software\Gazette" "InstallDir"

; Windows 95 has no notion of elevation, and on 2000 and XP a directory the
; user chose themselves does not need it.
RequestExecutionLevel user

SetCompressor /SOLID lzma
BrandingText "Gazette ${VERSION}"

; No MUI_ICON: the installer keeps NSIS's own icons, as Gateway's does.
; Setup is not Gazette, and giving it Gazette's icon makes two different
; things look like one in a folder.

; No licence page. Gazette is MIT, which asks nothing of the person
; installing it; a page with an "I Agree" button would ask for an agreement
; the licence does not need. LICENSE.TXT is installed beside the program.
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\Gazette.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open Gazette"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Gazette" SecMain
  SectionIn RO

  ; A Gazette that is running has Gazette.exe open, and replacing it would
  ; fail. Asked to close first, which also saves its feed list.
  FindWindow $0 "GazetteMainWindow" ""
  IntCmp $0 0 +3
    SendMessage $0 ${WM_CLOSE} 0 0
    Sleep 1000

  SetOutPath "$INSTDIR"
  File "${SRC}\Gazette.exe"
  File "/oname=README.TXT" "${SRC}\README.TXT"
  File "/oname=LICENSE.TXT" "${SRC}\LICENSE.TXT"

  ; Gazette.ini and the Cache folder are not shipped: Gazette writes them the
  ; first time it runs. An installation over an older one therefore leaves
  ; the user's feed list, read marks and cache exactly as they were.

  CreateDirectory "$SMPROGRAMS\Gazette"
  CreateShortCut "$SMPROGRAMS\Gazette\Gazette.lnk" "$INSTDIR\Gazette.exe"
  CreateShortCut "$SMPROGRAMS\Gazette\Read Me.lnk" "$INSTDIR\README.TXT"
  CreateShortCut "$SMPROGRAMS\Gazette\Uninstall Gazette.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "Software\Gazette" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Gazette" "Version" "${VERSION}"

  ; Add/Remove Programs.
  !define UNINST "Software\Microsoft\Windows\CurrentVersion\Uninstall\Gazette"
  WriteRegStr HKLM "${UNINST}" "DisplayName"     "Gazette ${VERSION}"
  WriteRegStr HKLM "${UNINST}" "DisplayVersion"  "${VERSION}"
  WriteRegStr HKLM "${UNINST}" "Publisher"       "Bruno Castello"
  WriteRegStr HKLM "${UNINST}" "DisplayIcon"     "$INSTDIR\Gazette.exe"
  WriteRegStr HKLM "${UNINST}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${UNINST}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "${UNINST}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST}" "NoRepair" 1

  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Uninstall"
  ; Closed first, or Gazette.exe is in use and stays behind.
  FindWindow $0 "GazetteMainWindow" ""
  IntCmp $0 0 +3
    SendMessage $0 ${WM_CLOSE} 0 0
    Sleep 1000

  Delete "$INSTDIR\Gazette.exe"
  Delete "$INSTDIR\README.TXT"
  Delete "$INSTDIR\LICENSE.TXT"
  Delete "$INSTDIR\Uninstall.exe"

  ; The feed list and the cache are the user's, not the installer's: asked,
  ; not assumed. The .new files are a save that did not finish.
  IfFileExists "$INSTDIR\Gazette.ini" 0 keep_done
    MessageBox MB_YESNO|MB_ICONQUESTION \
      "Remove your feed list and the article cache as well?$\n$\nKeep them if you mean to install Gazette again." \
      IDNO keep_done
    Delete "$INSTDIR\Gazette.ini"
    Delete "$INSTDIR\Gazette.ini.new"
    Delete "$INSTDIR\Gazette Preferences.txt"
    RMDir /r "$INSTDIR\Cache"
    RMDir /r "$INSTDIR\Gazette Cache"
  keep_done:

  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\Gazette\Gazette.lnk"
  Delete "$SMPROGRAMS\Gazette\Read Me.lnk"
  Delete "$SMPROGRAMS\Gazette\Uninstall Gazette.lnk"
  RMDir "$SMPROGRAMS\Gazette"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Gazette"
  DeleteRegKey HKLM "Software\Gazette"
SectionEnd
