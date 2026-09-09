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

#ifndef TERRAIN_H
#define TERRAIN_H
#include <memory>

#include <QWidget>
#include <QToolBar>
#include <QBitmap>
#include <QTimer>
#include <QPen>

#include "GshhsReader.h"
#include "GisReader.h"
#include "Projection.h"
#include "POI.h"

#include "MapDrawer.h"
#include "GribPlot.h"
#include "LongTaskProgress.h"
#include "VirtualBoat.h"


//==============================================================================
class Terrain : public QWidget
{
    Q_OBJECT

public:
    Terrain (QWidget *parent, Projection *proj, std::shared_ptr<GshhsReader> gshhsReader);

	void    setCurrentDate (time_t t);
	time_t  getCurrentDate ();
    
    MapDrawer   *getDrawer()      {return drawer;}
    Projection  *getProjection()  {return proj;}
    
    // keepPrevious=true adds the file as one more model slot instead of
    // replacing what is already loaded, which is what makes switching
    // between forecasts instant: every model stays parsed in memory.
    FileDataType  loadMeteoDataFile (const QString& fileName, bool zoom,
                                     bool keepPrevious=false);
	FileDataType  getMeteoFileType()  {return currentFileType;}

	void  closeMeteoDataFile();

	//-- loaded models ---------------------------------------------
	int      countModels () const        {return modelSlots.size();}
	int      getActiveModel () const     {return activeSlot;}
	QString  getModelName (int i) const;
	QString  getModelFileName (int i) const;
	// True when that forecast carries sea state. An atmospheric model and
	// a wave model arrive as two separate files, so the answer differs
	// from one slot to the next.
	bool     modelHasWaves (int i) const;
	// Switches the displayed model, keeping the valid time if the new
	// model has it (that is what makes the comparison meaningful).
	bool     setActiveModel (int i);
	void     removeModel (int i);

	//--------------------------------------------------------
	GriddedPlotter  *getGriddedPlotter ();
	//--------------------------------------------------------
	
    void  indicateWaitingMap();    // Affiche un message d'attente
    
    bool  isSelectingZone()      {return isSelectionZoneEnCours;}
    
    void  getLastMousePosition (int *x, int *y) { *x = lastMouseX;
												  *y = lastMouseY; }

    void  setMouseLeftSelect(bool isSelect);
	
    void  zoomOnZone      (double x0, double y0, double x1, double y1);
    void  setSpecialZone  (double x0, double y0, double x1, double y1);
	void  showSpecialZone (bool b);
    
    
    bool  getSelectedRectangle (double *x0, double *y0, double *x1, double *y1);
    bool  getSelectedLine      (double *x0, double *y0, double *x1, double *y1);
    bool  getGribFileRectangle (double *x0, double *y0, double *x1, double *y1);
    
	QList<POI*> getListPOIs() { return findChildren <POI*>(); }

	VirtualBoat * getVirtualBoat ()  {return &virtualBoat;}
	// Redraws the boat without rebuilding the map underneath.
	void  refreshVirtualBoat ()      {update();}

	//-- clicking the route directly on the map -------------------
	void  startRouteDrawing ();
	void  stopRouteDrawing  ();
	bool  isRouteDrawing () const    {return routeDrawing;}
	
	// remember=false applies the colour map without storing it as the
	// user's choice. Needed when a model simply does not carry the field
	// the user picked: the map must adapt, but the preference must survive.
	void     setColorMapData (const DataCode &dtc, bool remember=true);
	DataCode getColorMapData ();
					
	QPixmap * createPixmap (time_t date, int width, int height);
    
public slots :
    // Map
    void setProjection (Projection *);
    void setDrawRivers (bool);
    void setDrawLonLatGrid (bool);
    void setDrawCountriesBorders (bool);
    void setDrawOrthodromie (bool);
    void setCountriesNames  (bool);
    void slotTemperatureLabels (bool);
    void setMapQuality (int q);
	
    void slot_Zoom_In  ();
    void slot_Zoom_Out ();
    void slot_Zoom_Sel ();
    void slot_Zoom_All ();
    void slot_Go_Left  ();
    void slot_Go_Right ();
    void slot_Go_Up   ();
    void slot_Go_Down ();
    
    void setShowPOIs (bool);
    void updateGraphicsParameters ();
    
    void setColorMapSmooth 	  (bool);
    void setInterpolateMissingRecords(bool);
    void setDuplicateFirstCumulativeRecord 	  (bool);
    void setDuplicateMissingWaveRecords 	  (bool);
    void setInterpolateValues 	  (bool);
    void setWindArrowsOnGribGrid  (bool);
    void setDrawWindArrows    (bool);
    void setCurrentArrowsOnGribGrid  (bool);
    void setDrawCurrentArrows    (bool);
    void setBarbules          (bool);
    void setThinArrows        (bool);

    void setGribGrid          (bool);
    void setCitiesNamesLevel  (int level);
	void setWaveArrowsType    (int type);   // slot: remembers the choice
	// Same, but applied only to the file on screen (see setColorMapData).
	void setWaveArrowsTypeTemporary (int type);
    
