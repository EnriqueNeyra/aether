/*
 * The four libsodium functions HomeSpan needs that ESPHome's libsodium leaves
 * out.
 *
 * API encryption makes ESPHome link its own trimmed libsodium (via the noise
 * component), and a build may contain only one libsodium: adding Espressif's
 * full copy as well fails on duplicate symbols. ESPHome's copy has everything
 * HomeSpan uses except Ed25519 key generation and signing, the generic
 * crypto_sign verify wrapper, and crypto_box_keypair. The library internals
 * those need (ge25519/sc25519 and ESPHome's _crypto_sign_ed25519_ref10_hinit
 * port) are compiled in, so this file supplies only the missing entry points.
 *
 * Adapted from libsodium 1.0.21 (crypto_sign/ed25519/ref10/keypair.c and
 * sign.c, crypto_sign/crypto_sign.c,
 * crypto_box/curve25519xsalsa20poly1305/box_curve25519xsalsa20poly1305.c).
 * ISC license, Copyright (c) 2013-2025 Frank Denis <j at pureftpd dot org>.
 *
 * If ESPHome's libsodium ever compiles these itself, the link fails on
 * duplicate definitions; delete this file then. aether_sodium_compat_selftest()
 * checks the result against RFC 8032 and RFC 7748 vectors at boot, so a change
 * in the internals below disables HomeKit instead of producing bad keys.
 */

#include <stdint.h>
#include <string.h>

#include <sodium.h>

/* ge25519_p3 is four field elements, 160 bytes whether a field element is
 * int32_t[10] or uint64_t[5]. Only ever passed by pointer to libsodium. */
typedef struct
{
  uint64_t opaque[20];
} aether_ge25519_p3;
_Static_assert(sizeof(aether_ge25519_p3) == 160, "ge25519_p3 is 160 bytes");

/* libsodium internals, declared in its private headers. */
void ge25519_scalarmult_base(aether_ge25519_p3 *h, const unsigned char *a);
void ge25519_p3_tobytes(unsigned char *s, const aether_ge25519_p3 *h);
void sc25519_reduce(unsigned char s[64]);
void sc25519_muladd(unsigned char s[32], const unsigned char a[32],
                    const unsigned char b[32], const unsigned char c[32]);
void _crypto_sign_ed25519_ref10_hinit(crypto_hash_sha512_state *hs, int prehashed);

static void aether_ed25519_clamp(unsigned char k[32])
{
  k[0] &= 248;
  k[31] &= 127;
  k[31] |= 64;
}

static int aether_ed25519_seed_keypair(unsigned char *pk, unsigned char *sk,
                                       const unsigned char *seed)
{
  aether_ge25519_p3 A;

  crypto_hash_sha512(sk, seed, 32);
  aether_ed25519_clamp(sk);

  ge25519_scalarmult_base(&A, sk);
  ge25519_p3_tobytes(pk, &A);

  memmove(sk, seed, 32);
  memmove(sk + 32, pk, 32);

  return 0;
}

/* Deterministic RFC 8032 Ed25519 (libsodium's default build). */
static int aether_ed25519_detached(unsigned char *sig, unsigned long long *siglen_p,
                                   const unsigned char *m, unsigned long long mlen,
                                   const unsigned char *sk)
{
  crypto_hash_sha512_state hs;
  unsigned char az[64];
  unsigned char nonce[64];
  unsigned char hram[64];
  aether_ge25519_p3 R;

  _crypto_sign_ed25519_ref10_hinit(&hs, 0);

  crypto_hash_sha512(az, sk, 32);
  crypto_hash_sha512_update(&hs, az + 32, 32);

  crypto_hash_sha512_update(&hs, m, mlen);
  crypto_hash_sha512_final(&hs, nonce);

  memmove(sig + 32, sk + 32, 32);

  sc25519_reduce(nonce);
  ge25519_scalarmult_base(&R, nonce);
  ge25519_p3_tobytes(sig, &R);

  _crypto_sign_ed25519_ref10_hinit(&hs, 0);
  crypto_hash_sha512_update(&hs, sig, 64);
  crypto_hash_sha512_update(&hs, m, mlen);
  crypto_hash_sha512_final(&hs, hram);

  sc25519_reduce(hram);
  aether_ed25519_clamp(az);
  sc25519_muladd(sig + 32, hram, az, nonce);

  sodium_memzero(az, sizeof az);
  sodium_memzero(nonce, sizeof nonce);

  if (siglen_p != NULL)
    *siglen_p = 64U;
  return 0;
}

