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
#include "fileupdater.h"
#include <QtNetwork>
#include <QTime>
#include <QTimer>

#include "utility.h"



#define RETRY_TIME 15000
#define CHUNK_SIZE 256*1024


/*!
 * \brief FileUpdater::FileUpdater Base Class for the Slides and Spots File Transfer
 * \param sName A string to identify this instance of FileUpdater.
 * \param myServerUrl The Url of the File Server to connect to.
 * \param myLogFile The File for logging (if any).
 * \param parent The parent object.
 *
 * It is responsible to get the Slides and the Spots from the Server.
 * Indeed each panel maintain a local copy of the files.
 * In this way, after an initial delay due to the transfer,
 * no further delays are expected for launching the Slide or Movie Show.
 */
FileUpdater::FileUpdater(QString sName, QUrl myServerUrl, QFile *myLogFile, QObject *parent)
    : QObject(parent)
    , logFile(myLogFile)
    , serverUrl(myServerUrl)
{
    sMyName = sName;
    pUpdateSocket = Q_NULLPTR;
    destinationDir = QString(".");
    bytesReceived = 0;
}


/*!
 * \brief FileUpdater::setDestination Set the File destination Folder.
 * \param myDstinationDir The destination Folder
 * \param sExtensions The file extensions to look for
 * \return true if the Folder is ok; false otherwise
 *
 *  If the Folder does not exists it will be created
 */
bool
FileUpdater::setDestination(QString myDstinationDir, QString sExtensions) {
    destinationDir = myDstinationDir;
    sFileExtensions = sExtensions;
    QDir outDir(destinationDir);
    if(!outDir.exists()) {
        logMessage(logFile,
                   Q_FUNC_INFO,
                   QString("Creating new directory: %1")
                   .arg(destinationDir));
        if(!outDir.mkdir(destinationDir)) {
            logMessage(logFile,
                       Q_FUNC_INFO,
                       QString("Unable to create directory: %1")
                       .arg(destinationDir));
            return false;
        }
    }
    return true;
}


/*!
 * \brief FileUpdater::startUpdate Try to connect asynchronously to the File Server
 */
void
FileUpdater::startUpdate() {
    logMessage(logFile,
               Q_FUNC_INFO,
               sMyName +
               QString(" Connecting to file server: %1")
               .arg(serverUrl.toString()));

    pUpdateSocket = new QWebSocket();

    connect(pUpdateSocket, SIGNAL(connected()),
            this, SLOT(onUpdateSocketConnected()));
    connect(pUpdateSocket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(onUpdateSocketError(QAbstractSocket::SocketError)));
    connect(pUpdateSocket, SIGNAL(textMessageReceived(QString)),
            this, SLOT(onProcessTextMessage(QString)));
    connect(pUpdateSocket, SIGNAL(binaryFrameReceived(QByteArray, bool)),
            this,SLOT(onProcessBinaryFrame(QByteArray, bool)));
    connect(pUpdateSocket, SIGNAL(disconnected()),
            this, SLOT(onServerDisconnected()));

    pUpdateSocket->ignoreSslErrors();
    pUpdateSocket->open(QUrl(serverUrl));
}


/*!
 * \brief FileUpdater::onUpdateSocketConnected
 * invoked asynchronusly when the socket connects
 */
void
FileUpdater::onUpdateSocketConnected() {
    logMessage(logFile,
               Q_FUNC_INFO,
               sMyName +
               QString(" Connected to: %1")
               .arg(pUpdateSocket->peerAddress().toString()));
    // Query the file's list
    askFileList();
}


/*!
 * \brief FileUpdater::askFileList
 * Ask the file server for a list of files to transfer
 */
void
FileUpdater::askFileList() {
    if(pUpdateSocket->isValid()) {
        QString sMessage;
        sMessage = QString("<send_file_list>1</send_file_list>");
        qint64 bytesSent = pUpdateSocket->sendTextMessage(sMessage);
        if(bytesSent != sMessage.length()) {
            logMessage(logFile,
                       Q_FUNC_INFO,
                       sMyName +
                       QString(" Unable to ask for file list"));
            returnCode = SOCKET_ERROR;
            thread()->exit(returnCode);
            return;
        }
        else {
            logMessage(logFile,
                       Q_FUNC_INFO,
                       sMyName +
                       QString(" Sent %1 to %2")
                       .arg(sMessage)
                       .arg(pUpdateSocket->peerAddress().toString()));
        }
    }
}

