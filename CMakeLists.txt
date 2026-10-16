cmake_minimum_required(VERSION 3.29)

project(remindme VERSION 0.3.0 LANGUAGES CXX)

# Single source of truth for the D-Bus service KRunner auto-activates: the generated service file,
# the runner .desktop metadata and the C++ registration (compile definition) all derive from it.
set(REMINDME_DBUS_SERVICE_NAME "io.github.dcog989.remindme")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ECM 6.9 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMGenerateDBusServiceFile)
include(ECMAddTests)
include(CheckPIESupported)
include(FeatureSummary)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core DBus Widgets)
find_package(KF6 6.9 REQUIRED COMPONENTS CoreAddons I18n Notifications Config)

# Without this, POSITION_INDEPENDENT_CODE adds -fPIE but not -pie to the link line, and a non-PIE
# binary links copy relocations against Qt's protected data symbols, which the loader rejects
# (GNU_PROPERTY_1_NEEDED_INDIRECT_EXTERN_ACCESS) when it was built against another distribution's
# Qt than the one it runs with.
check_pie_supported()

# Compile any po/<lang>/*.po to .mo and install them under KDE_INSTALL_LOCALEDIR. No translations
# are shipped yet; dropping gettext catalogues into po/ is enough to enable a locale. Set
# KF_SKIP_PO_PROCESSING=ON to skip this for non-release builds.
ki18n_install(po)

# The runner metadata is generated from the CMake variables so they cannot drift.
set(REMINDME_DESKTOP_FILE ${CMAKE_CURRENT_BINARY_DIR}/plasma-runner-remindme.desktop)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/data/plasma-runner-remindme.desktop
    ${REMINDME_DESKTOP_FILE}
    @ONLY
)

# The application entry KWin matches the alarm window against: the window's Qt app id is
# "krunner-remindme" (see setDesktopFileName in src/engine/main.cpp), so the entry is named after
# it. Without it the window and its task manager entry fall back to a generic name and icon.
set(REMINDME_LIBEXEC_DIR ${KDE_INSTALL_FULL_LIBEXECDIR_KF})
set(REMINDME_APP_DESKTOP_FILE ${CMAKE_CURRENT_BINARY_DIR}/krunner-remindme.desktop)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/data/krunner-remindme.desktop
    ${REMINDME_APP_DESKTOP_FILE}
    @ONLY
)

add_subdirectory(src)

if(BUILD_TESTING)
    # Tests only; the installed runner does not link them.
    # Qt6::Test for the unit tests; KF6::Runner for AbstractRunnerTest/RunnerManager.
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test)
    find_package(KF6 6.9 REQUIRED COMPONENTS Runner)
    add_subdirectory(autotests)
endif()

feature_summary(WHAT ALL)
