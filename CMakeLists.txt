cmake_minimum_required(VERSION 3.15)
project(uwpXrayDepot NONE)

set(XRAY_BASE "${CMAKE_CURRENT_LIST_DIR}/x64")

# ── spdlog (header-only, via submodule) ──
add_library(spdlog INTERFACE IMPORTED GLOBAL)
set_target_properties(spdlog PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_LIST_DIR}/external/spdlog/include"
)

# ── nlohmann_json (header-only, via submodule) ──
add_library(nlohmann_json INTERFACE IMPORTED GLOBAL)
set_target_properties(nlohmann_json PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_LIST_DIR}/external/json/single_include"
)

# ── Lua 5.4 (static lib, prebuilt no depot) ──
add_library(lua5.4 STATIC IMPORTED GLOBAL)
set_target_properties(lua5.4 PROPERTIES
    IMPORTED_LOCATION "${XRAY_BASE}/lib/lua5.4.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${XRAY_BASE}/include"
)

# ── xray-sock (static lib, prebuilt no depot) ──
add_library(xray-sock STATIC IMPORTED GLOBAL)
set_target_properties(xray-sock PROPERTIES
    IMPORTED_LOCATION "${XRAY_BASE}/lib/xray-sock.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${XRAY_BASE}/include"
)

# ── xb-inspector (interface: headers + agrega dependencias) ──
add_library(xb-inspector INTERFACE IMPORTED GLOBAL)
set_target_properties(xb-inspector PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${XRAY_BASE}/include"
    INTERFACE_LINK_LIBRARIES "spdlog;nlohmann_json;lua5.4;xray-sock"
)
