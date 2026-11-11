@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d e:\GitHub\lang_github
cl /nologo /utf-8 /O2 /W3 /I. lexer.c ast.c parser.c value.c gc.c interp.c builtins.c bytecode.c compiler.c vm.c stdlib\*.c main.c /Fe:l.exe /link wininet.lib ws2_32.lib
echo CMDEXIT=%ERRORLEVEL%
