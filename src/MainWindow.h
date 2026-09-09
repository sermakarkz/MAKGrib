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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <memory>

#include <QApplication>
#include <QMainWindow>
#include <QMouseEvent>

#include "DialogGraphicsParams.h"
#include "ImageWriter.h"
#include "GshhsReader.h"
#include "Terrain.h"
#include "MenuBar.h"
#include "BoardPanel.h"
#include "DateChooser.h"
#include "ColorScaleWidget.h"

#include "DialogLoadGRIB.h"
#include "DialogServerStatus.h"
#include "DialogProxy.h"
#include "DialogUnits.h"
#include "DialogSelectMetar.h"
#include "POI.h"
#include "GribAnimator.h"
#include "Projection.h"
#include "DialogFonts.h"
#include "GriddedPlotter.h"
#include "SkewT.h"
#include "DialogVirtualBoat.h"



//--------------------------------------------
class ThreadNewInstance : public QThread
{
	public:
     void run();
};

//--------------------------------------------
class MainWindow: public QMainWindow
{
    Q_OBJECT
    
    public:
        MainWindow (int w, int h, QWidget *parent = 0);
        ~MainWindow();

        // automatic=true for the file reopened at start-up: a forecast
        // that has already expired is dropped silently instead of being
        // put on screen as if it were current.
        void openMeteoDataFile (const QString& fileName, bool automatic=false);
        // Adds a forecast without unloading the ones already open.
        void addMeteoDataFile (const QString& fileName);

        // True when the last step of the loaded forecast is already in
        // the past. *last receives that step.
        bool forecastHasExpired (time_t *last) const;
		
		void openSkewtDiagramWindow (double lon, double lat, 
									 GriddedReader *reader = NULL, 
									 time_t date = 0
									);
        void checkUpdates();       
        QString getMTLocation();

public slots:
		void slotGenericAction ();
		
        void slotOpenMeteotable ();
// 		void slotOpenCurveDrawer ();		// added by Tim Holtschneider, 05.2010
        void slotCreatePOI ();
        void slotCreateAnimation ();
        void slotExportImage ();
		void slotOpenMeteotablePOI (POI*);
		void slotOpenSelectMetar ();
        void slotMETARsListChanged ();
		void slotMETARSvisibility (bool vis);
		void slotShowSkewtDiagram ();

		//-- several forecasts loaded at once ----------------
		void slotFile_AddGRIB ();
		void slotFile_DownloadAllModels ();
		void slotModel_Selected (int index);
		void slotModel_Next ();
		void slotModel_Prev ();
		void slotModel_Close ();
		void slotModel_Shortcut ();
		void slotModelListChanged ();

		//-- virtual boat ------------------------------------
		void slotBoat_Draw ();
		void slotBoat_DrawingChanged (bool finished);
		void slotBoat_Edit ();
		void slotBoat_Track ();
		void slotBoat_Show ();
		void slotBoat_Clear ();
		void slotBoat_SetStartHere ();
		void slotBoat_InsertWaypoint ();
		void slotBoat_DeleteWaypoint ();
		void slotBoat_Changed ();

        void slotFile_Open ();
        void slotFile_Close ();
        void slotFile_Load_GRIB ();
		
        void slotFile_GribServerStatus ();
        void slotFile_Info_GRIB ();
        void slotFile_Quit ();
        void slotMap_Quality ();
        void slotMap_Projection (QAction *);
        void slotMap_CitiesNames ();
        void slotMap_FindCity ();
        void slotIsobarsStep ();
		
        void slotIsotherms0Step  ();
        void slotIsotherms_Step ();
        void slotGroupIsotherms (QAction *);
        void slotGroupLinesThetaE (QAction *);
        void slotGroupLinesThetaE_Step ();
		
        void slotMouseClicked (QMouseEvent * e);
        void slotMouseMoved (QMouseEvent * e);
        void slotPOImoved (POI *);
        void slotMouseLeaveTerre (QEvent * e);

        void slotDateGribChanged (int id);
        void slotDateGribChanged_next ();
        void slotDateGribChanged_prev ();
		void slotTimeZoneChanged ();
		void slotDateChooserChanged (time_t date, bool isMoving);

        void slotModelRectChanged(int sel);
		
		void slot_GroupColorMap (QAction *);
		void slot_GroupAltitude (QAction *);
		void slot_GroupWavesArrows (QAction *);
		
		void slot_GroupGeopotentialLines (QAction *);
		void slot_GroupGeopotentialStep (QAction *);
			 
