@echo off
rem snap.bat N  ->  suspinfo + stack states + thread contexts, saved with suffix N
D:\si.exe RelicCardinal.exe D:\aoe\si_%1.txt
D:\stk.exe
copy /y D:\stk.txt D:\aoe\stk_%1.txt >nul
D:\tctx.exe RelicCardinal.exe D:\aoe\tctx_%1.txt
