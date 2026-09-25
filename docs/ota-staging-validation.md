# Staged OTA validation

The OTA installer now follows the staged approach from
[CrossInk v1.5.1](https://github.com/uxjulia/CrossInk/blob/v1.5.1/src/network/OtaUpdater.cpp).
The first experiment retained `esp_http_client`. The next test build uses CrossInk's bounded wolfSSL
streaming transport for firmware assets carrying a trusted manifest SHA-256.

## Expected flow

1. Fetch the release manifest and select the current Tiny/Xlarge variant.
2. Download to `/.crosspoint/ota-update.bin`. A firmware asset with a valid manifest SHA-256 uses wolfSSL;
   assets without that digest retain the verified `esp_http_client` path.
3. Close the HTTP client and SD writer. Check the staged size and the manifest SHA-256 when supplied.
4. Close the SD reader before reopening through `firmware_flash::flashFromSdPath()`.
5. The SD installer checks the ESP image checksum, SHA trailer, and partition size before flash erasure.
6. Flash the inactive partition, switch the boot target, remove the staged file, then let the activity restart.

Download occupies 0-50% of progress. Installation occupies 50-100%. Hashing may hold progress around 50%.
There is no whole-image allocation. wolfSSL streams through one bounded 1 KiB stack buffer into the SD writer.
The extra hash buffer is a fallible 1 KiB allocation, released before the SD installer allocates its existing
4 KiB buffer. No runtime memory savings have been measured on hardware.

A writable SD card with room for the firmware is now required. This HAL does not expose free capacity;
short writes fail the download before any flash erasure. Partial or rejected downloads are removed.
Cancellation is supported before installation; once flash installation starts, it runs to completion.

## Host regression tests

```powershell
cmake -S test -B .pio/unit_tests
cmake --build .pio/unit_tests --config Release --target OtaStagingTest
ctest --test-dir .pio/unit_tests -C Release -R '^OtaStagingTest\.' --output-on-failure
```

These tests compile the production `OtaUpdater.cpp` without `SIMULATOR`. HTTP, SD, SHA-256, and flash
operations are test doubles. They check sequencing, failure propagation, file closure, cleanup, and progress.
They do not exercise real TLS, the SHA-256 implementation, or ESP flash validation. The GUI simulator still
disables OTA and is not evidence of real update reliability.

## Hardware experiment

Use the reference X3 first, then an X4/Xlarge if available. Capture USB serial at 115200 baud.
The experimental build includes other pre-existing local YACP changes and is not a clean public release.

1. Install the experimental firmware by the working SD update path.
2. In Settings, open the OTA update action and connect to Wi-Fi. Use a genuinely newer release with a matching
   variant. The production version comparison ignores the `.build` suffix after `-yacp`: a `1.7.2-yacp.1`
   build will not offer `1.7.2-yacp` or `1.7.1-yacp` as an update.
3. For an earlier controlled test, build a dedicated test image with `CROSSINK_OTA_RELEASE_URL` pointing to
   a local manifest. Use a higher numeric test tag, an explicitly selected known-good image of the same variant,
   its exact byte size, and its full SHA-256. Plain HTTP is accepted only with a valid manifest digest.
   This override must stay out of public builds. Increment the embedded version before this compilation too.
4. Confirm the logs show `Downloading firmware to SD`, then `Staged firmware sha256 verified`, then
   `Download closed; validating and flashing SD image`. No flash erase should occur during download.
5. Confirm a successful reboot into the intended embedded firmware version and normal book opening.
6. With the controlled endpoint, interrupt the network mid-download and test a manifest with a wrong digest.
   Both must fail before flash installation, remove the temporary file, and leave the current firmware usable.
7. Compare a cold-boot OTA attempt with one following a reading session. Record free heap, largest block,
   model, variant, error phase, and elapsed download time. If downloading still fails, investigate wolfSSL
   separately so the effect of SD staging remains distinguishable.

No EPUB cache reset is needed. Do not interrupt power during the installation phase.

## Prepared X3 experiment

- The user's available X3 at `192.168.1.151` reported `1.6.0-yacp.4-tiny` before deployment.
- `1.7.2-yacp.1-tiny` is the normal GitHub-manifest build. Tiny compilation and all 22 host tests passed.
- `1.7.2-yacp.2-tiny` is the dedicated local-manifest build, using
  `http://192.168.1.152:8768/release.json` from an ignored `platformio.local.ini` override.
  The override was moved to the Windows Recycle Bin after compilation so subsequent builds use GitHub normally.
- `analysis/ota-staging/serve_manifest.py` serves only that manifest for at most one hour. Its synthetic
  `v1.7.3-yacp-ota-test` tag enables the experiment, but the asset URL, size, and digest identify the unmodified
  official `YACP-1.7.1-yacp-tiny.bin` downloaded directly from GitHub over HTTPS.
- A successful OTA test ends on official 1.7.1 Tiny. The higher manifest tag is not a published release.
- Remote SD installation of the test build prepares the device; it is not itself a test of the new OTA flow.
  The user must launch the OTA activity on the device after reboot and confirm the offered test update.

### Results recorded on 2026-09-20

- 22/22 host orchestration tests passed.
- `pio run -e tiny` succeeded for both `.1` and `.2` experimental builds.
- `.2` image: 5,627,024 bytes, 926,576 bytes below the OTA partition limit. Embedded version and Tiny variant,
  ESP checksum, and appended SHA-256 were verified before transfer.
- `.2` file SHA-256: `517e7390a8bf4ff6e73ba9c17dca2af103a04cf571cceedffdef548534953ebd`.
- X3 upload succeeded and `/api/firmware/install` returned `Firmware installed, restarting`.
  No status polling was performed after this response.
- First on-device attempt failed: user reported progress stayed at 0% and ended with `Update failed`.
  The manifest server recorded HTTP 200 for the X3 at 23:46:46 on 2026-09-20, proving manifest retrieval.
  The exact failing operation and downloaded byte count are unknown without device logs; 0% on the UI does
  not prove zero bytes received because rendering is throttled. Staging alone has not resolved the issue.
- A second experiment uses `serve_manifest.py --local-firmware`: the same official 1.7.1 Tiny image and
  SHA-256 are served by the PC over local HTTP instead of GitHub HTTPS. No firmware changes or reflashing
  are needed; leave and reopen the OTA activity to fetch the new manifest. The complete local download
  was verified on the PC against the original release hash.
- On 2026-09-21, the user confirmed the local HTTP OTA succeeded. Server logs show the X3 requested the
  manifest at 23:59:39 and the binary at 23:59:41 on 2026-09-20, with all 5,632,416 bytes sent. This validates
  the staged download and installation flow for this X3/Tiny attempt. The final version was not independently
  queried after reboot; the intended installed image is official 1.7.1 Tiny.
- The same image failed through GitHub HTTPS and succeeded through local HTTP on the same test build.
  Prioritize the GitHub/TLS/redirect transport path. This comparison does not distinguish TLS negotiation,
  memory pressure, redirects, or WAN timeouts. It does not establish the original Xlarge user's exact cause.
- The bounded streaming wolfSSL download from current CrossInk and FreeInk has now been ported. The next
  experiment is `1.7.2-yacp.5-tiny` against the real GitHub asset, retaining trusted-manifest SHA-256
  verification before flash.
- `1.7.2-yacp.5-tiny` compiled successfully with wolfSSL. It is 5,843,504 bytes, leaving 710,096 bytes
  in the OTA application partition. Its SHA-256 is
  `bdf9a6b8914430ab93513d46260ef0dc44e45980f039bf7a855c7d29c79412ab`; the embedded version, Tiny
  variant, ESP checksum, and appended image SHA-256 passed verification.
- On 2026-09-21, the user confirmed that the real GitHub HTTPS OTA completed successfully on the X3 with
  `1.7.2-yacp.5-tiny`. The controlled manifest and target image were identical to the earlier failing
  GitHub experiment; the material change was the bounded wolfSSL firmware transport. This strongly identifies
  the former mbedTLS GitHub transport path as the cause on this device, although it does not prove which internal
  failure was responsible or reproduce the original Xlarge report.
