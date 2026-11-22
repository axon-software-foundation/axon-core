#include <functional>
/**
 * runtime/src/modules.cpp
 * ────────────────────────
 * Implements: image->, codec->, hash->
 *
 * image->  save, resize, info, convert
 * codec->  b64_encode, b64_decode, hex_encode, hex_decode, url_encode, url_decode
 * hash->   md5, sha1, sha256, sha512
 *
 * Image processing: stb_image (header-only, no deps)
 * Hash/codec: native C++ implementations
 */

// stb_image — define implementation once here
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996 4244 4267)
#endif
#include <stb_image.h>
#include <stb_image_write.h>
#include <stb_image_resize2.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "../include/interpreter.h"
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cstdint>

namespace axon {

// ─────────────────────────────────────────────────────────────
// Base64
// ─────────────────────────────────────────────────────────────
static const char B64T[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string modules_b64_encode(const std::string& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        unsigned char b0 = (unsigned char)data[i];
        unsigned char b1 = (i+1 < data.size()) ? (unsigned char)data[i+1] : 0;
        unsigned char b2 = (i+2 < data.size()) ? (unsigned char)data[i+2] : 0;
        out += B64T[b0 >> 2];
        out += B64T[((b0 & 3) << 4) | (b1 >> 4)];
        out += (i+1 < data.size()) ? B64T[((b1 & 0xF) << 2) | (b2 >> 6)] : '=';
        out += (i+2 < data.size()) ? B64T[b2 & 0x3F] : '=';
    }
    return out;
}

std::string modules_b64_decode(const std::string& enc) {
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    std::string out;
    out.reserve((enc.size() / 4) * 3);
    int val = 0, bits = -8;
    for (unsigned char c : enc) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        bits += 6;
        if (bits >= 0) { out += (char)((val >> bits) & 0xFF); bits -= 8; }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────
// Hex
// ─────────────────────────────────────────────────────────────
static std::string hex_encode(const std::string& data) {
    static const char HEX[] = "0123456789abcdef";
    std::string out; out.reserve(data.size() * 2);
    for (unsigned char c : data) {
        out += HEX[c >> 4];
        out += HEX[c & 0xF];
    }
    return out;
}

static std::string hex_decode(const std::string& hex) {
    std::string out; out.reserve(hex.size() / 2);
    auto h = [](char c) -> unsigned char {
        if (c>='0'&&c<='9') return (unsigned char)(c-'0');
        if (c>='a'&&c<='f') return (unsigned char)(c-'a'+10);
        if (c>='A'&&c<='F') return (unsigned char)(c-'A'+10);
        return 0;
    };
    for (size_t i = 0; i+1 < hex.size(); i += 2)
        out += (char)((h(hex[i]) << 4) | h(hex[i+1]));
    return out;
}

// ─────────────────────────────────────────────────────────────
// URL encode/decode
// ─────────────────────────────────────────────────────────────
static std::string url_encode(const std::string& s) {
    static const char HEX[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum((unsigned char)c) || c=='-'||c=='_'||c=='.'||c=='~') out += (char)c;
        else { out += '%'; out += HEX[c>>4]; out += HEX[c&0xF]; }
    }
    return out;
}

static std::string url_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i]=='%' && i+2 < s.size()) {
            auto h = [](char c) -> unsigned char {
                if (c>='0'&&c<='9') return (unsigned char)(c-'0');
                if (c>='a'&&c<='f') return (unsigned char)(c-'a'+10);
                if (c>='A'&&c<='F') return (unsigned char)(c-'A'+10);
                return 0;
            };
            out += (char)((h(s[i+1])<<4)|h(s[i+2])); i += 2;
        } else if (s[i]=='+') out += ' ';
        else out += s[i];
    }
    return out;
}

