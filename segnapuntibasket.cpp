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

#include <QtGlobal>
#include <QtNetwork>
#include <QtWidgets>
#include <QProcess>
#include <QWebSocket>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QTime>
#include <QSettings>
#include <QSerialPortInfo>

#include "segnapuntibasket.h"
#include "utility.h"

//#define QT_DEBUG
#define LOG_MESG


#define ACK char(255)
#define AreYouThere    0xAA
#define Stop           0x01
#define Start          0x02
#define NewPeriod      0x11
#define StopSending    0x81


SegnapuntiBasket::SegnapuntiBasket(QUrl _serverUrl, QFile *_logFile, bool bReflected)
    : ScorePanel(_serverUrl, _logFile, Q_NULLPTR)
    , isMirrored(bReflected)
    , iServizio(0)
{
    QString sFunctionName = " SegnapuntiBasket::SegnapuntiVolley ";
    Q_UNUSED(sFunctionName)

    connect(pServerSocket, SIGNAL(textMessageReceived(QString)),
            this, SLOT(onTextMessageReceived(QString)));
    connect(pServerSocket, SIGNAL(binaryMessageReceived(QByteArray)),
            this, SLOT(onBinaryMessageReceived(QByteArray)));

    // Arduino Serial Port
    baudRate = QSerialPort::Baud115200;
    waitTimeout = 1000;
    responseData.clear();

    if(ConnectToArduino()) {
        logMessage(logFile,
                   sFunctionName,
                   QString("No Arduino ready to use !"));
    }
    else {
        responseData.clear();
        connect(&serialPort, SIGNAL(readyRead()),
                this, SLOT(onSerialDataAvailable()));
        QByteArray requestData;
        requestData.append(char(NewPeriod));
//        requestData.append(char(10));// Ten minutes
        requestData.append(char(1));// One minute for tests
        requestData.append(char(24));// 24 seconds
        serialPort.write(requestData.append(char(127)));
    }

    pSettings = new QSettings(tr("Gabriele Salvato"), tr("Segnapunti Basket"));
    mySize = size();

    pal = QWidget::palette();
    pal.setColor(QPalette::Window,        Qt::black);
    pal.setColor(QPalette::WindowText,    Qt::yellow);
    pal.setColor(QPalette::Base,          Qt::black);
    pal.setColor(QPalette::AlternateBase, Qt::blue);
    pal.setColor(QPalette::Text,          Qt::yellow);
    pal.setColor(QPalette::BrightText,    Qt::white);
    setPalette(pal);

#ifdef Q_OS_ANDROID
    iTimeoutFontSize   = 28;
    iTimeFontSize      = 28;
    iTeamFontSize      = 28;
    iTeamFoulsFontSize = 28;
#else
    iTimeoutFontSize   = 96;
    iTimeFontSize      =160;
    iTeamFontSize      = 66;
    iTeamFoulsFontSize = 66;
#endif

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(createPanel());
    setLayout(mainLayout);
}


SegnapuntiBasket::~SegnapuntiBasket() {
    if(serialPort.isOpen()) {
        serialPort.waitForBytesWritten(1000);
        serialPort.close();
    }
}


void
SegnapuntiBasket::closeEvent(QCloseEvent *event) {
    if(serialPort.isOpen()) {
        QByteArray requestData;
        requestData.append(char(StopSending));
        serialPort.write(requestData.append(char(127)));
    }
    ScorePanel::closeEvent(event);
    event->accept();
}


