cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Platform selection
# Pass -DPLATFORM_PSV=ON, -DPLATFORM_SWITCH=ON, or -DPLATFORM_DESKTOP=ON
# to cmake when configuring. Exactly one must be ON.
# Example: cmake -B build -DPLATFORM_DESKTOP=ON
# ---------------------------------------------------------------------------

option(PLATFORM_PSV     "Build for PlayStation Vita"                   OFF)
option(PLATFORM_SWITCH  "Build for Nintendo Switch"                    OFF)
option(PLATFORM_PS4     "Build for PlayStation 4"                      OFF)
option(PLATFORM_DESKTOP "Build for Desktop (Linux/macOS/Windows)"      OFF)
option(PLATFORM_ANDROID "Build for Android"                            OFF)

# Default to desktop when nothing is specified
set(_PLATFORM_COUNT 0)
foreach(_P PSV SWITCH PS4 DESKTOP ANDROID)
    if(PLATFORM_${_P})
        math(EXPR _PLATFORM_COUNT "${_PLATFORM_COUNT} + 1")
    endif()
endforeach()
if(_PLATFORM_COUNT EQUAL 0)
    message(STATUS "No platform selected — defaulting to PLATFORM_DESKTOP=ON")
    set(PLATFORM_DESKTOP ON CACHE BOOL "Build for Desktop" FORCE)
elseif(_PLATFORM_COUNT GREATER 1)
    message(FATAL_ERROR "More than one PLATFORM_* flag is ON. Set exactly one.")
endif()

