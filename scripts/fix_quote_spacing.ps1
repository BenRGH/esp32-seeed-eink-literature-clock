param(
    [string]$HeaderPath = "src/litclock_data.h"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path $HeaderPath)) {
    Write-Error "Header not found: $HeaderPath"
    exit 2
}

function Decode-CString([string]$s) {
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

function Encode-CString([string]$s) {
    if ($null -eq $s) { return "" }
    return $s.Replace('\\', '\\\\').Replace('"', '\\"')
}

function Ensure-Boundary([string]$left, [string]$phrase, [string]$right) {
    $l = if ($null -eq $left) { "" } else { $left }
    $p = if ($null -eq $phrase) { "" } else { $phrase }
    $r = if ($null -eq $right) { "" } else { $right }

    $lTrim = $l.TrimEnd()
    $rTrim = $r.TrimStart()

    if ($lTrim.Length -gt 0 -and $p.Length -gt 0) {
        $lChar = $lTrim[$lTrim.Length - 1]
        $p0 = $p[0]
        if ([char]::IsLetterOrDigit($lChar) -and [char]::IsLetterOrDigit($p0)) {
            $lTrim += " "
        }
    }

    if ($rTrim.Length -gt 0 -and $p.Length -gt 0) {
        $pN = $p[$p.Length - 1]
        $r0 = $rTrim[0]
        if ([char]::IsLetterOrDigit($pN) -and [char]::IsLetterOrDigit($r0)) {
            $rTrim = " " + $rTrim
        }
    }

    return [PSCustomObject]@{ Left = $lTrim; Right = $rTrim }
}

$linePattern = '^\s*\{\s*(\d+)\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"(?:\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)")?\s*\},\s*$'

$lines = Get-Content $HeaderPath -Encoding UTF8
$outLines = New-Object System.Collections.Generic.List[string]
$fixedRows = 0

foreach ($line in $lines) {
    $m = [regex]::Match($line, $linePattern)
    if (-not $m.Success) {
        $outLines.Add($line) | Out-Null
        continue
    }

    $minute = $m.Groups[1].Value
    $before = Decode-CString $m.Groups[2].Value
    $phrase = Decode-CString $m.Groups[3].Value
    $after  = Decode-CString $m.Groups[4].Value
    $attr   = Decode-CString $m.Groups[5].Value

    $base = Ensure-Boundary $before $phrase $after
    $before2 = $base.Left
    $after2 = $base.Right

    if ($m.Groups[6].Success) {
        $tb = Decode-CString $m.Groups[6].Value
        $ta = Decode-CString $m.Groups[7].Value
        $cb = Decode-CString $m.Groups[8].Value
        $ca = Decode-CString $m.Groups[9].Value

        $t = Ensure-Boundary $tb $phrase $ta
        $c = Ensure-Boundary $cb $phrase $ca

        $tb2 = $t.Left
        $ta2 = $t.Right
        $cb2 = $c.Left
        $ca2 = $c.Right

        $changed = ($before2 -ne $before) -or ($after2 -ne $after) -or
                   ($tb2 -ne $tb) -or ($ta2 -ne $ta) -or ($cb2 -ne $cb) -or ($ca2 -ne $ca)

        if ($changed) { $fixedRows++ }

        $newline = '  { ' + $minute + ', "' + (Encode-CString $before2) + '", "' + (Encode-CString $phrase) + '", "' + (Encode-CString $after2) + '", "' + (Encode-CString $attr) + '", "' + (Encode-CString $tb2) + '", "' + (Encode-CString $ta2) + '", "' + (Encode-CString $cb2) + '", "' + (Encode-CString $ca2) + '" },'
        $outLines.Add($newline) | Out-Null
        continue
    }

    $changed = ($before2 -ne $before) -or ($after2 -ne $after)
    if ($changed) { $fixedRows++ }
    $newline = '  { ' + $minute + ', "' + (Encode-CString $before2) + '", "' + (Encode-CString $phrase) + '", "' + (Encode-CString $after2) + '", "' + (Encode-CString $attr) + '" },'
    $outLines.Add($newline) | Out-Null
}

[System.IO.File]::WriteAllLines((Resolve-Path $HeaderPath), $outLines, (New-Object System.Text.UTF8Encoding $false))
Write-Host "Applied spacing normalization to $fixedRows row(s)."