/*!
 * \brief FileUpdater::onServerDisconnected
 * invoked asynchronously whe the server disconnects
 */
void
FileUpdater::onServerDisconnected() {
    logMessage(logFile,
               Q_FUNC_INFO,
               sMyName +
               QString(" WebSocket disconnected from: %1")
               .arg(pUpdateSocket->peerAddress().toString()));
    returnCode = SERVER_DISCONNECTED;
    thread()->exit(returnCode);
}


/*!
 * \brief FileUpdater::onUpdateSocketError
 * File transfer error handler
 * \param error The socket error
 */
void
FileUpdater::onUpdateSocketError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error)
    
    logMessage(logFile,
               Q_FUNC_INFO,
               sMyName +
               QString(" %1 %2 Error %3")
               .arg(pUpdateSocket->localAddress().toString())
               .arg(pUpdateSocket->errorString())
               .arg(error));
    returnCode = SOCKET_ERROR;
    thread()->exit(returnCode);
}


/*!
 * \brief FileUpdater::onProcessBinaryFrame
 * Invoked asynchronously when a binary chunk of information is available
 * \param baMessage [in] the chunk of information
 * \param isLastFrame [in] is this the last chunk ?
 */
void
FileUpdater::onProcessBinaryFrame(QByteArray baMessage, bool isLastFrame) {
    QString sMessage;
    // Check if the file transfer must be stopped
    if(thread()->isInterruptionRequested()) {
        logMessage(logFile,
                   Q_FUNC_INFO,
                   sMyName +
                   QString(" Received an exit request"));
        returnCode = TRANSFER_DONE;
        thread()->exit(returnCode);
        return;
    }
    int len;
    qint64 written;
    if(bytesReceived == 0) {// It's a new file...
        QByteArray header = baMessage.left(1024);// Get the header...
        int iComma = header.indexOf(",");
        sFileName = QString(header.left(iComma));
        header = header.mid(iComma+1);
        iComma = header.indexOf('\0');
        len = header.left(iComma).toInt();// Get the file length...
        file.setFileName(destinationDir + sFileName);
        file.remove();
        file.setFileName(destinationDir + sFileName + QString(".temp"));
        file.remove();// Just in case of a previous aborted transfer
        if(file.open(QIODevice::Append)) {
            len = baMessage.size()-1024;
            written = file.write(baMessage.mid(1024));
            bytesReceived += written;
            if(len != written) {
                logMessage(logFile,
                           Q_FUNC_INFO,
                           sMyName +
                           QString(" File lenght mismatch in: %1 written(%2/%3)")
                           .arg(sFileName)
                           .arg(written)
                           .arg(len));
                handleWriteFileError();
                return;
            }
        } else {
            logMessage(logFile,
                       Q_FUNC_INFO,
                       sMyName +
                       QString(" Unable to write file: %1")
                       .arg(sFileName));
            handleOpenFileError();
            return;
        }
    }
    else {// It's a new frame for an already opened file...
        len = baMessage.size();
        written = file.write(baMessage);
        bytesReceived += written;
        if(len != written) {
            logMessage(logFile,
                       Q_FUNC_INFO,
                       sMyName +
                       QString(" File lenght mismatch in: %1 written(%2/%3)")
                       .arg(sFileName)
                       .arg(written)
                       .arg(len));
            handleWriteFileError();
            return;
        }
    }
#ifdef LOG_VERBOSE
    logMessage(logFile,
               Q_FUNC_INFO,
               QString("Received %1 bytes").arg(bytesReceived));
#endif
    if(isLastFrame) {
        if(bytesReceived < queryList.last().fileSize) {
            sMessage = QString("<get>%1,%2,%3</get>")
                       .arg(queryList.last().fileName)
                       .arg(bytesReceived)
                       .arg(CHUNK_SIZE);
            written = pUpdateSocket->sendTextMessage(sMessage);
            if(written != sMessage.length()) {
                logMessage(logFile,
                           Q_FUNC_INFO,
                           sMyName +
                           QString(" Error writing %1").arg(sMessage));
                returnCode = SOCKET_ERROR;
                thread()->exit(returnCode);
                return;
            }
#ifdef LOG_VERBOSE
            else {
                logMessage(logFile,
                           Q_FUNC_INFO,
                           sMyName +
                           QString(" Sent %1 to: %2")
                           .arg(sMessage)
                           .arg(pUpdateSocket->peerAddress().toString()));
            }
#endif
        }
        else {
            bytesReceived = 0;
            file.close();
            QDir renamed;
            renamed.rename(destinationDir + sFileName + QString(".temp"), destinationDir + sFileName);
            queryList.removeLast();
            if(!queryList.isEmpty()) {
                QString sMessage = QString("<get>%1,%2,%3</get>")
                                   .arg(queryList.last().fileName)
                                   .arg(bytesReceived)
                                   .arg(CHUNK_SIZE);
                written = pUpdateSocket->sendTextMessage(sMessage);
                if(written != sMessage.length()) {
                    logMessage(logFile,
                               Q_FUNC_INFO,
                               sMyName +
                               QString(" Error writing %1").arg(sMessage));
                    returnCode = SOCKET_ERROR;
                    thread()->exit(returnCode);
                    return;
                }
#ifdef LOG_VERBOSE
                else {
                    logMessage(logFile,
                               Q_FUNC_INFO,
                               sMyName +
                               QString(" Sent %1 to: %2")
                               .arg(sMessage)
                               .arg(pUpdateSocket->peerAddress().toString()));
                }
#endif
            }
            else {
                logMessage(logFile,
                           Q_FUNC_INFO,
                           sMyName +
                           QString(" No more file to transfer"));
                returnCode = TRANSFER_DONE;
                thread()->exit(returnCode);
                return;
            }
        }
    }
}


