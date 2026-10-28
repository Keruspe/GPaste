// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-secret-stream-converter.h>

#include <sodium.h>

/* Every key a binary compiling this in derives costs libsodium's minimum, as
 * in test-storage-failures.c, which says why the stores still read back.
 * GPaste derives with Argon2id alone, and never with parameters its own check
 * refuses: deriving at the minimum whatever is asked would let a derivation
 * attempted with those pass the cases refusing them. */
int
crypto_pwhash (unsigned char * const       out,
               unsigned long long          outlen,
               const char * const          passwd,
               unsigned long long          passwdlen,
               const unsigned char * const salt,
               unsigned long long          opslimit,
               size_t                      memlimit,
               int                         alg)
{
    g_assert_cmpint (alg, ==, crypto_pwhash_ALG_ARGON2ID13);
    g_assert_true (g_paste_crypto_parameters_derivable (opslimit, memlimit));

    return crypto_pwhash_argon2id (out, outlen, passwd, passwdlen, salt, crypto_pwhash_argon2id_OPSLIMIT_MIN, crypto_pwhash_argon2id_MEMLIMIT_MIN, alg);
}
