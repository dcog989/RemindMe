#pragma once

#include <QMetaType>
#include <QString>

namespace Remindme
{
// One pending or fired reminder. A single value is persisted to the state file and passed with
// the engine's signals, so it is declared as a metatype below.
struct TimerInfo {
    int id = -1;
    qint64 deadline = 0;
    QString message;
};
}

Q_DECLARE_METATYPE(Remindme::TimerInfo)
