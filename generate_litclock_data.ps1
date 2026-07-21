# generate_litclock_data.ps1
#
# Generates src/litclock_data.h from litclock_annotated.csv.
# Run once from the repo root before building the project:
#
#   .\generate_litclock_data.ps1
#
# Parameters:
#   -maxQuoteLen N   Target max characters for (before+phrase+after)
#                    per quote in generated header (default 280).
#                    Overlength quotes are retained and truncated around
#                    the time phrase (whole words only).
#   -maxBodyChars N  Target max chars for (before + phrase) shown in body
#                    text (default 185). Used to pre-trim `before` so the
#                    time phrase stays visible more often on-device.
#   -sfwOnly         Restrict to sfw-tagged quotes (default: all quotes).
#
# What this does:
#   - Includes ALL quotes (SFW + NSFW) by default
#   - Normalises curly/fancy punctuation to ASCII equivalents
#     (U8g2_font_helvR08_tu covers Latin Extended U+0000-U+00FF, so accented
#      characters like é ü ñ are kept as UTF-8 — characters above U+00FF are
#      compatibility-normalized and folded into safe equivalents)
#   - Splits each row into before / phrase / after for red-highlight rendering
#   - Retains overlength rows by truncating around the phrase with markers
#   - Pre-trims overlong `before` text with a small leading marker so runtime
#     drawing does not need expensive visibility trimming per refresh
#   - Sorts entries by minute-of-day (required for binary search in firmware)
#   - Writes UTF-8 without BOM (C++ compilers reject BOM in source files)
# ─────────────────────────────────────────────────────────────────────────────

param(
    [int]    $maxQuoteLen = 280,
    [int]    $maxBodyChars = 185,
    [switch] $sfwOnly
)

$csvPath = "litclock_annotated.csv"
$outPath = "src\litclock_data.h"

