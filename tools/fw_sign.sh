#!/bin/sh
# Sign a firmware file for network updates: <file>.sig = hex DER ECDSA P-256 / SHA-256.
#   tools/fw_sign.sh <private key pem> <firmware.bin> [more files...]
set -e
key=${1:?usage: tools/fw_sign.sh <key.pem> <file>...}; shift
for f in "$@"; do
  openssl dgst -sha256 -sign "$key" "$f" | od -An -v -tx1 | tr -d ' \n' > "$f.sig"
  echo "$f.sig"
done
