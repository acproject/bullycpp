PROJECT = bullycpp
CONFIG += qt c++11
QT += core gui network serialport widgets

SOURCES += \
    bullycpp/MemRow.cpp \
    bullycpp/PicBootloaderDriver.cpp \
    bullycpp.cpp \
    dataxfer/DataXferWrap.cpp \
    GitHubUpdateChecker.cpp \
    MainWindow.cpp \
    QtDataXfer.cpp \
    QtPicBootloaderDriver.cpp \
    QtPicDriver.cpp \
    SerialPort.cpp

HEADERS += \
    bullycpp/IProgressCallback.h \
    bullycpp/ISerialPort.h \
    bullycpp/MemRow.h \
    bullycpp/PicBootloaderDriver.h \
    bullycpp/PicDevice.h \
    bullycpp/util.h \
    dataxfer/IDataXferCallbacksWrap.h \
    dataxfer/DataXferWrap.h \
    CollapsingQTabWidget.h \
    GitHubUpdateChecker.h \
    InterceptQPlainTextEdit.h \
    MainWindow.h \
    QStdStreamBuf.h \
    QtDataXfer.h \
    QtPicBootloaderDriver.h \
    QtPicDriver.h \
    SerialPort.h

FORMS += mainwindow.ui

RESOURCES += bullycpp.qrc

ICON = bullycpp.icns

TARGET = BullyCPP
