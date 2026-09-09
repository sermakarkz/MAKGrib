/**********************************************************************
XyGrib: meteorological GRIB file viewer
Copyright (C) 2008-2012 - Jacques Zaninetti - http://www.zygrib.org

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

#include <QApplication>
#include <QTextStream>
#include <QDomDocument>
#include <QNetworkProxy>
#include <QMessageBox>

#include <cassert>
#include <cmath>

#include "FileLoaderGRIB.h"
#include "SourceRegistry.h"
#include "Util.h"
#include "Version.h"

//========================================================================
FileLoaderGRIB::FileLoaderGRIB (QNetworkAccessManager *manager, QWidget *parent)
			: FileLoader (manager)
{
	this->parent = parent;
    step = 0;
	downloadError = false;
    reply_step1 = nullptr;
    reply_step2 = nullptr;
    scriptpath = "/";
	stepTimer = new QTimer (this);
	stepTimer->setSingleShot (true);
	connect (stepTimer, SIGNAL(timeout()), this, SLOT(slotStepTimeout()));
}
//-------------------------------------------------------------------------------
FileLoaderGRIB::~FileLoaderGRIB () 
{
	if (reply_step1) {
		reply_step1->deleteLater ();
        reply_step1 = nullptr;
	}
	if (reply_step2) {
		reply_step2->deleteLater ();
        reply_step2 = nullptr;
	}
}

//-------------------------------------------------------------------------------
void FileLoaderGRIB::stop () 
{
	stepTimer->stop ();
	if (reply_step1) {
		reply_step1->close ();
		downloadError = true;
	}
	if (reply_step2) {
		reply_step2->close ();
		downloadError = true;
	}
}

//-------------------------------------------------------------------------------
void FileLoaderGRIB::abort ()
{
	stepTimer->stop ();
	if (reply_step1) {
		reply_step1->abort ();
		downloadError = true;
	}
	if (reply_step2) {
		reply_step2->abort ();
		downloadError = true;
	}
}

//-------------------------------------------------------------------------------
void FileLoaderGRIB::getGribFile(
        const QString& atmModel,
        float x0, float x1, float y0, float y1,
        float resolution, int interval, int days,
        const QString& cycle,
        bool wind, bool pressure, bool rain,
        bool cloud, bool temp, bool humid, bool isotherm0,
        bool snowDepth,
		bool snowCateg, bool frzRainCateg,
        bool CAPEsfc, bool CINsfc, bool reflectivity,
		bool altitudeData200,
		bool altitudeData300,
		bool altitudeData400,
		bool altitudeData500,
		bool altitudeData600,
		bool altitudeData700,
		bool altitudeData850,
		bool altitudeData925,
		bool skewTData,
		bool GUSTsfc,
        const QString& wvModel,
        bool sgwh,
        bool swell,
        bool wwav
	)
{
    QString page;
	step = 0;
    //----------------------------------------------------------------
    // Step 1: Request the creation of the Grib file (status, name, size & checksum in return)
    //----------------------------------------------------------------
    // build the parameter string
    QString parameters = "";
    if (wind) {
        parameters += "W;";
    }
    if (pressure && (atmModel != "ECMWF" && atmModel != "Arpege-EU" && atmModel != "Arome 0.025°")) {
        parameters += "P;";
    }
    if (pressure && (atmModel == "ECMWF" || atmModel == "Arpege-EU" || atmModel == "Arome 0.025°")){
        parameters += "p;";
    }
    if (rain) {
        parameters += "R;";
    }
    if (cloud) {
        parameters += "C;";
    }
    if (temp) {
        parameters += "T;";
    }
    if (humid) {
        parameters += "H;";
    }
	if (isotherm0) {
		parameters += "I;";
    }    
//    if (tempMin) {
//        parameters += "m;";
//    }
//    if (tempMax) {
//        parameters += "M;";
//    }
    if (snowDepth) {
        parameters += "S;";
    }
    if (snowCateg) {
        parameters += "s;";
    }
    if (frzRainCateg) {
        parameters += "Z;";
    }
    if (CAPEsfc) {
        parameters += "c;";
    }
    if (CINsfc) {
        parameters += "i;";
    }
    if (reflectivity) {
        parameters += "r;";
    }
    if (GUSTsfc) {
        parameters += "G;";
    }
//    if (SUNSDsfc) { // not currently used
//        parameters += "D;";
//    }
	
    if (altitudeData200) parameters += "2;";
    if (altitudeData300) parameters += "3;";
    if (altitudeData400) parameters += "4;";
    if (altitudeData500) parameters += "5;";
    if (altitudeData600) parameters += "6;";
    if (altitudeData700) parameters += "7;";
    if (altitudeData850) parameters += "8;";
    if (altitudeData925) parameters += "9;";
//    if (cloudLayers) parameters += "L;";
	if (skewTData)  parameters += "skewt;";
	

    // wave parameters have their own string
    QString waveParams = "";
    if (sgwh) {
        waveParams += "s;";
    }
    if (swell) {
        waveParams += "H;D;P;";
    }
    if (wwav) {
        waveParams += "h;d;p;";
    }

    // model identifier (combines resolution)
    QString amod = "";
    if (atmModel == "GFS" && resolution == 0.25 ){
        amod = "gfs_p25_";
    } else if (atmModel == "GFS" && resolution == 0.50 ){
        amod = "gfs_p50_";
    } else if (atmModel == "GFS" && resolution == 1.0 ){
        amod = "gfs_1p0_";
    } else if (atmModel == "ICON"){
        amod = "icon_p25_";
    } else if (atmModel == "Arpege"){
        amod = "arpege_p50_";
    } else if (atmModel == "ECMWF"){
        amod = "ecmwf_p50_";
    } else if (atmModel == "NAM CONUS"){
        amod = "nam_conus_12km_";
    } else if (atmModel == "NAM CACBN"){
        amod = "nam_cacbn_12km_";
    } else if (atmModel == "NAM PACIFIC"){
        amod = "nam_pacific_12km_";
    } else if (atmModel == "ICON-EU"){
        amod = "icon_eu_p06_";
    } else if (atmModel == "Arpege-EU"){
        amod = "arpege_eu_p10_";
    } else if (atmModel == "Arome 0.025°"){
        amod = "arome_p025_";
    } else if (atmModel == "None"){
        amod = "none";
    }

    // wave model identifier
    QString wmod = "";
    if (wvModel == "WW3"){
        wmod = "ww3_p50_";
    } else if (wvModel == "GWAM" ){
        wmod = "gwam_p25_";
    } else if (wvModel == "EWAM"){
        wmod = "ewam_p05_";
    } else if (wvModel == "None"){
        wmod = "none";
    }

//    QString runCycle = Util::getSetting("downloadRunCycle", "last").toString().toLower();
    //strbuf.clear();
    // Kept for the NOAA fallback below: the same area and depth have to
    // be asked for again if the OpenGribs server refuses.
    reqAtmCode  = amod;
    reqWaveCode = wmod;
    reqX0 = x0;  reqY0 = y0;  reqX1 = x1;  reqY1 = y1;
    reqDays = days;
    reqInterval = interval;

    if (!(parameters == "" && waveParams == ""))
    {
        step = 1;
        emit signalGribSendMessage(
                tr("Preparing file on server... Please wait..."));
        emit signalGribReadProgress(step, 0, 0);


        QString phpfilename = scriptpath+ "getmygribs2.php?";
        QString appVer = Version::getVersion();
        QString ptype = "Unknown";
#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
        ptype = QSysInfo::productType();
#endif
        QTextStream(&page) << phpfilename
                           << "osys=" << ptype
                           << "&ver=" << appVer
                           << "&model=" << amod
                           << "&la1=" << y0
                           << "&la2=" << y1
                           << "&lo1=" << x0
                           << "&lo2=" << x1
                           << "&intv=" << interval
                           << "&days=" << days
                           << "&cyc=" << cycle
                           << "&par=" << parameters
                           << "&wmdl=" << wmod
                           << "&wpar=" << waveParams
                           ;
//                            << "&client=" << Version::getCompleteName()

        QNetworkRequest request = Util::makeNetworkRequest ("http://"+Util::getServerName()+page);
		reply_step1 = networkManager->get (request);
		// Сервер готовит файл небыстро, но не бесконечно. Если за это
		// время он не ответил ничего — его нет, и ждать нечего.
		stepTimer->start (45000);
		connect (reply_step1, SIGNAL(downloadProgress (qint64,qint64)), 
				 this, SLOT(downloadProgress (qint64,qint64)));
		connect (reply_step1, SIGNAL(error(QNetworkReply::NetworkError)),
				 this, SLOT(slotNetworkError (QNetworkReply::NetworkError)));
		connect (reply_step1, SIGNAL(finished()),
				 this, SLOT(slotFinished_step1 ()));
    }
    else
    {
        QMessageBox::warning(parent, tr("Bad Request"), tr("No atmospheric or wave parameters were selected"));
    }
}
//-------------------------------------------------------------------------------
void FileLoaderGRIB::slotNetworkError (QNetworkReply::NetworkError /*err*/)
{
	if (! downloadError) {
		if (sender() == reply_step1) {
			// До сервера не достучались вовсе. Это ровно тот случай, ради
			// которого делался запасной путь, — не ошибка, а повод пойти
			// за данными к NOAA.
			stepTimer->stop ();
			QString msg = tr("The OpenGribs server is not answering.")
			            + " (" + reply_step1->errorString() + ")";
			QPointer<FileLoaderGRIB> alive (this);
			bool handled = tryNomads (msg);
			if (alive.isNull())
				return;
			if (handled)
				return;
			emit signalGribLoadError (msg);
			downloadError = true;
		}
		else if (sender() == reply_step2) {
			emit signalGribLoadError (reply_step2->errorString());
			downloadError = true;
		}
	}
}
//-------------------------------------------------------------------------------
void FileLoaderGRIB::downloadProgress (qint64 done, qint64 total)
{
	if (downloadError) {
		step = 1000;
		return;
	}
	if (sender() == reply_step2) {
		emit signalGribReadProgress (step, done, total);
	}
}
//-------------------------------------------------------------------------------
void FileLoaderGRIB::slotStepTimeout ()
{
	if (downloadError)
		return;
	downloadError = true;              // не даём slotNetworkError дублировать
	if (reply_step1 != nullptr && !reply_step1->isFinished()) {
		reply_step1->abort ();
		// Сервера нет на связи. Это ровно тот случай, ради которого и
		// делался запасной путь: пробуем взять данные напрямую у NOAA.
		QString msg = tr("The OpenGribs server is not answering.");
		downloadError = false;         // tryNomads решит, чем всё кончилось
		QPointer<FileLoaderGRIB> alive (this);
		bool handled = tryNomads (msg);
		if (alive.isNull())
			return;
		if (handled)
			return;
		downloadError = true;
		emit signalGribLoadError (msg);
		return;
	}
	if (reply_step2 != nullptr && !reply_step2->isFinished()) {
		reply_step2->abort ();
		emit signalGribLoadError (tr("The download stopped: no answer from "
		                             "the server."));
	}
}
//-------------------------------------------------------------------------------
void FileLoaderGRIB::slotFinished_step1 ()
{
DBG("slotFinished_step1");
	stepTimer->stop ();
	if (!downloadError) {
		//-------------------------------------------
        // Back from step 1: preparing the file
		//-------------------------------------------
        //xserv=reply_step1->rawHeader("XServer");
        //strbuf.append(xserv);
		QByteArray data = reply_step1->readAll ();
        QJsonDocument jsondoc = QJsonDocument::fromJson(data);
        QJsonObject jsondata = jsondoc.object();

        bool status = jsondata["status"].toBool();
        // if the result is valid the status is true and the message contains object
        // with name, size and checksum
        if (status) {
            QJsonObject msg = jsondata["message"].toObject();
            fileName = msg["url"].toString();
            fileSize = msg["size"].toInt();
            checkSumSHA1 = msg["sha1"].toString();

        }else{ // message contains only the error message
            QString m = jsondata["message"].toString();
            reply_step1->close();
            // The OpenGribs server has been out for days at a time (issue
            // #326). NOAA keeps publishing GFS and its wave model, so for
            // those the file can still be built, straight from NOMADS.
            // tryNomads() reports its own outcome, good or bad.
            QPointer<FileLoaderGRIB> alive (this);
            bool handled = tryNomads (m);
            if (alive.isNull())
                return;                 // cancelled: this object is gone
            if (handled)
                return;
            emit signalGribLoadError(m);
            downloadError = true;
        }

		//-------------------------------------------------------------
        // Start Step 2: Request the contents of the Grib file
		//-------------------------------------------------------------
        if (status) {
            step = 2;
            QString s;
            s = tr("Total size : ") + QString("%1 kb").arg(fileSize/1024, 0);
            emit signalGribSendMessage(s);
            emit signalGribStartLoadData();            
			QNetworkRequest request;
            request.setUrl (QUrl(fileName));
            reply_step2 = networkManager->get (request);
            stepTimer->start (300000);
			connect (reply_step2, SIGNAL(downloadProgress (qint64,qint64)), 
					 this, SLOT(downloadProgress (qint64,qint64)));
			connect (reply_step2, SIGNAL(error(QNetworkReply::NetworkError)),
					 this, SLOT(slotNetworkError (QNetworkReply::NetworkError)));
			connect (reply_step2, SIGNAL(finished()),
					this, SLOT(slotFinished_step2 ()));
		}
		else {
			downloadError = true;
		}
	}
}
//-------------------------------------------------------------------------------
void FileLoaderGRIB::slotFinished_step2 ()
{
// DBG("slotFinished_step2");
	stepTimer->stop ();
	if (!downloadError) {
		step = 1000;
		arrayContent = reply_step2->readAll ();
		if (arrayContent.size() < 80) {
			emit signalGribLoadError (tr("Empty file."));
			return;
		}
		emit signalGribSendMessage(tr("CheckSum control"));
		if (Util::sha1 (arrayContent) == checkSumSHA1)
		{
			emit signalGribSendMessage(tr("Finish")
                            + QString(" %1 kb").arg(arrayContent.size()/1024.0,0,'f',1));
            // TODO not sure about the .grb thing here
			emit signalGribDataReceived(&arrayContent, fileName.replace("%20",".grb"));
		}
		else {
			emit signalGribLoadError (tr("Bad checksum."));
		}
	}
}


