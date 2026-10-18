# Code signing

An unsigned `SomniumVolandi.exe` triggers Windows SmartScreen ("Windows protected your PC") and makes some antivirus
products suspicious. The release workflow (`.github/workflows/build.yml`, the `windows` job) signs the executable with
**Azure Artifact Signing** (formerly Trusted Signing) when the signing secrets are set. Without them a release still
builds, unsigned, with a warning in the run.

Only releases are signed: a tag push (`v*`) or a manual run with a `release_tag`. Branch builds stay unsigned.

## Why Artifact Signing

Since June 2023 a new OV or EV code-signing certificate must live on a hardware security module, so it can't be
exported as a `.pfx` file and stored as a CI secret. Artifact Signing keeps the key in Microsoft's HSMs, signs over
an API, and costs a small monthly fee. Its certificates chain to a root Windows trusts, and signatures build
SmartScreen reputation like an OV certificate's.

Alternatives that also work from CI: a certificate on a cloud HSM such as DigiCert KeyLocker or SSL.com eSigner. Each
has its own signing tool, which would replace the two Azure steps.

## One-time setup

1. **Azure.** In an Azure subscription, create an *Artifact Signing* account and note its endpoint, for example
   `https://eus.codesigning.azure.net/`.
2. **Identity validation.** In the account, complete identity validation for the publisher name that will appear on
   the signature (CDAIII). Check Microsoft's current eligibility rules for individuals and organisations first.
3. **Certificate profile.** Create a *Public Trust* certificate profile from the validated identity.
4. **An identity for GitHub.** Create an app registration (or a user-assigned managed identity) and give it a
   federated credential for this repository, so GitHub Actions can sign in with OpenID Connect and no stored
   password:
   - issuer `https://token.actions.githubusercontent.com`
   - subject `repo:<owner>/<repo>:ref:refs/tags/*` for tag releases. Add a second credential for manual runs from a
     branch, such as `repo:<owner>/<repo>:ref:refs/heads/<branch>`.
5. **Role.** On the Artifact Signing account, assign that identity the *Artifact Signing Certificate Profile Signer*
   role (named *Trusted Signing Certificate Profile Signer* on older accounts).
6. **Repository secrets** (Settings → Secrets and variables → Actions):

   | Secret | Value |
   |---|---|
   | `AZURE_TENANT_ID` | the directory (tenant) ID |
   | `AZURE_CLIENT_ID` | the app registration's or managed identity's client ID |
   | `AZURE_SUBSCRIPTION_ID` | the subscription holding the signing account |
   | `ARTIFACT_SIGNING_ENDPOINT` | the account's endpoint URL |
   | `ARTIFACT_SIGNING_ACCOUNT` | the account name (its presence turns signing on) |
   | `ARTIFACT_SIGNING_PROFILE` | the certificate profile name |

## What the workflow does

1. Copies `SomniumVolandi.exe` on its own into `build/sign`, so the signing step doesn't sign every test program
   beside it.
2. Signs in to Azure (`azure/login`, OpenID Connect).
3. Signs the exe with SHA-256 and a Microsoft RFC 3161 timestamp (`azure/artifact-signing-action`). The timestamp
   keeps the signature valid after the short-lived signing certificate expires.
4. Checks the signature with `Get-AuthenticodeSignature`. It must be valid and timestamped, or the release fails. The
   result is written to the job summary.
5. Packages the signed exe into the zip.

## Checking a release by hand

In PowerShell, next to the extracted exe:

```
Get-AuthenticodeSignature .\SomniumVolandi.exe | Format-List Status, SignerCertificate, TimeStamperCertificate
```

`Status` should be `Valid`, and the signer should be CDAIII. SmartScreen can still warn for the first downloads of a
new publisher until it has built reputation.
