#pragma once
/**
 * runtime/include/modules.h
 * image->, codec->, hash-> modules
 */
#include "interpreter.h"
#include <string>
#include <vector>

namespace axon {

std::string modules_b64_encode(const std::string& data);
std::string modules_b64_decode(const std::string& enc);

#include <functional>
Value eval_modules(const std::string& mod, const std::string& fn,
                   const std::vector<Value>& args,
                   const std::function<std::string(const Value&)>& to_str);

} // namespace axon
