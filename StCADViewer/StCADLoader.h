/**
 * This source is a part of sView program.
 *
 * Copyright © Kirill Gavrilov, 2011-2016
 */

#ifndef __StCADLoader_h_
#define __StCADLoader_h_

#include <AIS_InteractiveObject.hxx>
#include <NCollection_Sequence.hxx>
#include <TopoDS_Shape.hxx>

#include <StStrings/StString.h>
#include <StFile/StMIMEList.h>
#include <StGL/StPlayList.h>
#include <StGLMesh/StGLMesh.h>
#include <StSlots/StSignal.h>

class StLangMap;
class StThread;

class StCADLoader {

        public:

    static const StString ST_CAD_MIME_STRING;
    static const StMIMEList ST_CAD_MIME_LIST;
    static const StArrayList<StString> ST_CAD_EXTENSIONS_LIST;

    ST_LOCAL StCADLoader(const StHandle<StLangMap>& theLangMap);
    ST_LOCAL ~StCADLoader();

    ST_LOCAL void mainLoop();

    ST_LOCAL void doLoadNext() {
        myEvLoadNext.set();
    }

    ST_LOCAL bool getNextShape(NCollection_Sequence<Handle(AIS_InteractiveObject)>& thePrsList);

    ST_LOCAL StPlayList& getPlayList() {
        return myPlayList;
    }

        public:  //!< Signals

    struct {
        /**
         * Emit callback Slot on model load error.
         * @param theUserData error description
         */
        StSignal<void (const StCString& )> onError;
    } signals;

        private:

    ST_LOCAL TopoDS_Shape loadIGES(const StString& theFileToLoadPath);
    ST_LOCAL TopoDS_Shape loadSTEP(const StString& theFileToLoadPath);

    ST_LOCAL bool loadModel(const StHandle<StFileNode>& theSource);
    ST_LOCAL bool computeMesh(const TopoDS_Shape& theShape);

        private:

    StHandle<StThread>  myThread;
    StHandle<StLangMap> myLangMap;
    StPlayList          myPlayList;
    StCondition         myEvLoadNext;
    TopoDS_Shape        myShape;
    StMutex             myShapeLock;
    volatile bool       myIsLoaded;
    volatile bool       myToQuit;

};

#endif //__StCADLoader_h_
