# Settings page for aurora-display-light in Plasma's System Settings.
cmake_minimum_required(VERSION 3.22)
project(kcm_aurora_display_light LANGUAGES CXX)

set(QT_MIN_VERSION 6.7) # QStandardPaths::GenericStateLocation
set(KF_MIN_VERSION 6.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Qml Quick)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Config CoreAddons I18n KCMUtils)

kcmutils_add_qml_kcm(kcm_aurora_display_light
    SOURCES displaylightkcm.cpp displaylightkcm.h
    INSTALL_NAMESPACE "plasma/kcms/systemsettings")
kconfig_add_kcfg_files(kcm_aurora_display_light displaylightsettings.kcfgc GENERATE_MOC)
target_include_directories(kcm_aurora_display_light PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../display-light)
target_compile_definitions(kcm_aurora_display_light PRIVATE TRANSLATION_DOMAIN="kcm_aurora_display_light")
target_link_libraries(kcm_aurora_display_light PRIVATE
    Qt6::Qml
    Qt6::Quick
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::CoreAddons
    KF6::I18n
    KF6::KCMUtilsQuick)
