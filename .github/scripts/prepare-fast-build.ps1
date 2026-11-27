#requires -Version 7.0
<#
  .SYNOPSIS
    Fast-build 前置步骤：用 prebuilt-libs 里的预编译静态库替代「从 Native\deps 源码编译」。

  .DESCRIPTION
    1) 只跑一次 CMake configure（不编译）生成 ZXing 的 build/core/Version.h：
       该头是 configure_file 产物，源码树里没有，而 BarcodeFormat.h 会 #include "Version.h"，
       少了它直接 C1083。configure 参数与 build.yml 完全一致，保证与预编译 ZXing.lib 的宏匹配。

    2) 校验 prebuilt-libs 下的 4 个 .lib 是否存在，并按链接器的搜索路径摆放：
         prebuilt-libs/libwebp/libwebp.lib       -> Native/x64/Release/libwebp.lib
         prebuilt-libs/Ling/Ling.lib             -> Native/x64/Release/Ling.lib
         prebuilt-libs/Ling/yoga/yoga.lib        -> Native/x64/Release/yoga.lib
         prebuilt-libs/zxing-cpp-311/ZXing.lib   -> Native/deps/zxing-cpp-311/build/core/Release/ZXing.lib
       （ScreenShut.vcxproj 的 AdditionalLibraryDirectories 正好指向这两个目录）

    3) 就地改写 CI 工作区里的 Native/Src/ScreenShut.vcxproj：
         - 移除对 libwebp / yoga / Ling 的 ProjectReference（否则 msbuild 会去源码编译它们）
         - 在所有 Link/AdditionalDependencies 前面补上 Ling.lib;yoga.lib;
           （libwebp.lib / ZXing.lib 原本就在列表里）

    脚本只动 CI 工作区的副本，仓库中提交的 vcxproj 保持不变，
    因此 build.yml（从源码构建 deps）完全不受影响。
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = (Get-Location).Path
)

