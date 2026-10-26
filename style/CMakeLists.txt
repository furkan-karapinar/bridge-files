# Windows 11 widget style for Qt Widgets apps (the File Explorer fork loads it by name), and
# its Windows 7 variant "shellkit-win7".
# Build and install without sudo: bash tools/build-qtstyle.sh
cmake_minimum_required(VERSION 3.22)
project(shellkit-win11-style LANGUAGES CXX)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(shellkit-win11 MODULE
    src/plugin.cpp
    src/style.cpp
    src/win7style.cpp
)
target_link_libraries(shellkit-win11 PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

install(TARGETS shellkit-win11 DESTINATION ${KDE_INSTALL_PLUGINDIR}/styles)
