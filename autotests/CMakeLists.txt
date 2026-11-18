
# Source groups come from the top-level CMakeLists.txt (${REMINDME_*_SOURCES}).
ecm_add_test(remindmetimetest.cpp TEST_NAME remindmetimetest LINK_LIBRARIES Qt6::Test KF6::I18n)
target_sources(remindmetimetest PRIVATE ${REMINDME_ENGINE_DIR}/remindmetime.cpp)
target_include_directories(remindmetimetest PRIVATE ${REMINDME_ENGINE_DIR})

ecm_add_test(remindmeenginetest.cpp TEST_NAME remindmeenginetest LINK_LIBRARIES Qt6::Test KF6::I18n)
target_sources(remindmeenginetest PRIVATE ${REMINDME_CORE_SOURCES})
target_include_directories(remindmeenginetest PRIVATE ${REMINDME_ENGINE_DIR})

# The runner protocol without the bus: match grammar, id round trip, action scoping and the
# ringing-alarm rows. The end-to-end suite below covers the same calls through KRunner.
ecm_add_test(remindmeadaptertest.cpp TEST_NAME remindmeadaptertest LINK_LIBRARIES Qt6::Test Qt6::DBus KF6::CoreAddons KF6::I18n)
target_sources(remindmeadaptertest PRIVATE
    ${REMINDME_CORE_SOURCES}
    ${REMINDME_RUNNER_DIR}/remindmeadaptor.cpp
)
target_include_directories(remindmeadaptertest PRIVATE
    ${REMINDME_ENGINE_DIR}
    ${REMINDME_RUNNER_DIR}
)

ecm_add_test(deadlineschedulertest.cpp TEST_NAME deadlineschedulertest LINK_LIBRARIES Qt6::Test)
target_sources(deadlineschedulertest PRIVATE ${REMINDME_SCHEDULER_SOURCES})
target_include_directories(deadlineschedulertest PRIVATE ${REMINDME_ENGINE_DIR})

ecm_add_test(remindmeautostarttest.cpp TEST_NAME remindmeautostarttest LINK_LIBRARIES Qt6::Test KF6::ConfigCore)
target_sources(remindmeautostarttest PRIVATE ${REMINDME_ENGINE_DIR}/remindmeautostart.cpp)
target_include_directories(remindmeautostarttest PRIVATE ${REMINDME_ENGINE_DIR})

# The runner is a DBus runner, exercised end to end via AbstractRunnerTest against a real
# subprocess on a private session bus (krunner_configure_test picks the DBus branch because
# krunner-remindme is an executable).
ecm_add_test(remindmeintegrationtest.cpp TEST_NAME remindmeintegrationtest LINK_LIBRARIES Qt6::Test Qt6::DBus KF6::Runner)
target_compile_definitions(remindmeintegrationtest PRIVATE
    REMINDME_DBUS_SERVICE_NAME="${REMINDME_DBUS_SERVICE_NAME}"
)
krunner_configure_test(remindmeintegrationtest krunner-remindme
    DESKTOP_FILE ${REMINDME_DESKTOP_FILE}
)

# The alarm presenter decides the process's right to exit from its open-window count; a wrong count
# drops a live alarm or keeps the process alive. Drives the windowed path (the sound path needs a
# notification service).
ecm_add_test(remindmealarmpresentertest.cpp TEST_NAME remindmealarmpresentertest LINK_LIBRARIES Qt6::Test Qt6::Widgets KF6::CoreAddons KF6::I18n KF6::Notifications)
target_sources(remindmealarmpresentertest PRIVATE
    ${REMINDME_CORE_SOURCES}
    ${REMINDME_UI_SOURCES}
)
target_include_directories(remindmealarmpresentertest PRIVATE ${REMINDME_ENGINE_DIR})
# The presenter takes the windowed path for any platform but "offscreen"; "minimal" creates and
# destroys widgets without a display. Appended so ECM's QT_PLUGIN_PATH stays set.
set_property(TEST remindmealarmpresentertest APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=minimal")

# The tray icon is view-only: it must follow the engine's timer count and never hold a set of its
# own. "minimal" keeps QSystemTrayIcon's visible flag without a tray host.
ecm_add_test(remindmetrayicontest.cpp TEST_NAME remindmetrayicontest LINK_LIBRARIES Qt6::Test Qt6::Widgets KF6::I18n)
target_sources(remindmetrayicontest PRIVATE
    ${REMINDME_CORE_SOURCES}
    ${REMINDME_ENGINE_DIR}/remindmetrayicon.cpp
)
target_include_directories(remindmetrayicontest PRIVATE ${REMINDME_ENGINE_DIR})
set_property(TEST remindmetrayicontest APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=minimal")
