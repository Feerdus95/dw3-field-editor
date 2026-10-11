cmake_minimum_required(VERSION 3.21)
project(dw3fe VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(core STATIC src/core/field.cpp src/core/background.cpp src/core/characters.cpp src/core/text.cpp src/core/codes.cpp src/core/disc.cpp)
target_include_directories(core PUBLIC src/core)

# the exe's icon: build.py's square, multi-size one from res/dw3fe.ico, or the original
if(EXISTS "${CMAKE_BINARY_DIR}/gen/dw3fe.ico")
    set(DW3FE_ICON "${CMAKE_BINARY_DIR}/gen/dw3fe.ico")
else()
    set(DW3FE_ICON "${CMAKE_SOURCE_DIR}/res/dw3fe.ico")
endif()
configure_file(res/dw3fe.rc.in "${CMAKE_BINARY_DIR}/gen/dw3fe.rc" @ONLY)

add_executable(dw3fe WIN32
    src/app/main.cpp
    src/app/mainwindow.cpp src/app/mainwindow.h src/app/properties.cpp src/app/characters_ui.cpp src/app/codes_ui.cpp
    src/app/mapview.cpp src/app/mapview.h
    res/dw3fe.qrc
    "${CMAKE_BINARY_DIR}/gen/dw3fe.rc")
target_link_libraries(dw3fe PRIVATE core Qt6::Widgets)
target_compile_definitions(dw3fe PRIVATE QT_NO_KEYWORDS DW3FE_VERSION="${PROJECT_VERSION}")
