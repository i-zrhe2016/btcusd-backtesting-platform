cmake_minimum_required(VERSION 3.20)

project(BtcUsdReplay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(BtcUsdReplay WIN32
    src/data_feed.cpp
    src/stoch_indicator.cpp
    src/main.cpp
)

target_include_directories(BtcUsdReplay PRIVATE src)

target_compile_definitions(BtcUsdReplay PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(BtcUsdReplay PRIVATE /W4 /EHsc)
else()
    target_compile_options(BtcUsdReplay PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MINGW)
    target_link_options(BtcUsdReplay PRIVATE
        -mwindows
        -municode
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

target_link_libraries(BtcUsdReplay PRIVATE
    comctl32
    comdlg32
    gdi32
    shell32
    user32
    kernel32
)