int crypto_sign_keypair(unsigned char *pk, unsigned char *sk)
{
  unsigned char seed[32];
  int ret;

  randombytes_buf(seed, sizeof seed);
  ret = aether_ed25519_seed_keypair(pk, sk, seed);
  sodium_memzero(seed, sizeof seed);

  return ret;
}

int crypto_sign_detached(unsigned char *sig, unsigned long long *siglen_p,
                         const unsigned char *m, unsigned long long mlen,
                         const unsigned char *sk)
{
  return aether_ed25519_detached(sig, siglen_p, m, mlen, sk);
}

int crypto_sign_verify_detached(const unsigned char *sig, const unsigned char *m,
                                unsigned long long mlen, const unsigned char *pk)
{
  return crypto_sign_ed25519_verify_detached(sig, m, mlen, pk);
}

int crypto_box_keypair(unsigned char *pk, unsigned char *sk)
{
  randombytes_buf(sk, 32);
  return crypto_scalarmult_curve25519_base(pk, sk);
}

/* Returns 0 when key generation, signing, verification and X25519 match the
 * published test vectors (RFC 8032 section 7.1 test 1, RFC 7748 section 6.1). */
int aether_sodium_compat_selftest(void)
{
  static const unsigned char ed_seed[32] = {
      0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4,
      0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
  static const unsigned char ed_pk[32] = {
      0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
      0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
  static const unsigned char ed_sig[64] = {
      0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72, 0x90, 0x86, 0xe2, 0xcc, 0x80, 0x6e, 0x82, 0x8a,
      0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5, 0xd9, 0x74, 0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55,
      0x5f, 0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac, 0xc6, 0x1e, 0x39, 0x70, 0x1c, 0xf9, 0xb4, 0x6b,
      0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe, 0x24, 0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b};
  static const unsigned char x_sk[32] = {
      0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d, 0x3c, 0x16, 0xc1, 0x72, 0x51, 0xb2, 0x66, 0x45,
      0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0, 0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a};
  static const unsigned char x_pk[32] = {
      0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54, 0x74, 0x8b, 0x7d, 0xdc, 0xb4, 0x3e, 0xf7, 0x5a,
      0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38, 0x1a, 0xf4, 0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a};
  static const unsigned char empty[1] = {0};

  unsigned char pk[32], sk[64], sig[64];
  int result = 0;

  aether_ed25519_seed_keypair(pk, sk, ed_seed);
  if (memcmp(pk, ed_pk, sizeof pk) != 0)
    result = -1;
  else
  {
    crypto_sign_detached(sig, NULL, empty, 0, sk);
    if (memcmp(sig, ed_sig, sizeof sig) != 0)
      result = -2;
    else if (crypto_sign_verify_detached(sig, empty, 0, pk) != 0)
      result = -3;
    else
    {
      sig[0] ^= 1;
      if (crypto_sign_verify_detached(sig, empty, 0, pk) == 0)
        result = -4;
    }
  }

  if (result == 0)
  {
    crypto_scalarmult_curve25519_base(pk, x_sk);
    if (memcmp(pk, x_pk, sizeof pk) != 0)
      result = -5;
  }

  sodium_memzero(sk, sizeof sk);
  return result;
}