//-------------------------------------------------------------------------------
// The NOAA fallback
//-------------------------------------------------------------------------------
// Reports the download to the dialog that owns this loader.
namespace {
class GribDialogProgress : public ForecastProgress
{
	public:
		explicit GribDialogProgress (FileLoaderGRIB *o) : owner (o) {}
		void message (const QString &text) override
		    { owner->nomadsMessage (text); }
		void step (int done, int total, qint64 bytes) override
		    { owner->nomadsStep (done, total, bytes); }
		bool canceled () override
		    { return owner->nomadsCanceled(); }
	private:
		FileLoaderGRIB *owner;
};
}
//-------------------------------------------------------------------------------
void FileLoaderGRIB::nomadsMessage (const QString &text)
{
	emit signalGribSendMessage ("NOAA — " + text);
}
//-------------------------------------------------------------------------------
void FileLoaderGRIB::nomadsStep (int done, int total, qint64 bytes)
{
	// The dialog's bar is fed bytes, and shows a transfer rate from them.
	// Only a rough total is knowable before the end, so it is scaled from
	// what has arrived so far.
	qint64 guess = (done > 0) ? bytes*total/done : bytes;
	emit signalGribReadProgress (2, int(bytes), int(qMax (guess, bytes+1)));
}
//-------------------------------------------------------------------------------
bool FileLoaderGRIB::tryNomads (const QString &serverMessage)
{
	SourceRegistry &reg = SourceRegistry::instance ();
	bool wantAtm  = (!reqAtmCode.isEmpty()  && reqAtmCode  != "none");
	bool wantWave = (!reqWaveCode.isEmpty() && reqWaveCode != "none");
	ForecastSource *srcAtm  = wantAtm  ? reg.sourceFor (reqAtmCode)  : nullptr;
	ForecastSource *srcWave = wantWave ? reg.sourceFor (reqWaveCode) : nullptr;
	bool canAtm   = (srcAtm  != nullptr);
	bool canWave  = (srcWave != nullptr);

	if (!canAtm && !canWave)
		return false;              // nothing here NOAA could stand in for

	// Half a request is not the request. Handing back only the waves when
	// an ICON atmosphere was asked for would quietly change the answer,
	// so say what NOAA can do instead and let the choice be made again.
	if ((wantAtm && !canAtm) || (wantWave && !canWave)) {
		emit signalGribLoadError (serverMessage + "\n\n"
		        + tr("Only GFS and WW3 can be taken straight from NOAA. "
		             "Select those two to download while the server is down."));
		downloadError = true;
		return true;
	}

	emit signalGribSendMessage (
	        tr("The OpenGribs server is not delivering — asking NOAA directly..."));
	emit signalGribStartLoadData ();

	GribDialogProgress bar (this);
	arrayContent.clear ();
	QStringList notes;
	QString name, usedRun;

	if (canAtm) {
		ForecastSource::Request rq {reqAtmCode, reqX0, reqY0, reqX1, reqY1,
		                            reqDays, reqInterval};
		ForecastSource::Result o = srcAtm->fetch (rq, &arrayContent, &bar);
		if (!o.ok) {
			emit signalGribLoadError (serverMessage + "\n\nNOAA: " + o.error);
			downloadError = true;
			return true;
		}
		name = o.name;
		usedRun = o.run;
		if (o.truncated)
			notes << o.error;
	}

	if (canWave) {
		// Appended to the same buffer: a GRIB file is its messages end to
		// end, so atmosphere and waves travel together as one file.
		ForecastSource::Request rq {reqWaveCode, reqX0, reqY0, reqX1, reqY1,
		                            reqDays, reqInterval};
		ForecastSource::Result o = srcWave->fetch (rq, &arrayContent, &bar);
		if (!o.ok) {
			if (!canAtm) {
				emit signalGribLoadError (serverMessage + "\n\nNOAA: " + o.error);
				downloadError = true;
				return true;
			}
			notes << tr("no sea state from NOAA") + ": " + o.error;
		}
		else {
			name = name.isEmpty() ? o.name
			                      : QString(name).replace ("GFS_", "GFS+WAVE_");
			if (usedRun.isEmpty())
				usedRun = o.run;
			if (o.truncated)
				notes << o.error;
		}
	}

	if (arrayContent.size() < 100) {
		emit signalGribLoadError (serverMessage + "\n\nNOAA: "
		        + tr("NOAA returned nothing usable"));
		downloadError = true;
		return true;
	}

	// Naming the run makes it checkable at a glance that this really is
	// today's forecast and not something left over.
	QString done = tr("Taken from NOAA") + " — " + tr("run") + " " + usedRun
	             + QString(", %1 kb").arg (arrayContent.size()/1024);
	if (!notes.isEmpty())
		done += " (" + notes.join("; ") + ")";
	emit signalGribSendMessage (done);
	emit signalGribDataReceived (&arrayContent, name);
	return true;
}
