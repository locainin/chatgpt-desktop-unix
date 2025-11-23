cmake_minimum_required(VERSION 3.16)
project(chatgpt-desktop-unix VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 6
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    WebEngineWidgets
)

set(SOURCES
    src/main.cpp
    src/appwindow.cpp
    src/chatview.cpp
)

set(HEADERS
    src/appwindow.h
    src/chatview.h
)

qt_add_executable(chatgpt-desktop-unix
    ${SOURCES}
    ${HEADERS}
    resources/icons.qrc
)

target_link_libraries(chatgpt-desktop-unix
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::WebEngineWidgets
)
