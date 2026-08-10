#Requires -Version 5.1
<#
.SYNOPSIS
    Единственная точка входа для нативной сборки проекта под Windows (MSVC).

.DESCRIPTION
    Скрипт делает то же, что в Linux делает dev-контейнер, но без Docker и без
    WSL: вносит окружение MSVC в текущий процесс, подтягивает зависимости через
    Conan, конфигурирует CMake по пресету из CMakePresets.json, собирает и
    прогоняет тесты.

    Почему окружение MSVC вносится, а не запускается "cmd /c vcvars && cmake":
    генератор Ninja вызывает cl.exe десятки раз, и все вызовы должны видеть одни
    и те же INCLUDE/LIB/PATH. Один раз импортировать переменные в процесс
    надёжнее, чем оборачивать каждую команду в cmd.

    Почему conan install идёт после очистки: сгенерированный conan_toolchain.cmake
    лежит внутри каталога сборки, и -Clean, выполненный после, стёр бы его.

    Через Conan приезжают только gtest и sfml. stdexec и CPM.cmake тянутся
    самим CMake (FetchContent/CPM) на этапе конфигурации, поэтому первая
    конфигурация требует сети и заметно дольше повторных.

.PARAMETER BuildType
    Release (по умолчанию) или Debug. Выбирает соответствующий пресет.

.PARAMETER Clean
    Удалить каталог сборки перед конфигурацией — проверка сборки "с нуля".

.PARAMETER SkipTests
    Не запускать ctest после сборки.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -BuildType Debug -Clean
#>

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string] $BuildType = 'Release',

    [switch] $Clean,

    [switch] $SkipTests
)

# Молчаливый провал здесь дороже всего: собранный наполовину проект выглядит
# как успешный. Поэтому любая ошибка cmdlet-а — исключение, а код возврата
# каждой нативной команды проверяется явно (см. Invoke-CheckedNativeCommand).
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepositoryRoot = Split-Path -Parent $PSScriptRoot

function Write-Step {
    param([string] $Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Stop-WithClearMessage {
    param([string] $Message)
    Write-Host ""
    Write-Host "BUILD FAILED: $Message" -ForegroundColor Red
    exit 1
}

function Invoke-CheckedNativeCommand {
    <#
        Нативные .exe не бросают исключений — они лишь выставляют $LASTEXITCODE,
        а $ErrorActionPreference на них не действует. Без этой обёртки скрипт
        радостно дошёл бы до конца после провалившегося cmake.
    #>
    param(
        [Parameter(Mandatory = $true)][string]   $Executable,
        [Parameter(Mandatory = $true)][string[]] $Arguments,
        [Parameter(Mandatory = $true)][string]   $FailureMessage
    )

    Write-Host "    $Executable $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        Stop-WithClearMessage "$FailureMessage (exit code $LASTEXITCODE)"
    }
}

function Find-VcVarsBatchFile {
    <#
        Порядок поиска: сначала vswhere (официальный способ, переживает
        обновления и редакции Build Tools/Professional), потом жёстко заданные
        пути — на случай, когда vswhere не установлен.
    #>
    $vsWherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vsWherePath) {
        $installationPath = & $vsWherePath -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installationPath) {
            $candidate = Join-Path ($installationPath | Select-Object -First 1) 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }

    $fallbackCandidates = @(
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    )
    foreach ($candidate in $fallbackCandidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }

    return $null
}

