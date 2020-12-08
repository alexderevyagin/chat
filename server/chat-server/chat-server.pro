#-------------------------------------------------
#
# Project created by QtCreator 2020-11-25T17:12:43
#
#-------------------------------------------------

QT       += core gui sql xml network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = chat-server
TEMPLATE = app


SOURCES += main.cpp\
        chatserver.cpp

HEADERS  += chatserver.h

FORMS    += chatserver.ui
