#pragma once
/**
 * runtime/include/document_io.h
 */
#include <string>
#include <vector>
#include <unordered_map>
#include "interpreter.h"

namespace axon {

// ── CSV ──────────────────────────────────────────────────────
Value      csv_parse     (const std::string& text, char delimiter=',', bool has_header=true);
std::string csv_stringify (const Value& data, char delimiter=',');

// ── JSON ─────────────────────────────────────────────────────
Value      json_parse    (const std::string& text);
std::string json_stringify(const Value& v, int indent=0);

// ── File type detection ───────────────────────────────────────
enum class FileType { Text, CSV, JSON, PDF, DOCX, XLSX, Image, Audio, Unknown };
FileType detect_file_type(const std::string& path);

// ── Document readers ─────────────────────────────────────────
Value read_pdf_file  (const std::string& file_data);
Value parse_pdf_file (const std::string& path, const std::string& what_extract = "t");
Value read_docx_file (const std::string& file_data);
Value read_xlsx_file (const std::string& file_data);

} // namespace axon
