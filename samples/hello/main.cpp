#include <ezmu/foundation/version.hpp>

#include <print>

int main() {
    std::println("{} {}", ezmu::foundation::kEngineName, ezmu::foundation::kEngineVersionString);
    return 0;
}
