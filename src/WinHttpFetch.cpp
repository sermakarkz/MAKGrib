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

#include "WinHttpFetch.h"

#ifdef Q_OS_WIN
#include <QObject>
#include <windows.h>
#include <winhttp.h>

//---------------------------------------------------------------------
// Всё через RAII: у WinHTTP три уровня хэндлов, и забыть закрыть любой
// из них на пути ошибки очень легко.
namespace {
class Handle {
	public:
		explicit Handle (HINTERNET h = nullptr) : h_ (h) {}
		~Handle () { if (h_) WinHttpCloseHandle (h_); }
		Handle (const Handle &) = delete;
		Handle &operator= (const Handle &) = delete;
		operator HINTERNET () const { return h_; }
		HINTERNET get () const { return h_; }
		void reset (HINTERNET h) { if (h_) WinHttpCloseHandle (h_); h_ = h; }
	private:
		HINTERNET h_;
};

QString lastError (const char *stage)
{
	return QObject::tr("%1: Windows error %2")
	        .arg (QString::fromLatin1 (stage)).arg ((uint) GetLastError());
}
}

//---------------------------------------------------------------------
QByteArray winHttpFetch (const QString &url, int timeoutMs,
                         QString *why, int *httpStatus)
{
	if (why != nullptr)         *why = QString();
	if (httpStatus != nullptr)  *httpStatus = 0;

	std::wstring wurl = url.toStdWString ();

	// Разбор адреса делает сам WinHTTP — свой парсер тут только источник
	// ошибок.
	URL_COMPONENTS uc;
	ZeroMemory (&uc, sizeof(uc));
	uc.dwStructSize = sizeof(uc);
	wchar_t host[512] = {0};
	wchar_t path[4096] = {0};
	wchar_t extra[4096] = {0};
	uc.lpszHostName = host;   uc.dwHostNameLength = 512;
	uc.lpszUrlPath  = path;   uc.dwUrlPathLength  = 4096;
	uc.lpszExtraInfo= extra;  uc.dwExtraInfoLength= 4096;
	if (!WinHttpCrackUrl (wurl.c_str(), (DWORD) wurl.size(), 0, &uc)) {
		if (why) *why = lastError ("parsing the address");
		return QByteArray();
	}
	std::wstring object = std::wstring(path) + std::wstring(extra);
	bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);

	Handle session (WinHttpOpen (L"MAKGrib/1.1",
	                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
	                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session) {
		// На старых системах автоматического режима нет.
		session.reset (WinHttpOpen (L"MAKGrib/1.1",
		               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	}
	if (!session) {
		if (why) *why = lastError ("opening the session");
		return QByteArray();
	}
	WinHttpSetTimeouts (session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

	Handle connect (WinHttpConnect (session, host, uc.nPort, 0));
	if (!connect) {
		if (why) *why = lastError ("connecting");
		return QByteArray();
	}

	Handle request (WinHttpOpenRequest (connect, L"GET", object.c_str(),
	                nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
	                secure ? WINHTTP_FLAG_SECURE : 0));
	if (!request) {
		if (why) *why = lastError ("making the request");
		return QByteArray();
	}

	if (!WinHttpSendRequest (request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
	                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
		if (why) *why = lastError ("sending the request");
		return QByteArray();
	}
	if (!WinHttpReceiveResponse (request, nullptr)) {
		if (why) *why = lastError ("receiving the answer");
		return QByteArray();
	}

	// Код ответа: по нему отличается «сервер сказал 404» от «связи нет».
	DWORD code = 0, codeLen = sizeof(code);
	if (WinHttpQueryHeaders (request,
	        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	        WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeLen, WINHTTP_NO_HEADER_INDEX)
	    && httpStatus != nullptr)
		*httpStatus = (int) code;

	QByteArray body;
	for (;;) {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable (request, &avail)) {
			if (why) *why = lastError ("reading");
			return QByteArray();
		}
		if (avail == 0)
			break;
		QByteArray chunk (int(avail), Qt::Uninitialized);
		DWORD got = 0;
		if (!WinHttpReadData (request, chunk.data(), avail, &got)) {
			if (why) *why = lastError ("reading the data");
			return QByteArray();
		}
		chunk.resize (int(got));
		body.append (chunk);
	}

	if (code >= 400 && why != nullptr)
		*why = QObject::tr("the server answered %1").arg ((uint) code);
	return body;
}

#else

QByteArray winHttpFetch (const QString &, int, QString *why, int *httpStatus)
{
	if (why != nullptr)        *why = QStringLiteral("WinHTTP only exists on Windows");
	if (httpStatus != nullptr) *httpStatus = 0;
	return QByteArray();
}

#endif
