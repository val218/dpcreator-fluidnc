#!/bin/sh
# Make the dpCREATOR firmware signing key (once). Needs openssl.
#   tools/fw_key.sh <folder outside the repo>
# Writes <folder>/dpc-fw-sign.pem (PRIVATE: keep it safe, put it in the GitHub
# secret DPC_FW_SIGN_KEY of each firmware repo) and FluidNC/src/FwUpdateKey.h
# with the public half (commit that).
set -e
dir=${1:?usage: tools/fw_key.sh <folder outside the repo>}
case "$(cd "$dir" && pwd)" in "$(pwd)"*) echo "Not inside the repo: the private key must never be committed"; exit 1;; esac
key="$dir/dpc-fw-sign.pem"
[ -e "$key" ] && { echo "$key exists already: using it"; } || openssl ecparam -name prime256v1 -genkey -noout -out "$key"
chmod 600 "$key"
h=FluidNC/src/FwUpdateKey.h
sed -i '/^static const char DPC_FW_PUBKEY/,$d' "$h"
{ echo 'static const char DPC_FW_PUBKEY[] ='
  openssl ec -in "$key" -pubout 2>/dev/null | sed 's/.*/    "&\\n"/'
  echo '    "";'; } >> "$h"
echo "Private key: $key"
echo "Public key written to FluidNC/src/FwUpdateKey.h - commit it."
echo "GitHub: Settings > Secrets > Actions > DPC_FW_SIGN_KEY = the contents of $key"
