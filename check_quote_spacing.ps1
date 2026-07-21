param(
    [string]$HeaderPath = "src\litclock_data.h"
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $HeaderPath)) {
    Write-Error "Cannot find $HeaderPath"
    exit 1
}

function Unescape-CString([string]$s) {
    if ($null -eq $s) { return $s }
    return $s.Replace('\\', '\').Replace('\"', '"')
}

function Is-WordChar([char]$c) {
    return [char]::IsLetterOrDigit($c) -or $c -eq '_'
}

function Test-Boundary([string]$label, [string]$before, [string]$phrase, [string]$after, [int]$lineNo, [ref]$violations) {
    $full = "$before$phrase$after"

    if (-not [string]::IsNullOrEmpty($before) -and -not [string]::IsNullOrEmpty($phrase)) {
        $left = $before[$before.Length - 1]
        if (Is-WordChar $left) {
            $violations.Value.Add([PSCustomObject]@{
                Line = $lineNo
                Variant = $label
                Side = 'before'
                Full = $full
                Snippet = "$before|$phrase|$after"
                Reason = 'Missing separator before time phrase'
            }) | Out-Null
        }
    }

    if (-not [string]::IsNullOrEmpty($phrase) -and -not [string]::IsNullOrEmpty($after)) {
        $right = $after[0]
        if (Is-WordChar $right) {
            $violations.Value.Add([PSCustomObject]@{
                Line = $lineNo
                Variant = $label
                Side = 'after'
                Full = $full
                Snippet = "$before|$phrase|$after"
                Reason = 'Missing separator after time phrase'
            }) | Out-Null
        }
    }
}

$violations = [System.Collections.Generic.List[object]]::new()
$lineNo = 0
$entryCount = 0

Get-Content $HeaderPath -Encoding UTF8 | ForEach-Object {
    $lineNo++
    $line = $_.Trim()
    if (-not $line.StartsWith('{')) { return }

    $matches = [regex]::Matches($line, '"(?:\\.|[^"])*"')
    if ($matches.Count -lt 4) { return }

    # Generated rows now contain eight quoted string fields after the minute:
    # before, phrase, after, attr, beforeTight, afterTight, beforeCompact, afterCompact
    if ($matches.Count -ne 8) {
        Write-Warning "Line ${lineNo}: expected 8 quoted fields, found $($matches.Count). Skipping row."
        return
    }

    $entryCount++

    $before        = Unescape-CString $matches[0].Value.Trim('"')
    $phrase        = Unescape-CString $matches[1].Value.Trim('"')
    $after         = Unescape-CString $matches[2].Value.Trim('"')
    $beforeTight   = Unescape-CString $matches[4].Value.Trim('"')
    $afterTight    = Unescape-CString $matches[5].Value.Trim('"')
    $beforeCompact = Unescape-CString $matches[6].Value.Trim('"')
    $afterCompact  = Unescape-CString $matches[7].Value.Trim('"')

    Test-Boundary 'base' $before $phrase $after $lineNo ([ref]$violations)
    Test-Boundary 'tight' $beforeTight $phrase $afterTight $lineNo ([ref]$violations)
    Test-Boundary 'compact' $beforeCompact $phrase $afterCompact $lineNo ([ref]$violations)
}

if ($violations.Count -gt 0) {
    Write-Host "Found $($violations.Count) spacing issue(s) across $entryCount entries:" -ForegroundColor Red
    $violations |
        Sort-Object Line, Variant, Side |
        ForEach-Object {
            Write-Host "Line $($_.Line) [$($_.Variant)/$($_.Side)]: $($_.Reason)"
            Write-Host "  $($_.Snippet)"
        }
    exit 1
}

Write-Host "Checked $entryCount entries in ${HeaderPath}: no spacing issues found." -ForegroundColor Green