// ─────────────────────────────────────────────────────────────
// SHA-256 (native implementation)
// ─────────────────────────────────────────────────────────────
static const uint32_t SHA256_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static std::string sha256(const std::string& data) {
    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };
    auto ror = [](uint32_t x, int n) { return (x>>n)|(x<<(32-n)); };
    auto CH  = [](uint32_t e,uint32_t f,uint32_t g){ return (e&f)^(~e&g); };
    auto MAJ = [](uint32_t a,uint32_t b,uint32_t c){ return (a&b)^(a&c)^(b&c); };
    auto S0  = [&](uint32_t a){ return ror(a,2)^ror(a,13)^ror(a,22); };
    auto S1  = [&](uint32_t e){ return ror(e,6)^ror(e,11)^ror(e,25); };
    auto s0  = [&](uint32_t w){ return ror(w,7)^ror(w,18)^(w>>3); };
    auto s1  = [&](uint32_t w){ return ror(w,17)^ror(w,19)^(w>>10); };

    // Padding
    std::vector<uint8_t> msg(data.begin(), data.end());
    uint64_t bit_len = (uint64_t)data.size() * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back((uint8_t)(bit_len >> (i*8)));

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)msg[off+i*4]<<24)|((uint32_t)msg[off+i*4+1]<<16)|
                   ((uint32_t)msg[off+i*4+2]<<8)|(uint32_t)msg[off+i*4+3];
        for (int i = 16; i < 64; ++i)
            w[i] = s1(w[i-2]) + w[i-7] + s0(w[i-15]) + w[i-16];

        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + S1(e) + CH(e,f,g) + SHA256_K[i] + w[i];
            uint32_t t2 = S0(a) + MAJ(a,b,c);
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    std::string r;
    static const char HX[]="0123456789abcdef";
    for (int i=0;i<8;++i) for(int j=3;j>=0;--j)
        { r+=HX[(h[i]>>(j*8+4))&0xF]; r+=HX[(h[i]>>(j*8))&0xF]; }
    return r;
}

// ─────────────────────────────────────────────────────────────
// MD5 (native)
// ─────────────────────────────────────────────────────────────
static std::string md5(const std::string& data) {
    static const uint32_t S[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5, 9,14,20,5, 9,14,20,5, 9,14,20,5, 9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
    };
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };
    auto lrot=[](uint32_t x,int n){ return (x<<n)|(x>>(32-n)); };
    std::vector<uint8_t> msg(data.begin(),data.end());
    uint64_t bit_len=(uint64_t)data.size()*8;
    msg.push_back(0x80);
    while(msg.size()%64!=56) msg.push_back(0);
    for(int i=0;i<8;++i) msg.push_back((uint8_t)(bit_len>>(i*8)));
    uint32_t a0=0x67452301,b0=0xefcdab89,c0=0x98badcfe,d0=0x10325476;
    for(size_t off=0;off<msg.size();off+=64){
        uint32_t M[16];
        for(int i=0;i<16;++i) M[i]=((uint32_t)msg[off+i*4])|((uint32_t)msg[off+i*4+1]<<8)|
            ((uint32_t)msg[off+i*4+2]<<16)|((uint32_t)msg[off+i*4+3]<<24);
        uint32_t A=a0,B=b0,C=c0,D=d0;
        for(int i=0;i<64;++i){
            uint32_t F,g;
            if(i<16){F=(B&C)|(~B&D);g=(uint32_t)i;}
            else if(i<32){F=(D&B)|(~D&C);g=(uint32_t)(5*i+1)%16;}
            else if(i<48){F=B^C^D;g=(uint32_t)(3*i+5)%16;}
            else{F=C^(B|(~D));g=(uint32_t)(7*i)%16;}
            F=F+A+K[i]+M[g]; A=D; D=C; C=B; B=B+lrot(F,S[i]);
        }
        a0+=A; b0+=B; c0+=C; d0+=D;
    }
    static const char HX[]="0123456789abcdef";
    std::string r;
    for(uint32_t v:{a0,b0,c0,d0}) for(int j=0;j<4;++j)
        {r+=HX[(v>>(j*8+4))&0xF];r+=HX[(v>>(j*8))&0xF];}
    return r;
}

// ─────────────────────────────────────────────────────────────
// Image operations via stb
// ─────────────────────────────────────────────────────────────
struct ImgData {
    unsigned char* px = nullptr;
    int w=0, h=0, ch=0;
    ImgData() = default;
    ImgData(const ImgData&) = delete;
    ImgData& operator=(const ImgData&) = delete;
    ImgData(ImgData&& o) noexcept : px(o.px),w(o.w),h(o.h),ch(o.ch) { o.px=nullptr; }
    ImgData& operator=(ImgData&& o) noexcept {
        if (px) stbi_image_free(px);
        px=o.px; w=o.w; h=o.h; ch=o.ch; o.px=nullptr;
        return *this;
    }
    ~ImgData() { if(px) stbi_image_free(px); }
};

