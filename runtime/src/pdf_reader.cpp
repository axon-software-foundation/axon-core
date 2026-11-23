/**
 * runtime/src/pdf_reader.cpp
 * Fast PDF text + image extractor with hard limits.
 * No upfront object index — processes streams linearly.
 */
#include "../include/document_io.h"
#include "../include/interpreter.h"
#include "../include/modules.h"
#include "../../third_party/zlib/zlib.h"
#include <stb_image_write.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace axon {

// Hard limits to prevent hangs on complex PDFs
static const size_t MAX_STREAMS   = 2000;   // max streams to process
static const size_t MAX_CMAP_ITER = 50000;  // max iterations in CMap parse
static const size_t MAX_PDF_SIZE  = 200 * 1024 * 1024; // 200MB max

// ─────────────────────────────────────────────────────────────
// zlib inflate
// ─────────────────────────────────────────────────────────────
static std::string inflate_zlib(const std::string& c) {
    if (c.empty()) return "";
    std::string out; out.resize(std::max(c.size()*4,(size_t)4096));
    z_stream z{}; inflateInit(&z);
    z.next_in=(Bytef*)c.data(); z.avail_in=(uInt)c.size();
    int r;
    do {
        if (z.total_out>=out.size()) out.resize(out.size()*2);
        z.next_out=(Bytef*)out.data()+z.total_out;
        z.avail_out=(uInt)(out.size()-z.total_out);
        r=inflate(&z,Z_NO_FLUSH);
        if (r==Z_STREAM_ERROR||r==Z_DATA_ERROR||r==Z_MEM_ERROR) break;
    } while(r==Z_OK && z.avail_in>0);
    inflateEnd(&z);
    if (z.total_out==0) return "";
    out.resize(z.total_out);
    return out;
}

// ─────────────────────────────────────────────────────────────
// On-demand object stream resolver
// Finds "N M obj ... stream ... endstream" without upfront index
// ─────────────────────────────────────────────────────────────
static std::string resolve_obj_stream(const std::string& pdf,
                                       const std::string& obj_ref)
{
    // obj_ref is like "15 0" → search for "15 0 obj"
    std::string pattern = obj_ref + " obj";
    auto pos = pdf.find(pattern);
    if (pos == std::string::npos) return "";

    auto sk = pdf.find("stream", pos);
    if (sk == std::string::npos || sk > pos + 4096) return "";

    size_t ds = sk + 6;
    if (ds < pdf.size() && pdf[ds]=='\r') ++ds;
    if (ds < pdf.size() && pdf[ds]=='\n') ++ds;

    auto es = pdf.find("endstream", ds);
    if (es == std::string::npos) return "";

    size_t de = es;
    if (de>0 && pdf[de-1]=='\n') --de;
    if (de>0 && pdf[de-1]=='\r') --de;

    std::string raw = pdf.substr(ds, de-ds);
    std::string dict_region = pdf.substr(pos, sk-pos);

    if (dict_region.find("/FlateDecode") != std::string::npos) {
        try { return inflate_zlib(raw); } catch(...) {}
    }
    return raw;
}

// ─────────────────────────────────────────────────────────────
// ToUnicode CMap
// ─────────────────────────────────────────────────────────────
using CMap = std::unordered_map<std::string, std::string>;

static std::string cp_to_utf8(unsigned int cp) {
    std::string s;
    if (cp<0x80) s+=(char)cp;
    else if (cp<0x800){s+=(char)(0xC0|(cp>>6));s+=(char)(0x80|(cp&0x3F));}
    else{s+=(char)(0xE0|(cp>>12));s+=(char)(0x80|((cp>>6)&0x3F));s+=(char)(0x80|(cp&0x3F));}
    return s;
}
static unsigned int hex_to_uint(const std::string& h) {
    unsigned int v=0;
    for(char c:h){v<<=4;if(c>='0'&&c<='9')v|=(unsigned)(c-'0');
        else if(c>='a'&&c<='f')v|=(unsigned)(c-'a'+10);
        else if(c>='A'&&c<='F')v|=(unsigned)(c-'A'+10);}
    return v;
}
static std::string hex_to_bytes(const std::string& h) {
    std::string b;
    for(size_t i=0;i+1<h.size();i+=2){
        auto hv=[](char c)->unsigned char{
            if(c>='0'&&c<='9')return(unsigned char)(c-'0');
            if(c>='a'&&c<='f')return(unsigned char)(c-'a'+10);
            if(c>='A'&&c<='F')return(unsigned char)(c-'A'+10);
            return 0;};
        b+=(char)((hv(h[i])<<4)|hv(h[i+1]));
    }
    return b;
}
static void cmap_add(CMap& cm, const std::string& src_h, unsigned int cp) {
    std::string src=hex_to_bytes(src_h), utf8=cp_to_utf8(cp);
    cm[src]=utf8;
    if(src.size()==2&&(unsigned char)src[0]==0) cm[src.substr(1)]=utf8;
}

