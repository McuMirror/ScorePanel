#include <QNetworkInterface>
#include <QWebSocket>
#include <QFile>
#include <QMessageBox>
#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QApplication>
#include <QDesktopWidget>

#include "chooserwidget.h"
#include "serverdiscoverer.h"
#include "nonetwindow.h"
#include "scorepanel.h"
#include "segnapuntivolley.h"
#include "segnapuntibasket.h"
#include "segnapuntihandball.h"
#include "utility.h"


#define CONNECTION_TIME       3000  // Not to be set too low for coping with slow networks
#define NETWORK_CHECK_TIME    3000


chooserWidget::chooserWidget(QWidget *parent)
    : QObject(parent)
    , logFile(Q_NULLPTR)
    , pServerDiscoverer(Q_NULLPTR)
    , pNoNetWindow(Q_NULLPTR)
    , pServerSocket(Q_NULLPTR)
    , pScorePanel(Q_NULLPTR)
{
    QTime time(QTime::currentTime());
    qsrand(uint(time.msecsSinceStartOfDay()));

    QString sBaseDir;
#ifdef Q_OS_ANDROID
    sBaseDir = QStandardPaths::displayName(QStandardPaths::TempLocation);
#else
    sBaseDir = QDir::homePath();
#endif
    if(!sBaseDir.endsWith(QString("/"))) sBaseDir+= QString("/");
    logFileName = QString("%1score_panel.txt").arg(sBaseDir);

#ifndef Q_OS_ANDROID
    PrepareLogFile();
#endif

    startTimer.setSingleShot(true);
    connect(&startTimer, SIGNAL(timeout()),
            this, SLOT(onStart()));
    startTimer.start(3000);
}


void
chooserWidget::onStart() {
    pNoNetWindow = new NoNetWindow(Q_NULLPTR);

    // Creating a periodic Server Discovery Service
    pServerDiscoverer = new ServerDiscoverer(logFile);
    connect(pServerDiscoverer, SIGNAL(serverFound(QString, int)),
            this, SLOT(onServerFound(QString, int)));

    // This timer allow retrying connection attempts
    connect(&connectionTimer, SIGNAL(timeout()),
            this, SLOT(onConnectionTimerElapsed()));

    // This timer allow periodic check of ready network
    connect(&networkReadyTimer, SIGNAL(timeout()),
            this, SLOT(onTimeToCheckNetwork()));

    startServerDiscovery();
}


chooserWidget::~chooserWidget() {
}


void
chooserWidget::startServerDiscovery() {
    // Is the network available ?
    if(isConnectedToNetwork()) {// Yes. Start the Connection Attempts
        networkReadyTimer.stop();
        if(!pServerDiscoverer->Discover()) {
            pNoNetWindow->setDisplayedText(tr("Errore: Server Discovery Non Avviato"));
        }
        else
            pNoNetWindow->setDisplayedText(tr("In Attesa della Connessione con il Server"));
        connectionTime = int(CONNECTION_TIME * (1.0 + (double(qrand())/double(RAND_MAX))));
        connectionTimer.start(connectionTime);
    }
    else {// No. Wait until network become ready
        pNoNetWindow->setDisplayedText(tr("In Attesa della Connessione con la Rete"));
        networkReadyTimer.start(NETWORK_CHECK_TIME);
#ifdef LOG_VERBOSE
        logMessage(logFile,
                   Q_FUNC_INFO,
                   QString(" waiting for network..."));
#endif
    }
    pNoNetWindow->showFullScreen();
}


// We prepare a file to write the session log
bool
chooserWidget::PrepareLogFile() {
#ifdef LOG_MESG
    QFileInfo checkFile(logFileName);
    if(checkFile.exists() && checkFile.isFile()) {
        QDir renamed;
        renamed.remove(logFileName+QString(".bkp"));
        renamed.rename(logFileName, logFileName+QString(".bkp"));
    }
    logFile = new QFile(logFileName);
    if (!logFile->open(QIODevice::WriteOnly)) {
        QMessageBox::information(Q_NULLPTR, "Segnapunti Volley",
                                 QString("Impossibile aprire il file %1: %2.")
                                 .arg(logFileName).arg(logFile->errorString()));
        delete logFile;
        logFile = Q_NULLPTR;
    }
#endif
    return true;
}


// returns true if the network is available
bool
chooserWidget::isConnectedToNetwork() {
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
#ifdef LOG_VERBOSE
    logMessage(logFile,
               Q_FUNC_INFO,
               result ? QString("true") : QString("false"));
#endif
    return result;
}


// Network available retry check
void
chooserWidget::onTimeToCheckNetwork() {
    if(isConnectedToNetwork()) {
        networkReadyTimer.stop();
        pServerDiscoverer->Discover();
        connectionTime = int(CONNECTION_TIME * (1.0 + double(qrand())/double(RAND_MAX)));
        connectionTimer.start(connectionTime);
        pNoNetWindow->setDisplayedText(tr("In Attesa della Connessione con il Server"));
    }
#ifdef LOG_VERBOSE
    else {
        logMessage(logFile,
                   Q_FUNC_INFO,
                   QString("Waiting for network..."));
    }
#endif
    pNoNetWindow->showFullScreen();
}


void
chooserWidget::onServerFound(QString serverUrl, int panelType) {
    connectionTimer.stop();
    if(pScorePanel) {// Delete old instance to prevent memory leaks
        delete pScorePanel;
        pScorePanel = Q_NULLPTR;
    }
    if(panelType == VOLLEY_PANEL) {
        pScorePanel = new SegnapuntiVolley(serverUrl, logFile);
    }
    else if(panelType == BASKET_PANEL) {
        pScorePanel = new SegnapuntiBasket(serverUrl, logFile);
    }
    else if(panelType == HANDBALL_PANEL) {
        pScorePanel = new SegnapuntiHandball(serverUrl, logFile);
    }
    connect(pScorePanel, SIGNAL(panelClosed()),
            this, SLOT(onPanelClosed()));
    connect(pScorePanel, SIGNAL(exitRequest()),
            this, SLOT(onExitProgram()));

    pScorePanel->showFullScreen();
    pNoNetWindow->hide();
}


void
chooserWidget::onExitProgram() {
    logMessage(logFile, Q_FUNC_INFO, QString("Exiting..."));
    connectionTimer.stop();
    if(pServerDiscoverer) {
        pServerDiscoverer->deleteLater();
        pServerDiscoverer = Q_NULLPTR;
    }
    if(pNoNetWindow) delete pNoNetWindow;
    pNoNetWindow = Q_NULLPTR;
    if(logFile)
        logFile->close();
    delete logFile;
    exit(0);
}


void
chooserWidget::onPanelClosed() {
    startServerDiscovery();
    logMessage(logFile, Q_FUNC_INFO, QString("Restarting..."));
}


void
chooserWidget::onConnectionTimerElapsed() {
    if(!isConnectedToNetwork()) {
        pNoNetWindow->setDisplayedText(tr("In Attesa della Connessione con la Rete"));
        connectionTimer.stop();
        networkReadyTimer.start(NETWORK_CHECK_TIME);
#ifdef LOG_VERBOSE
        logMessage(logFile,
                   Q_FUNC_INFO,
                   QString("Waiting for network..."));
#endif
    }
    else {
#ifdef LOG_VERBOSE
        logMessage(logFile,
                   Q_FUNC_INFO,
                   QString("Connection time out... retrying"));
#endif
        pNoNetWindow->showFullScreen();
        pServerDiscoverer->Discover();
    }
}

