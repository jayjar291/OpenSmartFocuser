
include(CheckCCompilerFlag)

#IF (NOT ${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")
    #SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
    #SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
#ENDIF ()

# Build Position Independent Code
# Position-independent objects remain safe if this executable is later reused
# in a shared-library or plugin build.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# C++17 Support
# Keep the standard requirement for all non-Android builds; Android toolchains
# may supply their own language-standard configuration.
if (NOT ANDROID)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif(NOT ANDROID)

# Ccache support
# Only search for ccache on Unix-like platforms; activation remains opt-in via
# the CCACHE_SUPPORT cache setting.
IF (ANDROID OR UNIX OR APPLE)
    FIND_PROGRAM(CCACHE_FOUND ccache)
    SET(CCACHE_SUPPORT OFF CACHE BOOL "Enable ccache support")
    # Install ccache as the compile/link launcher only when both available and
    # explicitly enabled (Android is allowed even without a discovered binary).
    IF ((CCACHE_FOUND OR ANDROID) AND CCACHE_SUPPORT MATCHES ON)
        SET_PROPERTY(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
        SET_PROPERTY(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
    ENDIF ()
ENDIF ()

# Add security (hardening flags)
# Restrict compiler/linker hardening to Unix-like toolchains that understand
# these GNU/Clang options.
IF (UNIX OR APPLE OR ANDROID)
    # Older compilers are predefining _FORTIFY_SOURCE, so defining it causes a
    # warning, which is then considered an error. Second issue is that for
    # these compilers, _FORTIFY_SOURCE must be used while optimizing, else
    # causes a warning, which also results in an error. And finally, CMake is
    # not using optimization when testing for libraries, hence breaking the build.
    CHECK_C_COMPILER_FLAG("-Werror -D_FORTIFY_SOURCE=2" COMPATIBLE_FORTIFY_SOURCE)
    # Enable fortification only when the active compiler accepts the probe.
    IF (${COMPATIBLE_FORTIFY_SOURCE})
        SET(SEC_COMP_FLAGS "-D_FORTIFY_SOURCE=2")
    ENDIF ()
    # Make sure to add optimization flag. Some systems require this for _FORTIFY_SOURCE.
    # Supply minimal optimization only for custom build types or Debug when
    # fortification is enabled; release-like builds already optimize.
    IF (NOT CMAKE_BUILD_TYPE MATCHES "MinSizeRel" AND NOT CMAKE_BUILD_TYPE MATCHES "Release" AND NOT CMAKE_BUILD_TYPE MATCHES "Debug")
        # For custom/unknown build types, add -O1 if FORTIFY_SOURCE is active
        IF(${COMPATIBLE_FORTIFY_SOURCE})
            SET(SEC_COMP_FLAGS "${SEC_COMP_FLAGS} -O1")
        ENDIF()
    ELSEIF (COMPATIBLE_FORTIFY_SOURCE AND CMAKE_BUILD_TYPE MATCHES "Debug")
        # If FORTIFY_SOURCE is active in Debug mode, add minimal optimization (-O1) to satisfy it
        SET(SEC_COMP_FLAGS "${SEC_COMP_FLAGS} -O1")
    ENDIF ()
    IF (NOT ANDROID AND NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" AND NOT APPLE AND NOT CYGWIN)
        # GNU-compatible assemblers support this stack protection flag; avoid
        # passing it through unsupported toolchains.
        SET(SEC_COMP_FLAGS "${SEC_COMP_FLAGS} -Wa,--noexecstack")
    ENDIF ()
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SEC_COMP_FLAGS}")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SEC_COMP_FLAGS}")
    SET(SEC_LINK_FLAGS "")
    IF (NOT APPLE AND NOT CYGWIN AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD|OpenBSD")
        # Add ELF linker hardening except on platforms with incompatible flags.
        SET(SEC_LINK_FLAGS "${SEC_LINK_FLAGS} -Wl,-z,nodump -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now")
    ENDIF ()
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SEC_LINK_FLAGS}")
    SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${SEC_LINK_FLAGS}")
ENDIF ()

# Warning, debug and linker flags
# Configure project diagnostics only on the Unix/Apple compiler families
# supported by these options.
SET(FIX_WARNINGS OFF CACHE BOOL "Enable strict compilation mode to turn compiler warnings to errors")
IF (UNIX OR APPLE)
    SET(COMP_FLAGS "")
    SET(LINKER_FLAGS "")
    # Verbose warnings and turns all to errors
    SET(COMP_FLAGS "${COMP_FLAGS} -Wall -Wextra")
    # Treat warnings as errors only when maintainers opt in through CMake.
    IF (FIX_WARNINGS)
        SET(COMP_FLAGS "${COMP_FLAGS} -Werror")
    ENDIF ()
    # Omit problematic warnings
    IF ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 6.9.9)
        # Suppress a GCC diagnostic that is noisy for format-truncation checks.
        SET(COMP_FLAGS "${COMP_FLAGS} -Wno-format-truncation")
    ENDIF ()
    IF ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
        # AppleClang reports compatibility/deprecation warnings from external
        # APIs; suppress only those known toolchain-specific diagnostics.
        SET(COMP_FLAGS "${COMP_FLAGS} -Wno-nonnull -Wno-deprecated-declarations")
        IF (FIX_WARNINGS)
            SET(COMP_FLAGS "${COMP_FLAGS} -Wno-unused-parameter")
        ENDIF ()
    ENDIF ()

    # Minimal debug info with Clang
    IF ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        # Clang emits compact line tables; other compilers receive full symbols.
        SET(COMP_FLAGS "${COMP_FLAGS} -gline-tables-only")
    ELSE ()
        SET(COMP_FLAGS "${COMP_FLAGS} -g")
    ENDIF ()

    # Note: The following flags are problematic on older systems with gcc 4.8
    IF ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 4.9.9))
        # Optional gold support is limited to compiler versions able to emit
        # the required sections and linker options.
        IF ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
            SET(COMP_FLAGS "${COMP_FLAGS} -Wno-unused-command-line-argument")
        ENDIF ()
        FIND_PROGRAM(LDGOLD_FOUND ld.gold)
        SET(LDGOLD_SUPPORT OFF CACHE BOOL "Enable ld.gold support")
        # Optional ld.gold is 2x faster than normal ld
        # Keep the default linker unless gold is found, enabled, and supported
        # by the target platform/architecture.
        IF (LDGOLD_FOUND AND LDGOLD_SUPPORT MATCHES ON AND NOT APPLE AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES arm)
            SET(LINKER_FLAGS "${LINKER_FLAGS} -fuse-ld=gold")
            # Use Identical Code Folding
            SET(COMP_FLAGS "${COMP_FLAGS} -ffunction-sections")
            SET(LINKER_FLAGS "${LINKER_FLAGS} -Wl,--icf=safe")
            # Compress the debug sections
            # Note: Before valgrind 3.12.0, patch should be applied for valgrind (https://bugs.kde.org/show_bug.cgi?id=303877)
            IF (NOT APPLE AND NOT ANDROID AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES arm AND NOT CMAKE_CXX_CLANG_TIDY)
                # Compress debug sections only on compatible non-ARM targets
                # when no clang-tidy analysis is being run.
                SET(COMP_FLAGS "${COMP_FLAGS} -Wa,--compress-debug-sections")
                SET(LINKER_FLAGS "${LINKER_FLAGS} -Wl,--compress-debug-sections=zlib")
            ENDIF ()
        ENDIF ()
    ENDIF ()

    # Apply the flags
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMP_FLAGS}")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMP_FLAGS}")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LINKER_FLAGS}")
    SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${LINKER_FLAGS}")
ENDIF ()

# Sanitizer support
# Sanitizers are opt-in and accepted only for Clang-family Unix builds.
SET(CLANG_SANITIZERS OFF CACHE BOOL "Clang's sanitizer support")
IF (CLANG_SANITIZERS AND
    ((UNIX AND "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang") OR (APPLE AND "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")))
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,undefined -fno-omit-frame-pointer")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined -fno-omit-frame-pointer")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined -fno-omit-frame-pointer")
    SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address,undefined -fno-omit-frame-pointer")
ENDIF ()

# Unity Build support
# Define optional unity-build helpers; this driver does not currently enable
# unity compilation for its two translation units.
include(UnityBuild)
