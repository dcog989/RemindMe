
set(REMINDME_ENGINE_DIR ${CMAKE_SOURCE_DIR}/src/engine)

# The deadline scheduler the engine delegates waiting to; the engine test links it because the
# engine builds a scheduler by default even when the test injects its own.
set(REMINDME_SCHEDULER_SOURCES
    ${REMINDME_ENGINE_DIR}/deadlinescheduler.cpp
    ${REMINDME_ENGINE_DIR}/qtimerdeadlinescheduler.cpp
    ${REMINDME_ENGINE_DIR}/timerfddeadlinescheduler.cpp
)

ecm_add_test(remindmetimetest.cpp TEST_NAME remindmetimetest LINK_LIBRARIES Qt6::Test)
target_sources(remindmetimetest PRIVATE ${REMINDME_ENGINE_DIR}/remindmetime.cpp)
target_include_directories(remindmetimetest PRIVATE ${REMINDME_ENGINE_DIR})

ecm_add_test(remindmeenginetest.cpp TEST_NAME remindmeenginetest LINK_LIBRARIES Qt6::Test)
target_sources(remindmeenginetest PRIVATE
    ${REMINDME_ENGINE_DIR}/remindmeengine.cpp
    ${REMINDME_SCHEDULER_SOURCES}
)
target_include_directories(remindmeenginetest PRIVATE ${REMINDME_ENGINE_DIR})

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
