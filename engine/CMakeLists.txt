# ezMU Engine (layers L0–L3). Add modules lower layers first.

# L0 — Foundation
add_subdirectory(foundation)

# L1 — RHI / Platform
add_subdirectory(platform)
add_subdirectory(rhi)

# L2 — Services
add_subdirectory(renderer)

# --- Aggregate ---------------------------------------------------------------------------------
# ezmu::engine bundles every Engine module for executables and external consumers. It is not a
# module: engine and framework modules depend on the specific modules they use.
ezmu_add_aggregate(
    NAME engine
    LAYER 3
    MODULES
        ezmu::foundation
        ezmu::platform
        ezmu::rhi
        ezmu::renderer)