static CMap parse_cmap(const std::string& text) {
    CMap cm;
    size_t pos=0, len=text.size(), iters=0;
    auto sk=[&](){while(pos<len&&(text[pos]==' '||text[pos]=='\t'||text[pos]=='\r'||text[pos]=='\n'))++pos;};
    auto rd=[&]()->std::string{
        sk();
        if(pos>=len||text[pos]!='<')return "";
        ++pos; std::string h;
        while(pos<len&&text[pos]!='>'){h+=text[pos++];if(h.size()>32)break;}
        if (pos < len) { ++pos; }
        return h;
    };

    while(pos<len && iters++<MAX_CMAP_ITER) {
        sk(); if(pos>=len) break;
        // beginbfchar / begincidchar
        bool bf=(pos+11<=len&&text.substr(pos,11)=="beginbfchar");
        bool ci=(pos+12<=len&&text.substr(pos,12)=="begincidchar");
        if(bf||ci){
            pos+=bf?11:12;
            size_t inner=0;
            while(pos<len&&inner++<MAX_CMAP_ITER){
                sk(); if(pos>=len) break;
                if(pos+3<=len&&text.substr(pos,3)=="end") break;
                if(text[pos]!='<'){++pos;continue;}
                std::string s=rd(),d=rd();
                if(!s.empty()&&!d.empty()) cmap_add(cm,s,hex_to_uint(d));
            }
            continue;
        }
        // beginbfrange
        if(pos+12<=len&&text.substr(pos,12)=="beginbfrange"){
            pos+=12;
            size_t inner=0;
            while(pos<len&&inner++<MAX_CMAP_ITER){
                sk(); if(pos>=len) break;
                if(pos+10<=len&&text.substr(pos,10)=="endbfrange") break;
                if(text[pos]!='<'){++pos;continue;}
                std::string lo=rd(),hi=rd();
                if(lo.empty()||hi.empty()){++pos;continue;}
                unsigned int lv=hex_to_uint(lo),hv=hex_to_uint(hi);
                sk();
                if(pos<len&&text[pos]=='<'){
                    std::string dh=rd(); unsigned int dv=hex_to_uint(dh);
                    for(unsigned int i=lv;i<=hv&&i-lv<0x10000;++i){
                        char buf[8]; snprintf(buf,8,lo.size()>2?"%04X":"%02X",i);
                        cmap_add(cm,std::string(buf),dv+(i-lv));
                    }
                } else if(pos<len&&text[pos]=='['){
                    ++pos; unsigned int i=lv;
                    size_t al=0;
                    while(pos<len&&text[pos]!=']'&&al++<0x10000){
                        sk(); if(pos>=len) break;
                        if(text[pos]=='<'){
                            char buf[8]; snprintf(buf,8,lo.size()>2?"%04X":"%02X",i);
                            cmap_add(cm,std::string(buf),hex_to_uint(rd()));++i;
                        } else ++pos;
                    }
                    if(pos<len) ++pos;
                }
            }
            continue;
        }
        ++pos;
    }
    return cm;
}

