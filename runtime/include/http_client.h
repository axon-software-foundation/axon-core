#pragma once
#include "../interfaces/interfaces.hpp"

namespace axon {
// A separate public HTTP module; does not change the specialized Ollama client.
AxonValue http_module_call(const std::string& name, const std::vector<AxonValue>& args,
                          std::shared_ptr<const ExecutionControl> control);
}