/*!
 * \brief FileUpdater::onWriteFileError Write file error handler
 */
void
FileUpdater::handleWriteFileError() {
    file.close();
    logMessage(logFile,
               Q_FUNC_INFO,
               QString("Error writing File: %1")
               .arg(queryList.last().fileName));
    returnCode = FILE_ERROR;
    thread()->exit(returnCode);
}


/*!
 * \todo We have to rewrite this member!!!
 */
/*!
 * \brief FileUpdater::handleOpenFileError
 */
void
FileUpdater::handleOpenFileError() {
    queryList.removeLast();
    if(!queryList.isEmpty()) {
        QString sMessage = QString("<get>%1,%2,%3</get>")
                           .arg(queryList.last().fileName)
                           .arg(bytesReceived)
                           .arg(CHUNK_SIZE);
        qint64 written = pUpdateSocket->sendTextMessage(sMessage);
        if(written != sMessage.length()) {
            logMessage(logFile,
                       Q_FUNC_INFO,
                       sMyName +
                       QString(" Error writing %1").arg(sMessage));
            returnCode = SOCKET_ERROR;
            thread()->exit(returnCode);
            return;
        }
        else {
            logMessage(logFile,
                       Q_FUNC_INFO,
                       sMyName +
                       QString(" Sent %1 to: %2")
                       .arg(sMessage)
                       .arg(pUpdateSocket->peerAddress().toString()));
        }
    }
    else {
        logMessage(logFile,
                   Q_FUNC_INFO,
                   sMyName +
                   QString(" No more file to transfer"));
        returnCode = TRANSFER_DONE;
        thread()->exit(returnCode);
    }
}


/*!
 * \brief FileUpdater::onProcessTextMessage
 * asynchronously handle the text messages
 * \param sMessage
 */
