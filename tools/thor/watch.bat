@echo off
:loop
echo === %time% === >> D:\aoe\watch.txt
type "%USERPROFILE%\Documents\My Games\Age of Empires IV\warnings.log" 2>nul | findstr /i "10038 Shutdown WebSocketConnection TlsConnection StatusCode" >> D:\aoe\watch.txt 2>&1
timeout /t 2 /nobreak >nul
goto loop