# ---------------------------------------------------------------------------
# Borealis paths — must be set before the cmake includes below
# ---------------------------------------------------------------------------
set(BOREALIS_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

# Forward into borealis-internal cache variables
set(PLATFORM_DESKTOP ${PLATFORM_DESKTOP} CACHE BOOL "" FORCE)
set(PLATFORM_SWITCH  ${PLATFORM_SWITCH}  CACHE BOOL "" FORCE)
set(PLATFORM_PSV     ${PLATFORM_PSV}     CACHE BOOL "" FORCE)
set(PLATFORM_PS4     ${PLATFORM_PS4}     CACHE BOOL "" FORCE)
set(PLATFORM_IOS     OFF                 CACHE BOOL "" FORCE)
set(PLATFORM_ANDROID ${PLATFORM_ANDROID} CACHE BOOL "" FORCE)

if(PLATFORM_PSV)
    set(BOREALIS_USE_GXM ON  CACHE BOOL "Use GXM renderer for PS Vita" FORCE)
    set(USE_VITA_SHARK  OFF  CACHE BOOL "Use VitaShark for shader compilation" FORCE)
endif()

set(USE_LIBROMFS OFF CACHE BOOL "Use libromfs")

# For Android: point LIBROMFS_PREBUILT_GENERATOR at an existing file so that
# borealis toolchain.cmake's check_libromfs_generator() does not fatal.
# We force USE_LIBROMFS OFF below, so the generator is never actually invoked.
if(PLATFORM_ANDROID)
    set(LIBROMFS_PREBUILT_GENERATOR "${CMAKE_CURRENT_LIST_FILE}" CACHE STRING "" FORCE)
endif()

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# FIX: borealis toolchain.cmake forces USE_LIBROMFS=ON for Android, but we use
# APK-asset extraction instead (see src/platform/android_assets.cpp).  Override
# it back to OFF so borealis does not try to compile/link libromfs.
# Also set BRLS_PLATFORM_RESOURCES_PATH which borealis CMakeLists.txt leaves
# empty for Android — without it BRLS_RESOURCES is undefined and all file loads
# via fopen("resources/...") fail.
if(PLATFORM_ANDROID)
    set(USE_LIBROMFS OFF)
    set(BRLS_PLATFORM_RESOURCES_PATH "\"resources/\"")
endif()

# ---------------------------------------------------------------------------
# Project (after toolchain so cross-compilers are in effect)
# Pass -DAPP_VERSION=X.Y.Z from CI to override the default.
# ---------------------------------------------------------------------------
if(NOT DEFINED APP_VERSION OR APP_VERSION STREQUAL "")
    # Local-dev fallback ONLY: CI always passes -DAPP_VERSION. Indirect
    # builders (Gradle, flatpak-builder) forward it too — Gradle via project
    # properties, Flatpak by stamping this file before the build.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" APP_VERSION)
        string(STRIP "${APP_VERSION}" APP_VERSION)
    else()
        set(APP_VERSION "0.0.0")
    endif()
endif()
# The human-readable version shown in Settings and the updater UI. CI passes
# the labeled form (e.g. "Beta 2.0.0"); it defaults to APP_VERSION so local
# builds still show something sensible.
if(NOT DEFINED APP_DISPLAY_VERSION OR APP_DISPLAY_VERSION STREQUAL "")
    set(APP_DISPLAY_VERSION "${APP_VERSION}")
endif()
string(REGEX REPLACE "^v" "" APP_VERSION "${APP_VERSION}")
# project(VERSION) only accepts numeric X[.Y[.Z[.W]]] — extract it from
# whatever APP_VERSION carries (labels, build suffixes).
string(REGEX MATCH "[0-9]+(\\.[0-9]+)*" APP_NUMERIC_VERSION "${APP_VERSION}")
if(NOT APP_NUMERIC_VERSION)
    set(APP_NUMERIC_VERSION "0.0.0")
endif()
project(VitaABS VERSION ${APP_NUMERIC_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -frtti -fexceptions")

# XX.YY version string for Vita/PS4 param.sfo
if(PROJECT_VERSION_MAJOR LESS 10)
    set(_SFO_MAJOR "0${PROJECT_VERSION_MAJOR}")
else()
    set(_SFO_MAJOR "${PROJECT_VERSION_MAJOR}")
endif()
if(PROJECT_VERSION_MINOR LESS 10)
    set(_SFO_MINOR "0${PROJECT_VERSION_MINOR}")
else()
    set(_SFO_MINOR "${PROJECT_VERSION_MINOR}")
endif()
set(APP_SFO_VERSION "${_SFO_MAJOR}.${_SFO_MINOR}")

# ---------------------------------------------------------------------------
# Vita VPK metadata
# ---------------------------------------------------------------------------
if(PLATFORM_PSV)
    set(VITA_APP_NAME "VitaABS")
    set(VITA_TITLEID  "VABS00001")
    set(VITA_VERSION  "${APP_SFO_VERSION}")
    set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1 -d ATTRIBUTE2=12")
endif()

# ---------------------------------------------------------------------------
# PSV borealis patch
# ---------------------------------------------------------------------------
if(PLATFORM_PSV)
    set(PSV_PLATFORM_PATCH  ${CMAKE_CURRENT_SOURCE_DIR}/patches/psv_platform.cpp)
    set(PSV_PLATFORM_TARGET ${BOREALIS_LIBRARY}/lib/platforms/psv/psv_platform.cpp)
    if(EXISTS ${PSV_PLATFORM_PATCH})
        message(STATUS "Applying patch: psv_platform.cpp")
        configure_file(${PSV_PLATFORM_PATCH} ${PSV_PLATFORM_TARGET} COPYONLY)
    else()
        message(FATAL_ERROR "Patch file not found: ${PSV_PLATFORM_PATCH}")
    endif()
endif()

# Borealis fontstash patch (all platforms): caches per-glyph-pair kerning
# advances so text layout doesn't walk the font's GPOS table for every
# character every frame. Purely additive to the bundled copy.
set(FONTSTASH_PATCH  ${CMAKE_CURRENT_SOURCE_DIR}/patches/fontstash.h)
set(FONTSTASH_TARGET ${BOREALIS_LIBRARY}/include/borealis/extern/nanovg/fontstash.h)
if(EXISTS ${FONTSTASH_PATCH})
    message(STATUS "Applying patch: fontstash.h")
    configure_file(${FONTSTASH_PATCH} ${FONTSTASH_TARGET} COPYONLY)
else()
    message(FATAL_ERROR "Patch file not found: ${FONTSTASH_PATCH}")
endif()

# Borealis hints.json patch (all platforms): borealis 0bbc4222's new soft
# keyboard (EditTextDialog) registers a password show/hide action labelled
# "hints/password"_i18n, but that key is missing from borealis's own
# hints.json — and its i18n falls back to printing the raw key name, so the
# login password field showed a literal "hints/password". Ship the file with
# the key added. Every platform copies borealis's i18n dir, so patching the
# source file fixes them all without per-platform resource wiring.
set(HINTS_PATCH  ${CMAKE_CURRENT_SOURCE_DIR}/patches/hints.json)
set(HINTS_TARGET ${BOREALIS_DIR}/resources/i18n/en-US/hints.json)
if(EXISTS ${HINTS_PATCH})
    message(STATUS "Applying patch: hints.json")
    configure_file(${HINTS_PATCH} ${HINTS_TARGET} COPYONLY)
else()
    message(FATAL_ERROR "Patch file not found: ${HINTS_PATCH}")
endif()

# Borealis nanovg patch (all platforms): adds nvgTextBatchBegin/End so a run of
# nvgText calls sharing identical state flush as a single render call. Used by
# RecyclingGrid's batched title drawing. Purely additive to the bundled copy.
set(NANOVG_PATCH  ${CMAKE_CURRENT_SOURCE_DIR}/patches/nanovg.c)
set(NANOVG_TARGET ${BOREALIS_LIBRARY}/lib/extern/nanovg/nanovg.c)
if(EXISTS ${NANOVG_PATCH})
    message(STATUS "Applying patch: nanovg.c")
    configure_file(${NANOVG_PATCH} ${NANOVG_TARGET} COPYONLY)
else()
    message(FATAL_ERROR "Patch file not found: ${NANOVG_PATCH}")
endif()

add_subdirectory(${BOREALIS_LIBRARY})

# ---------------------------------------------------------------------------
# Application sources
# ---------------------------------------------------------------------------
set(APP_SOURCES
    # Main entry
    src/main.cpp

    # Application
    src/app/application.cpp
    src/app/audiobookshelf_client.cpp
    src/app/downloads_manager.cpp

    # Activities
    src/activity/main_activity.cpp
    src/activity/login_activity.cpp
    src/activity/player_activity.cpp

    # Views
    src/view/home_tab.cpp
    src/view/library_section_tab.cpp
    src/view/search_tab.cpp
    src/view/settings_tab.cpp
    src/view/media_detail_view.cpp
    src/view/media_item_cell.cpp
    src/view/recycling_grid.cpp
    src/view/downloads_tab.cpp
    src/view/progress_dialog.cpp
    src/view/podcast_search_tab.cpp

    # Player
    src/player/mpv_player.cpp

    # Utils
    src/utils/http_client.cpp
    src/utils/image_loader.cpp
    src/utils/audio_utils.cpp
    src/utils/app_update.cpp
)

# vita_stubs.c only needed on Vita (no-op stdio locks, SDL_OpenURL stub)
if(PLATFORM_PSV)
    list(APPEND APP_SOURCES src/utils/vita_stubs.c)
    # In-app self-update: unzip the downloaded VPK (miniz, sceIo-backed) and
    # promote it in place via ScePromoterUtility (utils/vita_install).
    list(APPEND APP_SOURCES src/utils/vita_install.cpp lib/miniz/miniz.c)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib/miniz)
    # miniz without stdio/time: newlib's fopen64/stat64/utime are absent on
    # Vita, so vita_install.cpp drives it through sceIo callbacks instead.
    add_definitions(-DMINIZ_NO_STDIO -DMINIZ_NO_TIME)
endif()

# PS4 in-app self-update: BGFT pkg install + updater-helper management
if(PLATFORM_PS4)
    list(APPEND APP_SOURCES src/utils/ps4_install.cpp)
endif()

# Switch: userAppInit/userAppExit (romfs, sockets, system services init)
if(PLATFORM_SWITCH)
    list(APPEND APP_SOURCES ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
endif()

# Android asset extraction helper
if(PLATFORM_ANDROID)
    list(APPEND APP_SOURCES src/platform/android_assets.cpp)
endif()

# Platform abstraction layer — one .cpp per platform, selected at build time
if(PLATFORM_PSV)
    list(APPEND APP_SOURCES src/platform/platform_vita.cpp)
elseif(PLATFORM_SWITCH)
    list(APPEND APP_SOURCES src/platform/platform_switch.cpp)
elseif(PLATFORM_PS4)
    list(APPEND APP_SOURCES src/platform/platform_ps4.cpp)
elseif(PLATFORM_ANDROID)
    list(APPEND APP_SOURCES src/platform/platform_android.cpp)
else()
    list(APPEND APP_SOURCES src/platform/platform_desktop.cpp)
endif()

set(APP_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${BOREALIS_LIBRARY}/include
    ${BOREALIS_LIBRARY}/include/borealis/extern
)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
if(PLATFORM_ANDROID)
    add_library(${PROJECT_NAME} SHARED ${APP_SOURCES})
else()
    add_executable(${PROJECT_NAME} ${APP_SOURCES})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${APP_INCLUDES})
if(PLATFORM_DESKTOP AND APPLE)
    target_include_directories(${PROJECT_NAME} PRIVATE
        /opt/homebrew/include
        /usr/local/include
    )
    target_link_directories(${PROJECT_NAME} PRIVATE
        /opt/homebrew/lib
        /usr/local/lib
    )
endif()

if(PLATFORM_PSV)
    target_compile_definitions(${PROJECT_NAME} PRIVATE RESOURCE_PREFIX=\"app0:resources/\")
elseif(PLATFORM_PS4)
    target_compile_definitions(${PROJECT_NAME} PRIVATE RESOURCE_PREFIX=\"/app0/resources/\")
elseif(PLATFORM_SWITCH OR USE_LIBROMFS)
    # Switch always uses romfs. Android does NOT — resources are APK assets via SDL.
    target_compile_definitions(${PROJECT_NAME} PRIVATE RESOURCE_PREFIX=\"romfs:/\")
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE RESOURCE_PREFIX=\"resources/\")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    VITAABS_VERSION=\"${APP_VERSION}\"
    VITAABS_DISPLAY_VERSION=\"${APP_DISPLAY_VERSION}\"
)

# Switch deko3d builds download the matching NRO variant in the in-app updater
if(PLATFORM_SWITCH AND USE_DEKO3D)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VITAABS_NRO_DEKO3D)
endif()

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
if(PLATFORM_ANDROID)
    # Android: manga reader only — libwebp handles animated WebP natively,
    # no video playback needed so skip mpv/ffmpeg entirely.
    set(APP_FFMPEG_LIBS)
    set(APP_FFMPEG_LINK_BLOCK)
elseif(WIN32)
    # Windows CI only ships mpv; FFmpeg headers not available.
    set(APP_FFMPEG_LIBS mpv)
    set(APP_FFMPEG_LINK_BLOCK ${APP_FFMPEG_LIBS})
    add_definitions(-DVitaABS_NO_FFMPEG)
elseif(UNIX AND NOT APPLE AND NOT PLATFORM_PS4)
    # Linux/SteamOS: full mpv + FFmpeg stack (CI installs dev packages).
    set(APP_FFMPEG_LIBS
        mpv
        avfilter
        avformat
        avcodec
        swresample
        swscale
        avutil
    )
    set(APP_FFMPEG_LINK_BLOCK ${APP_FFMPEG_LIBS})
else()
    set(APP_FFMPEG_LIBS
        mpv
        avfilter
        avformat
        avcodec
        swresample
        swscale
        avutil
    )
    if(APPLE OR PLATFORM_PS4)
        set(APP_FFMPEG_LINK_BLOCK ${APP_FFMPEG_LIBS})
    elseif(PLATFORM_SWITCH)
        # switchfin's switch-ffmpeg is built WITH the mbedtls (rtmp/tls) and
        # libssh2 (sftp) protocols, so libavformat.a pulls in mbedtls_* /
        # psa_crypto_init / libssh2_* symbols that devkitPro's lighter ffmpeg
        # didn't. Put ssh2 + mbedtls inside the SAME --start-group as ffmpeg so
        # the one-pass static linker resolves the cross-references (ffmpeg ↔
        # libssh2 ↔ mbedtls); listing mbedtls only in APP_NETWORK_LIBS (before
        # ffmpeg) left those refs undefined.
        set(APP_FFMPEG_LINK_BLOCK
            -Wl,--start-group
            ${APP_FFMPEG_LIBS}
            ssh2 mbedtls mbedx509 mbedcrypto z
            -Wl,--end-group)
    else()
        set(APP_FFMPEG_LINK_BLOCK -Wl,--start-group ${APP_FFMPEG_LIBS} -Wl,--end-group)
    endif()
endif()

if(PLATFORM_ANDROID)
    # Android dependency bundle intentionally omits libpng/libbz2/libzstd.
    # Keeping these out of the explicit link list avoids CI link failures when
    # those optional transitives are not packaged in jniLibs.
    set(APP_RENDER_LIBS
        ass
        freetype
        fribidi
        webp
        z
        c
        harfbuzz
    )
elseif(WIN32)
    set(APP_RENDER_LIBS
        bz2
        webp
        z
    )
else()
    set(APP_RENDER_LIBS
        ass
        freetype
        fribidi
        png
        bz2
        webp
        z
        c
    )
endif()

if(NOT PLATFORM_PS4 AND NOT WIN32 AND NOT PLATFORM_ANDROID)
    list(APPEND APP_RENDER_LIBS
        zstd
        harfbuzz
    )
endif()

set(APP_NETWORK_LIBS
    curl
)

# Switch is excluded HERE on purpose: its ffmpeg ALSO needs mbedtls, so
# mbedtls is placed inside the ffmpeg --start-group (after curl) instead —
# listing it here too would let CMake de-duplicate the in-group copy and
# re-break the static link order (ffmpeg's mbedtls refs going undefined).
if(NOT WIN32 AND NOT PLATFORM_SWITCH)
    list(APPEND APP_NETWORK_LIBS
        mbedtls
        mbedx509
        mbedcrypto
    )
endif()

target_link_libraries(${PROJECT_NAME}
    borealis
    ${APP_PLATFORM_LIB}
    ${APP_NETWORK_LIBS}
    ${APP_FFMPEG_LINK_BLOCK}
    ${APP_RENDER_LIBS}
)

if(NOT PLATFORM_SWITCH AND NOT PLATFORM_PS4 AND NOT WIN32 AND NOT PLATFORM_ANDROID)
    target_link_libraries(${PROJECT_NAME} mp3lame)
endif()

# Android needs the android library for AAssetManager (used by asset extraction)
if(PLATFORM_ANDROID)
    target_link_libraries(${PROJECT_NAME} android log)
endif()

if(PLATFORM_PSV)
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--wrap=pthread_create")
    
    target_link_libraries(${PROJECT_NAME}
        curl
        mbedtls
        mbedx509
        mbedcrypto
    
        # Use linker group for mpv/FFmpeg circular dependencies
        -Wl,--start-group
        mpv
        avfilter
        avformat
        avcodec
        swresample
        swscale
        avutil
        -Wl,--end-group
    
        # Archive support (required by mpv's libarchive stream)
        archive
    
        # Audio/compression libraries
        mp3lame
        zstd
        ass
        freetype
        harfbuzz
        fribidi
        png
        bz2
    
        # Vita SDK stubs (provide symbols for above libraries)
        vita2d
        SceNet_stub
        SceNetCtl_stub
        SceHttp_stub
        SceSsl_stub
        ScePgf_stub
        SceRtc_stub
        SceShellSvc_stub
        SceAudiodec_stub
        SceVideodec_stub
        SceAvcodec_stub
        SceCodecEngine_stub
        SceAudio_stub
        SceVshBridge_stub
        SceAppMgr_stub
        # In-app self-update promoter (utils/vita_install)
        ScePromoterUtil_stub
        SceSysmodule_stub

        # Standard libraries last
        z
        c
    )
elseif(PLATFORM_SWITCH)
    target_link_libraries(${PROJECT_NAME} nx dav1d placebo postproc jpeg)
elseif(PLATFORM_PS4)
    target_link_libraries(${PROJECT_NAME}
        SceNet
        # In-app self-update (utils/ps4_install): BGFT registers the PKG
        # with the system installer, AppInstUtil uninstalls the old title
        # (+ SceUserService for AppInstUtil's user context).
        SceBgft
        SceAppInstUtil
        SceUserService
        SceSystemService
    )
elseif(PLATFORM_DESKTOP)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
    if(APPLE)
        target_link_libraries(${PROJECT_NAME} "-framework CoreWLAN")
    endif()
endif()

# ---------------------------------------------------------------------------
# Platform packaging / post-build
# ---------------------------------------------------------------------------
if(PLATFORM_PSV)
    # ── Updater stub (self-update helper) ───────────────────────────────
    # A second, tiny Vita app with its own title id (VABSUPD01). VitaABS
    # cannot promote over itself — the installer returns 0x80101114 "in
    # use" for the running title — so at update time VitaABS installs and
    # launches this stub; with VitaABS now closed, the stub promotes the
    # downloaded VPK and relaunches VitaABS. This is the AutoPlugin2
    # technique. It shares the extract/promote code (utils/vita_install),
    # which is deliberately free of borealis/fmt so the stub links without
    # the UI stack. The stub's own .vpk is bundled inside VitaABS.vpk
    # (as updater.vpk) so VitaABS can install it on demand.
    add_executable(vitaabs_updater
        src/updater_stub/main.cpp
        src/utils/vita_install.cpp
        lib/miniz/miniz.c)
    target_include_directories(vitaabs_updater PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/miniz)
    target_compile_definitions(vitaabs_updater PRIVATE __PSV__ MINIZ_NO_STDIO MINIZ_NO_TIME)
    target_link_libraries(vitaabs_updater
        mbedcrypto            # mbedtls_sha1 for the fpkg hmac
        # vita2d + its stack, for the tiny progress screen (system PGF font).
        vita2d
        freetype
        png
        z
        SceGxm_stub
        SceDisplay_stub
        SceCommonDialog_stub
        ScePgf_stub
        SceCtrl_stub
        # self-update + relaunch
        ScePromoterUtil_stub
        SceSysmodule_stub
        SceAppMgr_stub
        SceIofilemgr_stub
        SceLibKernel_stub)
    vita_create_self(vitaabs_updater.self vitaabs_updater UNSAFE NOASLR)
    # No FILE args: vita_create_vpk accumulates them in a variable it does
    # not clear between calls, so any file listed here would leak into the
    # main VitaABS.vpk below and get packed twice. The stub is headless.
    vita_create_vpk(vitaabs_updater.vpk VABSUPD01 vitaabs_updater.self
        VERSION 01.00
        NAME "VitaABS Updater")

    vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE NOASLR)

    vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
        VERSION ${VITA_VERSION}
        NAME    ${VITA_APP_NAME}

        # The bundled updater stub, installed + launched at self-update
        FILE ${CMAKE_BINARY_DIR}/vitaabs_updater.vpk updater.vpk

        FILE app/platform/psv/sce_sys/icon0.png                      sce_sys/icon0.png
        FILE app/platform/psv/sce_sys/pic0.png                       sce_sys/pic0.png
        FILE app/platform/psv/sce_sys/livearea/contents/bg.png       sce_sys/livearea/contents/bg.png
        FILE app/platform/psv/sce_sys/livearea/contents/startup.png  sce_sys/livearea/contents/startup.png
        FILE app/platform/psv/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml

        FILE ${BOREALIS_DIR}/resources/i18n/en-US/hints.json resources/i18n/en-US/hints.json
        FILE resources/i18n/en-US/main.json                  resources/i18n/en-US/main.json

        FILE ${BOREALIS_DIR}/resources/img/sys/battery_back_dark.png  resources/img/sys/battery_back_dark.png
        FILE ${BOREALIS_DIR}/resources/img/sys/battery_back_light.png resources/img/sys/battery_back_light.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_0_dark.png        resources/img/sys/wifi_0_dark.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_0_light.png       resources/img/sys/wifi_0_light.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_1_dark.png        resources/img/sys/wifi_1_dark.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_1_light.png       resources/img/sys/wifi_1_light.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_2_dark.png        resources/img/sys/wifi_2_dark.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_2_light.png       resources/img/sys/wifi_2_light.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_3_dark.png        resources/img/sys/wifi_3_dark.png
        FILE ${BOREALIS_DIR}/resources/img/sys/wifi_3_light.png       resources/img/sys/wifi_3_light.png
        FILE ${BOREALIS_DIR}/resources/img/sys/ethernet_dark.png      resources/img/sys/ethernet_dark.png
        FILE ${BOREALIS_DIR}/resources/img/sys/ethernet_light.png     resources/img/sys/ethernet_light.png

        FILE resources/xml/activity/main.xml     resources/xml/activity/main.xml
        FILE resources/xml/activity/login.xml    resources/xml/activity/login.xml
        FILE resources/xml/activity/player.xml   resources/xml/activity/player.xml
        FILE resources/xml/tabs/library.xml      resources/xml/tabs/library.xml
        FILE resources/xml/tabs/search.xml       resources/xml/tabs/search.xml
        FILE resources/xml/tabs/settings.xml     resources/xml/tabs/settings.xml
        FILE resources/xml/tabs/home.xml   resources/xml/tabs/home.xml
        FILE resources/xml/view/media_detail.xml resources/xml/view/media_detail.xml
        FILE resources/xml/view/media_item.xml   resources/xml/view/media_item.xml
       

        FILE resources/icons/a.png                        resources/icons/a.png
        FILE resources/icons/az.png                       resources/icons/az.png
        FILE resources/icons/za.png                       resources/icons/za.png
        FILE resources/icons/back.png                     resources/icons/back.png
        FILE resources/icons/checkbox.png                 resources/icons/checkbox.png
        FILE resources/icons/checkbox_checked.png         resources/icons/checkbox_checked.png
        FILE resources/icons/checkbox_crossed.png         resources/icons/checkbox_crossed.png
        FILE resources/icons/circle.png                   resources/icons/circle.png
        FILE resources/icons/cross.png                    resources/icons/cross.png
        FILE resources/icons/download.png                 resources/icons/download.png
        FILE resources/icons/hide.png                     resources/icons/hide.png
        FILE resources/icons/history.png                  resources/icons/history.png
        FILE resources/icons/hot.png                      resources/icons/hot.png
        FILE resources/icons/import.png                   resources/icons/import.png
        FILE resources/icons/left.png                     resources/icons/left.png
        FILE resources/icons/menu.png                     resources/icons/menu.png
        FILE resources/icons/mini_star.png                resources/icons/mini_star.png
        FILE resources/icons/options.png                  resources/icons/options.png
        FILE resources/icons/radio.png                    resources/icons/radio.png
        FILE resources/icons/radio_checked.png            resources/icons/radio_checked.png
        FILE resources/icons/refresh.png                  resources/icons/refresh.png
        FILE resources/icons/right.png                    resources/icons/right.png
        FILE resources/icons/search.png                   resources/icons/search.png
        FILE resources/icons/show.png                     resources/icons/show.png
        FILE resources/icons/sort-1-9.png                 resources/icons/sort-1-9.png
        FILE resources/icons/sort-9-1.png                 resources/icons/sort-9-1.png
        FILE resources/icons/sort-clock-ascending.png     resources/icons/sort-clock-ascending.png
        FILE resources/icons/sort-clock-descending.png    resources/icons/sort-clock-descending.png
        FILE resources/icons/sort-calendar-ascending.png  resources/icons/sort-calendar-ascending.png
        FILE resources/icons/sort-calendar-descending.png resources/icons/sort-calendar-descending.png
        FILE resources/icons/book-open-page-variant.png   resources/icons/book-open-page-variant.png
        FILE resources/icons/book-multiple.png            resources/icons/book-multiple.png
        FILE resources/icons/book-arrow-down.png          resources/icons/book-arrow-down.png
        FILE resources/icons/star.png                     resources/icons/star.png
        FILE resources/icons/tag.png                      resources/icons/tag.png
        FILE resources/icons/web.png                      resources/icons/web.png
        FILE resources/icons/filter-menu-outline.png      resources/icons/filter-menu-outline.png
        FILE resources/icons/format-list-group.png        resources/icons/format-list-group.png

        FILE resources/images/circle_button.png   resources/images/circle_button.png
        FILE resources/images/cross_button.png    resources/images/cross_button.png
        FILE resources/images/dpad.png            resources/images/dpad.png
        FILE resources/images/gradient.png        resources/images/gradient.png
        FILE resources/images/l_button.png        resources/images/l_button.png
        FILE resources/images/logo-small.png      resources/images/logo-small.png
        FILE resources/images/logo.png            resources/images/logo.png
        FILE resources/images/r_button.png        resources/images/r_button.png
        FILE resources/images/select_button.png   resources/images/select_button.png
        FILE resources/images/square_button.png   resources/images/square_button.png
        FILE resources/images/start_button.png    resources/images/start_button.png
        FILE resources/images/triangle_button.png resources/images/triangle_button.png

        FILE resources/font/font.ttf                      resources/font/font.ttf
        FILE resources/font/icon.ttf                      resources/font/icon.ttf
        FILE resources/font/switch_font.ttf               resources/font/switch_font.ttf
        FILE resources/font/switch_icons.ttf              resources/font/switch_icons.ttf
        FILE resources/material/MaterialIcons-Regular.ttf resources/material/MaterialIcons-Regular.ttf
    )

elseif(PLATFORM_PS4)
    if(PS4)
        # PS4 variable is defined by the pacbrew toolchain
        add_self(${PROJECT_NAME})
        add_pkg(${PROJECT_NAME}
            ${CMAKE_BINARY_DIR}/ps4
            "VABS00002"
            "VitaABS"
            "${APP_SFO_VERSION}"
            # https://psdevwiki.com/ps4/param.sfo#ATTRIBUTE - Support PS4 Pro
            8388608
        )
    endif()
    add_custom_target(${PROJECT_NAME}.data
        # Copy borealis PS4 base files (sce_module, about/right.sprx)
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${BOREALIS_DIR}/ps4 ${CMAKE_BINARY_DIR}/ps4
        # Override with our custom sce_sys (icon)
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${CMAKE_SOURCE_DIR}/app/platform/ps4 ${CMAKE_BINARY_DIR}/ps4
        # Copy app resources
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resources ${CMAKE_BINARY_DIR}/ps4/resources
        # Copy borealis system resources
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${BOREALIS_DIR}/resources/i18n ${CMAKE_BINARY_DIR}/ps4/resources/i18n
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${BOREALIS_DIR}/resources/img ${CMAKE_BINARY_DIR}/ps4/resources/img
    )
    add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.data)

    # ── PS4 updater helper (self-update) ────────────────────────────────
    # A second, tiny PS4 app with its own title id (VABS00003). VitaABS
    # cannot replace itself in place: BGFT installs into the running
    # /user/app/VABS00002 (locked), the installer returns 0x80990088
    # "already installed", and uninstalling the running title just kills the
    # process. So at update time VitaABS launches this helper and quits;
    # with VitaABS closed the helper uninstalls VABS00002 and hands the
    # downloaded pkg to the system installer (BGFT), which finishes in the
    # background. Shipped as its own pkg, installed once (like the Vita
    # updater stub VABSUPD01). It has no UI, so it needs only the borealis
    # PS4 base (sce_module + sce_sys) — no app resources.
    if(PS4)
        add_executable(vitaabs_updater_ps4 src/updater_ps4/main.cpp)
        target_compile_definitions(vitaabs_updater_ps4 PRIVATE __PS4__)
        # stb_image (vendored with borealis's nanovg) decodes the app icon for
        # the helper's progress screen — it links no other image/font code.
        target_include_directories(vitaabs_updater_ps4 PRIVATE
            ${BOREALIS_LIBRARY}/include/borealis/extern/nanovg)
        # SDL2 is required at RUNTIME, not just to link: the helper calls
        # SDL_Init(SDL_INIT_VIDEO) before touching any system service, exactly
        # as borealis's Ps4Platform does. Without that call every privileged
        # call faults. Plus the libs whose functions the helper calls: BGFT +
        # AppInstUtil (+ SceUserService for AppInstUtil's user context).
        if(NOT TARGET SDL2::SDL2-static)
            find_package(SDL2 REQUIRED)
        endif()
        target_link_libraries(vitaabs_updater_ps4
            SceBgft SceAppInstUtil SceUserService SceSystemService SDL2::SDL2-static)
        add_custom_target(vitaabs_updater_ps4.data
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                ${BOREALIS_DIR}/ps4 ${CMAKE_BINARY_DIR}/ps4_updater
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                ${CMAKE_SOURCE_DIR}/app/platform/ps4 ${CMAKE_BINARY_DIR}/ps4_updater
            # The progress screen's artwork must be a PLAIN file: sce_sys is the
            # package metadata dir, so sce_sys/icon0.png is consumed by the pkg
            # tool and cannot be opened at runtime.
            COMMAND "${CMAKE_COMMAND}" -E copy
                ${CMAKE_SOURCE_DIR}/app/platform/ps4/sce_sys/icon0.png
                ${CMAKE_BINARY_DIR}/ps4_updater/updater_icon.png
        )
        add_dependencies(vitaabs_updater_ps4 vitaabs_updater_ps4.data)
        add_self(vitaabs_updater_ps4)
        add_pkg(vitaabs_updater_ps4
            ${CMAKE_BINARY_DIR}/ps4_updater
            "VABS00003"
            "VitaABS Updater"
            "01.00"
            8388608
        )

        # Bundle the helper's pkg INSIDE VitaABS's pkg (as /app0/updater.pkg),
        # so the user only ever installs one app — the same approach the Vita
        # build uses for its updater stub. The dependency forces the helper's
        # pkg to be built and copied in BEFORE VitaABS's pkg is packed. The pkg
        # tool names its output from the CONTENT_ID (IV0001-VABS00003_00-...),
        # so the file is located by glob at build time.
        add_custom_target(vitaabs_updater_ps4.bundle
            COMMAND "${CMAKE_COMMAND}"
                -DSRC_DIR=${CMAKE_BINARY_DIR}
                -DDST=${CMAKE_BINARY_DIR}/ps4/updater.pkg
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle_updater_pkg.cmake
            DEPENDS vitaabs_updater_ps4_pkg
        )
        add_dependencies(${PROJECT_NAME}_pkg vitaabs_updater_ps4.bundle)
    endif()

