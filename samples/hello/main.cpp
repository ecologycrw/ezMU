#include <ezmu/foundation/version.hpp>

#include <cstdio>
#include <exception>
#include <print>

int main() {
    try {
        std::println("{} {}", ezmu::foundation::kEngineName,
                     ezmu::foundation::kEngineVersionString);
    } catch (const std::exception& e) {
        std::fputs(e.what(), stderr);
        return 1;
    }
    return 0;
}
