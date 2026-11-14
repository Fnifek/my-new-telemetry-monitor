QT += core gui widgets

CONFIG += c++17 release windows
CONFIG -= debug_and_release debug

TEMPLATE = app
TARGET = cooling_telemetry_gui
DESTDIR = release

OBJECTS_DIR = build/obj
MOC_DIR = build/moc
RCC_DIR = build/rcc
UI_DIR = build/ui

INCLUDEPATH += ../include

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    telemetryplot.cpp \
    ../src/model_sensor.cpp \
    ../src/sampler.cpp \
    ../src/filters.cpp \
    ../src/acquisition_pipeline.cpp \
    ../src/platform.cpp

HEADERS += \
    mainwindow.h \
    telemetryplot.h

QMAKE_CXXFLAGS += -Wall -Wextra -pthread
QMAKE_LFLAGS += -pthread
LIBS += -lwinmm
