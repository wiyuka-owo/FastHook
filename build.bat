@echo off
setlocal enabledelayedexpansion

echo [*] Building FastHook (DX9/DX11/DX12)...

:: Create build directory if it doesn't exist
if not exist build mkdir build

:: Detect Visual Studio environment
where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    )
)

where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] MSVC compiler ^(cl.exe^) not found. Please run from a Visual Studio Developer Command Prompt.
    pause
    exit /b 1
)

:: Compile and output to the "build" folder
:: 注意：这里加入了 /I vendor\sdk\CppSDK 来包含 Dumper-7 目录
cl /nologo /O2 /EHsc /std:c++20 /MD /LD ^
   /I include /I vendor\sdk\CppSDK /I vendor\imgui /I vendor\imgui\backends /I vendor\minhook\include /I vendor\kiero /I vendor\unityresolve ^
   src\*.cpp ^
   vendor\sdk\CppSDK\SDK\Basic.cpp ^
   vendor\sdk\CppSDK\SDK\CoreUObject_functions.cpp ^
   vendor\sdk\CppSDK\SDK\Engine_functions.cpp ^
   vendor\sdk\CppSDK\SDK\FSD_functions.cpp ^
   vendor\imgui\*.cpp ^
   vendor\imgui\backends\imgui_impl_dx9.cpp ^
   vendor\imgui\backends\imgui_impl_dx11.cpp ^
   vendor\imgui\backends\imgui_impl_dx12.cpp ^
   vendor\imgui\backends\imgui_impl_win32.cpp ^
   vendor\kiero\*.cpp ^
   vendor\minhook\src\*.c vendor\minhook\src\hde\*.c ^
   user32.lib d3d9.lib d3d11.lib d3d12.lib dxgi.lib ^
   /Fo"build/" /Fe"build/FastHook.dll"

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

:: Clean up intermediate object files from the build folder
del build\*.obj 2>nul
echo.
echo [OK] FastHook.dll built successfully in the "build" directory.
pause
