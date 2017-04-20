#include <QNetworkInterface>
#include <QWebSocket>
#include <QFile>
#include <QMessageBox>
#include <QDir>


#include "chooserwidget.h"
#include "serverdiscoverer.h"
#include "nonetwindow.h"
#include "scorepanel.h"
#include "segnapuntivolley.h"
#include "segnapuntibasket.h"
#include "utility.h"


#define VOLLEY_PANEL 0
#define FIRST_PANEL  VOLLEY_PANEL
#define BASKET_PANEL 1
#define LAST_PANEL   BASKET_PANEL

#define CONNECTION_TIME       10000  // Not to be set too low for coping with slow networks
#define NETWORK_CHECK_TIME    3000

#define LOG_MESG

chooserWidget::chooserWidget(QWidget *parent)
    : QWidget(parent)
    , logFile(Q_NULLPTR)
    , pServerSocket(Q_NULLPTR)
    , pScorePanel(Q_NULLPTR)
{
    QString sFunctionName = QString(" chooserWidget::chooserWidget ");
    Q_UNUSED(sFunctionName)

    QString sBaseDir;
#ifdef Q_OS_ANDROID
    sBaseDir = QString("/storage/extSdCard/");
#else
    sBaseDir = QDir::homePath();
    if(!sBaseDir.endsWith(QString("/"))) sBaseDir+= QString("/");
#endif
    logFileName = QString("%1score_panel.txt").arg(sBaseDir);

#ifndef Q_OS_ANDROID
    PrepareLogFile();
#endif

    pNoNetWindow = new NoNetWindow(this);
    startServerDiscovery();
}


chooserWidget::~chooserWidget() {
}


void
chooserWidget::startServerDiscovery() {
    QString sFunctionName = QString(" chooserWidget::startServerDiscovery ");
    Q_UNUSED(sFunctionName)
    // Creating a periodic Server Discovery Service
    pServerDiscoverer = new ServerDiscoverer(logFile);
    connect(pServerDiscoverer, SIGNAL(serverFound(QString)),
            this, SLOT(onServerFound(QString)));

    // This timer allow retrying connection attempts
    connect(&connectionTimer, SIGNAL(timeout()),
            this, SLOT(onConnectionTimerElapsed()));

    // This timer allow periodic check of ready network
    connect(&networkReadyTimer, SIGNAL(timeout()),
            this, SLOT(onTimeToCheckNetwork()));

    // Is the network available ?
    if(!isConnectedToNetwork()) {// No. Wait until network become ready
        pNoNetWindow->setDisplayedText(tr("In Attesa della Connessione con la Rete"));
        networkReadyTimer.start(NETWORK_CHECK_TIME);
        logMessage(logFile,
                   sFunctionName,
                   QString(" waiting for network..."));
    }
    else {// Yes. Start the Connection Attempts
        networkReadyTimer.stop();
        pServerDiscoverer->Discover();
        connectionTime = int(CONNECTION_TIME * (1.0 + double(qrand())/double(RAND_MAX)));
        connectionTimer.start(connectionTime);
        pNoNetWindow->setDisplayedText(tr("In Attesa della Connessione con il Server"));
    }

    pNoNetWindow->showFullScreen();
}


// We prepare a file to write the session log
bool
chooserWidget::PrepareLogFile() {
#ifdef LOG_MESG
    QString sFunctionName = " chooserWidget::PrepareLogFile ";
    Q_UNUSED(sFunctionName)

    QFileInfo checkFile(logFileName);
    if(checkFile.exists() && checkFile.isFile()) {
        QDir renamed;
        renamed.remove(logFileName+QString(".bkp"));
        renamed.rename(logFileName, logFileName+QString(".bkp"));
    }
    logFile = new QFile(logFileName);
    if (!logFile->open(QIODevice::WriteOnly)) {
        QMessageBox::information(this, tr("Segnapunti Volley"),
                                 tr("Impossibile aprire il file %1: %2.")
                                 .arg(logFileName).arg(logFile->errorString()));
        delete logFile;
        logFile = NULL;
    }
#endif
    return true;
}


