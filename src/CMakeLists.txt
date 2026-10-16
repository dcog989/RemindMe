
# One process is the timer engine, the KRunner DBus runner and the alarm window. It is registered
# as the io.github.dcog989.remindme D-Bus service, which KRunner auto-activates on demand.
add_executable(krunner-remindme
    engine/main.cpp
    engine/deadlinescheduler.cpp
    engine/deadlinescheduler.h
    engine/timerfddeadlinescheduler.cpp
    engine/timerfddeadlinescheduler.h
    engine/qtimerdeadlinescheduler.cpp
    engine/qtimerdeadlinescheduler.h
    engine/remindmeengine.cpp
    engine/remindmeengine.h
    engine/timerinfo.h
    engine/remindmetime.cpp
    engine/remindmetime.h
    engine/remindmealarmdialog.cpp
    engine/remindmealarmdialog.h
    engine/remindmealarmnotification.cpp
    engine/remindmealarmnotification.h
    engine/remindmealarmpresenter.cpp
    engine/remindmealarmpresenter.h
    engine/remindmealarmsound.cpp
    engine/remindmealarmsound.h
    engine/remindmeautostart.cpp
    engine/remindmeautostart.h
    runner/remindmeadaptor.cpp
    runner/remindmeadaptor.h
    runner/remotematch.h
)

target_include_directories(krunner-remindme PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/runner
)

# PIE: a non-PIE executable links copy relocations against Qt's protected data symbols, which the
# loader rejects (GNU_PROPERTY_1_NEEDED_INDIRECT_EXTERN_ACCESS) once the binary is built against
# another distribution's Qt than the one it runs with.
set_target_properties(krunner-remindme PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(krunner-remindme PRIVATE
    REMINDME_DBUS_SERVICE_NAME="${REMINDME_DBUS_SERVICE_NAME}"
)

target_link_libraries(krunner-remindme PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Widgets
    KF6::CoreAddons
    KF6::I18n
    KF6::Notifications
    KF6::ConfigCore
)

install(TARGETS krunner-remindme DESTINATION ${KDE_INSTALL_LIBEXECDIR_KF})

ecm_generate_dbus_service_file(
    NAME ${REMINDME_DBUS_SERVICE_NAME}
    EXECUTABLE ${KDE_INSTALL_FULL_LIBEXECDIR_KF}/krunner-remindme
    DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR}
)

install(FILES ${REMINDME_DESKTOP_FILE}
    DESTINATION ${KDE_INSTALL_DATADIR}/krunner/dbusplugins
)

install(FILES ${REMINDME_APP_DESKTOP_FILE}
    DESTINATION ${KDE_INSTALL_APPDIR}
)

install(FILES ${CMAKE_SOURCE_DIR}/data/io.github.dcog989.remindme.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps
)

install(FILES ${CMAKE_SOURCE_DIR}/data/krunner-remindme.notifyrc
    DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR}
)
