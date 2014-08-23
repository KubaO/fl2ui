QT       += core
QT       -= gui

TARGET = fl2ui
CONFIG   += console c++11
CONFIG   -= app_bundle

TEMPLATE = app

SOURCES += main.cpp \
    read.cpp

OTHER_FILES += LICENSE COPYING README.md

HEADERS += \
    read.h
