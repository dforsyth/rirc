#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* Enabled ciphersuites, in order of preference.
 *   - Only ECHDE key exchanges, AEAD ciphers
 *   - Ordered by cipher:
 *     - ChaCha
 *     - AES-256-GCM
 *     - AES-256-CCM
 *     - AES-256-CCM_8
 *     - AES-128-GCM
 *     - AES-128-CCM
 *     - AES-128-CCM_8
 *   - Sub-ordered by authentication method:
 *     - ECDSA
 *     - RSA
 * Note: Only stream ciphers were chosen here, which may
 *       reveal the length of exchanged messages.
 */
#define MBEDTLS_SSL_CIPHERSUITES                           \
	/* ChaCha */                                           \
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256, \
	MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,   \
	/* AES-256 */                                          \
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,       \
	MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,         \
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CCM,              \
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CCM_8,            \
	/* AES-128 */                                          \
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,       \
	MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,         \
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM,              \
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8

/* Supported ECC curves */
#define MBEDTLS_ECP_DP_BP256R1_ENABLED
#define MBEDTLS_ECP_DP_BP384R1_ENABLED
#define MBEDTLS_ECP_DP_BP512R1_ENABLED
#define MBEDTLS_ECP_DP_SECP256K1_ENABLED
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_DP_CURVE448_ENABLED

/* System support */
#define MBEDTLS_DEPRECATED_REMOVED
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE
#define MBEDTLS_REMOVE_3DES_CIPHERSUITES
#define MBEDTLS_REMOVE_ARC4_CIPHERSUITES
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD

/* Ciphersuite elements */
#define MBEDTLS_CCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_GCM_C
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_POLY1305_C

/* TLS 1.2 client */
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2

/* TLS modules */
#define MBEDTLS_AESNI_C
#define MBEDTLS_AES_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CERTS_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_FS_IO
#define MBEDTLS_GENPRIME
#define MBEDTLS_HMAC_DRBG_C
#define MBEDTLS_MD_C
#define MBEDTLS_NET_C
#define MBEDTLS_OID_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C

/* TLS extensions */
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET /* RFC 7627 */
#define MBEDTLS_SSL_SERVER_NAME_INDICATION /* RFC 6066 */

/* Crypto features */
#define MBEDTLS_ECDSA_DETERMINISTIC
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_X509_CHECK_EXTENDED_KEY_USAGE
#define MBEDTLS_X509_CHECK_KEY_USAGE

/* Error strings */
#define MBEDTLS_ERROR_C

#include "lib/mbedtls/include/mbedtls/check_config.h"

#endif
