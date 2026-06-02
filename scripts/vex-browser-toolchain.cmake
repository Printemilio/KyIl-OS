set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_LINKER lld)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(VEXOS_ROOT "$ENV{VEXOS_ROOT}")
if(NOT VEXOS_ROOT)
    get_filename_component(VEXOS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(VEXOS_FORCED_INCLUDES
    "-include" "${VEXOS_ROOT}/kernel/include/unos/vex_web_types.h"
    "-include" "${VEXOS_ROOT}/kernel/include/unos/vex_legacy_compat.h"
)

set(CMAKE_C_FLAGS_INIT "-ffreestanding -fno-builtin")
set(CMAKE_CXX_FLAGS_INIT "-std=c++23 -ffreestanding -fno-builtin -fno-exceptions -fno-rtti")
