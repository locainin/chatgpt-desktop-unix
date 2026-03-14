cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------
# Project metadata
# ---------------------------------------------------------
project(chatgpt-desktop-unix
    VERSION 0.0.1         # Early 0.0.1 development build
    DESCRIPTION "Unofficial ChatGPT Desktop app for Linux (BSD & Unix soon). Built with Qt6."
    LANGUAGES CXX
)

include(GNUInstallDirs)

# ---------------------------------------------------------
# Build settings
# ---------------------------------------------------------
# C++20 baseline for modern language features and tooling
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable automatic MOC/UIC/RCC (Qt6 recommended)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---------------------------------------------------------
# Qt 6 Dependencies
# ---------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    # Required for QNetworkCookie used in persistence flush
    Network
    Widgets
    WebEngineWidgets
    WebEngineCore
)

# ---------------------------------------------------------
# Source files
# ---------------------------------------------------------
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/appwindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/browserprofile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chatinjections.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chatwebpage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chatview.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/trustedorigins.cpp
)

set(HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/appwindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/browserprofile.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chatinjections.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chatwebpage.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chatview.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/trustedorigins.h
)

set(RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons.qrc
)

# ---------------------------------------------------------
# Executable
# ---------------------------------------------------------
qt_add_executable(chatgpt-desktop-unix
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# ---------------------------------------------------------
# Linking
# ---------------------------------------------------------
target_link_libraries(chatgpt-desktop-unix
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Widgets
        Qt6::WebEngineWidgets
        Qt6::WebEngineCore
)

# ---------------------------------------------------------
# Install configuration (optional — distro packaging)
# ---------------------------------------------------------
install(TARGETS chatgpt-desktop-unix
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/aur/chatgpt-desktop-unix.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/aur/chatgpt-desktop-unix.png
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pixmaps
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/chatgpt-desktop-unix
)

include(CTest)
set(CHATGPT_DESKTOP_TESTS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/chatinjections_tests.cpp")
if(BUILD_TESTING AND EXISTS "${CHATGPT_DESKTOP_TESTS_SOURCE}")
    qt_add_executable(chatgpt-desktop-unix-tests
        ${CHATGPT_DESKTOP_TESTS_SOURCE}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/chatinjections.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/chatinjections.h
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons.qrc
    )

    target_link_libraries(chatgpt-desktop-unix-tests
        PRIVATE
            Qt6::Core
    )

    add_test(NAME chatinjections-tests COMMAND chatgpt-desktop-unix-tests)
endif()

# Print build version info
message(STATUS "Building chatgpt-desktop-unix v${PROJECT_VERSION}")
