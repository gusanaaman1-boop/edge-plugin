# Signing and notarisation

**Status: not done, and it cannot be done from here.** This is the one release
phase that is blocked on something no amount of engineering replaces — a paid
Apple Developer account (US $99/year) and, on Windows, a code-signing
certificate. Nothing was bought, and nothing will be without an explicit
decision.

This file records exactly what the gap costs, what fixes it, and the commands
that will work the day the certificates exist.

---

## What is signed today

| | |
|---|---|
| macOS VST3 / AU / Standalone | **ad-hoc signed** (`-` identity, applied automatically by JUCE's build) |
| `EDGE-<ver>.pkg` | **unsigned** |
| Windows VST3 / Standalone | **unsigned** |

Ad-hoc signing is enough for the binary to run on the machine that built it. It
is **not** enough for anyone else's machine.

---

## What the user actually experiences

**macOS, the standalone app.** Gatekeeper blocks it on first launch:
*"EDGE.app cannot be opened because the developer cannot be verified."* The
workaround — right-click → Open, or System Settings → Privacy & Security →
"Open Anyway" — works, and it is documented in the manual. It also looks exactly
like what malware looks like, which is the real cost.

**macOS, the plug-ins.** VST3 and AU loaded by a host are generally not
Gatekeeper-blocked, because the host is what launched. They are, however,
subject to the quarantine flag if the zip was downloaded from a browser, which
some hosts surface as a failed scan.

**macOS, the .pkg.** An unsigned installer package is refused outright by
default: *"cannot be opened because it is from an unidentified developer."*
This is the worst of the three, because the installer is the first thing a
paying customer touches.

**Windows.** SmartScreen shows *"Windows protected your PC"* on the installer,
with "Run anyway" hidden behind "More info". Unsigned installers also accumulate
SmartScreen reputation slowly even after a certificate is bought, so the warning
does not disappear on day one.

---

## What fixes it

### macOS — Apple Developer Program, $99/year

Two certificates come with it:

* **Developer ID Application** — for the .vst3, .component and .app
* **Developer ID Installer** — for the .pkg

Then notarisation: uploading the signed artefacts to Apple, waiting for a
verdict, and stapling the ticket so the check works offline.

### Windows — an OV or EV code-signing certificate

Roughly $200–$600/year depending on the issuer. EV certificates carry
SmartScreen reputation immediately; OV ones build it over time.

---

## The commands, for the day the certificates exist

Nothing below has been run. Each one is written to be checked rather than
trusted — every step verifies its own result.

### 1. Sign the binaries

```bash
IDENTITY="Developer ID Application: <NAME> (<TEAMID>)"
ART="build-universal/Edge_artefacts/Release"

# Inside out: a bundle's nested code must be signed before the bundle itself.
for b in "$ART/VST3/EDGE.vst3" "$ART/AU/EDGE.component" "$ART/Standalone/EDGE.app"; do
    codesign --force --deep --options runtime --timestamp \
             --sign "$IDENTITY" "$b"
    codesign --verify --deep --strict --verbose=2 "$b"
done
```

`--options runtime` (the hardened runtime) is **required** for notarisation.
Without it the upload is accepted and then rejected, minutes later, by e-mail.

### 2. Sign the installer

```bash
productsign --sign "Developer ID Installer: <NAME> (<TEAMID>)" \
            dist/EDGE-v0.7.pkg dist/EDGE-v0.7-signed.pkg
pkgutil --check-signature dist/EDGE-v0.7-signed.pkg
```

### 3. Notarise and staple

```bash
xcrun notarytool submit dist/EDGE-v0.7-signed.pkg \
      --apple-id "<APPLE ID>" --team-id "<TEAMID>" \
      --password "<APP-SPECIFIC PASSWORD>" --wait

xcrun stapler staple dist/EDGE-v0.7-signed.pkg
xcrun stapler validate dist/EDGE-v0.7-signed.pkg
```

Use an **app-specific password**, never the account password.

### 4. The acceptance test

```bash
spctl -a -vvv -t install dist/EDGE-v0.7-signed.pkg     # expect: accepted
spctl -a -vvv "$ART/Standalone/EDGE.app"               # expect: accepted
```

`spctl` must print `accepted` and a `source=Notarized Developer ID` line for
each. **Zero** Gatekeeper prompts on a machine that has never seen the build —
which means testing on a second machine, or in a fresh VM, not on the one that
signed it.

### 5. Windows

```bat
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 ^
         /a build\Edge_artefacts\Release\VST3\EDGE.vst3\Contents\x86_64-win\EDGE.vst3
signtool verify /pa /v <path>
```

Sign the plug-in binary **and** the installer, and always timestamp — an
untimestamped signature stops validating the day the certificate expires, which
retroactively breaks every copy already sold.

---

## Until then

The manual tells the user plainly what they will see and what to click. That is
the honest interim: a warning the user was warned about is an annoyance, and a
warning nobody mentioned is a support ticket.
