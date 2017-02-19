/*
 * crypto.c - Manage the global crypto
 *
 * Copyright (C) 2013 - 2017, Max Lv <max.c.lv@gmail.com>
 *
 * This file is part of the shadowsocks-libev.
 *
 * shadowsocks-libev is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * shadowsocks-libev is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with shadowsocks-libev; see the file COPYING. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <sodium.h>
#include <mbedtls/md5.h>

#include "crypto.h"
#include "stream.h"
#include "aead.h"
#include "utils.h"
#include "ppbloom.h"

int
balloc(buffer_t *ptr, size_t capacity)
{
    sodium_memzero(ptr, sizeof(buffer_t));
    ptr->data     = ss_malloc(capacity);
    ptr->capacity = capacity;
    return capacity;
}

int
brealloc(buffer_t *ptr, size_t len, size_t capacity)
{
    if (ptr == NULL)
        return -1;
    size_t real_capacity = max(len, capacity);
    if (ptr->capacity < real_capacity) {
        ptr->data     = ss_realloc(ptr->data, real_capacity);
        ptr->capacity = real_capacity;
    }
    return real_capacity;
}

void
bfree(buffer_t *ptr)
{
    if (ptr == NULL)
        return;
    ptr->idx      = 0;
    ptr->len      = 0;
    ptr->capacity = 0;
    if (ptr->data != NULL) {
        ss_free(ptr->data);
    }
}

int
bprepend(buffer_t *dst, buffer_t *src, size_t capacity)
{
    brealloc(dst, dst->len + src->len, capacity);
    memmove(dst->data + src->len, dst->data, dst->len);
    memcpy(dst->data, src->data, src->len);
    dst->len = dst->len + src->len;
    return dst->len;
}

int
rand_bytes(void *output, int len)
{
    randombytes_buf(output, len);
    // always return success
    return 0;
}

unsigned char *
crypto_md5(const unsigned char *d, size_t n, unsigned char *md)
{
    static unsigned char m[16];
    if (md == NULL) {
        md = m;
    }
    mbedtls_md5(d, n, md);
    return md;
}

crypto_t *
crypto_init(const char *password, const char *method)
{
    int i, m = -1;

    // Initialize sodium for random generator
    if (sodium_init() == -1) {
        FATAL("Failed to initialize sodium");
    }

    // Initialize IV or salt bloom filter
#ifdef MODULE_REMOTE
    ppbloom_init(1000000, 0.00001);
#else
    ppbloom_init(100000,  0.0000001);
#endif

    if (method != NULL) {
        for (i = 0; i < STREAM_CIPHER_NUM; i++)
            if (strcmp(method, supported_stream_ciphers[i]) == 0) {
                m = i;
                break;
            }
        if (m != -1) {
            cipher_t *cipher = stream_init(password, method);
            if (cipher == NULL)
                return NULL;
            crypto_t *crypto = (crypto_t *)malloc(sizeof(crypto_t));
            crypto_t tmp     = {
                .cipher      = cipher,
                .encrypt_all = &stream_encrypt_all,
                .decrypt_all = &stream_decrypt_all,
                .encrypt     = &stream_encrypt,
                .decrypt     = &stream_decrypt,
                .ctx_init    = &stream_ctx_init,
                .ctx_release = &stream_ctx_release,
            };
            memcpy(crypto, &tmp, sizeof(crypto_t));
            return crypto;
        }

        for (i = 0; i < AEAD_CIPHER_NUM; i++)
            if (strcmp(method, supported_aead_ciphers[i]) == 0) {
                m = i;
                break;
            }
        if (m != -1) {
            cipher_t *cipher = aead_init(password, method);
            if (cipher == NULL)
                return NULL;
            crypto_t *crypto = (crypto_t *)ss_malloc(sizeof(crypto_t));
            crypto_t tmp     = {
                .cipher      = cipher,
                .encrypt_all = &aead_encrypt_all,
                .decrypt_all = &aead_decrypt_all,
                .encrypt     = &aead_encrypt,
                .decrypt     = &aead_decrypt,
                .ctx_init    = &aead_ctx_init,
                .ctx_release = &aead_ctx_release,
            };
            memcpy(crypto, &tmp, sizeof(crypto_t));
            return crypto;
        }
    }

    LOGE("invalid cipher name: %s", method);
    return NULL;
}

#ifdef FS_DEBUG
void
dump(char *tag, char *text, int len)
{
    int i;
    printf("%s: ", tag);
    for (i = 0; i < len; i++)
        printf("0x%02x ", (uint8_t)text[i]);
    printf("\n");
}
#endif