// Load from file path
static ImgData img_load_file(const std::string& path) {
    ImgData d;
    d.px = stbi_load(path.c_str(), &d.w, &d.h, &d.ch, 0);
    return d;
}

// Load from raw bytes (JPEG/PNG binary data)
static ImgData img_load_mem(const std::string& raw) {
    ImgData d;
    d.px = stbi_load_from_memory(
        (const unsigned char*)raw.data(), (int)raw.size(),
        &d.w, &d.h, &d.ch, 0);
    return d;
}

// Write to memory buffer and return bytes
static std::string img_encode(ImgData& d, const std::string& fmt) {
    std::vector<unsigned char> buf;
    auto write_fn = [](void* ctx, void* data, int size) {
        auto* v = (std::vector<unsigned char>*)ctx;
        v->insert(v->end(), (unsigned char*)data, (unsigned char*)data+size);
    };

    if (fmt=="jpg"||fmt=="jpeg")
        stbi_write_jpg_to_func(write_fn, &buf, d.w, d.h, d.ch, d.px, 90);
    else if (fmt=="png")
        stbi_write_png_to_func(write_fn, &buf, d.w, d.h, d.ch, d.px, d.w*d.ch);
    else if (fmt=="bmp")
        stbi_write_bmp_to_func(write_fn, &buf, d.w, d.h, d.ch, d.px);
    else // default jpeg
        stbi_write_jpg_to_func(write_fn, &buf, d.w, d.h, d.ch, d.px, 90);

    return std::string(buf.begin(), buf.end());
}