// returns true if the network is available
bool
chooserWidget::isConnectedToNetwork() {
    QString sFunctionName = " chooserWidget::isConnectedToNetwork ";
    Q_UNUSED(sFunctionName)
    QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    bool result = false;

    for(int i=0; i<ifaces.count(); i++) {
        QNetworkInterface iface = ifaces.at(i);
        if(iface.flags().testFlag(QNetworkInterface::IsUp) &&
           iface.flags().testFlag(QNetworkInterface::IsRunning) &&
           iface.flags().testFlag(QNetworkInterface::CanMulticast) &&
          !iface.flags().testFlag(QNetworkInterface::IsLoopBack))
        {
            for(int j=0; j<iface.addressEntries().count(); j++) {
                // we have an interface that is up, and has an ip address
                // therefore the link is present
                if(result == false)
                    result = true;
            }
        }
    }
    logMessage(logFile,
               sFunctionName,
               result ? QString("true") : QString("false"));
    return result;
}


// Network available retry check
void
chooserWidget::onTimeToCheckNetwork() {
    QString sFunctionName = " chooserWidget::onTimeToCheckNetwork ";
    Q_UNUSED(sFunctionName)
    if(!isConnectedToNetwork()) {
        logMessage(logFile,
                   sFunctionName,
                   QString("Waiting for network..."));
    }
    else {
        networkReadyTimer.stop();
        pServerDiscoverer->Discover();
        connectionTime = int(CONNECTION_TIME * (1.0 + double(qrand())/double(RAND_MAX)));
        connectionTimer.start(connectionTime);
        pNoNetWindow->setDisplayedText(tr("In Attesa della Connessione con il Server"));
    }
    pNoNetWindow->showFullScreen();
}


void
chooserWidget::onServerFound(QString sUrl) {
    QString sFunctionName = " chooserWidget::onServerFound ";
    Q_UNUSED(sFunctionName)

    // Let's try to connect to server
    pServerSocket = new QWebSocket();
    connect(pServerSocket, SIGNAL(connected()),
            this, SLOT(onServerConnected()));
    connect(pServerSocket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(onServerSocketError(QAbstractSocket::SocketError)));

    pServerSocket->open(QUrl(sUrl));
}


void
chooserWidget::onServerConnected() {
    QString sFunctionName = " chooserWidget::onServerConnected ";
    Q_UNUSED(sFunctionName)
    connectionTimer.stop();

    serverUrl = pServerSocket->requestUrl();
    logMessage(logFile,
               sFunctionName,
               QString("WebSocket connected to: %1")
               .arg(serverUrl.toString()));

    connect(pServerSocket, SIGNAL(textMessageReceived(QString)),
            this, SLOT(onTextMessageReceived(QString)));
    connect(pServerSocket, SIGNAL(disconnected()),
            this, SLOT(onServerDisconnected()));

    pNoNetWindow->setDisplayedText(tr("In Attesa della Configurazione"));

    QString sMessage;
    sMessage = QString("<getConf>1</getConf>");
    qint64 bytesSent = pServerSocket->sendTextMessage(sMessage);
    if(bytesSent != sMessage.length()) {
        logMessage(logFile,
                   sFunctionName,
                   QString("Unable to ask panel configuration"));
    }
}


