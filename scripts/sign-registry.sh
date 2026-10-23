#!/usr/bin/env bash
set -euo pipefail
repository=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
private_key=${HAJIMU_REGISTRY_SIGNING_KEY:-$HOME/.config/hajimu/signing/registry-private.pem}
openssl dgst -sha256 -sign "$private_key" -out "$repository/registry/index.json.sig" "$repository/registry/index.json"
openssl dgst -sha256 -verify "$repository/registry/public-key.pem" \
  -signature "$repository/registry/index.json.sig" "$repository/registry/index.json"
