param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$BaseTag = "",

    [switch]$ApplyVersion
)

$ErrorActionPreference = "Stop"

$CleanVer = $Version.Trim() -replace '^[vV]', ''
if ($CleanVer -notmatch '^(\d+)\.(\d+)\.(\d+)(?:[-.]([A-Za-z0-9._-]+))?$') {
    Write-Error "Invalid version '$Version'. Expected format: vX.Y.Z or vX.Y.Z-alpha (e.g., v0.1.0-alpha, v0.2.0)."
    exit 1
}

$Major      = $Matches[1]
$Minor      = $Matches[2]
$Patch      = $Matches[3]
$BaseSemVer = "$Major.$Minor.$Patch"
$FullVer    = $CleanVer
$Tag        = "v$FullVer"
$CommaVer   = "$Major,$Minor,$Patch,0"
$DotFourVer = "$Major.$Minor.$Patch.0"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path

if ([string]::IsNullOrWhiteSpace($BaseTag)) {
    $tags = git -C $RepoRoot tag --list "v*" --sort=-creatordate 2>$null
    if ($LASTEXITCODE -eq 0 -and $tags) {
        foreach ($t in $tags) {
            $trimmed = $t.Trim()
            if ($trimmed -and $trimmed -ne $Tag) {
                $BaseTag = $trimmed
                break
            }
        }
    }
}

$script:PassCount = 0
$script:WarnCount = 0
$script:FailCount = 0

function Write-Pass {
    param([string]$Message)
    $script:PassCount++
    Write-Host ("[PASS] {0}" -f $Message) -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    $script:WarnCount++
    Write-Host ("[WARN] {0}" -f $Message) -ForegroundColor Yellow
}

function Write-Fail {
    param([string]$Message)
    $script:FailCount++
    Write-Host ("[FAIL] {0}" -f $Message) -ForegroundColor Red
}

function Write-Utf8NoBom {
    param(
        [string]$Path,
        [string]$Text
    )
    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Text, $enc)
}

Write-Host ("=== M.O.R.W.H.A Release Preparation Audit ({0}) ===" -f $Tag) -ForegroundColor Cyan
if ($BaseTag) {
    Write-Host ("Base tag: {0}" -f $BaseTag)
} else {
    Write-Host "Base tag: (none detected)"
}
Write-Host ""

$VersionTxtRel  = "SKSE\Plugins\MORWHA\version.txt"
$VersionTxtPath = Join-Path $RepoRoot $VersionTxtRel
$CMakePath      = Join-Path $RepoRoot "SKSE_Source\CMakeLists.txt"
$VersionRcPath  = Join-Path $RepoRoot "SKSE_Source\version.rc"
$MetaIniPath    = Join-Path $RepoRoot "meta.ini"
$ChangelogPath  = Join-Path $RepoRoot "CHANGELOG.md"

