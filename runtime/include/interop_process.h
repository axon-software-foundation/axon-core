#pragma once
#include "interfaces.hpp"
#include <string>
#include <ostream>
namespace axon {
// External processes; no CPython embedding or automatic dependency installation.
AxonValue run_python_block(const std::string& code, const AxonValue& inputs,
    const std::string& source, int line, const ExecutionControl& control,
    std::ostream& output, std::ostream& diagnostic);
AxonValue run_shell_block(const std::string& code, const ExecutionControl& control,
    std::ostream& output, std::ostream& diagnostic);
}
