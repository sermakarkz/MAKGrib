# Сборка MAKGrib под Android. Qt 6, отдельно от настольного проекта:
# движок общий, интерфейс свой, а настольные диалоги и печать сюда не
# едут вовсе.
#
#   qt-cmake -S android -B build-android -DANDROID_ABI=arm64-v8a
#   cmake --build build-android --target apk
cmake_minimum_required(VERSION 3.21)
project(MAKGrib LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Xml)

set(TOP "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(DEPS "$ENV{HOME}/android-deps/${ANDROID_ABI}")
if(NOT EXISTS "${DEPS}/lib/libbz2.a")
    message(FATAL_ERROR "Нет собранных библиотек в ${DEPS}. Сначала tools/android-deps.sh ${ANDROID_ABI}")
endif()

# PROJ здесь нет намеренно: он тянет SQLite и базу координатных систем.
# Остаются проекции на собственной арифметике — Projection_ZYGRIB и
# Projection_MERCATOR_Simple.
add_compile_definitions(MAKGRIB_NO_PROJ)

# Version.h порождается из шаблона, как и в настольной сборке.
set(VERSION_MAJOR 1)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)
set(XYGRIB_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
set(XYGRIB_VERSION_DATE "2026-09-10")
configure_file(${TOP}/cmake/Version.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${TOP}/src ${TOP}/src/util ${TOP}/src/map ${TOP}/src/forecast
    ${TOP}/src/g2clib-1.6.0
    ${DEPS}/include ${DEPS}/include/openjpeg-2.5
)

# g2clib берём как есть, его же CMakeLists: там выверенный список файлов
# (кодировщики исключены) и нужные определения. Свой список неизбежно
# разъехался бы с настольным.
add_subdirectory(${TOP}/src/g2clib-1.6.0 g2clib-build)

# Расчётная часть: всё, что не про настольный интерфейс.
set(ENGINE
    ${TOP}/src/GribReader.cpp        ${TOP}/src/GribRecord.cpp
    ${TOP}/src/Grib2Record.cpp       ${TOP}/src/GriddedReader.cpp
    ${TOP}/src/GriddedRecord.cpp     ${TOP}/src/GriddedPlotter.cpp
    ${TOP}/src/IrregularGridded.cpp  ${TOP}/src/IsoLine.cpp
    ${TOP}/src/DataColors.cpp        ${TOP}/src/DataPointInfo.cpp
    ${TOP}/src/DataQString.cpp       ${TOP}/src/DataMeteoAbstract.cpp
    ${TOP}/src/ColorScale.cpp        ${TOP}/src/Therm.cpp
    ${TOP}/src/Astro.cpp             ${TOP}/src/VirtualBoat.cpp
    ${TOP}/src/GribPlot.cpp          ${TOP}/src/LongTaskMessage.cpp
    ${TOP}/src/LongTaskProgress.cpp
    ${TOP}/src/NomadsLoader.cpp      ${TOP}/src/WinHttpFetch.cpp
    ${TOP}/src/forecast/NomadsSource.cpp
    ${TOP}/src/forecast/SourceRegistry.cpp
    ${TOP}/src/util/Settings.cpp     ${TOP}/src/util/Util.cpp
    ${TOP}/src/util/zuFile.cpp       ${TOP}/src/util/Font.cpp
    ${TOP}/src/util/Orthodromie.cpp
    ${TOP}/src/map/Projection.cpp
)

qt_add_executable(MAKGrib main.cpp ${ENGINE})

target_link_libraries(MAKGrib PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Xml
    g2clib
    ${DEPS}/lib/libopenjp2.a ${DEPS}/lib/libpng16.a
    ${DEPS}/lib/libnova.a    ${DEPS}/lib/libbz2.a
    z          # zlib берём системную, она есть в самой Android
)

# OpenSSL: Qt для Android его не кладёт в APK, и без него любое
# соединение по https обрывается — NOAA становится недостижим.
# Библиотеки берём готовые, от KDAB, они собраны под все архитектуры.
set(OPENSSL_DIR "$ENV{HOME}/android_openssl")
if(EXISTS "${OPENSSL_DIR}/android_openssl.cmake")
    include("${OPENSSL_DIR}/android_openssl.cmake")
    add_android_openssl_libraries(MAKGrib)
else()
    message(FATAL_ERROR "Нет ${OPENSSL_DIR}: git clone https://github.com/KDAB/android_openssl")
endif()

set_target_properties(MAKGrib PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/package"
    QT_ANDROID_MIN_SDK_VERSION 28
    QT_ANDROID_TARGET_SDK_VERSION 35
    QT_ANDROID_VERSION_NAME "1.1.0"
    QT_ANDROID_VERSION_CODE 110
)