# -----------------------------------------------------------------------------
# Optional -ApplyVersion: bump version.txt, CMakeLists.txt, version.rc, meta.ini
# -----------------------------------------------------------------------------
if ($ApplyVersion) {
    Write-Host "--> Applying version $FullVer (numeric $BaseSemVer) metadata..." -ForegroundColor Cyan

    if (Test-Path $VersionTxtPath) {
        $curTxt = [System.IO.File]::ReadAllText($VersionTxtPath).Trim()
        if ($curTxt -ne $FullVer) {
            Write-Utf8NoBom -Path $VersionTxtPath -Text "$FullVer`n"
            Write-Host ("[UPDATED] {0} -> {1}" -f $VersionTxtRel, $FullVer) -ForegroundColor Green
        } else {
            Write-Host ("[OK] {0} already at {1}" -f $VersionTxtRel, $FullVer)
        }
    }

    if (Test-Path $CMakePath) {
        $cmakeRaw = [System.IO.File]::ReadAllText($CMakePath)
        $newCmake = [regex]::Replace(
            $cmakeRaw,
            '(project\(\s*MORWHA\s+VERSION\s+)[^\s\)]+(\s+LANGUAGES\s+CXX\s*\))',
            ('${1}' + $BaseSemVer + '${2}'),
            1
        )
        if ($newCmake -ne $cmakeRaw) {
            Write-Utf8NoBom -Path $CMakePath -Text $newCmake
            Write-Host ("[UPDATED] SKSE_Source\CMakeLists.txt -> VERSION {0}" -f $BaseSemVer) -ForegroundColor Green
        } else {
            Write-Host ("[OK] SKSE_Source\CMakeLists.txt already at {0}" -f $BaseSemVer)
        }
    }

    if (Test-Path $VersionRcPath) {
        $rcRaw = [System.IO.File]::ReadAllText($VersionRcPath)
        $newRc = $rcRaw
        $newRc = [regex]::Replace($newRc, '(?m)^(\s*FILEVERSION\s+)\d+,\d+,\d+,\d+', ('${1}' + $CommaVer))
        $newRc = [regex]::Replace($newRc, '(?m)^(\s*PRODUCTVERSION\s+)\d+,\d+,\d+,\d+', ('${1}' + $CommaVer))
        $newRc = [regex]::Replace($newRc, '(VALUE\s+"FileVersion",\s*")[^"]*(")', ('${1}' + $DotFourVer + '${2}'))
        $newRc = [regex]::Replace($newRc, '(VALUE\s+"ProductVersion",\s*")[^"]*(")', ('${1}' + $DotFourVer + '${2}'))
        if ($newRc -ne $rcRaw) {
            Write-Utf8NoBom -Path $VersionRcPath -Text $newRc
            Write-Host ("[UPDATED] SKSE_Source\version.rc -> {0} ({1})" -f $DotFourVer, $CommaVer) -ForegroundColor Green
            Write-Host "[INFO] Run build-and-test.ps1 -Target all -Reconfigure to embed updated version resources into MORWHA.dll." -ForegroundColor Cyan
        } else {
            Write-Host ("[OK] SKSE_Source\version.rc already at {0}" -f $DotFourVer)
        }
    }

    if (Test-Path $MetaIniPath) {
        $metaRaw = [System.IO.File]::ReadAllText($MetaIniPath)
        $newMeta = [regex]::Replace($metaRaw, '(?m)^(version=).*$', ('${1}' + $BaseSemVer))
        if ($newMeta -ne $metaRaw) {
            Write-Utf8NoBom -Path $MetaIniPath -Text $newMeta
            Write-Host ("[UPDATED] meta.ini -> version={0}" -f $BaseSemVer) -ForegroundColor Green
        }
    }
    Write-Host ""
}

# -----------------------------------------------------------------------------
# 1. Version Metadata Sync
# -----------------------------------------------------------------------------
Write-Host "1. Version Metadata Sync" -ForegroundColor Cyan

if (Test-Path $VersionTxtPath) {
    $vTxt = [System.IO.File]::ReadAllText($VersionTxtPath).Trim()
    if ($vTxt -eq $FullVer -or $vTxt -eq $BaseSemVer) {
        Write-Pass ("{0} is '{1}'" -f $VersionTxtRel, $vTxt)
    } else {
        Write-Fail ("{0} is '{1}' (expected '{2}' or '{3}'). Run with -ApplyVersion." -f $VersionTxtRel, $vTxt, $FullVer, $BaseSemVer)
    }
} else {
    Write-Fail ("Missing {0}" -f $VersionTxtRel)
}

if (Test-Path $CMakePath) {
    $cmakeContent = [System.IO.File]::ReadAllText($CMakePath)
    if ($cmakeContent -match 'project\(\s*MORWHA\s+VERSION\s+([^\s\)]+)') {
        $cmakeVer = $Matches[1]
        if ($cmakeVer -eq $BaseSemVer) {
            Write-Pass ("SKSE_Source\CMakeLists.txt project VERSION is '{0}'" -f $BaseSemVer)
        } else {
            Write-Fail ("SKSE_Source\CMakeLists.txt project VERSION is '{0}' (expected '{1}'). Run with -ApplyVersion." -f $cmakeVer, $BaseSemVer)
        }
    } else {
        Write-Fail "Could not parse project(MORWHA VERSION ...) in SKSE_Source\CMakeLists.txt"
    }
} else {
    Write-Fail "Missing SKSE_Source\CMakeLists.txt"
}

