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
#include <QSettings>
#include <QGuiApplication>
#include <QScreen>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QSerialPortInfo>
#include <QLCDNumber>
#include <QThread>
#include <QSettings>
#include <QFile>
#include <QUrl>
#include <QWebSocket>


#include "utility.h"
#include "segnapuntihandball.h"

#define QT_NO_CAST_FROM_BYTEARRAY

SegnapuntiHandball::SegnapuntiHandball(QUrl _serverUrl, QFile *_logFile)
    : ScorePanel(_serverUrl, _logFile, Q_NULLPTR)
{
    QString sFunctionName = " SegnapuntiHandball::SegnapuntiHandball ";
    Q_UNUSED(sFunctionName)

    connect(pPanelServerSocket, SIGNAL(textMessageReceived(QString)),
            this, SLOT(onTextMessageReceived(QString)));
    connect(pPanelServerSocket, SIGNAL(binaryMessageReceived(QByteArray)),
            this, SLOT(onBinaryMessageReceived(QByteArray)));

    // Arduino Serial Port
    baudRate = QSerialPort::Baud115200;
    waitTimeout = 300;
    responseData.clear();

    if(ConnectToArduino()) {
        logMessage(logFile,
                   sFunctionName,
                   QString("No Arduino ready to use !"));
    }
    else {
        connect(&serialPort, SIGNAL(readyRead()),
                this, SLOT(onSerialDataAvailable()));
    }

    pSettings = new QSettings(tr("Gabriele Salvato"), tr("Segnapunti Handball"));

    pal = QWidget::palette();
    pal.setColor(QPalette::Window,        Qt::black);
    pal.setColor(QPalette::WindowText,    Qt::yellow);
    pal.setColor(QPalette::Base,          Qt::black);
    pal.setColor(QPalette::AlternateBase, Qt::blue);
    pal.setColor(QPalette::Text,          Qt::yellow);
    pal.setColor(QPalette::BrightText,    Qt::white);
    setPalette(pal);

    maxTeamNameLen = 15;

#ifdef Q_OS_ANDROID
    iTimeoutFontSize   = 28;
    iTimeFontSize      = 28;
    iTeamFontSize      = 28;
    iTeamFoulsFontSize = 28;
    iBonusFontSize     = 28;
#else
    buildFontSizes();
#endif
    createPanelElements();
    buildLayout();
}


SegnapuntiHandball::~SegnapuntiHandball() {
    if(serialPort.isOpen()) {
        serialPort.waitForBytesWritten(1000);
        serialPort.close();
    }
    if(pSettings != Q_NULLPTR) delete pSettings;
}


void
SegnapuntiHandball::closeEvent(QCloseEvent *event) {
    if(serialPort.isOpen()) {
        QByteArray requestData;
        requestData.append(char(StopSending));
        serialPort.write(requestData.append(char(127)));
    }
    ScorePanel::closeEvent(event);
    if(pSettings != Q_NULLPTR) delete pSettings;
    pSettings = Q_NULLPTR;
    event->accept();
}


void
SegnapuntiHandball::resizeEvent(QResizeEvent *event) {
    event->accept();
}


int
SegnapuntiHandball::ConnectToArduino() {
    QString sFunctionName = " SegnapuntiHandball::ConnectToArduino ";
    Q_UNUSED(sFunctionName)
    QList<QSerialPortInfo> serialPorts = QSerialPortInfo::availablePorts();
    if(serialPorts.isEmpty()) {
        logMessage(logFile,
                   sFunctionName,
                   QString("No serial port available"));
        return -1;
    }
    bool found = false;
    QSerialPortInfo info;
    QByteArray requestData;
    for(int i=0; i<serialPorts.size()&& !found; i++) {
        info = serialPorts.at(i);
        if(!info.portName().contains("tty")) continue;
        serialPort.setPortName(info.portName());
        if(serialPort.isOpen()) continue;
        serialPort.setBaudRate(115200);
        serialPort.setDataBits(QSerialPort::Data8);
        if(serialPort.open(QIODevice::ReadWrite)) {
            // Arduino will be reset upon a serial connectiom
            // so give it time to set it up before communicating.
            QThread::sleep(3);
            requestData = QByteArray(2, quint8(AreYouThere));
            if(writeSerialRequest(requestData) == 0) {
                found = true;
                break;
            }
            else
                serialPort.close();
        }
    }
    if(!found)
        return -1;
    logMessage(logFile,
               sFunctionName,
               QString("Arduino found at: %1")
               .arg(info.portName()));
    return 0;
}