void
chooserWidget::onServerSocketError(QAbstractSocket::SocketError error) {
    QString sFunctionName = " chooserWidget::onServerSocketError ";

    logMessage(logFile,
               sFunctionName,
               QString("%1 %2 Error %3")
               .arg(pServerSocket->peerAddress().toString())
               .arg(pServerSocket->errorString())
               .arg(error));
    if(!disconnect(pServerSocket, 0, 0, 0)) {
        logMessage(logFile,
                   sFunctionName,
                   QString("Unable to disconnect signals from Sever Socket"));
    }
    if(pServerSocket->isValid())
        pServerSocket->close(QWebSocketProtocol::CloseCodeAbnormalDisconnection, pServerSocket->errorString());

    if(!isConnectedToNetwork()) {
        pNoNetWindow->setDisplayedText(tr("In Attesa della Connessione con la Rete"));
        networkReadyTimer.start(NETWORK_CHECK_TIME);
        logMessage(logFile,
                   sFunctionName,
                   QString("Waiting for network..."));
    }
    else {
        networkReadyTimer.stop();
        pServerDiscoverer->Discover();
        connectionTime = int(CONNECTION_TIME * (1.0 + double(qrand())/double(RAND_MAX)));
        connectionTimer.start(connectionTime);
        pNoNetWindow->setDisplayedText(tr("In Attesa della Connessione con il Server"));
    }
    pNoNetWindow->showFullScreen();
}


void
chooserWidget::onServerDisconnected() {
    QString sFunctionName = " ScorePanel::onServerDisconnected ";

    logMessage(logFile,
               sFunctionName,
               QString("WebSocket disconnected !"));
    connectionTime = int(CONNECTION_TIME * (1.0 + double(qrand())/double(RAND_MAX)));

    if(!isConnectedToNetwork()) {
        pNoNetWindow->setDisplayedText(tr("In Attesa della Connessione con la Rete"));
        networkReadyTimer.start(NETWORK_CHECK_TIME);
        logMessage(logFile,
                   sFunctionName,
                   QString("Waiting for network..."));
    }
    else {
        networkReadyTimer.stop();
        pServerDiscoverer->Discover();
        connectionTime = int(CONNECTION_TIME * (1.0 + double(qrand())/double(RAND_MAX)));
        connectionTimer.start(connectionTime);
        pNoNetWindow->setDisplayedText(tr("In Attesa della Connessione con il Server"));
    }
    pNoNetWindow->showFullScreen();
}


void
chooserWidget::onConnectionTimerElapsed() {
    QString sFunctionName = " chooserWidget::onConnectionTimerElapsed ";
    Q_UNUSED(sFunctionName)

    if(!isConnectedToNetwork()) {
        pNoNetWindow->setDisplayedText(tr("In Attesa della Connessione con la Rete"));
        connectionTimer.stop();
        networkReadyTimer.start(NETWORK_CHECK_TIME);
        logMessage(logFile,
                   sFunctionName,
                   QString("Waiting for network..."));
    }
    else {
        logMessage(logFile,
                   sFunctionName,
                   QString("Connection time out... retrying"));
        pNoNetWindow->showFullScreen();
        pServerDiscoverer->Discover();
    }
}


void
chooserWidget::onTextMessageReceived(QString sMessage) {
    QString sFunctionName = " chooserWidget::onTextMessageReceived ";

    QString sToken;
    bool ok;
    int iVal;
    QString sNoData = QString("NoData");

    // Which game we would like to play ?
    sToken = XML_Parse(sMessage, "setConf");
    if(sToken != sNoData) {
        iVal = sToken.toInt(&ok);
        if(!ok || iVal<FIRST_PANEL || iVal>LAST_PANEL) {
            logMessage(logFile,
                       sFunctionName,
                       QString("Illegal configuration received: %1")
                       .arg(iVal));
            iVal = FIRST_PANEL;
        }
        disconnect(pServerSocket, 0, 0, 0);
        pServerSocket->close();
        pServerSocket->deleteLater();
        if(pScorePanel) {
            delete pScorePanel;
            pScorePanel = Q_NULLPTR;
        }
        if(iVal == VOLLEY_PANEL) {
            pScorePanel = new SegnapuntiVolley(serverUrl, logFile, true);
            pScorePanel->showFullScreen();
            pNoNetWindow->hide();
        }
        else if(iVal == BASKET_PANEL) {
            pScorePanel = new SegnapuntiBasket(serverUrl, logFile, true);
            pScorePanel->showFullScreen();
            pNoNetWindow->hide();
        }
        connect(pScorePanel, SIGNAL(panelClosed()),
                this, SLOT(startServerDiscovery()));
    }// getConf
}