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

#ifndef SEGNAPUNTIVOLLEY_H
#define SEGNAPUNTIVOLLEY_H

#include <QObject>
#include <QWidget>
#include <QList>
#include <QVector>
#include <QFileInfoList>
#include <QUrl>

#include "slidewindow.h"
#include "serverdiscoverer.h"
#include "scorepanel.h"


QT_FORWARD_DECLARE_CLASS(QSettings)
QT_FORWARD_DECLARE_CLASS(QGroupBox)
QT_FORWARD_DECLARE_CLASS(QLCDNumber)
QT_FORWARD_DECLARE_CLASS(QFile)
QT_FORWARD_DECLARE_CLASS(QGridLayout)
QT_FORWARD_DECLARE_CLASS(TimeoutWindow)


class SegnapuntiVolley : public ScorePanel
{
    Q_OBJECT

public:
    SegnapuntiVolley(const QString& myServerUrl, QFile *myLogFile);
    ~SegnapuntiVolley();
    void closeEvent(QCloseEvent *event);
    void changeEvent(QEvent *event);

private:
    QSettings         *pSettings;
    QLabel            *team[2];
    QLCDNumber        *score[2];
    QLabel            *scoreLabel;
    QLCDNumber        *set[2];
    QLabel            *setLabel;
    QLabel            *servizio[2];
    QLCDNumber        *timeout[2];
    QLabel            *timeoutLabel;
    QPalette           pal;
    int                iServizio;
    int                iTimeoutFontSize;
    int                iSetFontSize;
    int                iScoreFontSize;
    int                iServiceFontSize;
    int                iTeamFontSize;
    int                maxTeamNameLen;

    void               createPanelElements();
    QGridLayout*       createPanel();
    TimeoutWindow     *pTimeoutWindow;

private slots:
    void onTextMessageReceived(QString sMessage);
    void onBinaryMessageReceived(QByteArray baMessage);

protected:
    void buildFontSizes();
};

#endif // SEGNAPUNTIVOLLEY_H
