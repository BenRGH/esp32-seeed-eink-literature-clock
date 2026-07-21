param(
    [string]$HeaderPath = "src/litclock_data.h",
    [switch]$IncludeVariants = $true,
    [switch]$ShowAll
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path $HeaderPath)) {
    Write-Error "Header not found: $HeaderPath"
    exit 2
}

function ConvertFrom-CStringLiteral([string]$s) {
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt $s.Length; $i++) {
        $ch = $s[$i]
        if ($ch -eq '\\' -and $i + 1 -lt $s.Length) {
            $i++
            switch ($s[$i]) {
                'n' { [void]$sb.Append("`n") }
                'r' { [void]$sb.Append("`r") }
                't' { [void]$sb.Append("`t") }
                '\\' { [void]$sb.Append('\\') }
                '"' { [void]$sb.Append('"') }
                default { [void]$sb.Append($s[$i]) }
            }
        } else {
            [void]$sb.Append($ch)
        }
    }
    return $sb.ToString()
}

function Test-MissingSeparator([string]$left, [string]$right) {
    if ([string]::IsNullOrEmpty($left) -or [string]::IsNullOrEmpty($right)) { return $false }
    $l = $left[$left.Length - 1]
    $r = $right[0]
    return [char]::IsLetterOrDigit($l) -and [char]::IsLetterOrDigit($r)
}

function Ensure-PhraseBoundarySpacing([string]$before, [string]$phrase, [string]$after) {
    $b = if ($null -eq $before) { "" } else { $before }
    $a = if ($null -eq $after)  { "" } else { $after }

    $bTrim = $b.TrimEnd()
    $aTrim = $a.TrimStart()

    if (-not [string]::IsNullOrEmpty($bTrim) -and -not [string]::IsNullOrEmpty($phrase)) {
        $l = $bTrim[$bTrim.Length - 1]
        $r = $phrase[0]
        if ([char]::IsLetterOrDigit($l) -and [char]::IsLetterOrDigit($r)) {
            $bTrim += " "
        }
    }

    if (-not [string]::IsNullOrEmpty($aTrim) -and -not [string]::IsNullOrEmpty($phrase)) {
        $l = $phrase[$phrase.Length - 1]
        $r = $aTrim[0]
        if ([char]::IsLetterOrDigit($l) -and [char]::IsLetterOrDigit($r)) {
            $aTrim = " " + $aTrim
        }
    }

    return [PSCustomObject]@{ Before = $bTrim; After = $aTrim }
}

function New-Issue([int]$lineNo,[int]$minute,[string]$variant,[string]$side,[string]$phase,[string]$before,[string]$phrase,[string]$after) {
    $joined = $before + $phrase + $after
    return [PSCustomObject]@{
        Line = $lineNo
        Minute = $minute
        Variant = $variant
        Side = $side
        Phase = $phase
        Preview = if ($joined.Length -gt 160) { $joined.Substring(0, 160) + "..." } else { $joined }
    }
}

function Add-BoundaryIssues {
    param(
        [System.Collections.Generic.List[object]]$sink,
        [int]$lineNo,
        [int]$minute,
        [string]$variant,
        [string]$phase,
        [string]$before,
        [string]$phrase,
        [string]$after
    )

    if (Test-MissingSeparator $before $phrase) {
        $sink.Add((New-Issue $lineNo $minute $variant "before|phrase" $phase $before $phrase $after))
    }
    if (Test-MissingSeparator $phrase $after) {
        $sink.Add((New-Issue $lineNo $minute $variant "phrase|after" $phase $before $phrase $after))
    }
}

$linePattern = '^\s*\{\s*(\d+)\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"(?:\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)")?\s*\},\s*$'

$rawIssues = New-Object System.Collections.Generic.List[object]
$normalizedIssues = New-Object System.Collections.Generic.List[object]
$rows = 0

$lines = Get-Content $HeaderPath -Encoding UTF8
for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    $m = [regex]::Match($line, $linePattern)
    if (-not $m.Success) { continue }

    $rows++
    $lineNo = $i + 1
    $minute = [int]$m.Groups[1].Value

    $baseBefore = ConvertFrom-CStringLiteral $m.Groups[2].Value
    $phrase = ConvertFrom-CStringLiteral $m.Groups[3].Value
    $baseAfter = ConvertFrom-CStringLiteral $m.Groups[4].Value

    Add-BoundaryIssues $rawIssues $lineNo $minute "base" "raw" $baseBefore $phrase $baseAfter
    $baseNorm = Ensure-PhraseBoundarySpacing $baseBefore $phrase $baseAfter
    Add-BoundaryIssues $normalizedIssues $lineNo $minute "base" "normalized" $baseNorm.Before $phrase $baseNorm.After

    if ($IncludeVariants -and $m.Groups[6].Success) {
        $tb = ConvertFrom-CStringLiteral $m.Groups[6].Value
        $ta = ConvertFrom-CStringLiteral $m.Groups[7].Value
        $cb = ConvertFrom-CStringLiteral $m.Groups[8].Value
        $ca = ConvertFrom-CStringLiteral $m.Groups[9].Value

        Add-BoundaryIssues $rawIssues $lineNo $minute "tight" "raw" $tb $phrase $ta
        $tightNorm = Ensure-PhraseBoundarySpacing $tb $phrase $ta
        Add-BoundaryIssues $normalizedIssues $lineNo $minute "tight" "normalized" $tightNorm.Before $phrase $tightNorm.After

        Add-BoundaryIssues $rawIssues $lineNo $minute "compact" "raw" $cb $phrase $ca
        $compactNorm = Ensure-PhraseBoundarySpacing $cb $phrase $ca
        Add-BoundaryIssues $normalizedIssues $lineNo $minute "compact" "normalized" $compactNorm.Before $phrase $compactNorm.After
    }
}

Write-Host "Checked $rows quotes in $HeaderPath"
Write-Host "Raw boundary issues: $($rawIssues.Count)"
Write-Host "Post-normalization boundary issues: $($normalizedIssues.Count)"

if ($rawIssues.Count -gt 0) {
    Write-Host ""
    Write-Host "Sample raw issues:"
    if ($ShowAll) {
        $rawIssues | Format-Table -AutoSize
    } else {
        $rawIssues | Select-Object -First 20 | Format-Table -AutoSize
        if ($rawIssues.Count -gt 20) {
            Write-Host "(Showing first 20 raw issues. Re-run with -ShowAll for full list.)"
        }
    }
}

if ($normalizedIssues.Count -eq 0) {
    Write-Host ""
    Write-Host "No boundary spacing issues remain after normalization."
    exit 0
}

Write-Host ""
Write-Host "Normalized output still has issues (unexpected):"
if ($ShowAll) {
    $normalizedIssues | Format-Table -AutoSize
} else {
    $normalizedIssues | Select-Object -First 20 | Format-Table -AutoSize
    if ($normalizedIssues.Count -gt 20) {
        Write-Host "(Showing first 20 normalized issues. Re-run with -ShowAll for full list.)"
    }
}
exit 1