    void setDrawIsobars       (bool);
    void setDrawIsobarsLabels (bool);    
    void setIsobarsStep		  (double step);
    void setPressureMinMax    (bool);

	void setGeopotentialData	   (const DataCode &dtc);
    void setDrawGeopotential       (bool);
    void setDrawGeopotentialLabels (bool);    
    void setGeopotentialStep       (int);    
	
    void setDrawIsotherms0       (bool);
    void setDrawIsotherms0Labels (bool);
    void setIsotherms0Step	     (double step);
    
    void setDrawIsotherms       (bool);
    void setDrawIsotherms_Labels (bool);
    void setIsotherms_Step	      (double step);
    void setIsotherms_Altitude   (Altitude alt);
    
    void setDrawLinesThetaE       (bool);
    void setDrawLinesThetaE_Labels (bool);
    void setLinesThetaE_Step	    (double step);
    void setLinesThetaE_Altitude   (Altitude alt);
	
    void slotTimerResize();
    void slotTimerZoomWheel();
    void slotMustRedraw();
    
signals:
    // The set of loaded models or the active one has changed.
    void modelListChanged ();

    // Emitted on every change made by clicking the route on the map,
    // and once more with finished=true when the user ends the route.
    void routeDrawingChanged (bool finished);

    void selectionOK  (double x0, double y0, double x1, double y1);
    void mouseClicked (QMouseEvent * e);
    void mouseMoved   (QMouseEvent * e);
    void mouseLeave   (QEvent * e);


private:
	MapDrawer *drawer;
	FileDataType  currentFileType;
	VirtualBoat   virtualBoat;

	bool    routeDrawing;
	bool    routeCursorValid;
	double  routeCursorLon, routeCursorLat;
	int     draggedWaypoint;    // -1 when no waypoint is being moved
	// True when the boat route is on screen and can be edited.
	bool    routeIsEditable () const;

	//-----------------------------------------------
    Projection  *proj;
    GisReader   *gisReader;

    GriddedPlotter  *griddedPlot;      // the slot currently displayed

    struct ModelSlot {
        GriddedPlotter *plot;
        QString         name;
        QString         fileName;
    };
    QList<ModelSlot>  modelSlots;
    int               activeSlot;
    void  clearModelSlots ();

    bool        isEarthMapValid;
    bool        mustRedraw;
    bool    	isResizing;
    bool    	firstDrawingIsDone;
	int 		lastMouseX, lastMouseY;
	
	LongTaskProgress *taskProgress;

    QTimer      *timerResize;
    QTimer      *timerZoomWheel;
    QCursor		myCrossCursor;
    QCursor     enterCursor;

    QCursor     primaryCursor;
    QCursor     primaryCursorClick;
    QCursor     controlCursor;
    QCursor     controlCursorClick;
    QCursor     shiftCursor;
    QCursor     shiftCursorClick;

	double 		deltaZoomWheel;
        
    void  draw_OrthodromieSegment
            (QPainter &pnt, double x0,double y0, double x1,double y1, int recurs=0);
    
    //-----------------------------------------------
    void  paintEvent(QPaintEvent *e);
    void  resizeEvent (QResizeEvent *e);
    
    void  keyPressEvent (QKeyEvent *e);
    void  keyReleaseEvent (QKeyEvent *e);
    
    void  mousePressEvent (QMouseEvent * e);
    void  mouseReleaseEvent (QMouseEvent * e);
    void  mouseMoveEvent (QMouseEvent * e);
    void  enterEvent (QEvent * e);
    void  leaveEvent (QEvent * e);
    void  wheelEvent(QWheelEvent * e) ;
    void  zoomOnFileZone();


	//-----------------------------------------------
    bool     isMouseLeftSelect; // true -> mouseleft=select, mouseleft+control=pan, false -> mouseleft=pan, mouselef+control=select
    bool     isSelectionZoneEnCours;
    bool     isDraggingMapEnCours;
    double   selX0, selY0, selX1, selY1;   // sélection de zone (repère carte)
    double   globalX0, globalY0;		// global position of the mouse
    double   specialZoneX0, specialZoneY0, specialZoneX1, specialZoneY1;   // special zone
    bool     mustShowSpecialZone;
    
	QColor  selectColor;
	Qt::KeyboardModifiers keyModifiers;

    int     quality;
    
    //-----------------------------------------------
    // Flags indiquant les éléments à dessiner
    //-----------------------------------------------
    bool  showOrthodromie;
    bool  interpolateMissingRecords;
    bool  duplicateFirstCumulativeRecord;
	bool  duplicateMissingWaveRecords;
    bool  interpolateValues;
    bool  windArrowsOnGribGrid;
    bool  currentArrowsOnGribGrid;
	bool  drawCartouche;
    
    //-----------------------------------------------
    void  draw_Orthodromie(QPainter &painter);
	void createCrossCursor ();
    bool pleaseWait;     // long task in progress

};

#endif
