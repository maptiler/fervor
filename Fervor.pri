QT += core gui webenginewidgets network

contains(QT_VERSION, ^5\\.[0-9]\\..*){
    win32:INCLUDEPATH += $$[QT_INSTALL_PREFIX]/include/QtZlib
}else{
    win32:INCLUDEPATH += $$[QT_INSTALL_PREFIX]/../../../../QtSources/4.8.1/src/3rdparty/zlib
}

isEmpty(FV_APP_NAME) {
    warning("Fervor: falling back to application name '$$TARGET'")
    DEFINES += $$quote(FV_APP_NAME=\\\"$$TARGET\\\")
} else {
    message("Fervor: building for application name '$$FV_APP_NAME'")
    DEFINES += $$quote(FV_APP_NAME=\"\\\"$$FV_APP_NAME\\\"\")
}

isEmpty(FV_APP_VERSION) {
    warning("Fervor: falling back to application version '$$VERSION'")
    DEFINES += FV_APP_VERSION=\\\"$$VERSION\\\"
} else {
    message("Fervor: building for application version '$$FV_APP_VERSION'")
    DEFINES += FV_APP_VERSION=\\\"$$FV_APP_VERSION\\\"
}

# Unit tests
#DEFINES += FV_DEBUG=1
#DEPENDPATH += "$$PWD/tests/"
#INCLUDEPATH += "$$PWD/tests/"
#CONFIG += qtestlib
#SOURCES += tests/fvversioncomparatortest.cpp
#HEADERS += tests/fvversioncomparatortest.h

DEFINES +=QUAZIP_BUILD QUAZIP_STATIC

LIBS += -lz

INCLUDEPATH += $$PWD/quazip
HEADERS += $$PWD/quazip/crypt.h \
           $$PWD/quazip/ioapi.h \
           $$PWD/quazip/JlCompress.h \
           $$PWD/quazip/quaadler32.h \
           $$PWD/quazip/quachecksum32.h \
           $$PWD/quazip/quacrc32.h \
           $$PWD/quazip/quazip.h \
           $$PWD/quazip/quazipfile.h \
           $$PWD/quazip/quazipfileinfo.h \
           $$PWD/quazip/quazipnewinfo.h \
           $$PWD/quazip/unzip.h \
           $$PWD/quazip/zip.h
SOURCES += $$PWD/quazip/qioapi.cpp \
           $$PWD/quazip/JlCompress.cpp \
           $$PWD/quazip/quaadler32.cpp \
           $$PWD/quazip/quacrc32.cpp \
           $$PWD/quazip/quazip.cpp \
           $$PWD/quazip/quazipfile.cpp \
           $$PWD/quazip/quazipfileinfo.cpp \
           $$PWD/quazip/quazipnewinfo.cpp \
           $$PWD/quazip/unzip.c \
           $$PWD/quazip/zip.c

DEPENDPATH += "$$PWD"
INCLUDEPATH += "$$PWD"

SOURCES += \
        $$PWD/fvupdatewindow.cpp \
        $$PWD/fvupdater.cpp \
        $$PWD/fvversioncomparator.cpp \
        $$PWD/fvplatform.cpp \
        $$PWD/fvignoredversions.cpp \
        $$PWD/fvavailableupdate.cpp \
        $$PWD/fvupdatedownloadprogress.cpp

HEADERS += \
        $$PWD/fvupdatewindow.h \
        $$PWD/fvupdater.h \
        $$PWD/fvversioncomparator.h \
        $$PWD/fvplatform.h \
        $$PWD/fvignoredversions.h \
        $$PWD/fvavailableupdate.h \
        $$PWD/fvupdatedownloadprogress.h

FORMS += $$PWD/fvupdatewindow.ui \
        $$PWD/fvupdatedownloadprogress.ui

TRANSLATIONS += $$PWD/fervor_lt.ts
CODECFORTR = UTF-8
