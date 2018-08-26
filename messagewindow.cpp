/*
 *
Copyright (C) 2016  Gabriele Salvato

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*/
#include <QDir>
#include <QDebug>
#include <QTime>

#include "messagewindow.h"


#define MOVE_TIME 2000


/*!
 * \brief A Black Message Window with a short message
 * shown, on request, on top of the other windows.
 */
MessageWindow::MessageWindow(QWidget *parent)
    : QWidget(parent)
    , pMyLabel(Q_NULLPTR)
{
    Q_UNUSED(parent);
    setMinimumSize(QSize(320, 240));

    // The palette used by this window and the label shown
    pal = QWidget::palette();
    pal.setColor(QPalette::Window,        Qt::black);
    pal.setColor(QPalette::WindowText,    Qt::yellow);
    pal.setColor(QPalette::Base,          Qt::black);
    pal.setColor(QPalette::AlternateBase, Qt::blue);
    pal.setColor(QPalette::Text,          Qt::yellow);
    pal.setColor(QPalette::BrightText,    Qt::white);
    // Set the used palette
    setPalette(pal);
    setWindowOpacity(0.8);
    // The Label with the message.
    // Since it has a No Parent it is a QWidget that we can move !
    pMyLabel = new QLabel(Q_NULLPTR);
    pMyLabel->setFont(QFont("Arial", 24));
    pMyLabel->setAlignment(Qt::AlignCenter);
    pMyLabel->setPalette(pal);
    pMyLabel->setText(tr("No Text"));

    // Initialize the random number generator
    QTime time(QTime::currentTime());
    qsrand(uint(time.msecsSinceStartOfDay()));

    // The "Move Label" Timer
    connect(&moveTimer, SIGNAL(timeout()),
            this, SLOT(onTimeToMoveLabel()));
    moveTimer.start(MOVE_TIME);
}


MessageWindow::~MessageWindow() {
    moveTimer.disconnect();
    moveTimer.stop();
    delete pMyLabel;
}


/*!
 * \brief MessageWindow::showEvent Starts the moveTimer
 * \param event Unused
 */
void
MessageWindow::showEvent(QShowEvent *event) {
    moveTimer.start(MOVE_TIME);
    pMyLabel->show();
    event->accept();
}


/*!
 * \brief MessageWindow::hideEvent Stops the moveTimer
 * \param event Unused
 */
void
MessageWindow::hideEvent(QHideEvent *event) {
    moveTimer.stop();
    event->accept();
}


/*!
 * \brief MessageWindow::onTimeToMoveLabel
 * periodically invoked by the moveTimer to place the message
 * in different places of the screen
 */
void
MessageWindow::onTimeToMoveLabel() {
    QPoint newPoint = newLabelPosition();
    pMyLabel->move(newPoint);
    pMyLabel->show();
}


/*!
 * \brief MessageWindow::keyPressEvent
 * \param event
 *
 * Pressing Esc will close the window;
 *     "    F1  exit from full screen
 *     "    F2  reenter full screen mode
 */
void
MessageWindow::keyPressEvent(QKeyEvent *event) {
    if(event->key() == Qt::Key_Escape) {
        close();
    }
    if(event->key() == Qt::Key_F1) {
        showNormal();
        event->accept();
        return;
    }
    if(event->key() == Qt::Key_F2) {
        showFullScreen();
        event->accept();
        return;
    }
}


/*!
 * \brief MessageWindow::setDisplayedText
 * \param [in] sNewText: the new text to show in the window
 */
void
MessageWindow::setDisplayedText(QString sNewText) {
    delete pMyLabel;
    // The Label with the message
    pMyLabel = new QLabel(sNewText, this);
    pMyLabel->setFont(QFont("Arial", 24));
    pMyLabel->setAlignment(Qt::AlignCenter);
    pMyLabel->setPalette(pal);
    pMyLabel->move(newLabelPosition());
}


/*!
 * \brief MessageWindow::newLabelPosition
 * \return a QPoint object with the new Message Position
 */
QPoint
MessageWindow::newLabelPosition() {
    QRect labelGeometry = pMyLabel->geometry();
    QPoint newPoint = QPoint(qrand()%(width()-labelGeometry.width()),
                             qrand()%(height()-labelGeometry.height()));
    return newPoint;
}

