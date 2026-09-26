# Code signing & notarization

The release workflow (`.github/workflows/release.yml`, which runs the shared
`.github/workflows/build.yml` with `sign: true`) ships **unsigned**
installers by default. When you acquire an Apple Developer ID and/or a
Windows code-signing certificate, drop the secrets below into the repo
and signing + notarization activate automatically on the next tag.

Unsigned installers *work* — users just see an "unknown publisher"
warning (Windows SmartScreen, macOS Gatekeeper) and have to explicitly
allow the app on first launch. Signing removes that warning.

## macOS — Apple Developer Program

**Prereqs** ($99/yr):
- Apple ID enrolled in the [Apple Developer Program](https://developer.apple.com/programs/).
- A **Developer ID Application** certificate (not a regular "Mac App
  Store" cert — distribution outside the App Store uses Developer ID).
  Generate one in Xcode (Settings → Accounts → Manage Certificates) or
  through the developer web portal.
- An [**app-specific password**](https://support.apple.com/en-us/102654)
  for your Apple ID. Used by `notarytool`; never use your real Apple ID
  password in CI.

**Secrets to set** in GitHub repo → Settings → Secrets and variables → Actions:

| Secret                   | What to put in it                                                             |
|--------------------------|-------------------------------------------------------------------------------|
| `MACOS_CERTIFICATE`      | `base64 -i /path/to/DeveloperID.p12` — your exported .p12, base64-encoded.    |
| `MACOS_CERTIFICATE_PWD`  | Passphrase you set when exporting the .p12 from Keychain Access.              |
| `MACOS_CERTIFICATE_NAME` | Exact identity string: `Developer ID Application: Your Name (TEAMID)`.        |
| `MACOS_KEYCHAIN_PWD`     | Any short random string — used for the transient per-job keychain.            |
| `APPLE_ID`               | Email of the Apple ID enrolled in the Developer Program.                      |
| `APPLE_APP_PASSWORD`     | The app-specific password (NOT your login password).                          |
| `APPLE_TEAM_ID`          | 10-character team ID visible in the developer portal & `MACOS_CERTIFICATE_NAME`. |

**How it works**:
1. `Codesign (macOS)` step imports the .p12 into a fresh per-job
   keychain and runs `codesign --deep --options runtime --timestamp
   --sign "<NAME>"` on the `.app`. Falls back to no-op if the
   `MACOS_CERTIFICATE` secret isn't set (the bundle then keeps the
   ad-hoc signature applied after `cmake --install`).
2. `Notarize (macOS)` submits a zipped copy of the bundle to Apple's
   notary via `xcrun notarytool submit ... --wait`, then staples the
   ticket so it launches without an internet connection. Falls back
   to no-op if `APPLE_ID` isn't set.
3. The signed, notarized `.app` is then packaged into the `.zip` and
   `.dmg` that the release step uploads.

## Windows — code-signing cert

**Prereqs** (varies, typically $200–500/yr):
- An **OV** (Organization Validation) or **EV** (Extended Validation)
  code-signing certificate from a CA like DigiCert, Sectigo, GlobalSign,
  SSL.com, or Certum. EV certs bypass SmartScreen's reputation
  warm-up period but are more expensive and often shipped on a
  physical hardware token — which is a *huge* problem for CI
  (can't automate a USB token push). For CI-friendly signing you
  want an OV cert delivered as a `.pfx` file, or an EV cert via a
  cloud HSM service (Azure Key Vault, SSL.com CodeSign Now, etc).

**Secrets to set**:

| Secret                | What to put in it                                                 |
|-----------------------|-------------------------------------------------------------------|
| `WINDOWS_PFX_BASE64`  | `base64 -i yourcert.pfx` — your exported .pfx, base64-encoded.     |
| `WINDOWS_PFX_PWD`     | Passphrase for the .pfx.                                          |

**How it works**:
1. `Codesign exe (Windows)` signs every `.exe` / `.dll` in the staged
   tree BEFORE the MSI gets built — so the MSI's embedded binaries
   are trusted.
2. `Codesign MSI (Windows)` signs the output `.msi` after WiX produces
   it.
3. Both steps skip when `WINDOWS_PFX_BASE64` isn't set.

Secret presence is resolved into job-level `HAS_*` flags in `build.yml`:
a step's `if:` cannot read that same step's `env:`, so checking the
secret there would always skip signing. PR and nightly builds never
sign (they don't inherit secrets).

The workflow uses `signtool.exe` from the Windows SDK (pre-installed
on `windows-2022`). Timestamp URL is Sectigo's public one; any
RFC-3161 timestamp authority works if you prefer.

## Verifying

After a signed release:

- **macOS**: `codesign -dvv brick-layout-designer.app` and
  `spctl --assess --verbose=4 brick-layout-designer.app` —
  should report your team ID and `accepted`.
- **Windows**: right-click the `.msi` → Properties → Digital
  Signatures → should list your certificate with a valid timestamp.

## Why not both?

Short answer: cost. An Apple Developer membership + a decent Windows
code-signing cert runs $300–600/year in recurring fees. For a
volunteer / hobby project that's real money; this repo ships unsigned
and documents the "add a secret → signing activates" path so a future
maintainer (or sponsor) can turn it on without touching the workflow
YAML.
