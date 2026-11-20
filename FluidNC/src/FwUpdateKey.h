// FwUpdateKey.h - the dpCREATOR firmware signing key, public half (PEM).
//
// Network updates (FwUpdate.h) take only files signed with the private half.
// Empty = this build takes no network updates ("no update key in this build").
// Make the pair once with tools/fw_key.sh: it writes this file; the private
// key goes into the GitHub secret DPC_FW_SIGN_KEY (and a safe copy), never
// into a repo. The same public key goes into every dpCREATOR firmware
// (SmartTabUI, MPG, HMI, Link) that checks files itself.
#pragma once
static const char DPC_FW_PUBKEY[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEdVQfl8nn0Dpe1NeauPr+tD+jIFmh\n"
    "OrCYcucni6A74KEooR+7Xs3YZU6//vzo1IE9z8z5Twsmc5DZax57m56Cfw==\n"
    "-----END PUBLIC KEY-----\n"
    "";
