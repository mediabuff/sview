/**
 * Copyright © 2009-2014 Kirill Gavrilov <kirill@sview.ru>
 *
 * StOutPageFlip library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * StOutPageFlip library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _WIN32
#include "StDXNVWindow.h"
#include "StOutPageFlip.h"

#include <StThreads/StFPSMeter.h>
#include <StThreads/StProcess.h>

namespace {

    static const wchar_t ST_D3DWIN_CLASSNAME[] = L"StDirect3D";
    static StAtomic<int32_t> ST_D3DWIN_CLASSCOUNTER(0);

}

StDXNVWindow::StDXNVWindow(const StHandle<StMsgQueue>& theMsgQueue,
                           const size_t     theFboSizeX,
                           const size_t     theFboSizeY,
                           const StMonitor& theMonitor,
                           StOutPageFlip*   theStWin,
                           const bool       theHasWglDx)
: myMsgQueue(theMsgQueue),
  myBufferL(nullptr),
  myBufferR(nullptr),
  myFboSizeX(theFboSizeX),
  myFboSizeY(theFboSizeY),
  myHasWglDx(theHasWglDx),
  myWinD3d(nullptr),
  myMonitor(theMonitor),
  myStWin(theStWin),
  myEventReady (false),
  myEventQuit  (false),
  myEventShow  (false),
  myEventHide  (false),
  myEventUpdate(false) {
    stMemZero(myMouseState, sizeof(myMouseState));
    stMemZero(&myWinMsg,    sizeof(myWinMsg));
    stMemZero(myKeysMap,    sizeof(myKeysMap));
    stMemZero(myCharBuff,   sizeof(myCharBuff));
}

bool StDXNVWindow::allocateBuffers() {
    if(myBufferL == nullptr) {
        myBufferL = stMemAllocAligned<unsigned char*>(myFboSizeX * myFboSizeY * 4, 16);
    }
    if(myBufferR == nullptr) {
        myBufferR = stMemAllocAligned<unsigned char*>(myFboSizeX * myFboSizeY * 4, 16);
    }
    return myBufferL != nullptr
        && myBufferR != nullptr;
}

void StDXNVWindow::releaseBuffers() {
    stMemFreeAligned(myBufferL);
    stMemFreeAligned(myBufferR);
    myBufferL = myBufferR = nullptr;
}

StDXNVWindow::~StDXNVWindow() {
    releaseBuffers();
    unregisterClass(myWinClass);
}

bool StDXNVWindow::unregisterClass(StStringUtfWide& theName) {
    HMODULE aModule = ::GetModuleHandleW(nullptr);
    if(!theName.isEmpty()) {
        if(::UnregisterClassW(theName.toCString(), aModule) == 0) {
            ST_DEBUG_LOG("StDXNVWindow, FAILED to unregister Class= '" + theName.toUtf8() + "'");
            return false;
        }
        ST_DEBUG_LOG("StDXNVWindow, Unregistered Class= " + theName.toUtf8());
        theName.clear();
    }
    return true;
}

// static winproc function - used for callback on windows
LRESULT CALLBACK StDXNVWindow::wndProcWrapper(HWND   theWnd,
                                              UINT   theMsg,
                                              WPARAM theParamW,
                                              LPARAM theParamL) {
    if(theMsg == WM_CREATE) {
        // save pointer to our class instance (sent on window create) to window storage
        CREATESTRUCTW* aCreateInfo = (CREATESTRUCTW* )theParamL;
        ::SetWindowLongPtr(theWnd, GWLP_USERDATA, (LONG_PTR )aCreateInfo->lpCreateParams);
    }

    // get pointer to our class instance
    StDXNVWindow* aThis = (StDXNVWindow* )::GetWindowLongPtr(theWnd, GWLP_USERDATA);
    if(aThis != nullptr) {
        return aThis->wndProcFunction(theWnd, theMsg, theParamW, theParamL);
    } else {
        return ::DefWindowProcW(theWnd, theMsg, theParamW, theParamL);
    }
}

LRESULT StDXNVWindow::wndProcFunction(HWND   theWnd,
                                      UINT   theMsg,
                                      WPARAM theParamW,
                                      LPARAM theParamL) {
    // we do stupid checks here...
    if(myStWin->isFullScreen() && myStWin->isStereoOutput()) {
        if(theMsg == WM_MOUSEWHEEL) {
            int zDelta = GET_WHEEL_DELTA_WPARAM(theParamW);
            int mbtn = (zDelta > 0) ? ST_MOUSE_SCROLL_V_UP : ST_MOUSE_SCROLL_V_DOWN;
            updateMouseBtn(mbtn, true);  // emulate down
            updateMouseBtn(mbtn, false); // emulate up
        }

        updateMouseBtn(ST_MOUSE_LEFT,   GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) == 0 ? VK_LBUTTON : VK_RBUTTON) != 0);
        updateMouseBtn(ST_MOUSE_RIGHT,  GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) != 0 ? VK_LBUTTON : VK_RBUTTON) != 0);
        updateMouseBtn(ST_MOUSE_MIDDLE, GetAsyncKeyState(VK_MBUTTON) != 0);
    }
    return DefWindowProcW(theWnd, theMsg, theParamW, theParamL);
}

void StDXNVWindow::updateMouseBtn(const int theBtnId, bool theNewState) {
    if(myMouseState[theBtnId] != theNewState) {
        myMouseState[theBtnId] = theNewState;

        const StPointD_t aPnt = myStWin->getMousePos();
        StEvent anEvent;
        anEvent.Type = theNewState ? stEvent_MouseDown : stEvent_MouseUp;
        anEvent.Button.Time    = 0.0; //getEventTime(myEvent.time);
        anEvent.Button.Button  = (StVirtButton )theBtnId;
        anEvent.Button.Buttons = 0;
        anEvent.Button.PointX  = aPnt.x();
        anEvent.Button.PointY  = aPnt.y();
        myStWin->post(anEvent);
    }
}

static StString stLastError() {
    wchar_t* aMsgBuff = nullptr;
    DWORD anErrorCode = GetLastError();
    ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, anErrorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (wchar_t* )&aMsgBuff, 0, nullptr);

    StString aResult;
    if(aMsgBuff != nullptr) {
        aResult = StString(aMsgBuff) + " (" + int(anErrorCode) + ")";
        ::LocalFree(aMsgBuff);
    } else {
        aResult = StString("Error code #") + int(anErrorCode);
    }
    return aResult;
}

bool StDXNVWindow::initWinAPIWindow() {
    HINSTANCE anAppInstance = GetModuleHandleW(nullptr);
    if(myWinClass.isEmpty()) {
        myWinClass = StStringUtfWide(ST_D3DWIN_CLASSNAME) + StStringUtfWide(ST_D3DWIN_CLASSCOUNTER.increment());
        WNDCLASSW aWinClass; stMemZero(&aWinClass, sizeof(aWinClass));
        aWinClass.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        aWinClass.lpfnWndProc   = (WNDPROC )wndProcWrapper;
        aWinClass.hInstance     = anAppInstance;
        aWinClass.hIcon         = ::LoadIconW(anAppInstance, L"A");
        aWinClass.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        aWinClass.lpszClassName = myWinClass.toCString();
        if(RegisterClassW(&aWinClass) == 0) {
            myMsgQueue->pushError(StString("PageFlip output - Class registration '") + myWinClass.toUtf8() + "' has failed\n(" + stLastError() + ")");
            myWinClass.clear();
            return false;
        }
    }
    StRectI_t aMonRect = myMonitor.getVRect();
    myWinD3d = ::CreateWindowExW(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE, myWinClass.toCString(),
                                 L"sView Direct3D output", WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                 aMonRect.left(), aMonRect.top(), aMonRect.width(), aMonRect.height(),
                                 nullptr, nullptr, anAppInstance, this);
    if(myWinD3d == nullptr) {
        myMsgQueue->pushError(StString("PageFlip output - Failed to CreateWindow\n(") + stLastError() + ")");
        return false;
    }
    ST_DEBUG_LOG_AT("StDXWindow, Created help window");
    return true;
}

void StDXNVWindow::dxShow() {
    if(myDxManager.isNull()) {
        return;
    }

    myShowState = true;
    myMutex.lock();
    myDxSurface.nullify();

    POINT aCursorPos = {};
    ::GetCursorPos(&aCursorPos); // backup cursor position

    if(myDxManager->reset(myWinD3d, int(getD3dSizeX()), int(getD3dSizeY()), myShowState)) {
        myDxSurface = new StDXNVSurface(getD3dSizeX(), getD3dSizeY());
        if(!myDxSurface->create(*myDxManager, myHasWglDx)) {
            //stError(ST_TEXT("Output plugin, Failed to create Direct3D surface"));
            ST_ERROR_LOG("StDXNVWindow, Failed to create Direct3D surface");
            myMsgQueue->pushError(StString("PageFlip output - Failed to create Direct3D surface"));
            ///return;
        }
    } else {
        myDxManager->reset(myWinD3d, int(100), int(100), false);
        // workaround to ensure something show on the screen
        myStWin->setFullScreen(false);
        myShowState = false;
        ST_ERROR_LOG("StDXNVWindow, Failed to reset Direct3D device into FULLSCREEN state");
    }
    myMutex.unlock();
    ::ShowWindow(myWinD3d, SW_SHOWMAXIMIZED);
    ::UpdateWindow(myWinD3d); // debug staff

    myEventShow.reset();
    ::SetCursorPos(aCursorPos.x, aCursorPos.y); // restore cursor position
}

void StDXNVWindow::dxHide() {
    if(myDxManager.isNull()) {
        return;
    }

    myShowState = false;
    myMutex.lock();
    myDxSurface.nullify();

    POINT aCursorPos = {};
    ::GetCursorPos(&aCursorPos); // backup cursor position

    // release unused memory
    releaseBuffers();
    if(!myDxManager->reset(myWinD3d, int(getD3dSizeX()), int(getD3dSizeY()), myShowState)) {
        ST_ERROR_LOG("StDXNVWindow, Failed to reset Direct3D device into WINDOWED state");
    }
    myMutex.unlock();

    ::ShowWindow(myWinD3d, SW_HIDE);
    ::UpdateWindow(myWinD3d);

    myEventHide.reset();
    ::SetCursorPos(aCursorPos.x, aCursorPos.y); // restore cursor position
}

void StDXNVWindow::dxUpdate() {
    if(myDxManager.isNull()) {
        return;
    } else if(!myShowState) {
        myEventUpdate.reset();
        return;
    }

    if(myHasWglDx) {
        myMutex.lock();
        myDxManager->beginRender();
        myDxSurface->render(*myDxManager);
        myEventUpdate.reset();
        myDxManager->endRender();
        if(!myDxManager->swap()) {
            myEventShow.set();
        }
        myMutex.unlock();
    } else {
        myMutex.lock();
        if(myBufferL != nullptr && myBufferR != nullptr) {
            myDxSurface->update(myFboSizeX, myFboSizeY, myBufferR, false);
            myDxSurface->update(myFboSizeX, myFboSizeY, myBufferL, true);
        }
        myMutex.unlock();
        myDxManager->beginRender();
        myDxSurface->render(*myDxManager);
        myEventUpdate.reset();
        myDxManager->endRender();
        if(!myDxManager->swap()) {
            myEventShow.set();
        }
    }

    //static StFPSMeter dxFPSMeter;
    //if((++dxFPSMeter).isUpdated()) { ST_DEBUG_LOG("DX FPS= " + dxFPSMeter.getAverage()); }
}

void StDXNVWindow::dxPeekMessages() {
    // a windows message has arrived
    while(::PeekMessageW(&myWinMsg, nullptr, 0U, 0U, PM_REMOVE)) {
        // we process WM_KEYDOWN/WM_KEYUP manually - TranslateMessage is redundant
        //TranslateMessage(&myWinMsg);

        switch(myWinMsg.message) {
            // keys lookup
            case WM_KEYDOWN: {
                myKeyEvent.Key.Time = myStWin->getEventTime(myWinMsg.time);
                myKeyEvent.Key.VKey = (StVirtKey )myWinMsg.wParam;

                // ToUnicode needs high-order bit of a byte to be set for pressed keys...
                //GetKeyboardState(myKeysMap);
                StKeysState& aKeysState = myStWin->changeKeysState();
                for(int anIter = 0; anIter < 256; ++anIter) {
                    myKeysMap[anIter] = aKeysState.isKeyDown((StVirtKey )anIter) ? 0xFF : 0;
                }

                if(::ToUnicode(myKeyEvent.Key.VKey, HIWORD(myWinMsg.lParam) & 0xFF,
                               myKeysMap, myCharBuff, 4, 0) > 0) {
                    StUtfWideIter aUIter(myCharBuff);
                    myKeyEvent.Key.Char = *aUIter;
                } else {
                    myKeyEvent.Key.Char = 0;
                }

                myKeyEvent.Key.Flags = ST_VF_NONE;
                myKeyEvent.Type = stEvent_KeyDown;
                myStWin->post(myKeyEvent);
                break;
            }
            case WM_KEYUP: {
                myKeyEvent.Type      = stEvent_KeyUp;
                myKeyEvent.Key.VKey  = (StVirtKey )myWinMsg.wParam;
                myKeyEvent.Key.Time  = myStWin->getEventTime(myWinMsg.time);
                myKeyEvent.Key.Flags = ST_VF_NONE;
                myStWin->post(myKeyEvent);
                break;
            }
            default: break;
        }

        ::DispatchMessageW(&myWinMsg);
    }
}

void StDXNVWindow::dxLoop() {
    if(myWinD3d == nullptr && !initWinAPIWindow()) {
        return;
    }
    myDxManager = new StDXManager(); //create our direct Manager
    if(!myDxManager->init(myWinD3d, int(getD3dSizeX()), int(getD3dSizeY()), false, myMonitor)) {
        myMsgQueue->pushError(stCString("PageFlip output - Direct3D manager initialization has failed!"));
        return;
    }

    enum {
        StDXMsg_Quit = 0,
        StDXMsg_Show,
        StDXMsg_Hide,
        StDXMsg_Update,
        StDXMsg_WINDOW
    };
    HANDLE aWaitEvents[4] = {};
    aWaitEvents[StDXMsg_Quit]   = myEventQuit  .getHandle();
    aWaitEvents[StDXMsg_Show]   = myEventShow  .getHandle();
    aWaitEvents[StDXMsg_Hide]   = myEventHide  .getHandle();
    aWaitEvents[StDXMsg_Update] = myEventUpdate.getHandle();

    myEventReady.set();
    for(;;) {
        switch(::MsgWaitForMultipleObjects(4, aWaitEvents, FALSE, INFINITE, QS_ALLINPUT)) {
            case WAIT_OBJECT_0 + StDXMsg_Quit: {
                ST_DEBUG_LOG_AT("releaseDXWindow()");
                if(!myDxManager.isNull()) {
                    myShowState = false;

                    myMutex.lock();
                    myDxSurface.nullify();
                    /// TODO (Kirill Gavrilov#9) do we need this call here?
                    myDxManager->reset(myWinD3d, int(2), int(2), false);
                    myDxManager.nullify();
                    myMutex.unlock();
                }

                ::PostQuitMessage(0);
                ::DestroyWindow(myWinD3d);
                unregisterClass(myWinClass);
                return;
            }
            case WAIT_OBJECT_0 + StDXMsg_Show: {
                dxShow();
                break;
            }
            case WAIT_OBJECT_0 + StDXMsg_Hide: {
                dxHide();
                break;
            }
            case WAIT_OBJECT_0 + StDXMsg_Update: {
                dxUpdate();
                break;
            }
            case WAIT_OBJECT_0 + StDXMsg_WINDOW: {
                // a windows message has arrived
                dxPeekMessages();
                break;
            }
        }
    }
}

#endif // _WIN32
