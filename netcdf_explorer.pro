TARGET = "netcdf-explorer"
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
HEADERS = netcdf_explorer.hpp
SOURCES = netcdf_explorer.cpp
RESOURCES = netcdf_explorer.qrc
ICON = sample.icns
RC_FILE = netcdf_explorer.rc

unix:!macx {
 LIBS +=  -lnetcdf
}

macx: {
 INCLUDEPATH += /usr/local/include
 LIBS += /usr/local/lib/libnetcdf.a
 LIBS += /usr/local/lib/libhdf5.a
 LIBS += /usr/local/lib/libhdf5_hl.a
 LIBS += /usr/local/lib/libsz.a
 LIBS += -lcurl -lz
}

win32 {
 DEFINES += _CRT_SECURE_NO_WARNINGS
 DEFINES += _CRT_NONSTDC_NO_DEPRECATE
 INCLUDEPATH += 
 LIBS += 
}

