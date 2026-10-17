/**
 * @file test_nbitq_cxx.cpp
 * @brief Both headers promise extern "C". This proves it by compiling and
 *        linking them from C++, which is the only way that claim can be true.
 *
 * Copyright (c) 2026 deftio / M. A. Chatterjee <deftio at deftio dot com>
 * BSD-2-Clause -- see LICENSE.txt
 */

#include <cstdio>
#include <cstring>

#include "nbitq.h"
#include "nbitq_container.h"

int main()
{
    bool ok = true;

    // core: cursor write, indexed read back
    uint8_t bits[16] = {0};   // 12 + 25 + 32 bits in use
    nbq_writer w;
    ok = ok && nbq_writer_init(&w, bits, sizeof(bits) * 8) == NBQ_OK;
    ok = ok && nbq_write_u32(&w, 12, 3000) == NBQ_OK;
    ok = ok && nbq_write_varint(&w, 5, 123456) == NBQ_OK;
    ok = ok && nbq_write_f32(&w, 3.14159f) == NBQ_OK;   // header-only inline, from C++

    float f = 0.0f;
    ok = ok && nbq_get_f32(bits, sizeof(bits) * 8, w.pos - 32, &f) == NBQ_OK;
    ok = ok && nbq_f32_bits(f) == 0x40490FD0u;

    uint32_t v = 0;
    ok = ok && nbq_get_u32(bits, sizeof(bits) * 8, 0, 12, &v) == NBQ_OK;
    ok = ok && v == 3000;

    // container round trip
    uint8_t box[64];
    std::size_t n = 0;
    ok = ok && nbq_container_write(box, sizeof(box), "cxx", 3, bits, w.pos, &n) == NBQ_OK;

    nbq_container c;
    std::memset(&c, 0, sizeof(c));
    ok = ok && nbq_container_read(box, n, &c) == NBQ_OK;
    ok = ok && c.payload_bits == w.pos;
    ok = ok && c.type_len == 3 && std::memcmp(c.type, "cxx", 3) == 0;

    std::printf("C++ build: %s\n", ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
