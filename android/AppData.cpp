/**********************************************************************
MAKGrib для Android — распаковка данных из APK.

Карты, палитры и шрифты лежат внутри APK, в assets. Читать их оттуда
напрямую нельзя: GshhsReader и zuFile открывают файлы обычным fopen, а
assets — это не файлы, а записи в архиве. Поэтому при первом запуске
раскладываем их во внутреннюю папку приложения. Разрешений для этого не
нужно никаких, и при удалении приложения всё уходит вместе с ним.
***********************************************************************/
#include "AppData.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QStandardPaths>

#include "Settings.h"

//---------------------------------------------------------------------
static bool copyTree (const QString &from, const QString &to,
                      AppData::Report report, qint64 *done, qint64 total)
{
	QDir().mkpath (to);
	QDirIterator it (from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
	while (it.hasNext()) {
		it.next ();
		QString name = it.fileName ();
		QString src  = from + "/" + name;
		QString dst  = to   + "/" + name;
		if (it.fileInfo().isDir()) {
			if (!copyTree (src, dst, report, done, total))
				return false;
			continue;
		}
		QFile::remove (dst);
		if (!QFile::copy (src, dst)) {
			qWarning() << "не скопировалось:" << src << "->" << dst;
			return false;
		}
		// Из assets файлы приходят только для чтения, а настройки и
		// загруженные прогнозы лягут рядом — снимаем ограничение.
		QFile::setPermissions (dst, QFile::ReadOwner | QFile::WriteOwner);
		*done += it.fileInfo().size ();
		if (report && total > 0)
			report (int (100 * (*done) / total));
	}
	return true;
}

//---------------------------------------------------------------------
static qint64 treeSize (const QString &path)
{
	qint64 n = 0;
	QDirIterator it (path, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		it.next ();
		n += it.fileInfo().size ();
	}
	return n;
}

//---------------------------------------------------------------------
QString AppData::dataDir ()
{
	return QStandardPaths::writableLocation (QStandardPaths::AppDataLocation);
}

//---------------------------------------------------------------------
bool AppData::ready ()
{
	// Признак распаковки — не флажок, а сами карты: если их нет, всё
	// остальное бессмысленно.
	return QDir (dataDir() + "/data/maps/gshhs").exists()
	    && QDir (dataDir() + "/data/gis").exists();
}

//---------------------------------------------------------------------
bool AppData::unpack (Report report)
{
	if (ready())
		return true;

	const QString src = QStringLiteral("assets:/data");
	const QString dst = dataDir ();
	if (!QDir(src).exists()) {
		qWarning() << "в APK нет данных по пути" << src;
		return false;
	}
	QDir().mkpath (dst);

	qint64 total = treeSize (src);
	qint64 done  = 0;
	if (!copyTree (src, dst + "/data", report, &done, total))
		return false;

	// Программа ищет данные по этой настройке; ставим её сразу, чтобы
	// не полагаться на перебор стандартных мест.
	Settings::setUserSetting ("appDataDir", dst);
	return ready ();
}