// ─────────────────────────────────────────────────────────────
// Public eval_module_call — called from interpreter
// ─────────────────────────────────────────────────────────────
Value eval_modules(const std::string& mod, const std::string& fn,
                   const std::vector<Value>& args,
                   const std::function<std::string(const Value&)>& to_str) {

    // ── codec-> ──────────────────────────────────────────────
    if (mod == "codec") {
        if (fn == "b64_encode") {
            if (args.empty()) return Value::from(std::string(""));
            return Value::from(modules_b64_encode(to_str(args[0])));
        }
        if (fn == "b64_decode") {
            if (args.empty()) return Value::null();
            std::string s = to_str(args[0]);
            auto c = s.find(',');
            if (c != std::string::npos) s = s.substr(c+1);
            return Value::from(modules_b64_decode(s));
        }
        if (fn == "hex_encode") {
            if (args.empty()) return Value::from(std::string(""));
            return Value::from(hex_encode(to_str(args[0])));
        }
        if (fn == "hex_decode") {
            if (args.empty()) return Value::null();
            return Value::from(hex_decode(to_str(args[0])));
        }
        if (fn == "url_encode") {
            if (args.empty()) return Value::from(std::string(""));
            return Value::from(url_encode(to_str(args[0])));
        }
        if (fn == "url_decode") {
            if (args.empty()) return Value::from(std::string(""));
            return Value::from(url_decode(to_str(args[0])));
        }
    }

    // ── hash-> ───────────────────────────────────────────────
    if (mod == "hash") {
        std::string input = args.empty() ? "" : to_str(args[0]);
        if (fn == "sha256") return Value::from(sha256(input));
        if (fn == "md5")    return Value::from(md5(input));
        if (fn == "sha1" || fn == "sha512") {
            throw std::runtime_error("hash->" + fn +
                "() is not implemented; use hash->sha256() or hash->md5()");
        }
    }

    // ── image-> ──────────────────────────────────────────────
    if (mod == "image") {

        // image->info(path_or_b64) → {width, height, channels, format}
        if (fn == "info") {
            if (args.empty()) return Value::null();
            std::string src = to_str(args[0]);
            ImgData d;
            // Try as file path first
            bool is_path = (src.find('/') != std::string::npos ||
                src.find('\\') != std::string::npos ||
                src.find('.') != std::string::npos);
            if (is_path) { ImgData tmp = img_load_file(src); d = std::move(tmp); }
            if (!d.px) {
                std::string raw = modules_b64_decode(src);
                if (!raw.empty()) { ImgData tmp = img_load_mem(raw); d = std::move(tmp); }
            }
            if (!d.px) return Value::null();
            Value r; r.kind = Value::Kind::Dict;
            r.dict["width"]    = Value::from((double)d.w);
            r.dict["height"]   = Value::from((double)d.h);
            r.dict["channels"] = Value::from((double)d.ch);
            return r;
        }

        // image->save(path, b64_data)
        // Decodes base64 and writes raw bytes directly to disk.
        // The data must already be a valid image (JPEG/PNG/etc).
        // For format conversion use image->convert() first.
        if (fn == "save") {
            if (args.size() < 2) return Value::from(false);
            std::string path = to_str(args[0]);
            std::string b64  = to_str(args[1]);

            // Strip data URL prefix: "data:image/jpeg;base64,..."
            auto comma = b64.find(',');
            if (comma != std::string::npos) b64 = b64.substr(comma+1);

            std::string raw = modules_b64_decode(b64);
            if (raw.empty()) return Value::from(false);

            // Write raw bytes directly — no re-encoding
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f) return Value::from(false);
            f.write(raw.data(), (std::streamsize)raw.size());
            return Value::from(f.good());
        }

        // image->resize(path_or_b64, width, height, format="")
        // Returns base64 of resized image
        if (fn == "resize") {
            if (args.size() < 3) return Value::null();
            std::string src = to_str(args[0]);
            int new_w = (int)args[1].n;
            int new_h = (int)args[2].n;
            std::string fmt = args.size() >= 4 ? to_str(args[3]) : "jpeg";

            ImgData d;
            // Load from file or base64
            bool is_path = (src.find('/') != std::string::npos ||
                           src.find('\\') != std::string::npos);
            if (is_path) d = img_load_file(src);
            if (!d.px) {
                auto comma = src.find(',');
                std::string b64 = (comma != std::string::npos) ? src.substr(comma+1) : src;
                std::string raw = modules_b64_decode(b64);
                if (!raw.empty()) d = img_load_mem(raw);
            }
            if (!d.px) return Value::null();

            // Resize
            std::vector<unsigned char> resized(new_w * new_h * d.ch);
            stbir_resize_uint8_linear(
                d.px, d.w, d.h, 0,
                resized.data(), new_w, new_h, 0,
                (stbir_pixel_layout)d.ch);

            // Encode resized image directly without ImgData wrapper
            std::vector<unsigned char> out_buf;
            auto write_fn2 = [](void* ctx, void* data, int size) {
                auto* v = (std::vector<unsigned char>*)ctx;
                v->insert(v->end(), (unsigned char*)data, (unsigned char*)data+size);
            };
            if (fmt=="jpg"||fmt=="jpeg")
                stbi_write_jpg_to_func(write_fn2, &out_buf, new_w, new_h, d.ch, resized.data(), 90);
            else if (fmt=="png")
                stbi_write_png_to_func(write_fn2, &out_buf, new_w, new_h, d.ch, resized.data(), new_w*d.ch);
            else if (fmt=="bmp")
                stbi_write_bmp_to_func(write_fn2, &out_buf, new_w, new_h, d.ch, resized.data());
            else
                stbi_write_jpg_to_func(write_fn2, &out_buf, new_w, new_h, d.ch, resized.data(), 90);
            std::string encoded(out_buf.begin(), out_buf.end());

            return Value::from(modules_b64_encode(encoded));
        }

        // image->convert(path_or_b64, format) → base64 of converted image
        if (fn == "convert") {
            if (args.size() < 2) return Value::null();
            std::string src = to_str(args[0]);
            std::string fmt = to_str(args[1]);

            ImgData d;
            bool is_path = (src.find('/') != std::string::npos ||
                           src.find('\\') != std::string::npos);
            if (is_path) d = img_load_file(src);
            if (!d.px) {
                auto comma = src.find(',');
                std::string b64 = (comma != std::string::npos) ? src.substr(comma+1) : src;
                std::string raw = modules_b64_decode(b64);
                if (!raw.empty()) d = img_load_mem(raw);
            }
            if (!d.px) return Value::null();

            std::string encoded = img_encode(d, fmt);
            return Value::from(modules_b64_encode(encoded));
        }
    }

    return Value::null(); // not handled
}

} // namespace axon