int
SegnapuntiHandball::writeSerialRequest(QByteArray requestData) {
    QString sFunctionName = " SegnapuntiHandball::WriteRequest ";
    Q_UNUSED(sFunctionName)
    if(!serialPort.isOpen()) {
        logMessage(logFile,
                   sFunctionName,
                   QString("Serial port %1 has been closed")
                   .arg(serialPort.portName()));
        return -1;
    }
    serialPort.clear();
    responseData.clear();
    serialPort.write(requestData.append(quint8(127)));
    if (serialPort.waitForBytesWritten(waitTimeout)) {
        if (serialPort.waitForReadyRead(waitTimeout)) {
            responseData = serialPort.readAll();
            while(serialPort.waitForReadyRead(1))
                responseData.append(serialPort.readAll());
            if (quint8(responseData[0]) != quint8(Ack)) {
                QString response(responseData);
                logMessage(logFile,
                           sFunctionName,
                           QString("NACK on Command %1: expecting %2 read %3")
                            .arg(quint8(requestData[0]))
                            .arg(quint8(Ack))
                            .arg(quint8(response[0].toLatin1())));
                return -1;
            }
        }
        else {// Read timeout
            logMessage(logFile,
                       sFunctionName,
                       QString(" Wait read response timeout %1 %2")
                       .arg(QTime::currentTime().toString())
                       .arg(serialPort.portName()));
            return -1;
        }
    }
    else {// Write timeout
        logMessage(logFile,
                   sFunctionName,
                   QString(" Wait write request timeout %1 %2")
                   .arg(QTime::currentTime().toString())
                   .arg(serialPort.portName()));
        return -1;
    }
    responseData.remove(0, 1);
    return 0;
}


void
SegnapuntiHandball::onSerialDataAvailable() {
    responseData.append(serialPort.readAll());
    while(!serialPort.atEnd()) {
        responseData.append(serialPort.readAll());
    }
    qint32 val = 0;
    while(responseData.count() > 8) {
        long imin, isec, icent;
        QString sVal;

//        val = 0;
//        for(int i=0; i<4; i++)
//            val += quint8(responseData.at(i)) << i*8;
//        isec = val/100;
//        icent = 10*((val - int(val/100)*100)/10);
////        icent = val - int(val/100)*100;
//        sVal = QString("%1:%2")
//               .arg(isec, 2, 10, QLatin1Char('0'))
//               .arg(icent, 2, 10, QLatin1Char('0'));
////        timeLabel->setText(QString(sVal));

        val = 0;
        for(int i=4; i<8; i++)
            val += quint8(responseData[i]) << (i-4)*8;
        imin = val/6000;
        isec = (val-imin*6000)/100;
        icent = 10*((val - isec*100)/10);
        if(imin > 0) {
            sVal = QString("%1:%2")
                    .arg(imin, 2, 10, QLatin1Char('0'))
                    .arg(isec, 2, 10, QLatin1Char('0'));
        }
        else {
            sVal = QString("%1:%2")
                    .arg(isec, 2, 10, QLatin1Char('0'))
                    .arg(icent, 2, 10, QLatin1Char('0'));
        }
        timeLabel->setText(QString(sVal));
        responseData.remove(0, 8);
    }
}


