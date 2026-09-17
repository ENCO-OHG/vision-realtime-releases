# Vision Realtime Release Compliance

This checklist applies to every binary release built with the public GPLv3 `lib60870-C` source.

## Required Release Assets

Publish these assets together from the same build:

- `vision-realtime-<version>-windows-x64-setup.exe`
- `vision-realtime-<version>-windows-x64-service.zip`
- `SHA256SUMS.txt`

The release tag in the public `vision-realtime-releases` repository must point to the exact source used for the binary release. GitHub's automatically attached `Source code (zip)` and `Source code (tar.gz)` archives are generated from that tag.

## Source Archive Contents

Before publishing a binary release, verify that the public release tag contains:

- Vision Realtime native source under `native/`
- build and packaging scripts under `scripts/` and `packaging/`
- `third_party/lib60870/lib60870-C` with the exact source used for the build
- `LICENSE`
- `licenses/GPL-3.0.txt`
- `licenses/GATEWAY-LICENSE.txt`
- `licenses/THIRD-PARTY-NOTICES.txt`
- `licenses/WinSW-LICENSE.txt`
- documentation needed to build and install the release

Do not include CI tokens, signing keys, private credentials, customer configuration, or local build output directories.

## Publication Rule

Do not publish or distribute the installer or service ZIP unless the matching public source tag is available from the same release page at the same time.

## Alternative Licensing

If Vision Realtime is built against `lib60870-C` under a commercial MZ Automation license instead of the public GPLv3 source, confirm the commercial license obligations before using a different distribution model.