if (Test-Path $VersionRcPath) {
    $rcContent = [System.IO.File]::ReadAllText($VersionRcPath)
    $fvMatch  = $rcContent -match ('(?m)^\s*FILEVERSION\s+' + [regex]::Escape($CommaVer) + '\b')
    $pvMatch  = $rcContent -match ('(?m)^\s*PRODUCTVERSION\s+' + [regex]::Escape($CommaVer) + '\b')
    $fvsMatch = $rcContent -match ('VALUE\s+"FileVersion",\s*"' + [regex]::Escape($DotFourVer) + '"')
    $pvsMatch = $rcContent -match ('VALUE\s+"ProductVersion",\s*"' + [regex]::Escape($DotFourVer) + '"')
    if ($fvMatch -and $pvMatch -and $fvsMatch -and $pvsMatch) {
        Write-Pass ("SKSE_Source\version.rc matches '{0}' ({1})" -f $DotFourVer, $CommaVer)
    } else {
        Write-Fail ("SKSE_Source\version.rc does not match '{0}' ({1}). Run with -ApplyVersion." -f $DotFourVer, $CommaVer)
    }
} else {
    Write-Fail "Missing SKSE_Source\version.rc"
}

if (Test-Path $ChangelogPath) {
    $changelogContent = [System.IO.File]::ReadAllText($ChangelogPath)
    if ($changelogContent -match ('(?m)^##\s+v?' + [regex]::Escape($FullVer) + '\b')) {
        Write-Pass ("CHANGELOG.md contains section for {0}" -f $Tag)
    } else {
        Write-Warn ("CHANGELOG.md has no '## {0}' section (release-zip.yml will use fallback release body)" -f $Tag)
    }
} else {
    Write-Warn "CHANGELOG.md not present yet (release-zip.yml will use fallback release body)"
}
Write-Host ""

# -----------------------------------------------------------------------------
# 2. Release Zip Required Paths & Shipped Asset Counts
# -----------------------------------------------------------------------------
Write-Host "2. Release Zip Required Paths & Assets" -ForegroundColor Cyan

$RequiredPaths = @(
    "Scripts\MORWHA.pex",
    "Scripts\Source\MORWHA.psc",
    "SKSE\Plugins\MORWHA.dll",
    $VersionTxtRel,
    "SKSE\Plugins\MORWHA\ChimeraAccessories",
    "SKSE\Plugins\MORWHA\FaceAnchors",
    "SKSE\Plugins\MORWHA\SkinStencils",
    "SKSE_Source\external\morwha_core\lib\Release\morwha_core.lib",
    "SKSE_Source\external\morwha_core\include\morwha\core\MorwhaCore.h"
)

foreach ($rel in $RequiredPaths) {
    $full = Join-Path $RepoRoot $rel
    if (Test-Path $full) {
        Write-Pass ("Present: {0}" -f $rel)
    } else {
        Write-Fail ("Missing required path: {0}" -f $rel)
    }
}

$StencilDir = Join-Path $RepoRoot "SKSE\Plugins\MORWHA\SkinStencils"
if (Test-Path $StencilDir) {
    $mrstFiles = @(Get-ChildItem -Path $StencilDir -Filter "*.mrst" -File)
    if ($mrstFiles.Count -eq 62) {
        Write-Pass "SkinStencils contains all 62 .mrst stencil files"
    } else {
        Write-Fail ("SkinStencils has {0} .mrst file(s) (expected 62)" -f $mrstFiles.Count)
    }
}
Write-Host ""

# -----------------------------------------------------------------------------
# 3. SKSE Plugin (MORWHA.dll) & morwha_core.lib Symbol Hygiene
# -----------------------------------------------------------------------------
Write-Host "3. SKSE Plugin DLL & Core Archive Hygiene" -ForegroundColor Cyan

$DeployedDll = Join-Path $RepoRoot "SKSE\Plugins\MORWHA.dll"
$BuiltDll    = Join-Path $RepoRoot "SKSE_Source\build\release\Release\MORWHA.dll"

