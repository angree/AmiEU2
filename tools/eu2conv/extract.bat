@echo off
rem AmiEU2 - make the Amiga data set from YOUR OWN copy of Europa Universalis II (GOG v1.09).
rem
rem   Drop the GOG installer (setup_europa_universalis2_2.0.0.1.exe) or the installed game folder
rem   onto this file, or just run it: it looks next to itself and in the usual GOG folders.
rem   The result is the folder "data" next to this file. Copy that folder into the AmiEU2 drawer
rem   on the Amiga (it must sit beside the program, as AmiEU2/data).
rem
rem Nothing is downloaded and nothing is sent anywhere; eu2conv.exe only reads what you give it.
setlocal
set "HERE=%~dp0"
set "OUT=%HERE%data"
set "CONV=%HERE%eu2conv.exe"
if not exist "%CONV%" (
  echo eu2conv.exe must be in the same folder as this file.
  goto :end
)

set "SRC=%~1"
if not "%SRC%"=="" goto :have

for %%F in ("%HERE%setup_europa_universalis2*.exe" "%CD%\setup_europa_universalis2*.exe") do (
  if exist "%%~F" set "SRC=%%~F"
)
if not "%SRC%"=="" goto :have
for %%D in ("C:\GOG Games\Europa Universalis II" "C:\GOG Games\Europa Universalis 2" "C:\Program Files (x86)\GOG Galaxy\Games\Europa Universalis II" "C:\Program Files\GOG Galaxy\Games\Europa Universalis II") do (
  if exist "%%~D\eu2.exe" set "SRC=%%~D"
)
if not "%SRC%"=="" goto :have

echo Could not find Europa Universalis II.
echo Drop the GOG installer setup_europa_universalis2_2.0.0.1.exe, or the folder the game is
echo installed in, onto extract.bat.
goto :end

:have
if exist "%OUT%" (
  echo The folder "%OUT%" already exists - it will be written over.
)
for %%A in ("%SRC%") do set "ATTR=%%~aA"
if /i "%ATTR:~0,1%"=="d" (
  echo Converting the installed game in "%SRC%" ...
  "%CONV%" --dir "%SRC%" "%OUT%"
) else (
  echo Converting the installer "%SRC%" ...
  "%CONV%" "%SRC%" "%OUT%"
)
if errorlevel 1 (
  echo.
  echo The conversion FAILED - see the messages above. Only the GOG release v1.09 is supported.
  goto :end
)
echo.
echo Done. Copy the folder "%OUT%" to the Amiga, into the AmiEU2 drawer, as "data".

:end
echo.
pause
endlocal
