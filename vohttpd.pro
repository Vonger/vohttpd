TEMPLATE = app
TARGET   = vohttpd
INCLUDEPATH += . src
CONFIG  += console
CONFIG  -= app_bundle

HEADERS += src/vohttpd.h
SOURCES += src/vohttpd.c \
           src/vohttpdext.c

OTHER_FILES += \
           src/plugins/libvohttpd.plugin.c \
           src/plugins/libvohttpd.test.c
