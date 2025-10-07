
chcp 65001
@echo off
setlocal enabledelayedexpansion

echo 正在扫描当前目录下的.cpp文件...
set SOURCES=
for %%f in (*.cpp) do (
    set SOURCES=!SOURCES! "%%f"
)

if "!SOURCES!"=="" (
    echo 未找到任何.cpp文件
    pause
    exit /b 1
)

echo 找到以下源文件：!SOURCES!
echo 正在编译所有文件...

g++ !SOURCES! -o combined_program.exe --std=c++11

if exist combined_program.exe (
    echo 编译成功，运行程序...
    echo vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    combined_program.exe
    echo vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    echo 程序执行完毕，正在清理...
    del /Q combined_program.exe
    echo 清理完成
) else (
    echo 编译失败，请检查错误
)

pause
