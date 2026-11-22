/**
 * miniz.h — Minimal ZIP reader using zlib
 * Implements only what office_reader.cpp needs.
 */
#pragma once
#ifdef __cplusplus
#include <cstdint>
#include <cstring>
#include <cstdlib>
#else
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#endif
/* zlib is vendored in third_party/zlib */
#include "../zlib/zlib.h"

typedef uint32_t mz_uint;
typedef uint64_t mz_uint64;
typedef uint16_t mz_u16;
typedef uint32_t mz_u32;

static mz_u16 mz_r16(const unsigned char* p) {
    return (mz_u16)(p[0] | ((mz_u16)p[1] << 8));
}
static mz_u32 mz_r32(const unsigned char* p) {
    return (mz_u32)(p[0] | ((mz_u32)p[1]<<8) | ((mz_u32)p[2]<<16) | ((mz_u32)p[3]<<24));
}

typedef struct {
    char      m_filename[260];
    mz_uint64 m_uncomp_size;
    mz_uint64 m_comp_size;
    mz_uint64 m_local_header_ofs;
    mz_uint   m_method;
} mz_zip_archive_file_stat;

typedef struct {
    char      name[260];
    mz_uint64 uncomp_size;
    mz_uint64 comp_size;
    mz_uint64 local_ofs;
    mz_uint   method;
} mz_zip_entry;

typedef struct {
    const unsigned char* m_data;
    size_t               m_size;
    mz_uint              m_total_files;
    mz_zip_entry*        m_entries;
} mz_zip_archive;

static int mz_zip_reader_init_mem(mz_zip_archive* z,
                                   const void* data, size_t size, int flags) {
    (void)flags;
    memset(z, 0, sizeof(*z));
    if (size < 22) return 0;
    z->m_data = (const unsigned char*)data;
    z->m_size = size;

    /* Find End of Central Directory */
    int eocd = -1;
    size_t search_start = size >= 65558 ? size - 65558 : 0;
    for (size_t i = size - 22; i >= search_start && i < size; --i) {
        if (mz_r32(z->m_data + i) == 0x06054b50U) { eocd = (int)i; break; }
        if (i == 0) break;
    }
    if (eocd < 0) return 0;

    mz_u16 nfiles  = mz_r16(z->m_data + eocd + 10);
    mz_u32 cd_size = mz_r32(z->m_data + eocd + 12);
    mz_u32 cd_ofs  = mz_r32(z->m_data + eocd + 16);
    if ((size_t)(cd_ofs + cd_size) > size) return 0;

    z->m_total_files = nfiles;
    z->m_entries = (mz_zip_entry*)calloc(nfiles, sizeof(mz_zip_entry));
    if (!z->m_entries) return 0;

    const unsigned char* cd = z->m_data + cd_ofs;
    for (mz_uint i = 0; i < nfiles; ++i) {
        if (mz_r32(cd) != 0x02014b50U) break;
        mz_u16 fnlen  = mz_r16(cd + 28);
        mz_u16 extlen = mz_r16(cd + 30);
        mz_u16 cmtlen = mz_r16(cd + 32);
        z->m_entries[i].method     = mz_r16(cd + 10);
        z->m_entries[i].comp_size  = mz_r32(cd + 20);
        z->m_entries[i].uncomp_size= mz_r32(cd + 24);
        z->m_entries[i].local_ofs  = mz_r32(cd + 42);
        size_t cplen = fnlen < 259 ? fnlen : 259;
        memcpy(z->m_entries[i].name, cd + 46, cplen);
        z->m_entries[i].name[cplen] = '\0';
        cd += 46 + fnlen + extlen + cmtlen;
    }
    return 1;
}

static int mz_zip_reader_locate_file(mz_zip_archive* z,
                                      const char* name, const char* c, int f) {
    (void)c; (void)f;
    for (mz_uint i = 0; i < z->m_total_files; ++i)
        if (strcmp(z->m_entries[i].name, name) == 0) return (int)i;
    return -1;
}

static int mz_zip_reader_file_stat(mz_zip_archive* z, mz_uint idx,
                                    mz_zip_archive_file_stat* s) {
    if (!s || idx >= z->m_total_files) return 0;
    // Axon local ZIP reader: both arrays have the same fixed capacity. Preserve
    // the complete filename buffer and always terminate the destination.
    memcpy(s->m_filename, z->m_entries[idx].name, sizeof(s->m_filename));
    s->m_filename[sizeof(s->m_filename) - 1] = '\0';
    s->m_uncomp_size     = z->m_entries[idx].uncomp_size;
    s->m_comp_size       = z->m_entries[idx].comp_size;
    s->m_local_header_ofs= z->m_entries[idx].local_ofs;
    s->m_method          = z->m_entries[idx].method;
    return 1;
}

static int mz_zip_reader_extract_to_mem(mz_zip_archive* z, mz_uint idx,
                                         void* out, size_t out_size, int f) {
    (void)f;
    if (idx >= z->m_total_files || !out) return 0;
    mz_uint64 lofs = z->m_entries[idx].local_ofs;
    if (lofs + 30 > z->m_size) return 0;

    const unsigned char* lh = z->m_data + lofs;
    if (mz_r32(lh) != 0x04034b50U) return 0;
    mz_u16 fnlen  = mz_r16(lh + 26);
    mz_u16 extlen = mz_r16(lh + 28);
    const unsigned char* cdata = lh + 30 + fnlen + extlen;

    if (z->m_entries[idx].method == 0) {
        /* Stored */
        size_t n = (size_t)z->m_entries[idx].comp_size;
        if (n > out_size) n = out_size;
        memcpy(out, cdata, n);
        return 1;
    }
    if (z->m_entries[idx].method == 8) {
        /* Deflate */
        z_stream strm;
        memset(&strm, 0, sizeof(strm));
        inflateInit2(&strm, -15);
        strm.next_in  = (Bytef*)cdata;
        strm.avail_in = (uInt)z->m_entries[idx].comp_size;
        strm.next_out = (Bytef*)out;
        strm.avail_out= (uInt)out_size;
        inflate(&strm, Z_FINISH);
        inflateEnd(&strm);
        return 1;
    }
    return 0;
}

static void mz_zip_reader_end(mz_zip_archive* z) {
    free(z->m_entries);
    z->m_entries = 0;
    z->m_total_files = 0;
}
