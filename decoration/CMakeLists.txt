# Windows 11 window frame for KWin (KDecoration3 plugin).
# Build and install without sudo: bash tools/build-decoration.sh
cmake_minimum_required(VERSION 3.22)
project(shellkit-win11-decoration LANGUAGES CXX)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)
find_package(KF6CoreAddons REQUIRED)
find_package(KDecoration3 REQUIRED)

add_library(org.shellkit.win11 MODULE
    src/plugin.cpp
    src/decoration.cpp
    src/button.cpp
)
target_link_libraries(org.shellkit.win11 PRIVATE Qt6::Core Qt6::Gui KF6::CoreAddons KDecoration3::KDecoration)

install(TARGETS org.shellkit.win11 DESTINATION ${KDE_INSTALL_PLUGINDIR}/org.kde.kdecoration3)
