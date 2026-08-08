param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

$vcpkgRoot = $env:VCPKG_ROOT
if ([string]::IsNullOrWhiteSpace($vcpkgRoot) -or -not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) {
    $vcpkgRoot = Join-Path $projectRoot ".deps\vcpkg"
    $bootstrapScript = Join-Path $vcpkgRoot "bootstrap-vcpkg.bat"
    if (-not (Test-Path $bootstrapScript)) {
        # An interrupted first clone is safe to replace because this path is an
        # application-owned dependency cache, never a user-selected directory.
        if (Test-Path $vcpkgRoot) {
            Remove-Item -LiteralPath $vcpkgRoot -Recurse -Force
        }
        New-Item -ItemType Directory -Force (Split-Path -Parent $vcpkgRoot) | Out-Null
        git clone --filter=blob:none https://github.com/microsoft/vcpkg.git $vcpkgRoot
        if ($LASTEXITCODE -ne 0) { throw "Failed to clone vcpkg." }
    }
    if (-not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) {
        & $bootstrapScript -disableMetrics
        if ($LASTEXITCODE -ne 0) { throw "Failed to bootstrap vcpkg." }
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "Visual Studio Installer was not found. Install Visual Studio 2022 or newer with Desktop development with C++."
}

$visualStudioVersion = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
if ([string]::IsNullOrWhiteSpace($visualStudioVersion)) {
    throw "The MSVC C++ toolchain was not found. Install the Desktop development with C++ workload."
}

$majorVersion = [int]($visualStudioVersion.Split('.')[0])
$generator = switch ($majorVersion) {
    18 { "Visual Studio 18 2026" }
    17 { "Visual Studio 17 2022" }
    default { throw "Unsupported Visual Studio major version: $majorVersion" }
}

$buildDirectory = Join-Path $projectRoot "build"
$toolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"

cmake -S $projectRoot -B $buildDirectory -G $generator -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static"
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

cmake --build $buildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

$distributionDirectory = Join-Path $projectRoot "dist"
New-Item -ItemType Directory -Force $distributionDirectory | Out-Null
Copy-Item -Force (Join-Path $buildDirectory "$Configuration\HeicConverter.exe") $distributionDirectory

Write-Host ""
Write-Host "Build complete: $(Join-Path $distributionDirectory 'HeicConverter.exe')" -ForegroundColor Green