void
SegnapuntiHandball::buildFontSizes() {
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect  screenGeometry = screen->geometry();
    int width = screenGeometry.width();
    iTeamFontSize = 100;
    for(int i=12; i<100; i++) {
        QFontMetrics f(QFont("Arial", i, QFont::Black));
        int rW = f.maxWidth()*maxTeamNameLen;
        if(rW > width/2) {
            iTeamFontSize = i-1;
            break;
        }
    }
    iTimeoutFontSize = 100;
    for(int i=12; i<300; i++) {
        QFontMetrics f(QFont("Arial", i, QFont::Black));
        int rW = f.width("* * * ");
        if(rW > width/6) {
            iTimeoutFontSize = i-1;
            break;
        }
    }
    iTimeFontSize = 300;
    for(int i=12; i<300; i++) {
        QFontMetrics f(QFont("Helvetica", i, QFont::Black));
        int rW = f.width("00:00");
        if(rW > width/2) {
            iTimeFontSize = i-1;
            break;
        }
    }
}


void
SegnapuntiHandball::onBinaryMessageReceived(QByteArray baMessage) {
    QString sFunctionName = " SegnapuntiHandball::onBinaryMessageReceived ";
    Q_UNUSED(sFunctionName)
    logMessage(logFile,
               sFunctionName,
               QString("Received %1 bytes").arg(baMessage.size()));
    ScorePanel::onBinaryMessageReceived(baMessage);
}


void
SegnapuntiHandball::onTextMessageReceived(QString sMessage) {
    QString sFunctionName = " SegnapuntiHandball::onTextMessageReceived ";
    Q_UNUSED(sFunctionName)
    QString sToken;
    bool ok;
    int iVal;
    QString sNoData = QString("NoData");

    sToken = XML_Parse(sMessage, "team0");
    if(sToken != sNoData){
      team[0]->setText(sToken.left(maxTeamNameLen));
      int width = QGuiApplication::primaryScreen()->geometry().width();
      iVal = 100;
      for(int i=12; i<100; i++) {
          QFontMetrics f(QFont("Arial", i, QFont::Black));
          int rW = f.width(team[0]->text()+"  ");
          if(rW > width/2) {
              iVal = i-1;
              break;
          }
      }
      team[0]->setFont(QFont("Arial", iVal, QFont::Black));
    }// team0

    sToken = XML_Parse(sMessage, "team1");
    if(sToken != sNoData){
      team[1]->setText(sToken.left(maxTeamNameLen));
      int width = QGuiApplication::primaryScreen()->geometry().width();
      iVal = 100;
      for(int i=12; i<100; i++) {
          QFontMetrics f(QFont("Arial", i, QFont::Black));
          int rW = f.width(team[1]->text()+"  ");
          if(rW > width/2) {
              iVal = i-1;
              break;
          }
      }
      team[1]->setFont(QFont("Arial", iVal, QFont::Black));
    }// team1

    sToken = XML_Parse(sMessage, "period");
    if(sToken != sNoData) {
        QStringList sArgs = sToken.split(",", QString::SkipEmptyParts);
        iVal = sArgs.at(0).toInt(&ok);
        if(!ok || iVal<0 || iVal>99)
            iVal = 99;
        period->display(iVal);
        iVal = sArgs.at(1).toInt(&ok);
        if(!ok || iVal<0 || iVal>10)
            iVal = 10;
        QByteArray requestData;
        requestData.append(char(NewPeriod));
        requestData.append(char(iVal));
        requestData.append(char(24));// 24 seconds
        if(serialPort.isOpen())
            serialPort.write(requestData.append(char(127)));
    }// period

    sToken = XML_Parse(sMessage, "timeout0");
    if(sToken != sNoData){
        iVal = sToken.toInt(&ok);
        if(ok && iVal>=0 && iVal<4) {
            timeout[0]->clear();
            QString sTimeout = QString();
            for(int i=0; i<iVal; i++)
                sTimeout += QString("* ");
            timeout[0]->setText(sTimeout);
        }
    }// timeout0

    sToken = XML_Parse(sMessage, "timeout1");
    if(sToken != sNoData){
        iVal = sToken.toInt(&ok);
        if(ok && iVal>=0 && iVal<4) {
            timeout[1]->clear();
            QString sTimeout = QString();
            for(int i=0; i<iVal; i++)
                sTimeout += QString("* ");
            timeout[1]->setText(sTimeout);
        }
    }// timeout1

    sToken = XML_Parse(sMessage, "score0");
    if(sToken != sNoData){
      iVal = sToken.toInt(&ok);
      if(!ok || iVal<0 || iVal>999)
        iVal = 999;
      score[0]->display(iVal);
    }// score0

    sToken = XML_Parse(sMessage, "score1");
    if(sToken != sNoData){
      iVal = sToken.toInt(&ok);
      if(!ok || iVal<0 || iVal>999)
        iVal = 999;
      score[1]->display(iVal);
    }// score1

    ScorePanel::onTextMessageReceived(sMessage);
}


