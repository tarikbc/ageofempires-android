@echo off
copy /y "%USERPROFILE%\Documents\My Games\Age of Empires IV\warnings.log" D:\aoe\watch.txt >nul 2>&1
if "%1"=="s" D:\suspinfo.exe RelicCardinal.exe D:\aoe\susp_now.txt
if "%1"=="s" type D:\aoe\susp_now.txt >> D:\aoe\susp_hist.txt 2>&1
if "%1"=="s" echo ---- %time% >> D:\aoe\susp_hist.txt
echo %time% > D:\aoe\mon_done.txt
