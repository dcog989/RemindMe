#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class RemindmeEngine;

// Tray icon that exists only while reminders are waiting: clicking it opens the same overview
// "rme list" shows, and its tooltip names the next deadline. It holds no state of its own; the
// engine's timer set is the only source of truth.
class RemindmeTrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit RemindmeTrayIcon(RemindmeEngine &engine, QObject *parent = nullptr);

private:
    void refresh();

    RemindmeEngine &m_engine;
    QSystemTrayIcon m_icon;
};