void
FileUpdater::onProcessTextMessage(QString sMessage) {
    logMessage(logFile,
               Q_FUNC_INFO,
               sMyName +
               QString(" Updating Files"));
    QString sToken;
    QString sNoData = QString("NoData");
    sToken = XML_Parse(sMessage, "file_list");
#ifdef LOG_VERBOSE
    logMessage(logFile,
               Q_FUNC_INFO,
               sToken);
#endif
    if(sToken != sNoData) {
        QStringList tmpFileList = QStringList(sToken.split(",", QString::SkipEmptyParts));
        remoteFileList.clear();
        QStringList tmpList;
        for(int i=0; i< tmpFileList.count(); i++) {
            tmpList =   QStringList(tmpFileList.at(i).split(";", QString::SkipEmptyParts));
            if(tmpList.count() > 1) {// Both name and size are presents
                files newFile;
                newFile.fileName = tmpList.at(0);
                newFile.fileSize = tmpList.at(1).toLong();
                remoteFileList.append(newFile);
            }
        }
        updateFiles();
    }// file_list
    else {
        logMessage(logFile,
                   Q_FUNC_INFO,
                   QString("Nessun file da trasferire"));
        returnCode = TRANSFER_DONE;
        thread()->exit(returnCode);
    }
}


/*!
 * \brief FileUpdater::updateFiles
 * utility function to update the files
 */
void
FileUpdater::updateFiles() {
    bool bFound;
    QDir fileDir(destinationDir);
    QFileInfoList localFileInfoList = QFileInfoList();
    if(fileDir.exists()) {// Get the list of the spots already present
        QStringList nameFilter(sFileExtensions.split(" "));
        fileDir.setNameFilters(nameFilter);
        fileDir.setFilter(QDir::Files);
        localFileInfoList = fileDir.entryInfoList();
    }
    // build the list of files to copy from server
    queryList = QList<files>();
    for(int i=0; i<remoteFileList.count(); i++) {
        bFound = false;
        for(int j=0; j<localFileInfoList.count(); j++) {
            if(remoteFileList.at(i).fileName == localFileInfoList.at(j).fileName() &&
               remoteFileList.at(i).fileSize == localFileInfoList.at(j).size()) {
                bFound = true;
                break;
            }
        }
        if(!bFound) {
            queryList.append(remoteFileList.at(i));
        }
    }
    // Remove the local files not anymore requested
    for(int j=0; j<localFileInfoList.count(); j++) {
        bFound = false;
        for(int i=0; i<remoteFileList.count(); i++) {
            if(remoteFileList.at(i).fileName == localFileInfoList.at(j).fileName() &&
               remoteFileList.at(i).fileSize == localFileInfoList.at(j).size()) {
                bFound = true;
                break;
            }
        }
        if(!bFound) {
            QFile::remove(localFileInfoList.at(j).absoluteFilePath());
            logMessage(logFile,
                       Q_FUNC_INFO,
                       QString("Removed %1").arg(localFileInfoList.at(j).absoluteFilePath()));
        }
    }
    if(queryList.isEmpty()) {
        logMessage(logFile,
                   Q_FUNC_INFO,
                   sMyName +
                   QString(" All files are up to date !"));
        returnCode = TRANSFER_DONE;
        thread()->exit(returnCode);
        return;
    }
    else {
        askFirstFile();
    }
}


/*!
 * \brief FileUpdater::askFirstFile
 * utility function asking the server to start updating the files
 */
void
FileUpdater::askFirstFile() {
    QString sMessage = QString("<get>%1,%2,%3</get>")
                           .arg(queryList.last().fileName)
                           .arg(bytesReceived)
                           .arg(CHUNK_SIZE);
    qint64 written = pUpdateSocket->sendTextMessage(sMessage);
    if(written != sMessage.length()) {
        logMessage(logFile,
                   Q_FUNC_INFO,
                   sMyName +
                   QString(" Error writing %1").arg(sMessage));
        returnCode = SOCKET_ERROR;
        thread()->exit(returnCode);
        return;
    }
    else {
        logMessage(logFile,
                   Q_FUNC_INFO,
                   sMyName +
                   QString(" Sent %1 to: %2")
                   .arg(sMessage)
                   .arg(pUpdateSocket->peerAddress().toString()));
    }
}
