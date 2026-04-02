#ifndef MD5_H
#define MD5_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct md5_ctx_s md5_ctx_t;

void md5_init(md5_ctx_t *ctx);
void md5_update(md5_ctx_t *ctx, const uint8_t *input, size_t input_len);
void md5_finalize(md5_ctx_t *ctx);
void md5_step(uint32_t *buffer, const uint32_t *input);

void md5_buffer(const uint8_t *input, size_t input_len, uint8_t *result);

#if defined(__cplusplus)
}
#endif

#endif