if (Test-Path $DeployedDll) {
    if (Test-Path $BuiltDll) {
        $deployedHash = (Get-FileHash -Path $DeployedDll -Algorithm SHA256).Hash
        $builtHash    = (Get-FileHash -Path $BuiltDll -Algorithm SHA256).Hash
        if ($deployedHash -eq $builtHash) {
            Write-Pass "SKSE\Plugins\MORWHA.dll matches Release build output (SHA-256)"
        } else {
            Write-Fail "SKSE\Plugins\MORWHA.dll differs from SKSE_Source\build\release\Release\MORWHA.dll (post-build copy was likely blocked by a locked SkyrimSE.exe)"
        }
    } else {
        Write-Warn "SKSE_Source\build\release\Release\MORWHA.dll not present locally; cannot compare build hash"
    }

    $dllTime = (Get-Item $DeployedDll).LastWriteTimeUtc
    $cppSources = Get-ChildItem -Path (Join-Path $RepoRoot "SKSE_Source") -Recurse -File | Where-Object {
        $_.FullName -notmatch '[\\/]build[\\/]' -and
        ($_.Extension -in ".cpp", ".h", ".hpp" -or $_.Name -in "CMakeLists.txt", "version.rc")
    }
    $staleCpp = @($cppSources | Where-Object { $_.LastWriteTimeUtc -gt $dllTime.AddSeconds(2) })
    if ($staleCpp.Count -eq 0) {
        Write-Pass "SKSE\Plugins\MORWHA.dll is newer than all SKSE_Source files"
    } else {
        $names = ($staleCpp | ForEach-Object { $_.Name }) -join ", "
        Write-Fail ("SKSE\Plugins\MORWHA.dll is older than modified C++ source(s): {0}. Run build-and-test.ps1 -Target all." -f $names)
    }

    $fvi = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($DeployedDll)
    $numericVer = "{0}.{1}.{2}.{3}" -f $fvi.FileMajorPart, $fvi.FileMinorPart, $fvi.FileBuildPart, $fvi.FilePrivatePart
    if ($fvi.FileVersion -eq $DotFourVer -and $numericVer -eq $DotFourVer) {
        Write-Pass ("Embedded DLL FileVersion is '{0}'" -f $DotFourVer)
    } else {
        Write-Fail ("Embedded DLL FileVersion is '{0}' (numeric {1}, expected '{2}'). Rebuild with -Target all -Reconfigure." -f $fvi.FileVersion, $numericVer, $DotFourVer)
    }
}

$CoreLib = Join-Path $RepoRoot "SKSE_Source\external\morwha_core\lib\Release\morwha_core.lib"
if (Test-Path $CoreLib) {
    $libRaw = [System.IO.File]::ReadAllText($CoreLib, [System.Text.Encoding]::GetEncoding(28591))
    $leaks = ([regex]::Matches($libRaw, [regex]::Escape("Domain@MORWHA"))).Count
    if ($leaks -eq 0) {
        Write-Pass "morwha_core.lib passes symbol-masking hygiene (0 Domain@MORWHA leaks)"
    } else {
        Write-Fail ("morwha_core.lib leaked {0} Domain@MORWHA symbol reference(s)" -f $leaks)
    }
}
Write-Host ""

# -----------------------------------------------------------------------------
# 4. Papyrus .psc -> .pex Audit (Read-Only)
# -----------------------------------------------------------------------------
Write-Host "4. Papyrus .psc -> .pex Audit (read-only)" -ForegroundColor Cyan

$PscDir = Join-Path $RepoRoot "Scripts\Source"
$PexDir = Join-Path $RepoRoot "Scripts"
$MissingPex = @()
$StalePex   = @()

