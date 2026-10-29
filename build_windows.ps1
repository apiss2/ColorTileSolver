$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$compiler = $null
foreach ($name in @('x86_64-w64-mingw32-clang++', 'x86_64-w64-mingw32-g++', 'clang++', 'g++')) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($command) {
        $target = & $command.Source -dumpmachine
        if ($target -match 'x86_64.*(mingw|windows-gnu)') { $compiler = $command.Source; break }
    }
}
if (-not $compiler) { throw 'Add a 64-bit LLVM-MinGW or MinGW-w64 compiler to PATH.' }
$resourceCompiler = $null
foreach ($name in @('x86_64-w64-mingw32-windres', 'windres')) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($command) { $resourceCompiler = $command.Source; break }
}
if (-not $resourceCompiler) { throw 'The matching windres resource compiler is required.' }
New-Item -ItemType Directory -Force .build | Out-Null
& $resourceCompiler -I src src/resources.rc .build/resources.o
if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
$common = @('-std=c++17', '-O3', '-DNDEBUG', '-static', '-municode', '-pthread', '-Wl,--stack,8388608')
& $compiler @common -mwindows src/winmain.cpp src/solver.cpp src/recognition.cpp .build/resources.o -lwindowscodecs -lole32 -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -luuid -o ColorTileSolver.exe
if ($LASTEXITCODE -ne 0) { throw 'GUI compilation failed.' }
& $compiler @common src/solver.cpp src/cli.cpp -o ColorTileSolverCLI.exe
if ($LASTEXITCODE -ne 0) { throw 'CLI compilation failed.' }
Write-Host 'Built ColorTileSolver.exe and ColorTileSolverCLI.exe.'
