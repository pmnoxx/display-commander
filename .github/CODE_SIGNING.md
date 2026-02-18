git ad# Optional code signing for GitHub builds

Build workflows can sign the addon binaries with a **self-signed certificate** when the following repository secrets are set. If they are not set, the build runs as before and artifacts are unsigned.

## Repository secrets

| Secret | Description |
|--------|-------------|
| `SIGNING_CERT_PFX_B64` | Base64-encoded PFX (PKCS#12) file containing the code-signing certificate and private key |
| `SIGNING_CERT_PASSWORD` | Password for the PFX file |

**Where to set**: Repository → Settings → Secrets and variables → Actions → New repository secret.

## Create a self-signed code-signing certificate (one-time)

On Windows with PowerShell (run as Administrator if you install to the store):

```powershell
# Create a self-signed certificate valid for 5 years, for code signing
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=Display Commander" -CertStoreLocation "Cert:\CurrentUser\My" -NotAfter (Get-Date).AddYears(5)

# Export to PFX with a password (replace YourPassword with a strong password)
$pwd = ConvertTo-SecureString -String "YourPassword" -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath ".\DisplayCommanderSigning.pfx" -Password $pwd
```

Then base64-encode the PFX for the secret (PowerShell):

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes(".\DisplayCommanderSigning.pfx"))
```

Copy the output and paste it as the value of `SIGNING_CERT_PFX_B64`. Set `SIGNING_CERT_PASSWORD` to the same password you used when exporting.

## Behaviour

- **Build** and **Nightly** workflows run the step “Sign binaries (self-signed)” only when `SIGNING_CERT_PFX_B64` is non-empty.
- Signing uses the Windows SDK `signtool.exe`, a SHA256 signature, and the DigiCert timestamp server so the signature remains valid after the certificate expires.
- Self-signed signatures do not establish trust with Windows or SmartScreen; they only prove the binary was signed by your key. Users may still see warnings unless they install/trust your root cert.

## Security

- Keep the PFX and password private; do not commit them. Use only GitHub Actions secrets (or equivalent) in CI.
- Restrict who can read or edit Actions secrets to avoid misuse of the signing key.
