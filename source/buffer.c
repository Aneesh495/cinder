#include "cinder.h"

#include <string.h>
#include <stdlib.h>

/* This translation unit keeps byte-buffer policy next to the encoder. */
static size_t byte_capacity(size_t current, size_t required) {
    size_t capacity = current == 0U ? 64U : current;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            return required;
        }
        capacity *= 2U;
    }
    return capacity;
}

void cinder_bytes_reserve(CinderBytes *bytes, size_t extra) {
    if (extra > SIZE_MAX - bytes->len) {
        fprintf(stderr, "cinder: machine byte buffer overflow\n");
        abort();
    }
    size_t required = bytes->len + extra;
    if (required > bytes->cap) {
        bytes->data = cinder_realloc(bytes->data, byte_capacity(bytes->cap, required));
        bytes->cap = byte_capacity(bytes->cap, required);
    }
}

void cinder_bytes_put8(CinderBytes *bytes, uint8_t value) {
    cinder_bytes_reserve(bytes, 1U);
    bytes->data[bytes->len++] = value;
}

void cinder_bytes_put32(CinderBytes *bytes, uint32_t value) {
    cinder_bytes_reserve(bytes, 4U);
    for (unsigned i = 0U; i < 4U; ++i) {
        bytes->data[bytes->len++] = (unsigned char)(value >> (i * 8U));
    }
}

void cinder_bytes_put64(CinderBytes *bytes, uint64_t value) {
    cinder_bytes_reserve(bytes, 8U);
    for (unsigned i = 0U; i < 8U; ++i) {
        bytes->data[bytes->len++] = (unsigned char)(value >> (i * 8U));
    }
}

void cinder_bytes_patch32(CinderBytes *bytes, size_t offset, uint32_t value) {
    if (offset > bytes->len || bytes->len - offset < 4U) {
        return;
    }
    for (unsigned i = 0U; i < 4U; ++i) {
        bytes->data[offset + i] = (unsigned char)(value >> (i * 8U));
    }
}

void cinder_bytes_append(CinderBytes *bytes, const unsigned char *data, size_t length) {
    if (length == 0U) return;
    cinder_bytes_reserve(bytes, length);
    memcpy(bytes->data + bytes->len, data, length);
    bytes->len += length;
}
