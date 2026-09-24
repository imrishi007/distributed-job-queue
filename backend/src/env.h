#pragma once

#include <cstdlib>
#include <string>

// Reads an environment variable; falls back when unset or empty. Keeps the
// bare-metal defaults as the fallbacks so the same binaries run on a host or
// in a container, configured purely through DJQ_* variables.
inline std::string env_or(const char* name, std::string fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : std::move(fallback);
}