void
SegnapuntiHandball::createPanelElements() {
    // Teams
    for(int i=0; i<2; i++) {
        team[i] = new QLabel(QString(maxTeamNameLen, 'W'));
        team[i]->setFont(QFont("Arial", iTeamFontSize, QFont::Black));
        team[i]->setPalette(pal);
        team[i]->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    team[0]->setText(tr("Locali"));
    team[1]->setText(tr("Ospiti"));
    // Score
    for(int i=0; i<2; i++){
        score[i] = new QLCDNumber(3);
        score[i]->setSegmentStyle(QLCDNumber::Filled);
        score[i]->setFrameStyle(QFrame::NoFrame);
        score[i]->setPalette(pal);
        score[i]->display(188);
    }
    // Period
    period = new QLCDNumber(2);
    period->setFrameStyle(QFrame::NoFrame);
    period->setPalette(pal);
    period->display(88);
    // Timeouts
    for(int i=0; i<2; i++) {
        timeout[i] = new QLabel();
        timeout[i]->setFont(QFont("Arial", iTimeoutFontSize, QFont::Black));
        timeout[i]->setPalette(pal);
        timeout[i]->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        timeout[i]->setText("* * *");
    }
    // Time
    timeLabel = new QLabel("00:00");
    timeLabel->setFont(QFont("Helvetica", iTimeFontSize, QFont::Black));
    timeLabel->setPalette(pal);
    timeLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}


QGridLayout*
SegnapuntiHandball::createPanel() {
    // The panel is a (22x24) grid
    QGridLayout *layout = new QGridLayout();

    if(isMirrored) {// Reflect horizontally to respect teams position on the field
        // Teams
        layout->addWidget(team[1],       0,  0,  4, 12, Qt::AlignHCenter|Qt::AlignVCenter);
        layout->addWidget(team[0],       0, 12,  4, 12, Qt::AlignHCenter|Qt::AlignVCenter);
        // Score
        layout->addWidget(score[1],      4,  0,  6,  6);
        layout->addWidget(score[0],      4, 18,  6,  6);
        // Timeouts
        layout->addWidget(timeout[1],   12,  0,  3,  5, Qt::AlignRight|Qt::AlignVCenter);
        layout->addWidget(timeout[0],   12, 19,  3,  5, Qt::AlignLeft|Qt::AlignVCenter);
    }
    else {
        // Teams
        layout->addWidget(team[0],       0,  0,  4, 12, Qt::AlignHCenter|Qt::AlignVCenter);
        layout->addWidget(team[1],       0, 12,  4, 12, Qt::AlignHCenter|Qt::AlignVCenter);
        // Score
        layout->addWidget(score[0],      4,  0,  6,  6);
        layout->addWidget(score[1],      4, 18,  6,  6);
        // Timeouts
        layout->addWidget(timeout[0],   12,  0,  3,  5, Qt::AlignRight|Qt::AlignVCenter);
        layout->addWidget(timeout[1],   12, 19,  3,  5, Qt::AlignLeft|Qt::AlignVCenter);
    }
    // Period
    layout->addWidget(period,            4, 10,  6,  4);
    // Time
    layout->addWidget(timeLabel,        10,  5, 10, 14, Qt::AlignHCenter|Qt::AlignVCenter);

    return layout;
}
