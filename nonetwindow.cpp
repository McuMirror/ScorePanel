#include <QDir>
#include <QDebug>
#include <QPainter>

#include "nonetwindow.h"


NoNetWindow::NoNetWindow(QWidget *parent)
{
    Q_UNUSED(parent);
    setAlignment(Qt::AlignCenter);
    setMinimumSize(QSize(320, 240));
//    setAttribute(Qt::WA_TranslucentBackground);

    QFont *font;
    font = new QFont("Arial", 24);
    setFont(*font);
    QPalette pal(QWidget::palette());
    pal.setColor(QPalette::Window,        Qt::black);
    pal.setColor(QPalette::WindowText,    Qt::yellow);
    pal.setColor(QPalette::Base,          Qt::black);
    pal.setColor(QPalette::AlternateBase, Qt::blue);
    pal.setColor(QPalette::Text,          Qt::yellow);
    pal.setColor(QPalette::BrightText,    Qt::white);
    setPalette(pal);

    setWindowOpacity(0.8);
    sDisplayedText = tr("In Attesa della Connessione con la Rete");
    setText(sDisplayedText);
}


NoNetWindow::~NoNetWindow() {
}


void
NoNetWindow::keyPressEvent(QKeyEvent *event) {
    if(event->key() == Qt::Key_Escape) {
#ifndef Q_OS_ANDROID
        qDebug() << "Exit requested";
#endif
        close();
    }
    if(event->key() == Qt::Key_F1) {
        showNormal();
        event->accept();
        return;
    }
}


void
NoNetWindow::resizeEvent(QResizeEvent *event) {
    mySize = event->size();
}


void
NoNetWindow::setDisplayedText(QString sNewText) {
    sDisplayedText = sNewText;
    setText(sDisplayedText);
}