$ErrorActionPreference = 'Stop'
Push-Location $RepoRoot
try {
    $outDir = Join-Path $RepoRoot 'Native/x64/Release'
    $zxDir  = Join-Path $RepoRoot 'Native/deps/zxing-cpp-311/build/core/Release'
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    # ---------- 1. ZXing 的 Version.h（configure 产物） ----------
    $zxRoot  = 'Native/deps/zxing-cpp-311'
    $zxBuild = "$zxRoot/build"
    $zxVer   = "$zxBuild/core/Version.h"

    if (-not (Test-Path -LiteralPath $zxVer)) {
        $vsInstance = 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise'
        if (-not (Test-Path $vsInstance)) {
            $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
            if (Test-Path $vswhere) {
                $vsInstance = & $vswhere -latest -version '[17.0,18.0)' -products * `
                    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            }
        }
        if (-not $vsInstance -or -not (Test-Path $vsInstance)) { throw 'Visual Studio 2022 with VC tools was not found.' }
        $vsInstance = $vsInstance -replace '\\', '/'

        Write-Host '[cmake] configuring ZXing (versions.h only, no compile) ...'
        $env:VS170COMNTOOLS = "$vsInstance/Common7/Tools"
        $env:CMAKE_GENERATOR_INSTANCE = $vsInstance
        # 与 build.yml 的 configure 参数保持一致：Version.h 里的 ZXING_* 宏必须和预编译 ZXing.lib 一致
        cmake -S $zxRoot -B $zxBuild -G 'Visual Studio 17 2022' -A x64 `
            "-DCMAKE_GENERATOR_INSTANCE=$vsInstance" `
            -DBUILD_SHARED_LIBS=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
            -DZXING_WRITERS=OFF -DZXING_EXAMPLES=OFF `
            -DZXING_UNIT_TESTS=OFF -DZXING_BLACKBOX_TESTS=OFF
        if ($LASTEXITCODE -ne 0) { throw 'CMake configure for zxing failed' }
        if (-not (Test-Path -LiteralPath $zxVer)) { throw "Version.h not generated: $zxVer" }
    }
    Write-Host ("[cmake] Version.h -> {0}" -f $zxVer)

    New-Item -ItemType Directory -Force -Path $zxDir | Out-Null

    # ---------- 2. 摆放预编译库 ----------
    $libMap = @(
        @{ Src = 'prebuilt-libs/libwebp/libwebp.lib';     Dst = Join-Path $outDir 'libwebp.lib' },
        @{ Src = 'prebuilt-libs/Ling/Ling.lib';           Dst = Join-Path $outDir 'Ling.lib'    },
        @{ Src = 'prebuilt-libs/Ling/yoga/yoga.lib';      Dst = Join-Path $outDir 'yoga.lib'    },
        @{ Src = 'prebuilt-libs/zxing-cpp-311/ZXing.lib'; Dst = Join-Path $zxDir  'ZXing.lib'   }
    )

    $missing = @()
    foreach ($m in $libMap) { if (-not (Test-Path -LiteralPath $m.Src)) { $missing += $m.Src } }
    if ($missing) {
        throw "prebuilt libs missing:`n  $($missing -join "`n  ")`n请确认 prebuilt-libs 已提交到仓库。"
    }

    foreach ($m in $libMap) {
        Copy-Item -LiteralPath $m.Src -Destination $m.Dst -Force
        $len = (Get-Item -LiteralPath $m.Dst).Length
        Write-Host ("[prebuilt] {0,-45} -> {1}  ({2:N0} bytes)" -f $m.Src, $m.Dst, $len)
    }

    # ---------- 2. 改写 ScreenShut.vcxproj ----------
    $projPath = Join-Path $RepoRoot 'Native/Src/ScreenShut.vcxproj'
    if (-not (Test-Path -LiteralPath $projPath)) { throw "project not found: $projPath" }

    $xml = New-Object System.Xml.XmlDocument
    $xml.PreserveWhitespace = $true
    $xml.Load($projPath)

    $ns = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
    $ns.AddNamespace('msb', 'http://schemas.microsoft.com/developer/msbuild/2003')

    # 2.1 移除 ProjectReference
    $refs = @($xml.SelectNodes('//msb:ProjectReference', $ns))
    Write-Host ("[vcxproj] found {0} ProjectReference" -f $refs.Count)
    if ($refs.Count -eq 0) { throw 'ScreenShut.vcxproj 里没有 ProjectReference，工程结构可能已变化，请更新本脚本。' }
    foreach ($ref in $refs) {
        $include = $ref.GetAttribute('Include')
        $parent  = $ref.ParentNode
        $next    = $ref.NextSibling
        [void]$parent.RemoveChild($ref)
        # 连带吃掉紧随其后的缩进空白，避免留下空行
        if ($next -and $next.NodeType -eq [System.Xml.XmlNodeType]::Whitespace) {
            [void]$parent.RemoveChild($next)
        }
        Write-Host ("[vcxproj]   removed: {0}" -f $include)
    }

    # 清掉因此变空的 ItemGroup
    foreach ($g in @($xml.SelectNodes('//msb:ItemGroup[not(*)]', $ns))) {
        $prev = $g.PreviousSibling
        [void]$g.ParentNode.RemoveChild($g)
        if ($prev -and $prev.NodeType -eq [System.Xml.XmlNodeType]::Whitespace) {
            [void]$prev.ParentNode.RemoveChild($prev)
        }
        Write-Host '[vcxproj]   removed: empty ItemGroup'
    }

    # 2.2 显式链接 Ling.lib / yoga.lib（原来靠 ProjectReference 自动带上）
    $depNodes = @($xml.SelectNodes('//msb:ItemDefinitionGroup/msb:Link/msb:AdditionalDependencies', $ns))
    if ($depNodes.Count -eq 0) { throw 'ScreenShut.vcxproj 里没有 Link/AdditionalDependencies 节点。' }

    foreach ($node in $depNodes) {
        if ($node.InnerText -notmatch '(^|;)Ling\.lib(;|$)') {
            $node.InnerText = 'Ling.lib;yoga.lib;' + $node.InnerText
            $label = $node.ParentNode.ParentNode.GetAttribute('Condition')
            Write-Host ("[vcxproj]   + Ling.lib;yoga.lib  ({0})" -f $label)
        }
    }

    # 2.3 兜底校验：三个库都必须在链接输入里
    $all = ($depNodes | ForEach-Object { $_.InnerText }) -join ';'
    foreach ($need in 'Ling.lib', 'yoga.lib', 'libwebp.lib', 'ZXing.lib') {
        if ($all -notmatch [regex]::Escape($need)) { throw "link inputs missing $need" }
    }

    $xml.Save($projPath)

    # ---------- 3. 结果确认 ----------
    $after = New-Object System.Xml.XmlDocument
    $after.Load($projPath)
    $ns2 = New-Object System.Xml.XmlNamespaceManager($after.NameTable)
    $ns2.AddNamespace('msb', 'http://schemas.microsoft.com/developer/msbuild/2003')
    $leftRefs = @($after.SelectNodes('//msb:ProjectReference', $ns2)).Count
    if ($leftRefs -ne 0) { throw "ProjectReference 未清理干净，剩余 $leftRefs 个" }
    Write-Host '[ok] vcxproj patched: no ProjectReference, prebuilt libs wired.'

    # 供后续步骤使用的输出
    if ($env:GITHUB_OUTPUT) {
        "lib_dir=$outDir" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    }
}
finally {
    Pop-Location
}