function Enter-MsvcEnvironment {
    <#
        vcvars64.bat правит окружение только внутри своего cmd. Чтобы правки
        достались всем последующим вызовам cmake/ninja/cl, запускаем bat,
        печатаем `set` и переносим результат в текущий процесс PowerShell.
    #>
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        Write-Host "    cl.exe уже на PATH — окружение MSVC не трогаем."
        return
    }

    $vcVarsBatchFile = Find-VcVarsBatchFile
    if (-not $vcVarsBatchFile) {
        Stop-WithClearMessage @'
Не найден vcvars64.bat. Нужен Visual Studio 2022 (или Build Tools) с
компонентом "MSVC v143 - VS 2022 C++ x64/x86 build tools".
Проверенный путь по умолчанию:
  C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
'@
    }

    Write-Host "    vcvars64: $vcVarsBatchFile"

    $environmentDump = & cmd.exe /c "call `"$vcVarsBatchFile`" >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0 -or -not $environmentDump) {
        Stop-WithClearMessage "Не удалось выполнить $vcVarsBatchFile (exit code $LASTEXITCODE). Установка Visual Studio повреждена?"
    }

    foreach ($line in $environmentDump) {
        # Значения PATH сами содержат '=', поэтому режем только по первому.
        $separatorIndex = $line.IndexOf('=')
        if ($separatorIndex -lt 1) { continue }
        $name = $line.Substring(0, $separatorIndex)
        $value = $line.Substring($separatorIndex + 1)
        Set-Item -Path "Env:$name" -Value $value
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        Stop-WithClearMessage "Окружение MSVC импортировано, но cl.exe всё равно не найден на PATH."
    }
    Write-Host "    cl.exe: $((Get-Command cl.exe).Source)"
}

function Assert-ToolAvailable {
    param(
        [Parameter(Mandatory = $true)][string] $ToolName,
        [Parameter(Mandatory = $true)][string] $HowToInstall
    )
    $command = Get-Command $ToolName -ErrorAction SilentlyContinue
    if (-not $command) {
        Stop-WithClearMessage "Не найден $ToolName. $HowToInstall"
    }
    Write-Host "    $ToolName : $($command.Source)"
}

# --------------------------------------------------------------------------
# 1. Окружение и инструменты
# --------------------------------------------------------------------------
# Сначала MSVC, потом проверка cmake/ninja: у многих они приезжают вместе с
# Visual Studio ("C++ CMake tools for Windows") и появляются на PATH только
# после vcvars. Проверять до этого — значит отказывать рабочей машине.
Write-Step "Вхожу в окружение MSVC"
Enter-MsvcEnvironment

Write-Step "Проверяю инструменты"
Assert-ToolAvailable -ToolName 'cmake' -HowToInstall 'Поставьте CMake >= 3.30 и добавьте его в PATH: https://cmake.org/download/'
Assert-ToolAvailable -ToolName 'ninja' -HowToInstall 'Поставьте Ninja и добавьте его в PATH (или доставьте компонент VS "C++ CMake tools for Windows").'
Assert-ToolAvailable -ToolName 'ctest' -HowToInstall 'ctest ставится вместе с CMake — проверьте, что каталог CMake\bin целиком в PATH.'
Assert-ToolAvailable -ToolName 'conan' -HowToInstall 'Из Conan приезжают gtest и sfml. Поставьте его: pip install conan'
Assert-ToolAvailable -ToolName 'git'   -HowToInstall 'CPM.cmake и stdexec выкачиваются через git прямо во время конфигурации CMake.'

$presetName = if ($BuildType -eq 'Debug') { 'windows-msvc-debug' } else { 'windows-msvc-release' }
$buildDirectory = if ($BuildType -eq 'Debug') {
    Join-Path $RepositoryRoot 'build\windows-debug'
} else {
    Join-Path $RepositoryRoot 'build\windows'
}

# --------------------------------------------------------------------------
# 2. Чистая сборка (до Conan: conan_toolchain.cmake лежит внутри $buildDirectory)
# --------------------------------------------------------------------------
if ($Clean -and (Test-Path -LiteralPath $buildDirectory)) {
    Write-Step "Чищу каталог сборки"
    Write-Host "    $buildDirectory"
    Remove-Item -LiteralPath $buildDirectory -Recurse -Force
}

# --------------------------------------------------------------------------
# 3. Зависимости через Conan
# --------------------------------------------------------------------------
Write-Step "Ставлю зависимости через Conan"

# Профиль по умолчанию создаётся один раз на машину; без него conan install
# падает с "profile 'default' not found", что новичка ставит в тупик.
# $ErrorActionPreference временно ослаблен: в Windows PowerShell 5.1
# перенаправление stderr нативной программы порождает NativeCommandError, и при
# 'Stop' безобидная проверка превратилась бы в аварию.
$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& conan profile path default 2>&1 | Out-Null
$conanDefaultProfileExitCode = $LASTEXITCODE
$ErrorActionPreference = $previousErrorActionPreference

if ($conanDefaultProfileExitCode -ne 0) {
    Write-Host "    Профиля default нет — создаю (conan profile detect)."
    Invoke-CheckedNativeCommand -Executable 'conan' `
        -Arguments @('profile', 'detect', '--force') `
        -FailureMessage 'conan profile detect не сработал'
}

# compiler.cppstd намеренно не задаётся: под него в ConanCenter есть готовые
# бинарники gtest и sfml для msvc, а собственный стандарт проекта всё равно
# фиксирует CMakeLists.txt (CMAKE_CXX_STANDARD 23) уже после того, как
# conan_toolchain.cmake отработал. Форсировать здесь 23 — значит собирать sfml
# из исходников на пустом месте.
#
# tools.cmake.cmaketoolchain:generator=Ninja обязателен: по умолчанию Conan
# считает, что на Windows собирают через Visual Studio, и пишет в
# conan_toolchain.cmake `set(CMAKE_GENERATOR_PLATFORM x64 ... FORCE)`. С
# генератором Ninja CMake на этом падает («does not support platform
# specification»).
Invoke-CheckedNativeCommand -Executable 'conan' `
    -Arguments @('install', $RepositoryRoot,
                 '--output-folder', $buildDirectory,
                 '--build', 'missing',
                 '--settings', "build_type=$BuildType",
                 '--conf', 'tools.cmake.cmaketoolchain:generator=Ninja') `
    -FailureMessage 'conan install не сработал'

# Отдельно чинить CMakeUserPresets.json не нужно. Conan копит в нём по записи
# include на каждый каталог сборки, и после -Clean ссылка на удалённый каталог
# ломает cmake на любом пресете ("File not found: ...\CMakePresets.json") — но
# conan install идёт раньше конфигурации и выбрасывает мёртвые записи сам.
# Поэтому здесь и важен порядок: сначала очистка, потом Conan, и только потом
# cmake.

# Путь к conan_toolchain.cmake задаёт layout() из conanfile.py, а не скрипт,
# поэтому файл ищется, а не угадывается: если layout поменяют, здесь будет
# внятная ошибка, а не «Could NOT find GTest» через полминуты.
$toolchainCandidates = @(Get-ChildItem -LiteralPath $buildDirectory -Filter 'conan_toolchain.cmake' -File -Recurse |
    Sort-Object FullName)
if ($toolchainCandidates.Count -ne 1) {
    Stop-WithClearMessage "Ожидался ровно один conan_toolchain.cmake внутри $buildDirectory, найдено $($toolchainCandidates.Count)."
}
$toolchainFile = $toolchainCandidates[0].FullName
Write-Host "    conan_toolchain.cmake: $toolchainFile"

# --------------------------------------------------------------------------
# 4. Конфигурация и сборка
# --------------------------------------------------------------------------
Write-Step "Конфигурирую CMake (пресет $presetName)"
Write-Host "    Первый запуск дольше: CPM.cmake и stdexec выкачиваются из GitHub."
Push-Location $RepositoryRoot
try {
    Invoke-CheckedNativeCommand -Executable 'cmake' `
        -Arguments @('--preset', $presetName, "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile") `
        -FailureMessage "cmake --preset $presetName не сработал"

    Write-Step "Собираю"
    Invoke-CheckedNativeCommand -Executable 'cmake' `
        -Arguments @('--build', '--preset', $presetName) `
        -FailureMessage "cmake --build --preset $presetName не сработал"

    if (-not $SkipTests) {
        # Тесты не headless: почти каждый из них открывает настоящее окно SFML
        # (sf::RenderWindow) и требует живого рабочего стола с OpenGL. По ssh, в
        # сессии службы или на машине без видеодрайвера они упадут не по вине
        # кода — предупреждаем заранее, чтобы такой провал не выглядел загадкой.
        Write-Step "Запускаю тесты (ctest)"
        Write-Host "    Тесты открывают окна SFML — нужен интерактивный рабочий стол." -ForegroundColor Yellow
        Invoke-CheckedNativeCommand -Executable 'ctest' `
            -Arguments @('--preset', $presetName) `
            -FailureMessage 'ctest сообщил о провалившихся тестах'
    }
} finally {
    Pop-Location
}

# --------------------------------------------------------------------------
# 5. Что получилось
# --------------------------------------------------------------------------
# CMakeFiles\ и _deps\ отбрасываются: там лежат CMakeCXXCompilerId.exe и
# исполняемые файлы сторонних зависимостей, а не артефакты проекта. Показывать
# их как результат сборки нечестно.
$producedExecutables = @(Get-ChildItem -LiteralPath $buildDirectory -Filter '*.exe' -File -Recurse |
    Where-Object { $_.FullName -notmatch '\\CMakeFiles\\|\\_deps\\' } |
    Sort-Object FullName)

if (-not $producedExecutables) {
    Stop-WithClearMessage "Сборка завершилась без ошибок, но в $buildDirectory нет ни одного .exe. Это не успех."
}

Write-Step "Готово"
Write-Host "    Каталог сборки: $buildDirectory"
foreach ($executable in $producedExecutables) {
    Write-Host "    $($executable.FullName)" -ForegroundColor Green
}
Write-Host ""
Write-Host "Точка входа проекта — $(Join-Path $buildDirectory 'MandelbrotFractal.exe')"
