#pragma once
#include "interfaces.hpp"
#include "execution_control.h"
#include <memory>
#include <cstdint>

namespace axon {
// A registry belongs to one interpreter family; parallel branches share it.
class SqlRuntime {
public:
    SqlRuntime();
    ~SqlRuntime();
    SqlRuntime(const SqlRuntime&)=delete;
    SqlRuntime& operator=(const SqlRuntime&)=delete;
    AxonValue call(const std::string& name,const std::vector<AxonValue>& args,
                   std::shared_ptr<const ExecutionControl> control,uintptr_t owner);
    void release_owner(uintptr_t owner) noexcept;
    // Pure schema validation: never opens a database or reserves a connection name.
    static void validate_config(const AxonValue& config,bool explicit_fields=false);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace axon
