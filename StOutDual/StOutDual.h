/**
 * StOutDual, class providing stereoscopic output for Dual Input hardware using StCore toolkit.
 * Copyright © 2007-2015 Kirill Gavrilov <kirill@sview.ru>
 *
 * Distributed under the Boost Software License, Version 1.0.
 * See accompanying file license-boost.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt
 */

#ifndef __StOutDual_h_
#define __StOutDual_h_

#include <StCore/StWindow.h>
#include <StGL/StGLVertexBuffer.h>
#include <StThreads/StFPSControl.h>

class StSettings;
class StProgramMM;
class StGLFrameBuffer;

/**
 * This class implements stereoscopic rendering on displays
 * with independent connection to each view.
 */
class StOutDual : public StWindow {

        public:

    /**
     * Main constructor.
     */
    ST_CPPEXPORT StOutDual(const StHandle<StResourceManager>& theResMgr,
                           const StNativeWin_t                theParentWindow);

    /**
     * Destructor.
     */
    ST_CPPEXPORT virtual ~StOutDual();

    /**
     * Renderer about string.
     */
    ST_CPPEXPORT virtual StString getRendererAbout() const;

    /**
     * Renderer id.
     */
    ST_CPPEXPORT virtual const char* getRendererId() const;

    /**
     * Active Device id.
     */
    ST_CPPEXPORT virtual const char* getDeviceId() const;

    /**
     * Activate Device.
     */
    ST_CPPEXPORT virtual bool setDevice(const StString& theDevice);

    /**
     * Devices list.
     */
    ST_CPPEXPORT virtual void getDevices(StOutDevicesList& theList) const;

    /**
     * Retrieve options list.
     */
    ST_CPPEXPORT virtual void getOptions(StParamsList& theList) const;

    /**
     * Create and show window.
     * @return false if any critical error appeared
     */
    ST_CPPEXPORT virtual bool create();

    /**
     * Close the window.
     */
    ST_CPPEXPORT virtual void close();

    /**
     * Extra routines to be processed before window close.
     */
    ST_CPPEXPORT virtual void beforeClose();

    /**
     * Process callback.
     */
    ST_CPPEXPORT virtual void processEvents();

    /**
     * Stereo renderer.
     */
    ST_CPPEXPORT virtual void stglDraw();

        private:

    typedef enum tagDeviceEnum {
        DEVICE_AUTO       =-1,
        DUALMODE_SIMPLE   = 0, //!< no mirroring
        DUALMODE_XMIRROW  = 1, //!< mirror on X SLAVE  window
        DUALMODE_YMIRROW  = 2, //!< mirror on Y SLAVE  window
    } DeviceEnum;

        private:

    ST_LOCAL void replaceDualAttribute(const DeviceEnum theValue);

    /**
     * Release GL resources before window closing.
     */
    ST_LOCAL void releaseResources();

    /**
     * On/off VSync callback.
     */
    ST_LOCAL void doSwitchVSync(const int32_t theValue);

    /**
     * Change slave window position callback.
     */
    ST_LOCAL void doSlaveMon(const int32_t theValue);

        private:

    static StAtomic<int32_t> myInstancesNb; //!< shared counter for all instances

        private:

    struct {

        StHandle<StInt32Param> SlaveMonId; //!< slave window position
        StHandle<StBoolParam>  MonoClone;  //!< display mono in stereo

    } params;

        private:

    StOutDevicesList          myDevices;
    StHandle<StSettings>      mySettings;
    StString                  myAbout;           //!< about string

    StHandle<StGLContext>     myContext;
    StHandle<StGLFrameBuffer> myFrBuffer;        //!< OpenGL frame buffer object
    StHandle<StProgramMM>     myProgram;
    StFPSControl              myFPSControl;
    StGLVertexBuffer          myVertFlatBuf;     //!< buffers to draw simple fullsreen quad
    StGLVertexBuffer          myVertXMirBuf;
    StGLVertexBuffer          myVertYMirBuf;
    StGLVertexBuffer          myTexCoordBuf;

    DeviceEnum                myDevice;
    bool                      myToCompressMem;   //!< reduce memory usage
    bool                      myIsBroken;        //!< special flag for broke state - when FBO can not be allocated

};

#endif //__StOutDual_h_
