cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# 工具链探测（须在 project() 之前）：优先级 HFALGO_TOOLCHAIN_BIN（bin 目录）>
# PATH 中已知前缀。未找到时跳过构建（WARNING）。
# ---------------------------------------------------------------------------
set(HFALGO_KNOWN_PREFIXES riscv32-unknown-elf riscv-none-elf riscv64-unknown-elf riscv-none-embed)
# 裸机交叉编译：不做 host ABI 探测，测试编译产出静态库（避免注入 Windows 链接选项）
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(NOT DEFINED CMAKE_C_COMPILER)
    if(HFALGO_TOOLCHAIN_BIN)
        set(HFALGO_CC "${HFALGO_TOOLCHAIN_BIN}/riscv32-unknown-elf-gcc.exe")
        if(NOT EXISTS "${HFALGO_CC}")
            set(HFALGO_CC "${HFALGO_TOOLCHAIN_BIN}/riscv32-unknown-elf-gcc")
        endif()
        set(CMAKE_C_COMPILER "${HFALGO_CC}")
    else()
        foreach(prefix IN LISTS HFALGO_KNOWN_PREFIXES)
            find_program(HFALGO_FOUND_CC NAMES ${prefix}-gcc)
            if(HFALGO_FOUND_CC)
                set(CMAKE_C_COMPILER "${HFALGO_FOUND_CC}")
                break()
            endif()
        endforeach()
    endif()
endif()

project(HFLinkPack_HPM LANGUAGES C ASM)

if(NOT CMAKE_C_COMPILER MATCHES "riscv")
    message(WARNING "未找到 RISC-V 交叉工具链（riscv32-unknown-elf-gcc 等），跳过算法构建；"
                    "tools/pack.py 将使用仓库内已入库的 bin。")
    return()
endif()
message(STATUS "工具链: ${CMAKE_C_COMPILER}")
execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE HFALGO_GCC_VERSION
                OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "GCC 版本: ${HFALGO_GCC_VERSION}")

# size 体积打印工具：CMAKE_SIZE 在现代 CMake 已不自动设置（为空会让 POST_BUILD
# 直接执行 ELF 本身），从编译器同目录按前缀推导；找不到时跳过（非关键自检）
get_filename_component(HFALGO_CC_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(HFALGO_CC_NAME "${CMAKE_C_COMPILER}" NAME_WE)
string(REGEX REPLACE "-gcc$" "" HFALGO_BINUTILS_PREFIX "${HFALGO_CC_NAME}")
find_program(HFALGO_SIZE_TOOL NAMES "${HFALGO_BINUTILS_PREFIX}-size" HINTS "${HFALGO_CC_DIR}")

# ---------------------------------------------------------------------------
# SoC 清单（单一来源 soc/socs.json；tools/pack.py 读同一文件）
# ---------------------------------------------------------------------------
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/soc/socs.json HFALGO_SOCS_JSON)
string(JSON HFALGO_SOCS GET ${HFALGO_SOCS_JSON} socs)
string(JSON HFALGO_SOC_COUNT LENGTH ${HFALGO_SOCS})
string(JSON HFALGO_PAGE_SIZE GET ${HFALGO_SOCS_JSON} page_size)
string(JSON HFALGO_SECTOR_SIZE GET ${HFALGO_SOCS_JSON} sector_size)
string(JSON HFALGO_FLASH_SIZE GET ${HFALGO_SOCS_JSON} flash_size)
message(STATUS "SoC 数: ${HFALGO_SOC_COUNT}（按 XPI 控制器分组聚合构建）")

set(HFALGO_ARCH_FLAGS -march=rv32imac_zicsr_zifencei -mabi=ilp32 -mno-relax)
set(HFALGO_C_FLAGS -Os -ffreestanding -fno-builtin -msmall-data-limit=0
    -ffunction-sections -fdata-sections -Wall -Wextra)

# SEGGER 语义：一个系列共用一个算法。socs.json 的 group 字段按 (xpi0_base,
# xpi1_base) 元组聚合（同窗口基址在不同族映射不同 XPI 控制器，运行时不可推导，
# 只能按组链接）；组内首个 SoC 兼任头目录代表（类型定义组内一致）。
set(HFALGO_GROUPS "")
math(EXPR HFALGO_LAST_INDEX "${HFALGO_SOC_COUNT} - 1")
foreach(index RANGE ${HFALGO_LAST_INDEX})
    string(JSON HFALGO_GROUP GET ${HFALGO_SOCS} ${index} group)
    string(JSON HFALGO_SOC GET ${HFALGO_SOCS} ${index} soc)
    string(JSON HFALGO_XPI0 GET ${HFALGO_SOCS} ${index} xpi0_base)
    string(JSON HFALGO_XPI1_TYPE TYPE ${HFALGO_SOCS} ${index} xpi1_base)
    if(NOT "${HFALGO_GROUP}" IN_LIST HFALGO_GROUPS)
        list(APPEND HFALGO_GROUPS ${HFALGO_GROUP})

        set(HFALGO_DEFINES HF_XPI0_BASE=${HFALGO_XPI0}
                           HFALGO_PAGE_SIZE=${HFALGO_PAGE_SIZE}
                           HFALGO_SECTOR_SIZE=${HFALGO_SECTOR_SIZE}
                           HFALGO_FLASH_SIZE=${HFALGO_FLASH_SIZE})
        if(NOT HFALGO_XPI1_TYPE STREQUAL "NULL")
            string(JSON HFALGO_XPI1 GET ${HFALGO_SOCS} ${index} xpi1_base)
            list(APPEND HFALGO_DEFINES HF_XPI1_BASE=${HFALGO_XPI1})
        endif()

        add_executable(algo_${HFALGO_GROUP}
            algo/hf_flash_algo.c
        )
        set_target_properties(algo_${HFALGO_GROUP} PROPERTIES
            OUTPUT_NAME ${HFALGO_GROUP}
            SUFFIX ".elf"
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out
        )
        target_include_directories(algo_${HFALGO_GROUP} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/algo
            ${CMAKE_CURRENT_SOURCE_DIR}/hpm_headers/common
            ${CMAKE_CURRENT_SOURCE_DIR}/hpm_headers/${HFALGO_SOC}
        )
        target_compile_definitions(algo_${HFALGO_GROUP} PRIVATE ${HFALGO_DEFINES})
        target_compile_options(algo_${HFALGO_GROUP} PRIVATE ${HFALGO_ARCH_FLAGS} ${HFALGO_C_FLAGS})
        target_link_options(algo_${HFALGO_GROUP} PRIVATE
            ${HFALGO_ARCH_FLAGS}
            -nostartfiles
            -Wl,--gc-sections
            -T ${CMAKE_CURRENT_SOURCE_DIR}/algo/hf_flash_algo.ld
            -Wl,-Map=${CMAKE_BINARY_DIR}/out/${HFALGO_GROUP}.map
        )

        # 产物即 ELF（FLM 同容器）：几何/入口经符号表自描述，不做 objcopy 裸化
        if(HFALGO_SIZE_TOOL)
            add_custom_command(TARGET algo_${HFALGO_GROUP} POST_BUILD
                COMMAND ${HFALGO_SIZE_TOOL} $<TARGET_FILE:algo_${HFALGO_GROUP}>
                COMMENT "size ${HFALGO_GROUP}.elf"
            )
        endif()
    endif()
endforeach()
message(STATUS "算法分组: ${HFALGO_GROUPS}")
