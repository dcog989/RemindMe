#include "remindmereminderlistdialog.h"

#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

#include <KLocalizedString>

#include "remindmeengine.h"
#include "remindmetime.h"

namespace
{
// Floor for the window width, so a short list still gets a usable row instead of a narrow strip.
constexpr int minWidth = 480;

// Ceiling for the window width: a longer message is clipped in its row (and shown as its tooltip)
// rather than widening this fixed-size window past the screen.
constexpr int maxWidth = 640;

// The overview opens at its rows' height, up to this share of the available screen; a longer list
// scrolls instead of producing a window taller than the display.
constexpr int maxScreenHeightShare = 3;
}

ReminderListDialog::ReminderListDialog(const QList<Remindme::TimerInfo> &timers, QWidget *parent)
    : QDialog(parent)
    , m_rowCount(timers.size())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(i18n("RemindMe active timers"));
    auto *layout = new QVBoxLayout(this);
    if (timers.isEmpty()) {
        layout->addWidget(new QLabel(i18n("No active timers."), this));
    } else {
        auto *rows = new QWidget;
        auto *rowLayout = new QVBoxLayout(rows);
        for (qsizetype i = 0; i < timers.size(); ++i) {
            // A separator between two reminders only, never above the first or below the last.
            rowLayout->addWidget(makeRow(timers.at(i), i + 1 < timers.size()));
        }
        // Surplus height goes below the rows instead of being spread between them.
        rowLayout->addStretch();

        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setWidget(rows);
        // The rows' size hint is exact because their labels do not wrap: the window opens wide and
        // tall enough to show them all. Rows never scroll sideways; a message longer than the
        // window allows is clipped (and shown as a tooltip).
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setMinimumWidth(qMin(rows->sizeHint().width(), maxWidth));
        layout->addWidget(scroll);
    }

    // A fixed size, in both directions: on Wayland KWin ignores a client's minimum once the user
    // drags an edge and clamps to its own 150 px floor instead, so a window that opens shorter than
    // that could be grown and then never brought back. A fixed-size window cannot be dragged at all.
    // A list too long for the screen scrolls inside it.
    layout->activate();
    const QScreen *screen = QGuiApplication::primaryScreen();
    const int cap = screen ? screen->availableGeometry().height() / maxScreenHeightShare : 0;
    const QSize content = layout->sizeHint();
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
    setFixedSize(qMin(qMax(minWidth, content.width()), maxWidth), cap > 0 ? qMin(content.height(), cap) : content.height());
}

QWidget *ReminderListDialog::makeRow(const Remindme::TimerInfo &timer, bool withSeparator)
{
    auto *row = new QWidget(this);
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    auto *content = new QWidget(row);
    auto *layout = new QHBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);

    // Due time on top, the timer's description underneath. No wrapping: the window widens to the
    // longest line instead, so the row heights the layout reports are the real ones and the whole
    // list opens fully visible.
    auto *text = new QWidget(row);
    auto *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    auto *dueLabel = new QLabel(i18n("Due %1.", Remindme::formatReminderTime(QDateTime::fromMSecsSinceEpoch(timer.deadline), false)), text);
    dueLabel->setTextFormat(Qt::PlainText);
    textLayout->addWidget(dueLabel);

    auto *descriptionLabel = new QLabel(timer.message.isEmpty() ? i18n("No description") : timer.message, text);
    descriptionLabel->setTextFormat(Qt::PlainText);
    descriptionLabel->setToolTip(timer.message.toHtmlEscaped());
    // Italic sets the description apart from the due line above it.
    QFont descriptionFont = descriptionLabel->font();
    descriptionFont.setItalic(true);
    descriptionLabel->setFont(descriptionFont);
    if (timer.message.isEmpty()) {
        // The style's placeholder colour, so the hint reads as an absence rather than a message.
        descriptionLabel->setForegroundRole(QPalette::PlaceholderText);
    }
    textLayout->addWidget(descriptionLabel);
    layout->addWidget(text, 1);

    rowLayout->addWidget(content);

    if (withSeparator) {
        auto *separator = new QFrame(row);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        rowLayout->addWidget(separator);
    }

    auto *cancelButton = new QPushButton(i18n("Cancel timer"), content);
    layout->addWidget(cancelButton);

    // The row goes away once acted on: a cancelled timer is no longer waiting here, and leaving
    // it would show a timer that no longer matches the engine.
    const auto dropRow = [this, row]() {
        row->deleteLater();
        if (--m_rowCount <= 0) {
            close();
        }
    };
    connect(cancelButton, &QPushButton::clicked, this, [this, timer, dropRow]() {
        Q_EMIT cancelRequested(timer);
        dropRow();
    });
    return row;
}