function Normalize-ForFont([string]$s) {
    if ([string]::IsNullOrEmpty($s)) { return $s }

    # First pass: explicit punctuation replacements that commonly appear
    # in source data and should be converted deterministically.
    $s = $s `
        -replace [char]0x201C, '"'   -replace [char]0x201D, '"' `
        -replace [char]0x2018, "'"   -replace [char]0x2019, "'" `
        -replace [char]0x201A, "'"   -replace [char]0x201B, "'" `
        -replace [char]0x2039, '<'   -replace [char]0x203A, '>' `
        -replace [char]0x2014, '--'  -replace [char]0x2013, '-' `
        -replace [char]0x2026, '...' `
        -replace [char]0x2212, '-'   `
        -replace [char]0x00A0, ' '   `
        -replace [char]0x00AD, ''

    # Second pass: compatibility normalization folds stylized Unicode letters
    # (e.g. mathematical italic) back to plain Latin characters when possible.
    $compat = $s.Normalize([Text.NormalizationForm]::FormKD)
    $sb = New-Object System.Text.StringBuilder

    foreach ($ch in $compat.ToCharArray()) {
        $cp = [int]$ch

        # Keep anything in U+0000..U+00FF (font coverage target).
        if ($cp -le 0x00FF) {
            [void]$sb.Append($ch)
            continue
        }

        $cat = [Globalization.CharUnicodeInfo]::GetUnicodeCategory($ch)

        # Drop combining marks introduced by compatibility decomposition.
        if ($cat -eq [Globalization.UnicodeCategory]::NonSpacingMark -or
            $cat -eq [Globalization.UnicodeCategory]::SpacingCombiningMark -or
            $cat -eq [Globalization.UnicodeCategory]::EnclosingMark) {
            continue
        }

        # Map common Unicode spacing/dash punctuation into ASCII-safe forms.
        if ($cat -eq [Globalization.UnicodeCategory]::SpaceSeparator) {
            [void]$sb.Append(' ')
            continue
        }
        if ($cat -eq [Globalization.UnicodeCategory]::DashPunctuation) {
            [void]$sb.Append('-')
            continue
        }

        # Unknown out-of-range glyph: replace with a harmless placeholder.
        [void]$sb.Append('?')
    }

    $out = $sb.ToString()
    # Keep ellipsis compact per project style: no surrounding spaces.
    $out = [regex]::Replace($out, '\s*\.\.\.\s*', '...')
    return $out
}

function Trim-BeforeForPhraseVisibility([string]$before, [string]$phrase, [int]$maxBodyChars) {
    if ([string]::IsNullOrEmpty($before)) { return $before }
    if ([string]::IsNullOrEmpty($phrase)) { return $before }

    # Reserve room for the highlighted time phrase; trim only as much leading
    # text as needed to fit this conservative character budget.
    $budget = [Math]::Max(0, $maxBodyChars - $phrase.Length)
    if ($before.Length -le $budget) { return $before }

    $marker = "..."
    $keepAfterMarker = [Math]::Max(0, $budget - $marker.Length)
    if ($keepAfterMarker -le 0) { return "" }

    # Keep a suffix of complete words only (single-spaced). If a word would
    # overflow the remaining budget, we drop the whole word.
    $raw = $before.TrimStart()
    if ([string]::IsNullOrEmpty($raw)) { return "" }

    $matches = [regex]::Matches($raw, '\S+')
    if ($matches.Count -eq 0) { return "" }

    $keptWords = New-Object System.Collections.Generic.List[string]
    $used = 0
    for ($i = $matches.Count - 1; $i -ge 0; $i--) {
        $w = $matches[$i].Value
        $add = if ($keptWords.Count -eq 0) { $w.Length } else { 1 + $w.Length }
        if ($used + $add -le $keepAfterMarker) {
            $keptWords.Insert(0, $w)
            $used += $add
        } else {
            # Preserve a contiguous suffix from the source text.
            break
        }
    }

    if ($keptWords.Count -eq 0) { return "" }

    # Drop leading punctuation-only tokens so we don't start with hanging
    # punctuation after trimming (for example: "...,and then...").
    while ($keptWords.Count -gt 0 -and $keptWords[0] -match '^[^\p{L}\p{Nd}]+$') {
        $keptWords.RemoveAt(0)
    }

    if ($keptWords.Count -eq 0) { return "" }

    $body = ($keptWords -join ' ').Trim()
    $body = [regex]::Replace($body, '^[\p{P}\p{S}\s]+', '')
    if ([string]::IsNullOrEmpty($body)) { return "" }
    return $marker + $body
}

function Keep-RightWordsWithMarker([string]$text, [int]$maxChars, [string]$marker) {
    if ([string]::IsNullOrEmpty($text) -or $maxChars -le 0) { return "" }

    $raw = $text.Trim()
    if ([string]::IsNullOrEmpty($raw)) { return "" }
    if ($raw.Length -le $maxChars) { return $raw }

    $keepBudget = [Math]::Max(0, $maxChars - $marker.Length)
    if ($keepBudget -le 0) { return "" }

    $matches = [regex]::Matches($raw, '\S+')
    if ($matches.Count -eq 0) { return "" }

    $kept = New-Object System.Collections.Generic.List[string]
    $used = 0
    for ($i = $matches.Count - 1; $i -ge 0; $i--) {
        $w = $matches[$i].Value
        $add = if ($kept.Count -eq 0) { $w.Length } else { 1 + $w.Length }
        if ($used + $add -le $keepBudget) {
            $kept.Insert(0, $w)
            $used += $add
        } else {
            # Preserve a contiguous suffix from the source text.
            break
        }
    }

    if ($kept.Count -eq 0) { return "" }
    while ($kept.Count -gt 0 -and $kept[0] -match '^[^\p{L}\p{Nd}]+$') {
        $kept.RemoveAt(0)
    }
    if ($kept.Count -eq 0) { return "" }

    $body = ($kept -join ' ').Trim()
    $body = [regex]::Replace($body, '^[\p{P}\p{S}\s]+', '')
    if ([string]::IsNullOrEmpty($body)) { return "" }
    return $marker + $body
}

function Keep-LeftWordsWithMarker([string]$text, [int]$maxChars, [string]$marker) {
    if ([string]::IsNullOrEmpty($text) -or $maxChars -le 0) { return "" }

    $raw = $text.Trim()
    if ([string]::IsNullOrEmpty($raw)) { return "" }
    if ($raw.Length -le $maxChars) { return $raw }

    $keepBudget = [Math]::Max(0, $maxChars - $marker.Length)
    if ($keepBudget -le 0) { return "" }

    $matches = [regex]::Matches($raw, '\S+')
    if ($matches.Count -eq 0) { return "" }

    $kept = New-Object System.Collections.Generic.List[string]
    $used = 0
    for ($i = 0; $i -lt $matches.Count; $i++) {
        $w = $matches[$i].Value
        $add = if ($kept.Count -eq 0) { $w.Length } else { 1 + $w.Length }
        if ($used + $add -le $keepBudget) {
            $kept.Add($w)
            $used += $add
        } else {
            # Preserve a contiguous prefix from the source text.
            break
        }
    }

    if ($kept.Count -eq 0) { return "" }

    $body = ($kept -join ' ').Trim()
    $body = [regex]::Replace($body, '[\p{P}\p{S}\s]+$', '')
    if ([string]::IsNullOrEmpty($body)) { return "" }
    return $body + $marker
}

function Truncate-AroundPhrase([string]$before, [string]$phrase, [string]$after, [int]$maxLen) {
    # 1) Check current length.
    $combined = $before + $phrase + $after
    if ($combined.Length -le $maxLen) {
        return [PSCustomObject]@{ Before = $before; After = $after; Truncated = $false }
    }

    # 2) Truncate around phrase with whole-word helpers and ellipsis markers.
    $spaceBudget = [Math]::Max(0, $maxLen - $phrase.Length)
    if ($spaceBudget -le 0) {
        return [PSCustomObject]@{ Before = ""; After = ""; Truncated = $true }
    }

    $beforeCap = [Math]::Min($before.Length, [int][Math]::Floor($spaceBudget * 0.60))
    $afterCap  = [Math]::Max(0, $spaceBudget - $beforeCap)

    $build = {
        param([int]$bCap, [int]$aCap)

        $bRaw = $before.Trim()
        $aRaw = $after.Trim()

        $b = if ($bRaw.Length -le $bCap) { $bRaw } else { Keep-RightWordsWithMarker $before $bCap "..." }
        $a = if ($aRaw.Length -le $aCap) { $aRaw } else { Keep-LeftWordsWithMarker  $after  $aCap "..." }

        if ([string]::IsNullOrEmpty($b) -and $bRaw.Length -gt 0) {
            $a = Keep-LeftWordsWithMarker $after $spaceBudget "..."
        } elseif ([string]::IsNullOrEmpty($a) -and $aRaw.Length -gt 0) {
            $b = Keep-RightWordsWithMarker $before $spaceBudget "..."
        }

        [PSCustomObject]@{ Before = $b; After = $a }
    }

    # 3) Verify final length; if still too long, shrink caps and retry.
    $maxIters = $spaceBudget + 8
    for ($i = 0; $i -lt $maxIters; $i++) {
        $cand = & $build $beforeCap $afterCap
        $finalLen = ($cand.Before + $phrase + $cand.After).Length
        if ($finalLen -le $maxLen) {
            return [PSCustomObject]@{ Before = $cand.Before; After = $cand.After; Truncated = $true }
        }

        if ($beforeCap -le 0 -and $afterCap -le 0) { break }

        # Reduce whichever side currently has more budget.
        if ($beforeCap -ge $afterCap -and $beforeCap -gt 0) {
            $beforeCap--
            $afterCap = [Math]::Max(0, $spaceBudget - $beforeCap)
        } elseif ($afterCap -gt 0) {
            $afterCap--
            $beforeCap = [Math]::Max(0, $spaceBudget - $afterCap)
        }
    }

    # Fallback: keep only phrase if budget loop couldn't satisfy constraints.
    return [PSCustomObject]@{ Before = ""; After = ""; Truncated = $true }
}

if (-not (Test-Path $csvPath)) {
    Write-Error "Cannot find $csvPath — run from the repo root."
    exit 1
}

Write-Host "Reading $csvPath ..."
$csvLines = Get-Content $csvPath -Encoding UTF8
Write-Host "$($csvLines.Count) source rows."
$trimmedBeforeCount = 0
$truncatedLongCount = 0
$tightVariantCount = 0
$compactVariantCount = 0

$entries = foreach ($line in $csvLines) {
    $parts = $line.Split('|')
    if ($parts.Count -lt 6) { continue }

    $sfwFlag = $parts[-1].Trim()
    if ($sfwOnly -and $sfwFlag -ne 'sfw') { continue }

    $timeStr = $parts[0].Trim()
    $phrase  = $parts[1].Trim()
    $title   = $parts[-3].Trim()
    $author  = $parts[-2].Trim()
    $full    = ($parts[2..($parts.Count - 4)]) -join '|'
    $full    = $full.Trim()

    # Parse HH:MM into minute-of-day (0–1439)
    $tp = $timeStr.Split(':')
    if ($tp.Count -ne 2) { continue }
    $minute = [int]$tp[0] * 60 + [int]$tp[1]

    # Strip HTML and collapse whitespace
    $full = $full -replace '<br\s*/?>', ' '
    $full = $full -replace '<[^>]+>',   ''
    $full = $full -replace '&amp;',     '&'
    $full = $full -replace '\r\n|\r|\n',' '
    $full = $full -replace '\s+',       ' ' -replace '^\s+|\s+$', ''

    # Normalize to fit display font coverage (U+0000..U+00FF) while preserving
    # readable text as much as possible.
    foreach ($var in @('full', 'phrase', 'title', 'author')) {
        Set-Variable $var (Normalize-ForFont ((Get-Variable $var).Value))
    }

    # Length guard (minimum only). Overlength rows are truncated later.
    if ($full.Length -lt 10) { continue }

    # Locate the time phrase in the full quote (case-insensitive, word-boundary)
    $phraseEsc = [regex]::Escape($phrase)
    $m = [regex]::Match($full, "(?<![a-zA-Z0-9])$phraseEsc(?![a-zA-Z0-9])", 'IgnoreCase')
    if (-not $m.Success) { continue }

    $before       = $full.Substring(0, $m.Index)
    $actualPhrase = $full.Substring($m.Index, $m.Length)  # original casing
    $after        = $full.Substring($m.Index + $m.Length)

    $trunc = Truncate-AroundPhrase $before $actualPhrase $after $maxQuoteLen
    if ($trunc.Truncated) { $truncatedLongCount++ }
    $before = $trunc.Before
    $after  = $trunc.After

    $beforeTrimmed = Trim-BeforeForPhraseVisibility $before $actualPhrase $maxBodyChars
    if ($beforeTrimmed -ne $before) { $trimmedBeforeCount++ }
    $before = $beforeTrimmed

    $tightBudget = [Math]::Max(80, [int][Math]::Floor($maxQuoteLen * 0.75))
    $compactBudget = [Math]::Max(64, [int][Math]::Floor($maxQuoteLen * 0.55))

    $tight = Truncate-AroundPhrase $before $actualPhrase $after $tightBudget
    $compact = Truncate-AroundPhrase $before $actualPhrase $after $compactBudget

    $tightBefore = Trim-BeforeForPhraseVisibility $tight.Before $actualPhrase $maxBodyChars
    $tightAfter = $tight.After
    if ($tight.Truncated) { $tightVariantCount++ }

    $compactBodyBudget = [Math]::Max(48, [int][Math]::Floor($maxBodyChars * 0.75))
    $compactBefore = Trim-BeforeForPhraseVisibility $compact.Before $actualPhrase $compactBodyBudget
    $compactAfter = $compact.After
    if ($compact.Truncated) { $compactVariantCount++ }

    $attr         = "$title, $author"

    # Escape for C string literals (backslash first, then double-quote)
    $esc = { param($s) $s.Replace('\', '\\').Replace('"', '\"') }

    # Skip entries containing unescapable control characters (0x00-0x1F except tab)
    $combined = $before + $actualPhrase + $after
    if ($combined -match '[\x00-\x08\x0B\x0C\x0E-\x1F]') { continue }

    [PSCustomObject]@{
        Minute = $minute
        Before = (& $esc $before)
        Phrase = (& $esc $actualPhrase)
        After  = (& $esc $after)
        Attr   = (& $esc $attr)
        BeforeTight = (& $esc $tightBefore)
        AfterTight  = (& $esc $tightAfter)
        BeforeCompact = (& $esc $compactBefore)
        AfterCompact  = (& $esc $compactAfter)
    }
}

# Sort by minute-of-day for the firmware binary search
$sorted = @($entries | Sort-Object Minute)
Write-Host "$($sorted.Count) entries retained after filtering."
Write-Host "$truncatedLongCount overlength entries were retained via truncation."
Write-Host "$trimmedBeforeCount entries had leading text pre-trimmed for phrase visibility."
Write-Host "$tightVariantCount entries generated a tighter fallback variant."
Write-Host "$compactVariantCount entries generated a compact fallback variant."

# ── Build header ──────────────────────────────────────────────────────────────
$sfwLabel = if ($sfwOnly) { 'SFW only' } else { 'SFW + NSFW' }
$h = [System.Collections.Generic.List[string]]::new()
$h.Add("// Auto-generated by generate_litclock_data.ps1 - do not edit by hand.")
$h.Add("// $($sorted.Count) quotes ($sfwLabel), sorted by minute, UTF-8 encoded.")
$h.Add("//")
$h.Add("// Re-generate:  .\generate_litclock_data.ps1")
$h.Add("")
$h.Add("#pragma once")
$h.Add("#include <stdint.h>")
$h.Add("")
$h.Add("struct LitQuote {")
$h.Add("  uint16_t    minute;   ///< hour*60 + min, 0-1439")
$h.Add("  const char* before;   ///< text before the time phrase")
$h.Add("  const char* phrase;   ///< time phrase (rendered in red)")
$h.Add("  const char* after;    ///< text after the time phrase")
$h.Add("  const char* attr;     ///< attribution: Title, Author")
$h.Add("  const char* beforeTight;   ///< precomputed tighter before-text fallback")
$h.Add("  const char* afterTight;    ///< precomputed tighter after-text fallback")
$h.Add("  const char* beforeCompact; ///< precomputed compact before-text fallback")
$h.Add("  const char* afterCompact;  ///< precomputed compact after-text fallback")
$h.Add("};")
$h.Add("")
$h.Add("const LitQuote QUOTES[] = {")
foreach ($q in $sorted) {
    $h.Add("  { $($q.Minute), `"$($q.Before)`", `"$($q.Phrase)`", `"$($q.After)`", `"$($q.Attr)`", `"$($q.BeforeTight)`", `"$($q.AfterTight)`", `"$($q.BeforeCompact)`", `"$($q.AfterCompact)`" },")
}
$h.Add("};")
$h.Add("")
$h.Add("const uint16_t NUM_QUOTES = $($sorted.Count)u;")

# Write UTF-8 without BOM (C++ compilers reject the BOM as invalid syntax)
[System.IO.File]::WriteAllLines(
    (Join-Path (Get-Location) $outPath),
    $h,
    (New-Object System.Text.UTF8Encoding $false)
)
Write-Host "Written to $outPath"
