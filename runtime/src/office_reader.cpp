/**
 * runtime/src/office_reader.cpp
 * ─────────────────────────────
 * Reads DOCX and XLSX files natively.
 * DOCX/XLSX are ZIP archives containing XML files.
 * Uses miniz for ZIP extraction + our own minimal XML text extractor.
 *
 * Phase 4B: read-only (text extraction)
 * Phase 5+: write support
 */
#include "../include/document_io.h"
#include "../include/interpreter.h"

// ZIP reading via our miniz.h (uses zlib internally)
#include "../../third_party/miniz/miniz.h"

#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace axon {

// ─────────────────────────────────────────────────────────────
// ZIP helper: read a specific file from a ZIP archive in memory
// ─────────────────────────────────────────────────────────────
static std::string zip_read_file(const void* zip_data, size_t zip_size,
                                  const char* filename) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zip_data, zip_size, 0))
        return "";

    int idx = mz_zip_reader_locate_file(&zip, filename, nullptr, 0);
    if (idx < 0) {
        mz_zip_reader_end(&zip);
        return "";
    }

    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&zip, (mz_uint)idx, &stat)) {
        mz_zip_reader_end(&zip);
        return "";
    }

    std::string out(stat.m_uncomp_size, '\0');
    mz_zip_reader_extract_to_mem(&zip, (mz_uint)idx,
                                  &out[0], out.size(), 0);
    mz_zip_reader_end(&zip);
    return out;
}

// ─────────────────────────────────────────────────────────────
// DOCX reader
// Structure: word/document.xml contains all text
// ─────────────────────────────────────────────────────────────
static std::string read_docx(const std::string& file_data) {
    const void* data = file_data.data();
    size_t      size = file_data.size();

    // Main document content
    std::string doc_xml = zip_read_file(data, size, "word/document.xml");
    if (doc_xml.empty()) return "";

    // Extract text from <w:t> tags (Word text runs)
    std::string text;
    size_t pos = 0;
    while (pos < doc_xml.size()) {
        auto start = doc_xml.find("<w:t", pos);
        if (start == std::string::npos) break;

        // Check for paragraph end before this run
        auto para = doc_xml.rfind("<w:p", start);
        auto prev_t = doc_xml.rfind("<w:t", start - 1);
        if (para != std::string::npos &&
            (prev_t == std::string::npos || para > prev_t)) {
            if (!text.empty() && text.back() != '\n')
                text += '\n';
        }

        // Find closing >
        auto tag_end = doc_xml.find('>', start);
        if (tag_end == std::string::npos) break;

        // Check xml:space="preserve" for spaces
        std::string tag = doc_xml.substr(start, tag_end - start);

        auto close = doc_xml.find("</w:t>", tag_end);
        if (close == std::string::npos) { pos = tag_end + 1; continue; }

        std::string run = doc_xml.substr(tag_end + 1, close - tag_end - 1);

        // Decode entities
        std::string decoded;
        for (size_t i = 0; i < run.size(); ) {
            if (run[i] == '&') {
                if (run.substr(i,4)=="&lt;")        { decoded+='<'; i+=4; }
                else if (run.substr(i,4)=="&gt;")   { decoded+='>'; i+=4; }
                else if (run.substr(i,5)=="&amp;")  { decoded+='&'; i+=5; }
                else if (run.substr(i,6)=="&quot;") { decoded+='"'; i+=6; }
                else { decoded += run[i++]; }
            } else {
                decoded += run[i++];
            }
        }
        text += decoded;
        pos = close + 6;
    }

    return text;
}

// ─────────────────────────────────────────────────────────────
// XLSX reader → returns Value (List of Lists or List of Dicts)
// Structure:
//   xl/sharedStrings.xml  — string table
//   xl/worksheets/sheet1.xml — cell data
// ─────────────────────────────────────────────────────────────
static std::vector<std::string> parse_shared_strings(const std::string& xml) {
    std::vector<std::string> strings;
    size_t pos = 0;
    while (pos < xml.size()) {
        auto si = xml.find("<si>", pos);
        if (si == std::string::npos) break;
        auto si_end = xml.find("</si>", si);
        if (si_end == std::string::npos) break;

        std::string si_block = xml.substr(si + 4, si_end - si - 4);
        // Extract all <t> text values and concatenate
        std::string val;
        size_t tp = 0;
        while (tp < si_block.size()) {
            auto t = si_block.find("<t", tp);
            if (t == std::string::npos) break;
            auto te = si_block.find('>', t);
            if (te == std::string::npos) break;
            auto tc = si_block.find("</t>", te);
            if (tc == std::string::npos) { tp = te + 1; continue; }
            val += si_block.substr(te + 1, tc - te - 1);
            tp = tc + 4;
        }
        strings.push_back(val);
        pos = si_end + 5;
    }
    return strings;
}

