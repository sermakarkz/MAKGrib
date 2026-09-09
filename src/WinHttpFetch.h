/**********************************************************************
MAKGrib: meteorological GRIB file viewer
Copyright (C) 2026 - MAKGrib contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
***********************************************************************/

#ifndef WINHTTPFETCH_H
#define WINHTTPFETCH_H

#include <QByteArray>
#include <QString>

//===================================================================
// Загрузка по http и https средствами самой Windows (WinHTTP, поверх
// Schannel).
//
// Зачем это нужно вместо Qt: Qt под mingw собран с OpenSSL, у которой
// все пути зашиты линуксовыми —
//     /usr/x86_64-w64-mingw32/sys-root/mingw/etc/pki/tls/cert.pem
//     /usr/x86_64-w64-mingw32/sys-root/mingw/lib/ossl-modules
// На Windows их не существует, корневых сертификатов не находится ни
// одного, и соединение с NOAA обрывается на проверке. Складывать набор
// сертификатов рядом с программой можно, но он протухает и его надо
// обновлять; хранилище Windows обновляется само.
//
// Функция синхронная. Вызывать её следует в отдельном потоке: NOMADS
// отвечает не мгновенно, а окно должно жить.
//===================================================================

// Возвращает тело ответа. Пустой массив — неудача, причина в *why.
// *httpStatus получает код HTTP, если сервер вообще ответил, иначе 0 —
// по этому и отличается «нет связи» от «сервер сказал 404».
QByteArray winHttpFetch (const QString &url, int timeoutMs,
                         QString *why, int *httpStatus);

#endif