elseif(PLATFORM_SWITCH)
    # Build NRO with embedded romfs resources (matches switchfin approach).
    # nx_create_nro doesn't embed romfs or set metadata correctly, so we
    # call nacptool + elf2nro manually.
    if(USE_DEKO3D)
        gen_dksh("${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders")
    endif()
    add_custom_target(${PROJECT_NAME}.nro ALL DEPENDS ${PROJECT_NAME}
        # Generate NACP metadata (author, version shown in hbmenu)
        COMMAND ${NX_NACPTOOL_EXE} --create "VitaABS" "Breezyslasher"
            "${PROJECT_VERSION}" ${PROJECT_NAME}.nacp
        # Assemble romfs: app resources + borealis system resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources ${CMAKE_BINARY_DIR}/resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${BOREALIS_DIR}/resources/i18n ${CMAKE_BINARY_DIR}/resources/i18n
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${BOREALIS_DIR}/resources/img ${CMAKE_BINARY_DIR}/resources/img
        # Build NRO with icon, metadata, and romfs
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro
            --icon=${CMAKE_CURRENT_SOURCE_DIR}/app/platform/icon_switch.jpg
            --nacp=${PROJECT_NAME}.nacp
            --romfsdir=${CMAKE_BINARY_DIR}/resources
    )

elseif(PLATFORM_DESKTOP)
    # Copy app resources next to the binary so relative paths resolve.
    # Also copy borealis system resources (i18n/hints.json, img/sys/) which
    # are not checked in to resources/ but are required by borealis at runtime.
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${BOREALIS_DIR}/resources/i18n
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/resources/i18n
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${BOREALIS_DIR}/resources/img
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/resources/img
        COMMENT "Copying resources (app + borealis) for desktop build"
    )
endif()