Value read_xlsx(const std::string& file_data) {
    Value result; result.kind = Value::Kind::List;

    const void* data = file_data.data();
    size_t      size = file_data.size();

    // Load shared strings
    std::string ss_xml = zip_read_file(data, size,
                                        "xl/sharedStrings.xml");
    auto shared = parse_shared_strings(ss_xml);

    // Load first sheet
    std::string sheet_xml = zip_read_file(data, size,
                                           "xl/worksheets/sheet1.xml");
    if (sheet_xml.empty()) return result;

    // Parse rows and cells
    std::vector<std::string> headers;
    bool first_row = true;

    size_t pos = 0;
    while (pos < sheet_xml.size()) {
        auto row_start = sheet_xml.find("<row ", pos);
        if (row_start == std::string::npos) break;
        auto row_end = sheet_xml.find("</row>", row_start);
        if (row_end == std::string::npos) break;

        std::string row_xml = sheet_xml.substr(row_start,
                                               row_end - row_start);

        std::vector<std::string> row_vals;
        size_t cp = 0;
        while (cp < row_xml.size()) {
            auto c_start = row_xml.find("<c ", cp);
            if (c_start == std::string::npos) break;
            auto c_end   = row_xml.find("</c>", c_start);
            if (c_end   == std::string::npos) break;

            std::string cell = row_xml.substr(c_start, c_end - c_start);

            // Cell type: t="s" = shared string, t="b" = bool, else number
            bool is_shared = (cell.find("t=\"s\"") != std::string::npos);

            // Get value inside <v>
            std::string val;
            auto v_start = cell.find("<v>");
            auto v_end   = cell.find("</v>");
            if (v_start != std::string::npos && v_end != std::string::npos)
                val = cell.substr(v_start + 3, v_end - v_start - 3);

            // Also check inline string <is><t>
            if (val.empty()) {
                auto is_pos = cell.find("<is>");
                auto t_pos  = cell.find("<t>", is_pos);
                auto t_end  = cell.find("</t>", t_pos);
                if (t_pos != std::string::npos && t_end != std::string::npos)
                    val = cell.substr(t_pos + 3, t_end - t_pos - 3);
            }

            if (is_shared && !val.empty()) {
                size_t idx = (size_t)std::stoul(val);
                val = (idx < shared.size()) ? shared[idx] : val;
            }

            row_vals.push_back(val);
            cp = c_end + 4;
        }

        if (first_row) {
            first_row = false;
            headers = row_vals;
        } else if (!row_vals.empty()) {
            Value row; row.kind = Value::Kind::Dict;
            for (size_t i = 0; i < row_vals.size(); ++i) {
                std::string key = (i < headers.size() && !headers[i].empty())
                                  ? headers[i]
                                  : "col" + std::to_string(i);
                const std::string& v = row_vals[i];
                // Try numeric
                bool is_num = false;
                if (!v.empty()) {
                    try {
                        size_t np;
                        double n = std::stod(v, &np);
                        if (np == v.size()) {
                            row.dict[key] = Value::from(n);
                            is_num = true;
                        }
                    } catch (...) {}
                }
                if (!is_num) row.dict[key] = Value::from(v);
            }
            result.list.push_back(std::move(row));
        }

        pos = row_end + 6;
    }

    return result;
}

// ─────────────────────────────────────────────────────────────
// Public API — called from interpreter.cpp
// ─────────────────────────────────────────────────────────────
Value read_docx_file(const std::string& file_data) {
    std::string text = read_docx(file_data);
    return Value::from(text);
}

Value read_xlsx_file(const std::string& file_data) {
    return read_xlsx(file_data);
}

} // namespace axon
