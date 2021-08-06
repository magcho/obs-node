@echo off

set OBS_STUDIO_VERSION=26.0.2.31
set WINDOWS_DEPS_VERSION=dependencies2019
set CEF_VERSION=75.1.14+gc81164e+chromium-75.0.3770.100

set BASE_DIR=%CD%
set BUILD_DIR=%BASE_DIR%\build
set OBS_STUDIO_BUILD_DIR=%BASE_DIR%\obs-studio-build
if not "%OBS_STUDIO_DIR%" == "" (
    set USE_EXISTING_OBS_STUDIO=true
) else (
    set OBS_STUDIO_DIR=%OBS_STUDIO_BUILD_DIR%\obs-studio-%OBS_STUDIO_VERSION%
)
set WINDOWS_DEPS_DIR=%OBS_STUDIO_BUILD_DIR%\%WINDOWS_DEPS_VERSION%
set CEF_DIR=%OBS_STUDIO_BUILD_DIR%\cef\cef_binary_%CEF_VERSION%_windows64
set OBS_INSTALL_PREFIX=%OBS_STUDIO_BUILD_DIR%\obs-installed
set PREBUILD_DIR=%BASE_DIR%\prebuild

set BUILD_TYPE=%1
set BUILD_CEF=false
set BUILD_OBS_STUDIO=false
set BUILD_OBS_NODE=false
if "%BUILD_TYPE%" == "all" (
    set BUILD_CEF=true
    set BUILD_OBS_STUDIO=true
    set BUILD_OBS_NODE=true
)
if "%BUILD_TYPE%" == "cef" set BUILD_CEF=true
if "%BUILD_TYPE%" == "obs-studio" set BUILD_OBS_STUDIO=true
if "%BUILD_TYPE%" == "obs-node" set BUILD_OBS_NODE=true
if not "%BUILD_CEF%" == "true" (
    if not "%BUILD_OBS_STUDIO%" == "true" (
        if not "%BUILD_OBS_NODE%" == "true" (
            echo "The first argument should be 'all', 'cef', obs-studio' or 'obs-node'"
            exit /B 1
        )
    )
)

set RELEASE_TYPE=%2
if "%RELEASE_TYPE%" == "" set RELEASE_TYPE=RelWithDebInfo
if not "%RELEASE_TYPE%" == "RelWithDebInfo" (
    if not "%RELEASE_TYPE%" == "Debug" (
        echo "The second argument should be 'RelWithDebInfo' or 'Debug'"
        exit /B 1
    )
)

mkdir "%PREBUILD_DIR%" 2>NUL
if "%BUILD_CEF%" == "true" (
    echo "Building CEF"
    cd "%OBS_STUDIO_BUILD_DIR%"
    if not exist "%CEF_DIR%" (
        if not exist "cef.tar.bz2" (
            curl -kL https://cef-builds.spotifycdn.com/cef_binary_75.1.14%2Bgc81164e%2Bchromium-75.0.3770.100_windows64.tar.bz2 -f --retry 5 -o cef.tar.bz2
        )
        mkdir cef
        tar xf "cef.tar.bz2" -C "cef"
    )
    cmake -G"Visual Studio 16 2019" -A x64 -H%CEF_DIR% -B%CEF_DIR%\build -DCEF_RUNTIME_LIBRARY_FLAG="/MD"
    cmake --build %CEF_DIR%\build --config %RELEASE_TYPE% --target libcef_dll_wrapper -v
)

if "%BUILD_OBS_STUDIO%" == "true" (
    echo "Building obs-studio"
    mkdir "%OBS_STUDIO_BUILD_DIR%" 2>NUL
    cd "%OBS_STUDIO_BUILD_DIR%"
    if not "%USE_EXISTING_OBS_STUDIO%" == "true" (
        if not exist "%OBS_STUDIO_DIR%" (
            git clone --recursive -b %OBS_STUDIO_VERSION% --single-branch https://github.com/BICBT/obs-studio.git "obs-studio-%OBS_STUDIO_VERSION%"
        )
    )
    if not exist "%WINDOWS_DEPS_DIR%" (
        if not exist "%WINDOWS_DEPS_VERSION%.zip" (
            curl -kLO "https://obsproject.com/downloads/%WINDOWS_DEPS_VERSION%.zip" -f --retry 5 -C -
        )
        7z x "%WINDOWS_DEPS_VERSION%.zip" -o"%WINDOWS_DEPS_DIR%"
    )

    mkdir "%OBS_STUDIO_DIR%\build" 2>NUL
    cd "%OBS_STUDIO_DIR%\build"
    cmake -G"Visual Studio 16 2019" ^
        -A x64 ^
        -DCMAKE_INSTALL_PREFIX="%OBS_INSTALL_PREFIX%" ^
        -DCMAKE_SYSTEM_VERSION=10.0 ^
        -DDepsPath="%WINDOWS_DEPS_DIR%\win64" ^
        -DDISABLE_UI=TRUE ^
        -DDISABLE_PYTHON=ON ^
        -DCMAKE_BUILD_TYPE="%RELEASE_TYPE%" ^
        -DCEF_ROOT_DIR="%CEF_DIR%" ^
        -DENABLE_SCRIPTING=false ^
        -DBUILD_BROWSER=true ^
        -DBROWSER_FRONTEND_API_SUPPORT=false ^
        -DBROWSER_PANEL_SUPPORT=false ^
        -DBROWSER_USE_STATIC_CRT=false ^
        -DEXPERIMENTAL_SHARED_TEXTURE_SUPPORT=true ^
        -DUSE_UI_LOOP=false ^
        ..
    rmdir /s /q %OBS_INSTALL_PREFIX% 2>NUL
    cmake --build . --target install --config %RELEASE_TYPE% -v

    cd "%BASE_DIR%"
    :: Copy obs files to prebuild
    echo "Copy obs installed files to prebuild"
    rmdir /s /q "%PREBUILD_DIR%\obs-studio" 2>NUL
    mkdir "%PREBUILD_DIR%\obs-studio" 2>NUL
    xcopy "%OBS_INSTALL_PREFIX%\bin" "%PREBUILD_DIR%\obs-studio\bin\" /E /Y
    xcopy "%OBS_INSTALL_PREFIX%\data" "%PREBUILD_DIR%\obs-studio\data\" /E /Y
    :: copy plugins to bin directory to make loadModule work
    xcopy "%OBS_INSTALL_PREFIX%\obs-plugins\64bit" "%PREBUILD_DIR%\obs-studio\bin\64bit\" /E /Y
)

if "%BUILD_OBS_NODE%" == "true" (
  echo "Building obs-node"
  rmdir /s /q "build" 2>NUL
  mkdir "build" 2>NUL
  if not exist "%BASE_DIR%\node_modules" (
    npm ci
  )
  set DEBUG_TAG=""
  if "%RELEASE_TYPE%" == "Debug" set DEBUG_TAG=-D
  node_modules\.bin\cmake-js.cmd configure ^
    %DEBUG_TAG% ^
    --CDOBS_STUDIO_DIR="%OBS_INSTALL_PREFIX%"
  cmake --build build --config %RELEASE_TYPE%

  :: Copy obs-node to prebuild
  echo "Copy %BUILD_DIR%\%RELEASE_TYPE%\obs-node.node to %PREBUILD_DIR%"
  copy /y "%BUILD_DIR%\%RELEASE_TYPE%\obs-node.node" "%PREBUILD_DIR%"
  copy /y "%BUILD_DIR%\%RELEASE_TYPE%\obs-node.pdb" "%PREBUILD_DIR%"
)