int
SegnapuntiBasket::ConnectToArduino() {
    QString sFunctionName = " SegnapuntiBasket::ConnectToArduino ";
    Q_UNUSED(sFunctionName)
    QList<QSerialPortInfo> serialPorts = QSerialPortInfo::availablePorts();
    if(serialPorts.isEmpty()) {
        qDebug() << QString("No serial port available");
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
            // so give time to set it up before communicating.
            QThread::sleep(3);
            requestData = QByteArray(2, char(AreYouThere));
            if(WriteRequest(requestData) == 0) {
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
SegnapuntiBasket::WriteRequest(QByteArray requestData) {
    QString sFunctionName = " SegnapuntiBasket::WriteRequest ";
    Q_UNUSED(sFunctionName)
    if(!serialPort.isOpen()) {
        logMessage(logFile,
                   sFunctionName,
                   QString("Serial port %1 has been closed")
                   .arg(serialPort.portName()));
        return -1;
    }
    serialPort.write(requestData.append(char(127)));
    if (serialPort.waitForBytesWritten(waitTimeout)) {
        if (serialPort.waitForReadyRead(waitTimeout)) {
            responseData = serialPort.readAll();
            while(serialPort.waitForReadyRead(1))
                responseData.append(serialPort.readAll());
            if (responseData.at(0) != ACK) {
                QString response(responseData);
                logMessage(logFile,
                           sFunctionName,
                           QString("NACK on Command %1: expecting %2 read %3")
                            .arg(int(requestData.at(0)))
                            .arg(int(ACK))
                            .arg(int(response.at(0).toLatin1())));
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
    return 0;
}


void
SegnapuntiBasket::onSerialDataAvailable() {
    responseData.append(serialPort.readAll());
    while(!serialPort.atEnd()) {
        responseData.append(serialPort.readAll());
    }
    while(responseData.count() > 8) {
        qint32 val = 0;
        long imin, isec, icent;
        QString sVal;
//        for(int i=0; i<4; i++)
//            val += quint8(responseData.at(i)) << i*8;
//        isec = val/100;
//        icent = 10*((val - int(val/100)*100)/10);
////        icent = val - int(val/100)*100;
//        sVal = QString("%1:%2")
//               .arg(isec, 2, 10, QLatin1Char('0'))
//               .arg(icent, 2, 10, QLatin1Char('0'));
////        timeLabel->setText(QString(sVal));

//        val = 0;
        for(int i=4; i<8; i++)
            val += quint8(responseData.at(i)) << i*8;
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
SegnapuntiBasket::resizeEvent(QResizeEvent *event) {
    mySize = event->size();
    event->accept();
}


void
SegnapuntiBasket::onBinaryMessageReceived(QByteArray baMessage) {
    QString sFunctionName = " SegnapuntiBasket::onBinaryMessageReceived ";
    Q_UNUSED(sFunctionName)
    logMessage(logFile,
               sFunctionName,
               QString("Received %1 bytes").arg(baMessage.size()));
    ScorePanel::onBinaryMessageReceived(baMessage);
}


void
SegnapuntiBasket::onTextMessageReceived(QString sMessage) {
    QString sFunctionName = " SegnapuntiBasket::onTextMessageReceived ";
    Q_UNUSED(sFunctionName)
    QString sToken;
    bool ok;
    int iVal;
    QString sNoData = QString("NoData");

    sToken = XML_Parse(sMessage, "team0");
    if(sToken != sNoData){
      team[0]->setText(sToken);
    }// team0

    sToken = XML_Parse(sMessage, "team1");
    if(sToken != sNoData){
      team[1]->setText(sToken);
    }// team1

    sToken = XML_Parse(sMessage, "period");
    if(sToken != sNoData){
      iVal = sToken.toInt(&ok);
      if(!ok || iVal<0 || iVal>3)
        iVal = 8;
      period->display(iVal);
    }// set0

    sToken = XML_Parse(sMessage, "timeout0");
    if(sToken != sNoData){
        iVal = sToken.toInt(&ok);
        if(ok && iVal>=0 && iVal<4) {
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



QGridLayout*
SegnapuntiBasket::createPanel() {
    QGridLayout *layout = new QGridLayout();

    QFont *font;
    QLabel *label;

    // Teams
    font = new QFont("Arial", iTeamFontSize, QFont::Black);
    for(int i=0; i<2; i++) {
        team[i] = new QLabel();
        team[i]->setFont(*font);
        team[i]->setPalette(pal);
        team[i]->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    if(isMirrored) {// Reflect horizontally to respect teams position on the field
        layout->addWidget(team[1],    0,  0,  4, 11, Qt::AlignHCenter|Qt::AlignVCenter);
        layout->addWidget(team[0],    0, 13,  4, 11, Qt::AlignHCenter|Qt::AlignVCenter);
        team[1]->setText(tr("Locali"));
        team[0]->setText(tr("Ospiti"));
    } else {
        layout->addWidget(team[0],    0,  0,  4, 11, Qt::AlignHCenter|Qt::AlignVCenter);
        layout->addWidget(team[1],    0, 13,  4, 11, Qt::AlignHCenter|Qt::AlignVCenter);
        team[0]->setText(tr("Locali"));
        team[1]->setText(tr("Ospiti"));
    }

    // Score
    for(int i=0; i<2; i++){
        score[i] = new QLCDNumber(3);
        score[i]->setSegmentStyle(QLCDNumber::Filled);
        score[i]->setFrameStyle(QFrame::NoFrame);
        score[i]->setPalette(pal);
        score[i]->display(188);
    }
    if(isMirrored) {// Reflect horizontally to respect teams position on the field
        layout->addWidget(score[1],    4,  0,  6,  8);
        layout->addWidget(score[0],    4, 16,  6,  8);

    } else {
        layout->addWidget(score[0],    4,  0,  6,  8);
        layout->addWidget(score[1],    4, 16,  6,  8);
    }

    // Period
    period = new QLCDNumber(1);
    period->setFrameStyle(QFrame::NoFrame);
    period->setPalette(pal);
    period->display(8);
    layout->addWidget(period,  4,  8,  6,  8);

    // Timeouts
    font = new QFont("Arial", iTimeoutFontSize, QFont::Black);
    for(int i=0; i<2; i++) {
        timeout[i] = new QLabel();
        timeout[i]->setFont(*font);
        timeout[i]->setPalette(pal);
        timeout[i]->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        timeout[i]->setText("* * *");
    }
    if(isMirrored) {// Reflect horizontally to respect teams position on the field
        layout->addWidget(timeout[1], 12,  0,  4,  5, Qt::AlignRight|Qt::AlignVCenter);
        layout->addWidget(timeout[0], 12, 19,  4,  5, Qt::AlignLeft|Qt::AlignVCenter);
    } else {
        layout->addWidget(timeout[0], 12,  0,  4,  5, Qt::AlignRight|Qt::AlignVCenter);
        layout->addWidget(timeout[1], 12, 19,  4,  5, Qt::AlignLeft|Qt::AlignVCenter);
    }

    // Time
    font = new QFont("Helvetica", iTimeFontSize, QFont::Black);
    timeLabel = new QLabel("00:00");
    timeLabel->setFont(*font);
    timeLabel->setPalette(pal);
    timeLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(timeLabel, 10,  8, 10,  8, Qt::AlignHCenter|Qt::AlignVCenter);

    // Team Fouls
    font = new QFont("Arial", iTeamFoulsFontSize, QFont::Black);
    label = new QLabel("Team Fouls");
    label->setFont(*font);
    label->setPalette(pal);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    for(int i=0; i<2; i++) {
        teamFouls[i] = new QLCDNumber(1);
        teamFouls[i]->setFrameStyle(QFrame::NoFrame);
        teamFouls[i]->setPalette(pal);
        teamFouls[i]->display(0);
    }
    if(isMirrored) {// Reflect horizontally to respect teams position on the field
        layout->addWidget(teamFouls[1], 22,  3,  2,  2);
        layout->addWidget(label,  23,  5,  1, 15, Qt::AlignHCenter|Qt::AlignVCenter);
        layout->addWidget(teamFouls[0], 22, 20,  2,  2);
    } else {
        layout->addWidget(teamFouls[0], 22,  3,  2,  2);
        layout->addWidget(label,  23,  5,  1, 15, Qt::AlignHCenter|Qt::AlignVCenter);
        layout->addWidget(teamFouls[1], 22, 20,  2,  2);
    }
    return layout;
}