// ─────────────────────────────────────────────────────────────
// Extract CMaps from PDF — on-demand, no upfront index
// ─────────────────────────────────────────────────────────────
static CMap extract_cmaps(const std::string& pdf) {
    CMap merged;
    size_t pos=0, count=0;
    while(pos<pdf.size() && count<50) {  // max 50 ToUnicode entries
        auto tu=pdf.find("/ToUnicode",pos);
        if(tu==std::string::npos) break;
        pos=tu+10; ++count;

        size_t p=pos;
        while(p<pdf.size()&&(pdf[p]==' '||pdf[p]=='\t')) ++p;
        if(p>=pdf.size()) continue;

        std::string cmap_text;
        if(isdigit((unsigned char)pdf[p])){
            // Indirect ref: "N M R"
            std::string ref;
            while(p<pdf.size()&&pdf[p]!='R'){ref+=pdf[p++];}
            while(!ref.empty()&&isspace((unsigned char)ref.back())) ref.pop_back();
            cmap_text=resolve_obj_stream(pdf,ref);
        } else {
            // Inline — look for stream within 1000 chars
            auto sk=pdf.find("stream",tu);
            if(sk==std::string::npos||sk>tu+1000) continue;
            size_t ds=sk+6;
            if(ds<pdf.size()&&pdf[ds]=='\r')++ds;
            if(ds<pdf.size()&&pdf[ds]=='\n')++ds;
            auto es=pdf.find("endstream",ds);
            if(es==std::string::npos) continue;
            size_t de=es;
            if(de>0&&pdf[de-1]=='\n')--de;
            if(de>0&&pdf[de-1]=='\r')--de;
            std::string raw=pdf.substr(ds,de-ds);
            std::string d=pdf.substr(tu,sk-tu);
            if(d.find("/FlateDecode")!=std::string::npos)
                try{cmap_text=inflate_zlib(raw);}catch(...){}
            else cmap_text=raw;
        }
        if(!cmap_text.empty()&&cmap_text.size()<512*1024&&
           (cmap_text.find("beginbf")!=std::string::npos||
            cmap_text.find("begincid")!=std::string::npos)){
            auto cm=parse_cmap(cmap_text);
            for(auto& kv:cm) merged[kv.first]=kv.second;
        }
    }
    return merged;
}

// ─────────────────────────────────────────────────────────────
// Apply CMap
// ─────────────────────────────────────────────────────────────
static std::string apply_cmap(const std::string& g, const CMap& cm) {
    if(cm.empty()){
        std::string r;
        for(unsigned char c:g) if(c>=32&&c<128) r+=(char)c; else if(c=='\n') r+='\n';
        return r;
    }
    std::string r; size_t i=0;
    while(i<g.size()){
        if(i+1<g.size()){auto it=cm.find(g.substr(i,2));if(it!=cm.end()){r+=it->second;i+=2;continue;}}
        auto it=cm.find(g.substr(i,1));
        if(it!=cm.end()){r+=it->second;i++;continue;}
        unsigned char c=(unsigned char)g[i];
        if(c>=32&&c<128) r+=(char)c; else if(c=='\n') r+='\n';
        ++i;
    }
    return r;
}

// ─────────────────────────────────────────────────────────────
// PDF string decoders
// ─────────────────────────────────────────────────────────────
static std::string dec_lit(const std::string& raw){
    std::string r;
    for(size_t i=0;i<raw.size();++i){
        if(raw[i]=='\\'&&i+1<raw.size()){
            char n=raw[i+1];
            if(n=='n'){r+='\n';i++;}else if(n=='r'){r+='\r';i++;}
            else if(n=='t'){r+='\t';i++;}else if(n=='('||n==')'||n=='\\'){r+=n;i++;}
            else r+=raw[i];
        } else r+=raw[i];
    }
    return r;
}
static std::string dec_hex(const std::string& h){
    std::string r,s=h;
    s.erase(std::remove_if(s.begin(),s.end(),[](char c){return isspace((unsigned char)c);}),s.end());
    if(s.size()%2) s+="0";
    for(size_t i=0;i+1<s.size();i+=2){
        auto hv = [](char c) -> unsigned char {
            if (c >= '0' && c <= '9') { return (unsigned char)(c - '0'); }
            if (c >= 'a' && c <= 'f') { return (unsigned char)(c - 'a' + 10); }
            if (c >= 'A' && c <= 'F') { return (unsigned char)(c - 'A' + 10); }
            return 0;
        };
        r+=(char)((hv(s[i])<<4)|hv(s[i+1]));
    }
    return r;
}

