# Writes kz_version.h from the repository's VERSION and the patched toolkit's .patchset, for main.cpp (KZ1HD
# --version and the first line of the log) and app.rc (the version block of KZ1HD.exe). The first two macros are
# the ones tools/release/build_launcher.py writes for the launcher, from the same VERSION (tools/tests/
# test_exe_resources.py checks they agree). No version is written by hand anywhere else.
#   include(version.cmake) from CMakeLists.txt, or
#   cmake -DKZ_VERSION_FILE=<VERSION> -DKZ_PATCHSET_FILE=<.patchset> -DKZ_VERSION_OUT=<kz_version.h> -P version.cmake
# Out: KZ_VERSION (X.Y.Z). A missing .patchset (a toolkit not made by tools/ps3recomp-patched.sh) says "unknown".
file(STRINGS "${KZ_VERSION_FILE}" KZ_VERSION LIMIT_COUNT 1)
string(STRIP "${KZ_VERSION}" KZ_VERSION)
if(NOT KZ_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "${KZ_VERSION_FILE} does not hold MAJOR.MINOR.PATCH: '${KZ_VERSION}'")
endif()
string(REPLACE "." "," _kz_comma "${KZ_VERSION}")
set(_kz_patchset "unknown")
set(_kz_pin "unknown")
if(EXISTS "${KZ_PATCHSET_FILE}")
    # tools/kzbuild.py patchset_id: <sha256 of the series and patches, 16 hex>-<ps3recomp's pinned commit, short>
    file(STRINGS "${KZ_PATCHSET_FILE}" _kz_id LIMIT_COUNT 1)
    if(_kz_id MATCHES "^([0-9a-f]+)-(.+)$")
        set(_kz_patchset "${CMAKE_MATCH_1}")
        set(_kz_pin "${CMAKE_MATCH_2}")
    endif()
endif()
file(CONFIGURE OUTPUT "${KZ_VERSION_OUT}" @ONLY NEWLINE_STYLE UNIX CONTENT
"#define VEKTA_VER_COMMA ${_kz_comma},0
#define VEKTA_VER_STR \"${KZ_VERSION}\"
#define KZ_PATCHSET \"${_kz_patchset}\"
#define KZ_PS3RECOMP \"${_kz_pin}\"
#define KZ_VERSION_LINE \"Project Vekta ${KZ_VERSION} (patch set ${_kz_patchset}, ps3recomp ${_kz_pin})\"
")
