cmake_minimum_required(VERSION 3.20)
project(AC2AP CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static runtime: no vcredist dependency inside the game process
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(AC2AP_WITH_AP "Build the Archipelago client (apclientpp)" ON)

set(DEPS ${CMAKE_CURRENT_SOURCE_DIR}/deps)

# zlib compiled into the DLL (websocketpp permessage_deflate requires it)
file(GLOB ZLIB_SRC ${DEPS}/zlib/*.c)
# MinHook: robust function hooks (trampoline + relocation + thread freeze)
file(GLOB MINHOOK_SRC ${DEPS}/minhook/src/*.c ${DEPS}/minhook/src/hde/*.c)
# Dear ImGui: in-game overlay (core + D3D9/Win32 backends)
set(IMGUI ${DEPS}/imgui)
set(IMGUI_SRC
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_dx9.cpp ${IMGUI}/backends/imgui_impl_win32.cpp)

add_library(ac2ap_asi SHARED src/dllmain.cpp ${ZLIB_SRC} ${MINHOOK_SRC} ${IMGUI_SRC})
set_target_properties(ac2ap_asi PROPERTIES OUTPUT_NAME "AC2AP" SUFFIX ".asi" PREFIX "")
target_link_libraries(ac2ap_asi PRIVATE ws2_32 d3d9 dinput8 dxguid)
target_include_directories(ac2ap_asi PRIVATE ${DEPS}/minhook/include ${IMGUI} ${IMGUI}/backends)

add_executable(test_records src/test_records.cpp)

foreach(t ac2ap_asi test_records)
    target_compile_definitions(${t} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(${t} PRIVATE /EHsc /W3)
endforeach()

if(AC2AP_WITH_AP)
    # TLS: archipelago.gg rooms are WSS-only. Without OpenSSL the client speaks
    # plaintext to a TLS endpoint and the server just closes ("End of file").
    # OpenSSL x86 static comes from vcpkg (deps/vcpkg, gitignored):
    #   deps/vcpkg/vcpkg.exe install openssl:x86-windows-static
    set(OPENSSL_VCPKG ${DEPS}/vcpkg/installed/x86-windows-static)
    if(NOT EXISTS ${OPENSSL_VCPKG}/lib/libssl.lib)
        message(FATAL_ERROR "OpenSSL x86 static not found - run: deps/vcpkg/vcpkg.exe install openssl:x86-windows-static")
    endif()
    target_compile_definitions(ac2ap_asi PRIVATE
        AC2AP_WITH_AP
        ASIO_STANDALONE
        AP_NO_SCHEMA
        _WEBSOCKETPP_CPP11_THREAD_
        _WEBSOCKETPP_CPP11_STL_
        _WIN32_WINNT=0x0601)
    target_include_directories(ac2ap_asi PRIVATE
        ${DEPS}/apclientpp
        ${DEPS}/wswrap/include
        ${DEPS}/websocketpp
        ${DEPS}/asio/asio/include
        ${DEPS}/json/include
        ${DEPS}/zlib
        ${OPENSSL_VCPKG}/include)
    target_link_libraries(ac2ap_asi PRIVATE
        ${OPENSSL_VCPKG}/lib/libssl.lib
        ${OPENSSL_VCPKG}/lib/libcrypto.lib
        crypt32)
endif()