// ─────────────────────────────────────────────────────────────
// Extract text from content stream
// ─────────────────────────────────────────────────────────────
static std::string extract_text(const std::string& s, const CMap& cm) {
    std::string text; bool in_bt=false;
    size_t pos=0,len=s.size();
    auto ws=[&](){while(pos<len&&(s[pos]==' '||s[pos]=='\t'||s[pos]=='\r'||s[pos]=='\n'))++pos;};

    while(pos<len){
        ws(); if(pos>=len) break;
        if(pos+2<=len&&s.substr(pos,2)=="BT"){in_bt=true;pos+=2;continue;}
        if(pos+2<=len&&s.substr(pos,2)=="ET"){in_bt=false;if(!text.empty()&&text.back()!='\n')text+='\n';pos+=2;continue;}
        if(!in_bt){++pos;continue;}
        char c=s[pos];
        if(c=='('){
            int d=1;++pos; std::string raw;
            while(pos<len&&d>0){char ch=s[pos++];if(ch=='\\'&&pos<len){raw+=ch;raw+=s[pos++];}
                else if(ch=='('){d++;raw+=ch;}else if(ch==')'){if(--d>0)raw+=ch;}else raw+=ch;}
            ws(); std::string op;
            while(pos<len&&!isspace((unsigned char)s[pos])&&s[pos]!='('&&s[pos]!='['&&s[pos]!='<') op+=s[pos++];
            if(op=="Tj"||op=="'"||op=="\"") text+=apply_cmap(dec_lit(raw),cm);
            if(op=="'") text+='\n';
            continue;
        }
        if(c=='['){
            ++pos; std::string arr;
            while(pos<len&&s[pos]!=']'){
                ws(); if(pos>=len||s[pos]==']') break;
                if(s[pos]=='('){int d=1;++pos;std::string raw;
                    while(pos<len&&d>0){char ch=s[pos++];if(ch=='\\'&&pos<len){raw+=ch;raw+=s[pos++];}
                        else if(ch=='('){d++;raw+=ch;}else if(ch==')'){if(--d>0)raw+=ch;}else raw+=ch;}
                    arr+=dec_lit(raw);
                } else if(s[pos]=='<'){++pos;std::string h;while(pos<len&&s[pos]!='>') h+=s[pos++];if(pos<len)++pos;arr+=dec_hex(h);}
                else if(s[pos]=='%'){
                    // Comments are valid between elements of a PDF TJ array.
                    while(pos<len&&s[pos]!='\r'&&s[pos]!='\n') ++pos;
                }
                else{
                    std::string n;
                    while(pos<len&&(isdigit((unsigned char)s[pos])||s[pos]=='-'||s[pos]=='+'||s[pos]=='.'))
                        n+=s[pos++];
                    if(!n.empty()){
                        try{if(std::stod(n)<-100)arr+=' ';}catch(...){}
                    } else {
                        // Unsupported/malformed tokens must still make progress.
                        ++pos;
                    }
                }
            }
            if (pos < len) { ++pos; }
            ws();
            std::string op;while(pos<len&&!isspace((unsigned char)s[pos])&&s[pos]!='('&&s[pos]!='[') op+=s[pos++];
            if(op=="TJ") text+=apply_cmap(arr,cm);
            continue;
        }
        if(c=='<'&&pos+1<len&&s[pos+1]!='<'){
            ++pos; std::string h;while(pos<len&&s[pos]!='>') h+=s[pos++];if(pos<len)++pos;
            ws(); std::string op;while(pos<len&&!isspace((unsigned char)s[pos])) op+=s[pos++];
            if(op=="Tj") text+=apply_cmap(dec_hex(h),cm);
            continue;
        }
        ++pos;
    }
    return text;
}

