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

[release.yml](../.github/workflows/release.yml) runs on every push to `main`, and can also be dispatched manually on `main`. It checks public source, builds on a Windows runner, runs native and driver-core tests, packages the app, and checks binaries for personal Windows paths and account SIDs.

Only a successful run uploads a draft release and then publishes it as the latest release. Each run has a distinct tag, `v<app-version>-build.<run-number>.<attempt>`, so the source commit behind a download stays identifiable. Stable asset names keep the README's latest-download links working. A newer push cancels an older in-progress run.

GitHub's repository-scoped `GITHUB_TOKEN` supplies release access through `contents: write`. No personal token or signing key is committed. Actions are pinned to commit SHAs. Compilers and pinned dependency sources are cached; release payloads and user data are not.

The distributed app is unsigned. `SHA256SUMS.txt` verifies downloads against the hashes published with that release; it is not a code-signing certificate.

## Privacy checks

```powershell
node scripts/verify-public-source.mjs --staged
node scripts/verify-public-source.mjs --all
```

The source check rejects generated/private files and common secret or account-identifier patterns without printing matched values. Explicitly allowed images must be inspected before publication. The release check additionally scans executable metadata; these checks complement review rather than guarantee that every possible form of sensitive data can be recognized.
