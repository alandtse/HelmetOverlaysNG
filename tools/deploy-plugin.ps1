<#
.SYNOPSIS
    Deploy the plugin to a Skyrim install and write a complete attestation record.

.DESCRIPTION
    The records this produces used to be written by hand, which is why fields drifted:
    a field present in one deploy was simply forgotten in the next, and nothing noticed
    because the only check asserted on the size of a hardcoded list of field names.

    So this script GENERATES the record from a template that covers the whole schema,
    then refuses to finish unless tools/check_attestation_schema.py accepts it. A record
    that is missing a field cannot be produced by this script at all; the checker is the
    enforcement and tests/test_attestation_schema.cpp is the detection.

    Fields that cannot be known at deploy time (the shipped-shader check result, mutation
    proofs, what the record does not prove) are emitted as explicit nulls with a
    `...Provenance` string saying they must be filled in, and the checker rejects a null
    without one. That way an unfilled field is red, not silently absent.

.PARAMETER Game
    VR or SE. Decides the install directory.

.PARAMETER Holder
    Lock holder name, released in `finally`.

.PARAMETER DLL
    Built plugin DLL to deploy.

.PARAMETER PDB
    Matching PDB.

.PARAMETER Commit
    Commit the build came from.

.PARAMETER Change
    One-line description of what changed.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools/deploy-plugin.ps1 `
        -Game SE -Holder hong-deploy -DLL build/windows/x64/releasedbg/HelmetOverlaysNG.dll `
        -PDB build/windows/x64/releasedbg/HelmetOverlaysNG.pdb `
        -Commit (git rev-parse HEAD) -Change 'Describe the change'
#>
param(
    [Parameter(Mandatory = $true)][ValidateSet('VR', 'SE')][string]$Game,
    [Parameter(Mandatory = $true)][string]$Holder,
    [Parameter(Mandatory = $true)][string]$DLL,
    [Parameter(Mandatory = $true)][string]$PDB,
    [Parameter(Mandatory = $true)][string]$Commit,
    [Parameter(Mandatory = $true)][string]$Change,
    # Epoch seconds when the build that produced $DLL started. Required for --rebootstrap: the
    # other evidence available is mtimes, and a source stamped OLDER than the artefact passes
    # them, so a claim that a build happened needs a start time that cannot be backdated.
    [long]$BuildStartedAt = 0
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$tool = 'C:\Users\Alan\.claude\tools\skyrim-game.ps1'
$checker = Join-Path $repo 'tools\check_attestation_schema.py'

$destinations = @{
    SE = 'E:\SteamLibrary\steamapps\common\Skyrim Special Edition\Data\SKSE\Plugins'
    VR = 'E:\SteamLibrary\steamapps\common\SkyrimVR\Data\SKSE\Plugins'
}
$dest = $destinations[$Game]

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = "E:\tmp\hong-$($Game.ToLower())-deploy-$stamp"

# Freshness first, then bytes -- and a REFUSAL, not a field to fill in later. This script
# used to attest `dllByteIdenticalToTreeBuild` while admitting in its own provenance string
# that "a stale tree build would be attested as fresh": the comparison it performed was
# against whatever was sitting in the build directory, which is the artefact a FAILED build
# leaves behind. tools/verify_build.py gates on whether this build produced that file at all,
# and the record can no longer be produced when it did not.
$verify = Join-Path $repo 'tools\verify_build.py'
# --since is passed whenever the caller supplies a build start time: without it the digest check
# alone cannot tell a legitimate rebuild from a backdated edit in the rebootstrap path, so the
# evidence is only as good as the start time this script is given.
$sinceArgs = @()
if ($BuildStartedAt -gt 0) { $sinceArgs = @('--since', "$BuildStartedAt") }
& python $verify --dll $DLL --require-record @sinceArgs
if ($LASTEXITCODE -ne 0) {
    throw "Refusing to deploy: tools/verify_build.py exit $LASTEXITCODE. A deployed binary must be one this build produced; a content check that passes against the previous build is the failure mode this gate exists to stop."
}

# Record the input digest this build consumed, so the NEXT run can tell a no-op touch from an
# edit without relying on mtimes. Recording here -- after a build this script was just handed
# the output of -- is the honest place for the claim: tools/check_artifact_fresh.py cannot
# verify that an artefact came from its inputs, only that the inputs changed since the last
# time someone said so.
& python $verify --dll $DLL --record | Out-Null
if ($LASTEXITCODE -ne 0) { throw "tools/check_artifact_fresh.py --record failed with exit $LASTEXITCODE" }
$dllHash = (Get-FileHash $DLL -Algorithm SHA256).Hash
$pdbHash = (Get-FileHash $PDB -Algorithm SHA256).Hash

Write-Host "$Game deploy: $dllHash"

& $tool acquire -Game $Game -Holder $Holder -Mode exclusive `
    -Purpose "Deploy HelmetOverlaysNG $Change; no game launch" `
    -Contact 'F:/repos/HelmetOverlaysNG' -EstMinutes 2 -Minutes 5 | Out-Null

$recordPath = $null
try {
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    foreach ($file in 'HelmetOverlaysNG.dll', 'HelmetOverlaysNG.pdb', 'HelmetOverlaysNG.ini') {
        $source = Join-Path $dest $file
        if (Test-Path $source) { Copy-Item $source -Destination $backup -Force }
    }

    & $tool set-built -Holder $Holder -Plugin HelmetOverlaysNG -Game $Game `
        -Hash $dllHash -Files (Join-Path $dest 'HelmetOverlaysNG.dll') | Out-Null
    & $tool want -Holder $Holder -Plugin HelmetOverlaysNG -Game $Game -Hash $dllHash | Out-Null

    Copy-Item $DLL -Destination (Join-Path $dest 'HelmetOverlaysNG.dll') -Force
    Copy-Item $PDB -Destination (Join-Path $dest 'HelmetOverlaysNG.pdb') -Force

    $gotDLL = (Get-FileHash (Join-Path $dest 'HelmetOverlaysNG.dll') -Algorithm SHA256).Hash
    $gotPDB = (Get-FileHash (Join-Path $dest 'HelmetOverlaysNG.pdb') -Algorithm SHA256).Hash
    if ($gotDLL -ne $dllHash) { throw "DLL hash mismatch after copy: $gotDLL" }
    if ($gotPDB -ne $pdbHash) { throw "PDB hash mismatch after copy: $gotPDB" }

    $ini = Join-Path $dest 'HelmetOverlaysNG.ini'
    $iniHash = if (Test-Path $ini) { (Get-FileHash $ini -Algorithm SHA256).Hash } else { 'absent' }

    # Preserve the DEPLOYED bytes. Every older deploy survives an install overwrite because
    # a later record's backup keeps a copy; the newest has no later record, so without this
    # its artefact exists only in the live install and one mod-manager overwrite loses it.
    $deployedDir = Join-Path $backup 'deployed'
    New-Item -ItemType Directory -Force -Path $deployedDir | Out-Null
    Copy-Item (Join-Path $dest 'HelmetOverlaysNG.dll') -Destination $deployedDir -Force
    Copy-Item (Join-Path $dest 'HelmetOverlaysNG.pdb') -Destination $deployedDir -Force
    $keptDLL = (Get-FileHash (Join-Path $deployedDir 'HelmetOverlaysNG.dll') -Algorithm SHA256).Hash
    $keptPDB = (Get-FileHash (Join-Path $deployedDir 'HelmetOverlaysNG.pdb') -Algorithm SHA256).Hash
    if ($keptDLL -ne $gotDLL) { throw "preserved DLL does not match deployed bytes: $keptDLL" }
    if ($keptPDB -ne $gotPDB) { throw "preserved PDB does not match deployed bytes: $keptPDB" }

    & $tool set-deployed -Holder $Holder -Plugin HelmetOverlaysNG -Game $Game `
        -Hash $gotDLL -Files (Join-Path $dest 'HelmetOverlaysNG.dll') | Out-Null

    $backupHashes = [ordered]@{}
    foreach ($file in 'HelmetOverlaysNG.dll', 'HelmetOverlaysNG.pdb', 'HelmetOverlaysNG.ini') {
        $path = Join-Path $backup $file
        if (Test-Path $path) {
            $backupHashes[$file] = (Get-FileHash $path -Algorithm SHA256).Hash
        }
    }

    # The values that cannot be observed by copying files are computed from the artefacts,
    # not left null: a null with "not computed" is the shape of a field that exists to be
    # filled in, and null fields are exactly what drifted before.
    $normaliser = Join-Path $repo 'tools\normalise_build.py'
    $symbolArgs = @()
    foreach ($symbol in 'MaskPermit', 'PermitMask', 'OpacitySlider', 'ArtFitFor', 'MaskShaders') {
        $symbolArgs += @('--symbol', $symbol)
    }
    $normalised = (& python $normaliser --dll $DLL --pdb $PDB @symbolArgs --json | ConvertFrom-Json)
    if (-not $normalised.dllNormalisedSHA256) { throw 'tools/normalise_build.py produced no normalised hash' }
    $symbolCounts = $normalised.symbolsExpectPresent

    # Every record under E:/tmp, whatever the directory happens to be called: the naming has
    # changed between deploys (hong-alpha255-se-DATE, hong-reformat-se-DATE, hong-se-deploy-DATE)
    # and globbing one pattern found nothing while this field's provenance string still claimed a
    # comparison had been made. Ordering is by the record's own deployedAt, never by path.
    $previousRecord = Get-ChildItem 'E:\tmp' -Directory -Filter 'hong-*' |
        Where-Object { $_.FullName -ne $backup -and (Test-Path (Join-Path $_.FullName 'attestation.json')) } |
        Sort-Object { (Get-Content (Join-Path $_.FullName 'attestation.json') -Raw | ConvertFrom-Json).deployedAt } |
        Select-Object -Last 1
    $normalisedDiffers = $null
    if ($previousRecord) {
        # Re-normalise the previous DEPLOYED BYTES with this same tool rather than trusting the
        # number stored in its record. The records in this history were normalised by two
        # different functions (an earlier one used hardcoded offsets and did not zero the RSDS
        # GUID), so a fresh hash compared against a stored one compares the two tools, not the
        # two builds. Only bytes are comparable across a change of method.
        $previousDLL = Join-Path $previousRecord.FullName 'deployed\HelmetOverlaysNG.dll'
        if (-not (Test-Path $previousDLL)) { $previousDLL = Join-Path $previousRecord.FullName 'HelmetOverlaysNG.dll' }
        if (Test-Path $previousDLL) {
            $previousNormalised = (& python $normaliser --dll $previousDLL --json | ConvertFrom-Json).dllNormalisedSHA256
            if ($previousNormalised) { $normalisedDiffers = ($previousNormalised -ne $normalised.dllNormalisedSHA256) }
        }
    }

    $branch = (git -C $repo rev-parse --abbrev-ref HEAD).Trim()
    $subject = (git -C $repo log -1 --format=%s $Commit).Trim()

    # Every key the schema asks for. Values that cannot be known here are null WITH a
    # provenance string saying so; the checker rejects a null that has no reason.
    $record = [ordered]@{
        schemaVersion                      = 3
        deployedAt                         = (Get-Date -Format 'o')
        backup                             = $backup
        gameLaunched                       = $false
        commit                             = $Commit
        commitSubject                      = $subject
        branch                             = $branch
        change                             = $Change
        dllSHA256                          = $gotDLL
        pdbSHA256                          = $gotPDB
        treeBuildDLLSHA256                 = $dllHash
        treeBuildPDBSHA256                 = $pdbHash
        treeBuildFieldsProvenance          = 'Observed at deploy time: the build output this script copied, before any copy left the machine. Byte identity is the primary evidence; RSDS GUID/age is secondary.'
        dllByteIdenticalToTreeBuild        = $true
        pdbByteIdenticalToTreeBuild        = $true
        dllNormalisedSHA256                = $normalised.dllNormalisedSHA256
        normalisedSource                   = 'Computed by tools/normalise_build.py from the tree build, with the COFF TimeDateStamp, the PE CheckSum, and every debug entry''s TimeDateStamp plus the CV_INFO_PDB70 GUID and age zeroed first. Validated both ways: a rebuild of unchanged source reproduces the value, and a real source edit changes it.'
        normalisedDiffersFromPreviousDeploy = $normalisedDiffers
        normalisedDiffersFromPreviousDeployProvenance = 'Computed by re-normalising the previous deploy''s preserved DLL bytes with the same tool. The stored dllNormalisedSHA256 in older records were produced by an earlier normaliser (hardcoded offsets, RSDS GUID not zeroed), so a stored value is not comparable: only bytes are. Null when there is no earlier record or its bytes were not preserved.'
        freshness                          = 'Set after the freshness gate runs, below. Not a placeholder: the gate refuses to produce this record at all if the artefact is older than its sources.'
        pdbLink                            = $normalised.pdbLink
        pdbLinkProvenance                  = 'Computed from the DLL''s CV_INFO_PDB70 entry. SECONDARY sanity check only, and it rules out an UNRELATED PDB rather than a stale one. The GUID and age are properties of the PDB''s lifecycle, not of the source: with the PDB present the linker appends (GUID fixed, age increments), with it deleted the linker recreates (fresh GUID, age 1). Measured on this machine, both lifecycles. A value that moves with the lifecycle cannot identify a build, which is why this field is secondary and why dllNormalisedSHA256 zeroes the GUID. Byte identity to a provably-fresh tree build is the primary evidence.'
        symbolsExpectPresent               = $symbolCounts
        symbolsContainer                   = 'PDB. C++ symbol names live in the .pdb, NOT the .dll - probing the DLL returns 0 for all of them and that 0 is not evidence the record is false.'
        mutationProof                      = $null
        mutationProofProvenance            = 'NOT RECORDED by the script. Fill in with the mutation that was tried and which test caught it, or leave null and say the tests were not mutation-checked.'
        whatThisDoesNotProve               = 'Byte identity to the tree build was observed by this script, so a stale tree build would be attested as fresh. Cross-machine reproducibility was not checked. Symbols counts, if present, must match EXACTLY.'
        whyThisExists                      = 'A hash records the bytes shipped, not that they came from the intended source.'
        backupFileHashes                   = $backupHashes
        backupDiffersFromDeployedAsExpected = -not ($backupHashes['HelmetOverlaysNG.dll'] -eq $gotDLL)
        backupHolds                        = 'PRE-deployment files. The backup DLL is deliberately NOT equal to dllSHA256; a mismatch there means the backup worked, not that the install was tampered with.'
        deployedCopiesPreserved            = [ordered]@{
            directory     = 'deployed/'
            dllSHA256     = $keptDLL
            pdbSHA256     = $keptPDB
            matchesRecord = ($keptDLL -eq $gotDLL -and $keptPDB -eq $gotPDB)
        }
        whyDeployedCopyExists              = 'Every earlier deploy survives an install overwrite because a later backup/ holds it. The newest has no such record, so it preserves its own bytes here.'
        postHocAnnotations                 = @()
    }

    # Nulls that need an explicit reason, written rather than left to be guessed.
    # `freshness` used to be prose an agent filled in afterwards, which is why the field could
    # read "verified" for a build nothing had verified. The gate above now refuses to produce a
    # record at all when the artefact is older than its sources, so this states what was RUN.
    $record.freshness = 'GATED by tools/verify_build.py before this record was written. The gate compares a CONTENT DIGEST of the inputs that feed the plugin (src/**.cpp, src/**.h, xmake.lua) against the digest recorded after the previous build, and falls back to mtimes when no digest exists. LIMIT: a digest recorded without a real build is indistinguishable from one recorded after it, so the digest is an explicit claim made at deploy time, not a proof. A header edited with its own mtime restored IS caught by the digest and was not caught by the mtime check that preceded it. Byte identity of the deployed DLL against the file hashed above remains the primary evidence.'

    $recordPath = Join-Path $backup 'attestation.json'
    $record | ConvertTo-Json -Depth 6 | Set-Content $recordPath -Encoding utf8

    # The enforcement half. A record the checker rejects must not survive, or the next
    # reader cannot tell a complete record from one that was written and ignored.
    $previous = Get-ChildItem 'E:\tmp' -Directory -Filter 'hong-*' |
        Where-Object { $_.FullName -ne $backup -and (Test-Path (Join-Path $_.FullName 'attestation.json')) } |
        Sort-Object { (Get-Content (Join-Path $_.FullName 'attestation.json') -Raw | ConvertFrom-Json).deployedAt } |
        Select-Object -Last 1

    $checkArgs = @($checker, $recordPath)
    if ($previous) { $checkArgs = @($checker, $recordPath, (Join-Path $previous.FullName 'attestation.json')) }
    & python @checkArgs
    if ($LASTEXITCODE -ne 0) {
        throw "attestation record rejected by tools/check_attestation_schema.py (see output above). The $Game install HAS been updated; the record at $recordPath is incomplete and must be fixed before this deploy is considered attested."
    }

    Write-Host "deployed DLL $gotDLL"
    Write-Host "deployed PDB $gotPDB"
    Write-Host "ini preserved $iniHash"
    Write-Host "attestation $recordPath"
}
finally {
    & $tool release -Game $Game -Holder $Holder | Out-Null
    Write-Host 'lock released'
}