// ─────────────────────────────────────────────────────────────
// Base64
// ─────────────────────────────────────────────────────────────
static const char B64C[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string b64(const std::string& d){
    std::string o;o.reserve(((d.size()+2)/3)*4);
    for(size_t i=0;i<d.size();i+=3){
        unsigned char b0=(unsigned char)d[i],b1=(i+1<d.size())?(unsigned char)d[i+1]:0,b2=(i+2<d.size())?(unsigned char)d[i+2]:0;
        o+=B64C[b0>>2];o+=B64C[((b0&3)<<4)|(b1>>4)];
        o+=(i+1<d.size())?B64C[((b1&0xF)<<2)|(b2>>6)]:'=';
        o+=(i+2<d.size())?B64C[b2&0x3F]:'=';
    }
    return o;
}

// ─────────────────────────────────────────────────────────────
// Main parse
// ─────────────────────────────────────────────────────────────
struct PdfImg{std::string data,fmt;int w=0,h=0,page=0;};
struct PdfPage{int n=0;std::string text;};
struct PdfResult{std::string full;std::vector<PdfPage> pages;std::vector<PdfImg> imgs;};

static PdfResult parse_pdf(const std::string& pdf) {
    PdfResult R;
    if (pdf.size() > MAX_PDF_SIZE) return R;

    // Extract CMaps first (fast, on-demand)
    CMap cm = extract_cmaps(pdf);

    int page_num=0;
    size_t search=0, stream_count=0;

    while(stream_count++ < MAX_STREAMS) {
        auto sk=pdf.find("stream",search);
        if(sk==std::string::npos) break;

        size_t ds=sk+6;
        if(ds<pdf.size()&&pdf[ds]=='\r')++ds;
        if(ds<pdf.size()&&pdf[ds]=='\n')++ds;

        auto es=pdf.find("endstream",ds);
        if(es==std::string::npos) break;
        if(es<=ds){search=sk+6;continue;} // safety

        size_t de=es;
        if(de>0&&pdf[de-1]=='\n')--de;
        if(de>0&&pdf[de-1]=='\r')--de;
        if(de<=ds){search=es+9;continue;} // empty stream

        std::string raw=pdf.substr(ds,de-ds);

        // Get dict: search only within 2KB before the stream keyword
        std::string dict;
        {
            size_t win_start = (sk > 2048) ? sk - 2048 : 0;
            std::string win = pdf.substr(win_start, sk - win_start);
            size_t dc = win.rfind(">>");
            if (dc != std::string::npos) {
                size_t dop = win.rfind("<<", dc);
                if (dop != std::string::npos && dc > dop)
                    dict = win.substr(dop, dc + 2 - dop);
            }
        }

        bool is_flate=(dict.find("/FlateDecode")!=std::string::npos);
        bool is_dct  =(dict.find("/DCTDecode")  !=std::string::npos);
        bool is_img  =(dict.find("/Subtype /Image")!=std::string::npos||
                       dict.find("/Subtype/Image") !=std::string::npos);

        if(is_img){
            PdfImg img; img.page=page_num;
            auto pi=[&](const std::string& k)->int{
                auto p=dict.find(k);if(p==std::string::npos)return 0;
                p+=k.size();while(p<dict.size()&&(dict[p]==' '||dict[p]=='/'))++p;
                std::string n;while(p<dict.size()&&isdigit((unsigned char)dict[p]))n+=dict[p++];
                try{return n.empty()?0:std::stoi(n);}catch(...){return 0;}
            };
            img.w=pi("/Width");img.h=pi("/Height");

            if(is_dct){
                img.fmt="jpeg";
                // May have FlateDecode wrapper
                std::string jpeg_bytes=raw;
                if(is_flate) try{jpeg_bytes=inflate_zlib(raw);}catch(...){}
                // Trim to JPEG FF D8 ... FF D9
                size_t js=std::string::npos;
                for(size_t k=0;k+1<jpeg_bytes.size();++k)
                    if((unsigned char)jpeg_bytes[k]==0xFF&&(unsigned char)jpeg_bytes[k+1]==0xD8){js=k;break;}
                size_t je=std::string::npos;
                if(js!=std::string::npos)
                    for(size_t k=jpeg_bytes.size()-1;k>js+1;--k)
                        if((unsigned char)jpeg_bytes[k-1]==0xFF&&(unsigned char)jpeg_bytes[k]==0xD9){je=k+1;break;}
                img.data=(js!=std::string::npos)
                    ?(je!=std::string::npos?jpeg_bytes.substr(js,je-js):jpeg_bytes.substr(js))
                    :jpeg_bytes;
            } else if(is_flate){
                std::string pixels;
                try{pixels=inflate_zlib(raw);}catch(...){pixels=raw;}
                // Check if inflated result is JPEG
                bool inf_jpeg=(pixels.size()>=2&&(unsigned char)pixels[0]==0xFF&&(unsigned char)pixels[1]==0xD8);
                if(inf_jpeg){
                    img.fmt="jpeg";
                    size_t js=std::string::npos;
                    for(size_t k=0;k+1<pixels.size();++k)
                        if((unsigned char)pixels[k]==0xFF&&(unsigned char)pixels[k+1]==0xD8){js=k;break;}
                    img.data=(js!=std::string::npos)?pixels.substr(js):pixels;
                } else if(img.w>0&&img.h>0&&!pixels.empty()){
                    // Raw pixels → encode as PNG
                    img.fmt="png";
                    int expected_rgba=img.w*img.h*4;
                    int ch=3;
                    if((int)pixels.size()==expected_rgba) ch=4;
                    else if((int)pixels.size()==img.w*img.h) ch=1;
                    std::vector<unsigned char> buf;
                    auto wf=[](void* ctx,void* data,int sz){auto* v=(std::vector<unsigned char>*)ctx;v->insert(v->end(),(unsigned char*)data,(unsigned char*)data+sz);};
                    stbi_write_png_to_func(wf,&buf,img.w,img.h,ch,(const unsigned char*)pixels.data(),img.w*ch);
                    img.data=std::string(buf.begin(),buf.end());
                } else {
                    img.fmt="raw"; img.data=pixels;
                }
            } else {
                img.fmt="raw"; img.data=raw;
            }
            if(!img.data.empty()) R.imgs.push_back(std::move(img));

        } else {
            std::string content;
            if(is_flate){try{content=inflate_zlib(raw);}catch(...){content=raw;}}
            else content=raw;

            if(content.size()<10*1024*1024 && content.find("BT")!=std::string::npos){
                std::string pt=extract_text(content,cm);
                if(!pt.empty()){
                    // Cuenta si hay mas paginas con texto y crea otra nueva
                    if(!R.pages.empty()&&R.pages.back().text.size()>100) page_num++;
                    if((int)R.pages.size()<=page_num){PdfPage pg;pg.n=page_num+1;R.pages.push_back(std::move(pg));}
                    R.pages[page_num].text+=pt;
                    R.full+=pt;
                    if(!R.full.empty()&&R.full.back()!='\n') R.full+='\n';
                }
            }
        }
        search=es+9;
    }

    auto clean=[](std::string s){
        std::string o;bool pnl=false;
        for(char c:s){if(c=='\r')continue;if(c=='\n'){if(!pnl&&!o.empty()){o+='\n';pnl=true;}}else{pnl=false;if((unsigned char)c>=32||c=='\t')o+=c;}}
        size_t a=o.find_first_not_of(" \t\n"),b=o.find_last_not_of(" \t\n");
        return(a==std::string::npos)?std::string(""):o.substr(a,b-a+1);
    };
    R.full=clean(R.full);
    for(auto& pg:R.pages) pg.text=clean(pg.text);
    return R;
}

// ─────────────────────────────────────────────────────────────
// parse_pdf_jpg — image-optimized parser (separate dict detection)
// Uses "N M obj" boundary to correctly identify image streams
// with nested dicts like /DecodeParms without contamination.
// ─────────────────────────────────────────────────────────────
static PdfResult parse_pdf_jpg(const std::string& pdf) {
    PdfResult R;
    if (pdf.size() > MAX_PDF_SIZE) return R;

    // CMap extraction skipped for now — large CMaps in real PDFs cause hangs
    // TODO Phase 5: async CMap parsing with timeout
    CMap cm;  // empty CMap — passthrough ASCII

    int page_num=0;
    size_t search=0, stream_count=0;

    while(stream_count++ < MAX_STREAMS) {
        auto sk=pdf.find("stream",search);
        if(sk==std::string::npos) break;

        size_t ds=sk+6;
        if(ds<pdf.size()&&pdf[ds]=='\r')++ds;
        if(ds<pdf.size()&&pdf[ds]=='\n')++ds;

        auto es=pdf.find("endstream",ds);
        if(es==std::string::npos) break;
        if(es<=ds){search=sk+6;continue;} // safety

        size_t de=es;
        if(de>0&&pdf[de-1]=='\n')--de;
        if(de>0&&pdf[de-1]=='\r')--de;
        if(de<=ds){search=es+9;continue;} // empty stream

        std::string raw=pdf.substr(ds,de-ds);

        // Get dict: find the "N M obj" immediately before this stream,
        // then take everything from the first "<<" after it to the stream.
        // This correctly isolates the current object's dict without
        // picking up previous streams or being confused by nested dicts.
        std::string dict;
        {
            // Find last " obj" before sk (within 8KB)
            size_t obj_search_start = (sk > 8192) ? sk - 8192 : 0;
            std::string obj_window = pdf.substr(obj_search_start, sk - obj_search_start);
            size_t obj_pos = obj_window.rfind(" obj");
            if (obj_pos != std::string::npos) {
                // Find first "<<" after "obj"
                size_t dc_rel = obj_window.find("<<", obj_pos + 4);
                if (dc_rel != std::string::npos && dc_rel < obj_window.size()) {
                    // Take from that "<<" to end of window (just before stream)
                    dict = obj_window.substr(dc_rel);
                }
            }
            // Fallback: use last 512 bytes
            if (dict.empty()) {
                size_t fs = (sk > 512) ? sk - 512 : 0;
                dict = pdf.substr(fs, sk - fs);
            }
        }

        bool is_flate=(dict.find("/FlateDecode")!=std::string::npos);
        bool is_dct  =(dict.find("/DCTDecode")  !=std::string::npos);
        bool is_img  =(dict.find("/Subtype /Image")!=std::string::npos||
                       dict.find("/Subtype/Image") !=std::string::npos);

        if(is_img){
            PdfImg img; img.page=page_num;
            auto pi=[&](const std::string& k)->int{
                auto p=dict.find(k);if(p==std::string::npos)return 0;
                p+=k.size();while(p<dict.size()&&(dict[p]==' '||dict[p]=='/'))++p;
                std::string n;while(p<dict.size()&&isdigit((unsigned char)dict[p]))n+=dict[p++];
                try{return n.empty()?0:std::stoi(n);}catch(...){return 0;}
            };
            img.w=pi("/Width");img.h=pi("/Height");

            if(is_dct){
                img.fmt="jpeg";
                // May have FlateDecode wrapper
                std::string jpeg_bytes=raw;
                if(is_flate) try{jpeg_bytes=inflate_zlib(raw);}catch(...){}
                // Trim to JPEG FF D8 ... FF D9
                size_t js=std::string::npos;
                for(size_t k=0;k+1<jpeg_bytes.size();++k)
                    if((unsigned char)jpeg_bytes[k]==0xFF&&(unsigned char)jpeg_bytes[k+1]==0xD8){js=k;break;}
                size_t je=std::string::npos;
                if(js!=std::string::npos)
                    for(size_t k=jpeg_bytes.size()-1;k>js+1;--k)
                        if((unsigned char)jpeg_bytes[k-1]==0xFF&&(unsigned char)jpeg_bytes[k]==0xD9){je=k+1;break;}
                img.data=(js!=std::string::npos)
                    ?(je!=std::string::npos?jpeg_bytes.substr(js,je-js):jpeg_bytes.substr(js))
                    :jpeg_bytes;
            } else if(is_flate){
                std::string pixels;
                try{pixels=inflate_zlib(raw);}catch(...){pixels=raw;}
                // Check if inflated result is JPEG
                bool inf_jpeg=(pixels.size()>=2&&(unsigned char)pixels[0]==0xFF&&(unsigned char)pixels[1]==0xD8);
                if(inf_jpeg){
                    img.fmt="jpeg";
                    size_t js=std::string::npos;
                    for(size_t k=0;k+1<pixels.size();++k)
                        if((unsigned char)pixels[k]==0xFF&&(unsigned char)pixels[k+1]==0xD8){js=k;break;}
                    img.data=(js!=std::string::npos)?pixels.substr(js):pixels;
                } else if(img.w>0&&img.h>0&&!pixels.empty()){
                    // Raw pixels → encode as PNG
                    img.fmt="png";
                    int expected_rgba=img.w*img.h*4;
                    int ch=3;
                    if((int)pixels.size()==expected_rgba) ch=4;
                    else if((int)pixels.size()==img.w*img.h) ch=1;
                    std::vector<unsigned char> buf;
                    auto wf=[](void* ctx,void* data,int sz){auto* v=(std::vector<unsigned char>*)ctx;v->insert(v->end(),(unsigned char*)data,(unsigned char*)data+sz);};
                    stbi_write_png_to_func(wf,&buf,img.w,img.h,ch,(const unsigned char*)pixels.data(),img.w*ch);
                    img.data=std::string(buf.begin(),buf.end());
                } else {
                    img.fmt="raw"; img.data=pixels;
                }
            } else {
                img.fmt="raw"; img.data=raw;
            }
            if(!img.data.empty()) R.imgs.push_back(std::move(img));

        } else {
            std::string content;
            if(is_flate){try{content=inflate_zlib(raw);}catch(...){content=raw;}}
            else content=raw;

            if(content.size()<10*1024*1024 && content.find("BT")!=std::string::npos){
                std::string pt=extract_text(content,cm);
                if(!pt.empty()){
                    // Cuenta si hay mas paginas con texto y crea otra nueva
                    if(!R.pages.empty()&&R.pages.back().text.size()>100) page_num++;
                    if((int)R.pages.size()<=page_num){
                        PdfPage pg; pg.n=page_num+1;
                        R.pages.push_back(std::move(pg));
                    }
                    R.pages[page_num].text+=pt;
                    R.full+=pt;
                    if(!R.full.empty()&&R.full.back()!='\n') R.full+='\n';
                }
            }
        }
        search=es+9;
    }

    auto clean=[](std::string s){
        std::string o;bool pnl=false;
        for(char c:s){if(c=='\r')continue;if(c=='\n'){if(!pnl&&!o.empty()){o+='\n';pnl=true;}}else{pnl=false;if((unsigned char)c>=32||c=='\t')o+=c;}}
        size_t a=o.find_first_not_of(" \t\n"),b=o.find_last_not_of(" \t\n");
        return(a==std::string::npos)?std::string(""):o.substr(a,b-a+1);
    };
    R.full=clean(R.full);
    for(auto& pg:R.pages) pg.text=clean(pg.text);
    return R;
}

// ─────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────
Value read_pdf_file(const std::string& file_data) {
    auto r=parse_pdf(file_data);
    return Value::from(r.full.empty()
        ?std::string("[PDF: no extractable text — may be scanned or encrypted]")
        :r.full);
}

Value parse_pdf_file(const std::string& path, const std::string& what_extract) {
    std::ifstream f(path,std::ios::binary);
    if(!f) return Value::null();
    std::ostringstream ss; ss<<f.rdbuf();
    std::string data = ss.str();

    // Dispatch: "i" = image extraction, anything else = text extraction
    auto r = (what_extract == "i") ? parse_pdf_jpg(data) : parse_pdf(data);

    Value out; out.kind=Value::Kind::Dict;
    out.dict["text"]=Value::from(r.full.empty()?std::string("[no extractable text]"):r.full);
    out.dict["pages"]=Value::from((double)std::max((int)r.pages.size(),r.full.empty()?0:1));

    Value pgs; pgs.kind=Value::Kind::List;
    for(auto& pg:r.pages){
        Value pd; pd.kind=Value::Kind::Dict;
        pd.dict["number"]=Value::from((double)pg.n);
        pd.dict["text"]=Value::from(pg.text);
        pgs.list.push_back(std::move(pd));
    }
    out.dict["pages_content"]=pgs;

    Value imgs; imgs.kind=Value::Kind::List;
    for(size_t i=0;i<r.imgs.size();++i){
        auto& img=r.imgs[i];
        Value id; id.kind=Value::Kind::Dict;
        id.dict["index"] =Value::from((double)i);
        id.dict["format"]=Value::from(img.fmt);
        id.dict["width"] =Value::from((double)img.w);
        id.dict["height"]=Value::from((double)img.h);
        id.dict["page"]  =Value::from((double)img.page+1);
        id.dict["data"]  =Value::from(b64(img.data));
        imgs.list.push_back(std::move(id));
    }
    out.dict["images"]=imgs;
    return out;
}

} // namespace axon