		void updateGraphicsParameters ();
        void slotWindArrows (bool b);
        void slotChangeFonts ();

        void slotChangeSkin (bool b);

		void slotShowDateChooser (bool b);
		void slotShowColorScale (bool b);
		void slotShowBoardPanel (bool b);
        void slotOptions_Language ();
        void slotHelp_Help ();
        void slotOpenAngleConverter ();
        void slotHelp_APropos ();
        void slotHelp_AProposQT ();
		void slotUseJetStreamColorMap  (bool);
		void slotUseAbsoluteGustSpeed (bool b);

        void slotPanSelectToggle(); // menu slot
        void slotPanToggle();	    // toolbar slot
        void slotSelectToggle();    // toolbar slot

        void slotCheckForUpdates();
        void slotRunMaintenanceTool();
        void slotNetworkError(QNetworkReply::NetworkError);
        void slotFinished();

    signals:
        void signalMapQuality (int quality);

    private:
        std::shared_ptr<GshhsReader> gshhsReader;
        Projection  *proj;
        
        QString      gribFileName;
        QString      gribFilePath;
        
		QNetworkAccessManager *networkManager;

		DialogProxy      *dialogProxy;
        DialogUnits      *dialogUnits;
        DialogFonts      *dialogFonts;
        DialogGraphicsParams *dialogGraphicsParams;
		
		DialogSelectMetar    *dialogSelectMetar;
		QList <MetarWidget *> listAllMetars;
		
        Terrain      *terre;
        MenuBar      *menuBar;
        QToolBar     *toolBar;
        // Second row: the forecast slots and the boat. They were on the
        // one row with everything else, which overflowed on any window
        // narrower than about 1600 px and hid them behind the ">>".
        QToolBar     *toolBarBoat;
        BoardPanel   *boardPanel;
        QStatusBar   *statusBar;
		DateChooser  *dateChooser;
		ColorScaleWidget *colorScaleWidget;

        QString errorMessage;
        QNetworkReply *reply;
        bool downloadError;
        bool startCheckUpdateFlag = false;
        QString maintenanceToolLocation = "";


        QMenu    *menuPopupBtRight;

        BoatTrackWindow *boatTrackWindow;
        // Widgets put on a toolbar are shown/hidden through the action
        // the toolbar creates for them, not through the widget itself.
        QAction *actModelsCombo;

        void    connectSignals();
		void    createPOIs ();
		void    connectPOI (POI *poi);
		
		void    createAllMETARs ();
		
        void    disableMenubarItems();
        void    setMenubarItems();
        // Everything the window must refresh when the shown forecast
        // changes: menus, date list, date chooser, colour scale, map.
        void    applyLoadedModel (const QString &fileName);

        void    InitActionsStatus();
		void 	setMenubarColorMapData (const DataCode &dtc, bool trigAction=true);
		void 	setMenubarAltitudeData (DataCode dtc);
		void 	checkAltitude (int levelType,int levelValue, 
							   QAction *action, 
							   const Altitude &alt, 
							   const DataCode &dtc
							   );
		void 	setMenuBarGeopotentialLines (const DataCode &dtc,
											 bool drawGeopot,
											 bool drawLabels,
											 int  step );

        void        statusBar_showSelectedZone();
        QString     dataPresentInGrib (GribReader* grib,
        						int dataType,int levelType,int levelValue,
        						bool *ok=NULL);
		void		initProjection();
		
        int  mouseClicX, mouseClicY;
		void updateBoardPanel ();
		void updateGriddedData ();

        bool selectToggled; // true - select mode, false - pan mode
        void setSelectPanToggle(bool isSelect);

		void closeEvent (QCloseEvent *) {QApplication::quit();}
		void moveEvent  (QMoveEvent *event);
		void resizeEvent(QResizeEvent *event);
		void createToolBar ();
		void autoClose ();
        int versionCompare(QString v1, QString v2);
        QString findMaintenanceTool();
        const double modelRectangles[7][4] = {{-8, 38, 12, 53 },                // Arome 0.025
                                        {-23.5, 29.5, 45.0, 70.5 },             // ICON-EU Nest
                                        {-32.0, 20.0, 42.0, 72.0},              // Arpege 0.1 deg
                                        {-152.879, 12.220, -49.416, 61.310},    // NAM CONUS
                                        {-100.0, 0.138, -60.148, 30.054},       // NAM CACBN
                                        {-170.0, 8.133, -140.084, 32.973},      // NAM PACIFIC
                                        {-10.5, 30.0, 42.0, 66.0}               // EWAM
                                       };
;
};

#endif
