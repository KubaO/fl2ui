qlalr.output = ${QMAKE_FILE_BASE}_parser.cpp
qlalr.commands = qlalr ${QMAKE_FILE_NAME}
qlalr.input = QLALRSOURCES
qlalr.dependency_type = TYPE_C
qlalr.variable_out = GENERATED_SOURCES
QMAKE_EXTRA_COMPILERS += qlalr

QT       += core
QT       -= gui

TARGET = fl2ui
CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE = app

SOURCES += main.cpp \
    read.cpp

OTHER_FILES += LICENSE COPYING README.md

HEADERS += \
    read.h
