TEMPLATE = lib


HEADERS = main.h logViewWindow.h
SOURCES += main.cpp logViewWindow.cpp
FORMS += log_view_contents.ui
QT += core gui sql
QMAKE_CXXFLAGS += "-std=c++0x"
QMAKE_LFLAGS += "-std=c++0x"