if (Test-Path $PscDir) {
    $pscFiles = Get-ChildItem -Path $PscDir -Filter "*.psc" -File
    foreach ($psc in $pscFiles) {
        $pexName = "$($psc.BaseName).pex"
        $pexPath = Join-Path $PexDir $pexName
        if (-not (Test-Path $pexPath)) {
            $MissingPex += $psc.Name
            continue
        }
        $pex = Get-Item $pexPath
        if ($psc.LastWriteTimeUtc -gt $pex.LastWriteTimeUtc.AddSeconds(2)) {
            $StalePex += "$($psc.Name) (disk timestamp newer than $pexName)"
        }
    }
    if ($MissingPex.Count -eq 0 -and $StalePex.Count -eq 0) {
        Write-Pass ("All {0} Papyrus .psc scripts have matching up-to-date .pex files" -f $pscFiles.Count)
    } else {
        foreach ($m in $MissingPex) { Write-Fail ("Missing compiled .pex for {0}" -f $m) }
        foreach ($s in $StalePex)   { Write-Fail ("Stale compiled .pex for {0}" -f $s) }
    }
}
Write-Host ""

# -----------------------------------------------------------------------------
# 5. JSON / .jslot Syntax & Release Zip Hygiene
# -----------------------------------------------------------------------------
Write-Host "5. JSON/.jslot Syntax & Release Hygiene" -ForegroundColor Cyan

$JsonFiles = @()
foreach ($sub in @("SKSE\Plugins\MORWHA\ChimeraAccessories", "SKSE\Plugins\MORWHA\FaceAnchors")) {
    $dirFull = Join-Path $RepoRoot $sub
    if (Test-Path $dirFull) {
        $JsonFiles += Get-ChildItem -Path $dirFull -Include "*.json","*.jslot" -Recurse -File
    }
}

$BadJson = @()
foreach ($jf in $JsonFiles) {
    try {
        $null = [System.IO.File]::ReadAllText($jf.FullName) | ConvertFrom-Json
    } catch {
        $BadJson += "$($jf.FullName): $($_.Exception.Message)"
    }
}
if ($BadJson.Count -eq 0) {
    Write-Pass ("All {0} shipped JSON/.jslot manifest(s) parse cleanly" -f $JsonFiles.Count)
} else {
    foreach ($bj in $BadJson) { Write-Fail ("Invalid JSON/.jslot: {0}" -f $bj) }
}

$ArtifactExts = @(".pdb", ".iobj", ".ipdb", ".ilk", ".exp", ".lib", ".tmp", ".bak", ".orig", ".log")
$DisallowedNames = @("MORWHA.ini", "DynamicNPCRegistry.json", "GeneratedSkinCache.json")
$StrayFiles = @()
foreach ($zipFolder in @("Scripts", "SKSE")) {
    $folderFull = Join-Path $RepoRoot $zipFolder
    if (Test-Path $folderFull) {
        $hits = Get-ChildItem -Path $folderFull -Recurse -File | Where-Object {
            $_.Extension -in $ArtifactExts -or $_.Name -in $DisallowedNames
        }
        $StrayFiles += $hits
    }
}
if ($StrayFiles.Count -eq 0) {
    Write-Pass "No stray debug/temp/local-cache files inside Scripts/ or SKSE/"
} else {
    foreach ($sf in $StrayFiles) {
        Write-Fail ("Disallowed file in release folder: {0}" -f $sf.FullName)
    }
}
Write-Host ""

# -----------------------------------------------------------------------------
# 6. Git Context & Summary
# -----------------------------------------------------------------------------
Write-Host "6. Git Context" -ForegroundColor Cyan
if ($BaseTag) {
    Write-Host ("Commits since {0}:" -f $BaseTag)
    $commits = git -C $RepoRoot log "$BaseTag..HEAD" --oneline 2>$null
    if ($commits) {
        $commits | ForEach-Object { Write-Host ("  {0}" -f $_) }
    } else {
        Write-Host "  (none)"
    }
}

Write-Host "Working tree status (git status -s):"
$statusLines = git -C $RepoRoot status -s 2>$null
if ($statusLines) {
    $statusLines | ForEach-Object { Write-Host ("  {0}" -f $_) }
} else {
    Write-Host "  (clean)"
}
Write-Host ""

Write-Host ("Summary: {0} PASS, {1} WARN, {2} FAIL" -f $script:PassCount, $script:WarnCount, $script:FailCount) -ForegroundColor Cyan

if ($script:FailCount -gt 0) {
    exit 1
}
exit 0
