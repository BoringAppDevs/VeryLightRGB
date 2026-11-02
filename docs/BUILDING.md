# Building and releases

Most users should download the installer from [GitHub Releases](https://github.com/BoringAppDevs/VeryLightRGB/releases/latest).

## Build locally

Use Windows x64, PowerShell 7, Node.js 22, and Git:

```powershell
git clone https://github.com/BoringAppDevs/VeryLightRGB.git
cd VeryLightRGB
./scripts/build.ps1
./scripts/build-installer.ps1 -SkipBuild
./scripts/package-native.ps1
./scripts/prepare-release.ps1
```

The first build downloads pinned LLVM-MinGW and native-driver source archives, verifies their SHA-256 hashes, compiles the driver core and app, and runs tests without accessing RGB hardware. The installer build obtains a pinned Inno Setup compiler. Later builds reuse local caches. Building does not install or launch the lighting app.

`artifacts/release/` contains stable download filenames for the installer, portable ZIP, source ZIP, and checksums. Release packages include the corresponding source, dependency archives, and license notices. Local configuration, diagnostics, compiler caches, and generated builds are excluded from Git.

## Automatic releases

[release.yml](../.github/workflows/release.yml) runs when a stable version tag such as `v1.2.1` is pushed, or when manually dispatched on `main`. Ordinary branch pushes, README edits, and benchmark-data updates do not start a release build.

Before allocating a Windows build runner, a lightweight job checks that the app header, Windows resources, installer, and version tag agree. It queries all release pages and skips any version that already has a canonical or legacy build-number release, including drafts. API failures stop the run. Re-running a workflow does not rebuild or overwrite an existing release.

A new version then builds and tests on Windows, packages the app, and checks binary metadata for personal Windows paths and account SIDs. Only a successful run uploads a draft release and publishes it as the latest release under the single tag `v<app-version>`. Stable asset names keep the README's latest-download links working. Release runs are serialized, and existing releases are never overwritten.

To publish the next version, update `native/include/core.hpp`, `native/assets/app.rc`, and `installer/VeryLightRGB.iss` together, commit the changes, and push the matching version tag. A manual run on `main` publishes only if that app version has no existing release. Resolve an incomplete draft deliberately before retrying its version.

GitHub's repository-scoped `GITHUB_TOKEN` supplies read access for planning and `contents: write` only for the release job. No personal token or signing key is committed. Actions are pinned to commit SHAs. Compilers and pinned dependency sources are cached; release payloads and user data are not.

The distributed app is unsigned. `SHA256SUMS.txt` verifies downloads against the hashes published with that release; it is not a code-signing certificate.

## Privacy checks

```powershell
node scripts/verify-public-source.mjs --staged
node scripts/verify-public-source.mjs --all
```

The source check rejects generated/private files and common secret or account-identifier patterns without printing matched values. Explicitly allowed images must be inspected before publication. The release check additionally scans executable metadata; these checks complement review rather than guarantee that every possible form of sensitive data can be recognized.
