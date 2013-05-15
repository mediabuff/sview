/**
 * Copyright © 2012-2013 Kirill Gavrilov <kirill@sview.ru>
 *
 * Distributed under the Boost Software License, Version 1.0.
 * See accompanying file license-boost.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt
 */

#ifdef _WIN32
    #include <windows.h>
#endif

#include <StGL/StGLContext.h>
#include <StGL/StGLFunctions.h>

#include <StGLCore/StGLCore42.h>

#include <StStrings/StLogger.h>

#include <stAssert.h>

#if(defined(__APPLE__))
    #include <dlfcn.h>
    #include <OpenGL/OpenGL.h>
#elif(!defined(_WIN32))
    #include <GL/glx.h> // glXGetProcAddress()
#endif

StGLContext::StGLContext()
: core11(NULL),
  core11fwd(NULL),
  core20(NULL),
  core20fwd(NULL),
  core32(NULL),
  core32back(NULL),
  core41(NULL),
  core41back(NULL),
  core42(NULL),
  core42back(NULL),
  arbFbo(NULL),
  extAll(NULL),
  extSwapTear(false),
  myFuncs(new StGLFunctions()),
  myGpuName(GPU_UNKNOWN),
  myVerMajor(0),
  myVerMinor(0),
  myIsRectFboSupported(false),
  myWasInit(false) {
    stMemZero(&(*myFuncs), sizeof(StGLFunctions));
    extAll = &(*myFuncs);
#ifdef __APPLE__
    mySysLib.loadSimple("/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL");
#endif
}

StGLContext::StGLContext(const bool theToInitialize)
: core11(NULL),
  core11fwd(NULL),
  core20(NULL),
  core20fwd(NULL),
  core32(NULL),
  core32back(NULL),
  core41(NULL),
  core41back(NULL),
  core42(NULL),
  core42back(NULL),
  arbFbo(NULL),
  extAll(NULL),
  extSwapTear(false),
  myFuncs(new StGLFunctions()),
  myGpuName(GPU_UNKNOWN),
  myVerMajor(0),
  myVerMinor(0),
  myIsRectFboSupported(false),
  myWasInit(false) {
    stMemZero(&(*myFuncs), sizeof(StGLFunctions));
    extAll = &(*myFuncs);
#ifdef __APPLE__
    mySysLib.loadSimple("/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL");
#endif
    if(theToInitialize) {
        stglInit();
    }
}

StGLContext::~StGLContext() {
    //
}

void* StGLContext::stglFindProc(const char* theName) const {
#ifdef _WIN32
    return (void* )wglGetProcAddress(theName);
#elif defined(__APPLE__)
    return mySysLib.isOpened() ? mySysLib.find(theName) : NULL;
#else
    // Notice that some libGL implementations will NEVER return NULL pointer!
    // This is because glXGetProcAddress() can be called without GL context bound
    // (this is explicitly permitted unlike wglGetProcAddress())
    // creating functions table for both known (exported by driver when GL context created)
    // and unknown (requested by user with glXGetProcAddress())
    return (void* )glXGetProcAddress((const GLubyte* )theName);
#endif
}

bool StGLContext::stglCheckExtension(const char* theName) const {
    ST_ASSERT_SLIP(theName != NULL, "stglCheckExtension() called with NULL string", return false);

    // available since OpenGL 3.0
    /*if(isGlGreaterEqual(3, 0)) {
        const int aNameLen = std::strlen(theName);
        GLint anExtNb = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &anExtNb);
        for(GLint anIter = 0; anIter < anExtNb; ++anIter) {
            const char* anExtension = (const char* )core30->glGetStringi(GL_EXTENSIONS, (GLuint )anIter);
            if(anExtension[aNameLen] == '\0'
            && std::strncmp(anExtension, theName, aNameLen) == 0) {
                return true;
            }
        }
        return false;
    }*/

    return stglCheckExtension((const char* )glGetString (GL_EXTENSIONS), theName);
}

bool StGLContext::stglCheckExtension(const char* theStringList,
                                     const char* theName) const {
    if(theName == NULL || theStringList == NULL) {
        return false;
    }
    const size_t aNameLen = std::strlen(theName);
    const char* aPtrEnd = theStringList + std::strlen(theStringList);
    for(char* aPtrIter = (char* )theStringList; aPtrIter < aPtrEnd;) {
        const size_t aWordLen = std::strcspn(aPtrIter, " ");
        if((aWordLen == aNameLen) && (std::strncmp(aPtrIter, theName, aNameLen) == 0)) {
            return true;
        }
        aPtrIter += (aWordLen + 1);
    }
    return false;
}

StString StGLContext::stglErrorToString(const GLenum theError) {
    switch(theError) {
        case GL_NO_ERROR:          return "GL_NO_ERROR";
        case GL_INVALID_ENUM:      return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:     return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_STACK_OVERFLOW:    return "GL_STACK_OVERFLOW";
        case GL_STACK_UNDERFLOW:   return "GL_STACK_UNDERFLOW";
        case GL_OUT_OF_MEMORY:     return "GL_OUT_OF_MEMORY";
        default: {
            return StString("Unknown GL error #") + int(theError);
        }
    }
}

void StGLContext::stglResetErrors() {
    GLenum anErr = glGetError();
    for(int aLimit = 1000; (anErr != GL_NO_ERROR) && (aLimit > 0); --aLimit, anErr = glGetError()) {
        ST_DEBUG_LOG("Unhandled GL error (" + stglErrorToString(anErr) + ")");
    }
}

void StGLContext::stglReadVersion() {
    // reset values
    myVerMajor = 0;
    myVerMinor = 0;

    // available since OpenGL 3.0
    glGetIntegerv(GL_MAJOR_VERSION, &myVerMajor);
    glGetIntegerv(GL_MINOR_VERSION, &myVerMinor);
    if(glGetError() == GL_NO_ERROR
    && myVerMajor >= 3) {
        return;
    }
    stglResetErrors();

    // Read version string.
    // Notice that only first two numbers splitted by point '2.1 XXXXX' are significant.
    // Following trash (after space) is vendor-specific.
    // New drivers also returns micro version of GL like '3.3.0' which has no meaning
    // and should be considered as vendor-specific too.
    const char* aVerStr = (const char* )glGetString(GL_VERSION);
    if(aVerStr == NULL || *aVerStr == '\0') {
        // invalid GL context
        return;
    }

    // parse string for major number
    char aMajorStr[32];
    char aMinorStr[32];
    size_t aMajIter = 0;
    while(aVerStr[aMajIter] >= '0' && aVerStr[aMajIter] <= '9') {
        ++aMajIter;
    }
    if(aMajIter == 0 || aMajIter >= sizeof(aMajorStr)) {
        return;
    }
    stMemCpy(aMajorStr, aVerStr, aMajIter);
    aMajorStr[aMajIter] = '\0';

    // parse string for minor number
    aVerStr += aMajIter + 1;
    size_t aMinIter = 0;
    while(aVerStr[aMinIter] >= '0' && aVerStr[aMinIter] <= '9') {
        ++aMinIter;
    }
    if(aMinIter == 0 || aMinIter >= sizeof(aMinorStr)) {
        return;
    }
    stMemCpy(aMinorStr, aVerStr, aMinIter);
    aMinorStr[aMinIter] = '\0';

    // read numbers
    myVerMajor = std::atoi(aMajorStr);
    myVerMinor = std::atoi(aMinorStr);

    if(myVerMajor <= 0) {
        myVerMajor = 0;
        myVerMinor = 0;
    }
}

StString StGLContext::stglInfo() {
    StString anInfo = StString("OpenGL info:\n")
        + "  GLvendor    = '" + (const char* )glGetString(GL_VENDOR)   + "'\n"
        + "  GLdevice    = '" + (const char* )glGetString(GL_RENDERER) + "'\n"
        + "  GLversion   = '" + (const char* )glGetString(GL_VERSION)  + "'\n"
        + "  GLSLversion = '" + (const char* )glGetString(GL_SHADING_LANGUAGE_VERSION) + "'\n";
    return anInfo;
}

StString StGLContext::stglFullInfo() const {
    StString anInfo = StString()
        + "  GLvendor: "    + (const char* )glGetString(GL_VENDOR)   + "\n"
        + "  GLdevice: "    + (const char* )glGetString(GL_RENDERER) + "\n"
        + "  GLversion: "   + (const char* )glGetString(GL_VERSION)  + "\n"
        + "  GLSLversion: " + (const char* )glGetString(GL_SHADING_LANGUAGE_VERSION);

    if(stglCheckExtension("GL_ATI_meminfo")) {
        GLint aMemInfo[4]; stMemSet(aMemInfo, -1, sizeof(aMemInfo));
        core11fwd->glGetIntegerv(GL_VBO_FREE_MEMORY_ATI, aMemInfo);
        anInfo = anInfo + "\n"
        + "  Free GPU memory: " + (aMemInfo[0] / 1024)  + " MiB";
    } else if(stglCheckExtension("GL_NVX_gpu_memory_info")) {
        GLint aDedicated     = -1;
        GLint aDedicatedFree = -1;
        glGetIntegerv(0x9047, &aDedicated);     // GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX
        glGetIntegerv(0x9049, &aDedicatedFree); // GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
        anInfo = anInfo + "\n"
        + "  Free GPU memory: " + (aDedicatedFree / 1024)  + " MiB (from " + (aDedicated / 1024) + " MiB)";
    }
    return anInfo;
}

void StGLContext::stglSyncState() {
    while(!myScissorStack.empty()) {
        myScissorStack.pop();
    }

    if(core11fwd->glIsEnabled(GL_SCISSOR_TEST)) {
        StGLBoxPx aRect;
        core11fwd->glGetIntegerv(GL_SCISSOR_BOX, aRect.v);
        myScissorStack.push(aRect);
    }
}

void StGLContext::stglSetScissorRect(const StGLBoxPx& theRect,
                                     const bool       thePushStack) {
    if(myScissorStack.empty()) {
        core11fwd->glEnable(GL_SCISSOR_TEST);
    }
    if(thePushStack || myScissorStack.empty()) {
        StGLBoxPx aDummyRect; // will be initialized right after
        myScissorStack.push(aDummyRect);
    }

    StGLBoxPx& aRect = myScissorStack.top();
    aRect = theRect;
    core11fwd->glScissor(aRect.x(), aRect.y(), aRect.width(), aRect.height());
}

void StGLContext::stglResetScissorRect() {
    if(!myScissorStack.empty()) {
        myScissorStack.pop();
    }
    if(myScissorStack.empty()) {
        core11fwd->glDisable(GL_SCISSOR_TEST);
        return;
    }

    // setup previous value in stack
    const StGLBoxPx& aRect = myScissorStack.top();
    core11fwd->glScissor(aRect.x(), aRect.y(), aRect.width(), aRect.height());
}

void StGLContext::stglResizeViewport(const StGLBoxPx& theRect) {
    const GLsizei aHeight = (theRect.height() == 0) ? 1 : theRect.height();
    core11fwd->glViewport(theRect.x(), theRect.y(), theRect.width(), aHeight);
}

bool StGLContext::stglSetVSync(const VSync_Mode theVSyncMode) {
    GLint aSyncInt = (GLint )theVSyncMode;
    if(theVSyncMode == VSync_MIXED && !extSwapTear) {
        aSyncInt = 1;
    }
#ifdef _WIN32
    if(myFuncs->wglSwapIntervalEXT != NULL) {
        myFuncs->wglSwapIntervalEXT(aSyncInt);
        return true;
    }
#elif(defined(__APPLE__))
    return CGLSetParameter(CGLGetCurrentContext(), kCGLCPSwapInterval, &aSyncInt) == kCGLNoError;
#else
    if(aSyncInt == -1 && myFuncs->glXSwapIntervalEXT != NULL) {
        typedef int (*glXSwapIntervalEXT_t_x)(Display* theDisplay, GLXDrawable theDrawable, int theInterval);
        glXSwapIntervalEXT_t_x aFuncPtr = (glXSwapIntervalEXT_t_x )myFuncs->glXSwapIntervalEXT;
        aFuncPtr(glXGetCurrentDisplay(), glXGetCurrentDrawable(), aSyncInt);
        return true;
    } else if(myFuncs->glXSwapIntervalSGI != NULL) {
        myFuncs->glXSwapIntervalSGI(aSyncInt);
        return true;
    }
#endif
    return false;
}

bool StGLContext::stglInit() {
    if(myWasInit) {
        return true;
    }

    // read version
    stglReadVersion();

    core11    = (StGLCore11*    )(&(*myFuncs));
    core11fwd = (StGLCore11Fwd* )(&(*myFuncs));

    myMaxTexDim = 2048;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &myMaxTexDim);

    bool has12 = false;
    bool has13 = false;
    bool has14 = false;
    bool has15 = false;
    bool has20 = false;
    bool has21 = false;
    bool has30 = false;
    bool has31 = false;
    bool has32 = false;
    bool has33 = false;
    bool has40 = false;
    bool has41 = false;
    bool has42 = false;

    #define STGL_READ_FUNC(theFunc) stglFindProc(#theFunc, myFuncs->theFunc)

    // retrieve platform-dependent extensions
#ifdef _WIN32
    if(STGL_READ_FUNC(wglGetExtensionsStringARB)) {
        const char* aWglExts = myFuncs->wglGetExtensionsStringARB(wglGetCurrentDC());
        if(stglCheckExtension(aWglExts, "WGL_EXT_swap_control")) {
            STGL_READ_FUNC(wglSwapIntervalEXT);
        }
        extSwapTear = stglCheckExtension(aWglExts, "WGL_EXT_swap_control_tear");
    }
#elif(defined(__APPLE__))
    //
#else
    Display* aDisp = glXGetCurrentDisplay();
    const char* aGlxExts = glXQueryExtensionsString(aDisp, DefaultScreen(aDisp));
    if(stglCheckExtension(aGlxExts, "GLX_EXT_swap_control")) {
        STGL_READ_FUNC(glXSwapIntervalEXT);
    }
    if(stglCheckExtension(aGlxExts, "GLX_SGI_swap_control")) {
        STGL_READ_FUNC(glXSwapIntervalSGI);
    }
    extSwapTear = stglCheckExtension(aGlxExts, "GLX_EXT_swap_control_tear");
#endif

    // load OpenGL 1.2 new functions
    has12 = isGlGreaterEqual(1, 2)
         && STGL_READ_FUNC(glBlendColor)
         && STGL_READ_FUNC(glBlendEquation)
         && STGL_READ_FUNC(glDrawRangeElements)
         && STGL_READ_FUNC(glTexImage3D)
         && STGL_READ_FUNC(glTexSubImage3D)
         && STGL_READ_FUNC(glCopyTexSubImage3D);

    // load OpenGL 1.3 new functions
    has13 = isGlGreaterEqual(1, 3)
         && STGL_READ_FUNC(glActiveTexture)
         && STGL_READ_FUNC(glSampleCoverage)
         && STGL_READ_FUNC(glCompressedTexImage3D)
         && STGL_READ_FUNC(glCompressedTexImage2D)
         && STGL_READ_FUNC(glCompressedTexImage1D)
         && STGL_READ_FUNC(glCompressedTexSubImage3D)
         && STGL_READ_FUNC(glCompressedTexSubImage2D)
         && STGL_READ_FUNC(glCompressedTexSubImage1D)
         && STGL_READ_FUNC(glGetCompressedTexImage)
         && STGL_READ_FUNC(glClientActiveTexture)
         && STGL_READ_FUNC(glMultiTexCoord1d)
         && STGL_READ_FUNC(glMultiTexCoord1dv)
         && STGL_READ_FUNC(glMultiTexCoord1f)
         && STGL_READ_FUNC(glMultiTexCoord1fv)
         && STGL_READ_FUNC(glMultiTexCoord1i)
         && STGL_READ_FUNC(glMultiTexCoord1iv)
         && STGL_READ_FUNC(glMultiTexCoord1s)
         && STGL_READ_FUNC(glMultiTexCoord1sv)
         && STGL_READ_FUNC(glMultiTexCoord2d)
         && STGL_READ_FUNC(glMultiTexCoord2dv)
         && STGL_READ_FUNC(glMultiTexCoord2f)
         && STGL_READ_FUNC(glMultiTexCoord2fv)
         && STGL_READ_FUNC(glMultiTexCoord2i)
         && STGL_READ_FUNC(glMultiTexCoord2iv)
         && STGL_READ_FUNC(glMultiTexCoord2s)
         && STGL_READ_FUNC(glMultiTexCoord2sv)
         && STGL_READ_FUNC(glMultiTexCoord3d)
         && STGL_READ_FUNC(glMultiTexCoord3dv)
         && STGL_READ_FUNC(glMultiTexCoord3f)
         && STGL_READ_FUNC(glMultiTexCoord3fv)
         && STGL_READ_FUNC(glMultiTexCoord3i)
         && STGL_READ_FUNC(glMultiTexCoord3iv)
         && STGL_READ_FUNC(glMultiTexCoord3s)
         && STGL_READ_FUNC(glMultiTexCoord3sv)
         && STGL_READ_FUNC(glMultiTexCoord4d)
         && STGL_READ_FUNC(glMultiTexCoord4dv)
         && STGL_READ_FUNC(glMultiTexCoord4f)
         && STGL_READ_FUNC(glMultiTexCoord4fv)
         && STGL_READ_FUNC(glMultiTexCoord4i)
         && STGL_READ_FUNC(glMultiTexCoord4iv)
         && STGL_READ_FUNC(glMultiTexCoord4s)
         && STGL_READ_FUNC(glMultiTexCoord4sv)
         && STGL_READ_FUNC(glLoadTransposeMatrixf)
         && STGL_READ_FUNC(glLoadTransposeMatrixd)
         && STGL_READ_FUNC(glMultTransposeMatrixf)
         && STGL_READ_FUNC(glMultTransposeMatrixd);

    // load OpenGL 1.4 new functions
    has14 = isGlGreaterEqual(1, 4)
         && STGL_READ_FUNC(glBlendFuncSeparate)
         && STGL_READ_FUNC(glMultiDrawArrays)
         && STGL_READ_FUNC(glMultiDrawElements)
         && STGL_READ_FUNC(glPointParameterf)
         && STGL_READ_FUNC(glPointParameterfv)
         && STGL_READ_FUNC(glPointParameteri)
         && STGL_READ_FUNC(glPointParameteriv);

    // load OpenGL 1.5 new functions
    has15 = isGlGreaterEqual(1, 5)
         && STGL_READ_FUNC(glGenQueries)
         && STGL_READ_FUNC(glDeleteQueries)
         && STGL_READ_FUNC(glIsQuery)
         && STGL_READ_FUNC(glBeginQuery)
         && STGL_READ_FUNC(glEndQuery)
         && STGL_READ_FUNC(glGetQueryiv)
         && STGL_READ_FUNC(glGetQueryObjectiv)
         && STGL_READ_FUNC(glGetQueryObjectuiv)
         && STGL_READ_FUNC(glBindBuffer)
         && STGL_READ_FUNC(glDeleteBuffers)
         && STGL_READ_FUNC(glGenBuffers)
         && STGL_READ_FUNC(glIsBuffer)
         && STGL_READ_FUNC(glBufferData)
         && STGL_READ_FUNC(glBufferSubData)
         && STGL_READ_FUNC(glGetBufferSubData)
         && STGL_READ_FUNC(glMapBuffer)
         && STGL_READ_FUNC(glUnmapBuffer)
         && STGL_READ_FUNC(glGetBufferParameteriv)
         && STGL_READ_FUNC(glGetBufferPointerv);

    // load OpenGL 2.0 new functions
    has20 = isGlGreaterEqual(2, 0)
         && STGL_READ_FUNC(glBlendEquationSeparate)
         && STGL_READ_FUNC(glDrawBuffers)
         && STGL_READ_FUNC(glStencilOpSeparate)
         && STGL_READ_FUNC(glStencilFuncSeparate)
         && STGL_READ_FUNC(glStencilMaskSeparate)
         && STGL_READ_FUNC(glAttachShader)
         && STGL_READ_FUNC(glBindAttribLocation)
         && STGL_READ_FUNC(glCompileShader)
         && STGL_READ_FUNC(glCreateProgram)
         && STGL_READ_FUNC(glCreateShader)
         && STGL_READ_FUNC(glDeleteProgram)
         && STGL_READ_FUNC(glDeleteShader)
         && STGL_READ_FUNC(glDetachShader)
         && STGL_READ_FUNC(glDisableVertexAttribArray)
         && STGL_READ_FUNC(glEnableVertexAttribArray)
         && STGL_READ_FUNC(glGetActiveAttrib)
         && STGL_READ_FUNC(glGetActiveUniform)
         && STGL_READ_FUNC(glGetAttachedShaders)
         && STGL_READ_FUNC(glGetAttribLocation)
         && STGL_READ_FUNC(glGetProgramiv)
         && STGL_READ_FUNC(glGetProgramInfoLog)
         && STGL_READ_FUNC(glGetShaderiv)
         && STGL_READ_FUNC(glGetShaderInfoLog)
         && STGL_READ_FUNC(glGetShaderSource)
         && STGL_READ_FUNC(glGetUniformLocation)
         && STGL_READ_FUNC(glGetUniformfv)
         && STGL_READ_FUNC(glGetUniformiv)
         && STGL_READ_FUNC(glGetVertexAttribdv)
         && STGL_READ_FUNC(glGetVertexAttribfv)
         && STGL_READ_FUNC(glGetVertexAttribiv)
         && STGL_READ_FUNC(glGetVertexAttribPointerv)
         && STGL_READ_FUNC(glIsProgram)
         && STGL_READ_FUNC(glIsShader)
         && STGL_READ_FUNC(glLinkProgram)
         && STGL_READ_FUNC(glShaderSource)
         && STGL_READ_FUNC(glUseProgram)
         && STGL_READ_FUNC(glUniform1f)
         && STGL_READ_FUNC(glUniform2f)
         && STGL_READ_FUNC(glUniform3f)
         && STGL_READ_FUNC(glUniform4f)
         && STGL_READ_FUNC(glUniform1i)
         && STGL_READ_FUNC(glUniform2i)
         && STGL_READ_FUNC(glUniform3i)
         && STGL_READ_FUNC(glUniform4i)
         && STGL_READ_FUNC(glUniform1fv)
         && STGL_READ_FUNC(glUniform2fv)
         && STGL_READ_FUNC(glUniform3fv)
         && STGL_READ_FUNC(glUniform4fv)
         && STGL_READ_FUNC(glUniform1iv)
         && STGL_READ_FUNC(glUniform2iv)
         && STGL_READ_FUNC(glUniform3iv)
         && STGL_READ_FUNC(glUniform4iv)
         && STGL_READ_FUNC(glUniformMatrix2fv)
         && STGL_READ_FUNC(glUniformMatrix3fv)
         && STGL_READ_FUNC(glUniformMatrix4fv)
         && STGL_READ_FUNC(glValidateProgram)
         && STGL_READ_FUNC(glVertexAttrib1d)
         && STGL_READ_FUNC(glVertexAttrib1dv)
         && STGL_READ_FUNC(glVertexAttrib1f)
         && STGL_READ_FUNC(glVertexAttrib1fv)
         && STGL_READ_FUNC(glVertexAttrib1s)
         && STGL_READ_FUNC(glVertexAttrib1sv)
         && STGL_READ_FUNC(glVertexAttrib2d)
         && STGL_READ_FUNC(glVertexAttrib2dv)
         && STGL_READ_FUNC(glVertexAttrib2f)
         && STGL_READ_FUNC(glVertexAttrib2fv)
         && STGL_READ_FUNC(glVertexAttrib2s)
         && STGL_READ_FUNC(glVertexAttrib2sv)
         && STGL_READ_FUNC(glVertexAttrib3d)
         && STGL_READ_FUNC(glVertexAttrib3dv)
         && STGL_READ_FUNC(glVertexAttrib3f)
         && STGL_READ_FUNC(glVertexAttrib3fv)
         && STGL_READ_FUNC(glVertexAttrib3s)
         && STGL_READ_FUNC(glVertexAttrib3sv)
         && STGL_READ_FUNC(glVertexAttrib4Nbv)
         && STGL_READ_FUNC(glVertexAttrib4Niv)
         && STGL_READ_FUNC(glVertexAttrib4Nsv)
         && STGL_READ_FUNC(glVertexAttrib4Nub)
         && STGL_READ_FUNC(glVertexAttrib4Nubv)
         && STGL_READ_FUNC(glVertexAttrib4Nuiv)
         && STGL_READ_FUNC(glVertexAttrib4Nusv)
         && STGL_READ_FUNC(glVertexAttrib4bv)
         && STGL_READ_FUNC(glVertexAttrib4d)
         && STGL_READ_FUNC(glVertexAttrib4dv)
         && STGL_READ_FUNC(glVertexAttrib4f)
         && STGL_READ_FUNC(glVertexAttrib4fv)
         && STGL_READ_FUNC(glVertexAttrib4iv)
         && STGL_READ_FUNC(glVertexAttrib4s)
         && STGL_READ_FUNC(glVertexAttrib4sv)
         && STGL_READ_FUNC(glVertexAttrib4ubv)
         && STGL_READ_FUNC(glVertexAttrib4uiv)
         && STGL_READ_FUNC(glVertexAttrib4usv)
         && STGL_READ_FUNC(glVertexAttribPointer);

    // load OpenGL 2.1 new functions
    has21 = isGlGreaterEqual(2, 1)
         && STGL_READ_FUNC(glUniformMatrix2x3fv)
         && STGL_READ_FUNC(glUniformMatrix3x2fv)
         && STGL_READ_FUNC(glUniformMatrix2x4fv)
         && STGL_READ_FUNC(glUniformMatrix4x2fv)
         && STGL_READ_FUNC(glUniformMatrix3x4fv)
         && STGL_READ_FUNC(glUniformMatrix4x3fv);

    // load GL_ARB_framebuffer_object (added to OpenGL 3.0 core)
    const bool hasFBO = (isGlGreaterEqual(3, 0) || stglCheckExtension("GL_ARB_framebuffer_object"))
         && STGL_READ_FUNC(glIsRenderbuffer)
         && STGL_READ_FUNC(glBindRenderbuffer)
         && STGL_READ_FUNC(glDeleteRenderbuffers)
         && STGL_READ_FUNC(glGenRenderbuffers)
         && STGL_READ_FUNC(glRenderbufferStorage)
         && STGL_READ_FUNC(glGetRenderbufferParameteriv)
         && STGL_READ_FUNC(glIsFramebuffer)
         && STGL_READ_FUNC(glBindFramebuffer)
         && STGL_READ_FUNC(glDeleteFramebuffers)
         && STGL_READ_FUNC(glGenFramebuffers)
         && STGL_READ_FUNC(glCheckFramebufferStatus)
         && STGL_READ_FUNC(glFramebufferTexture1D)
         && STGL_READ_FUNC(glFramebufferTexture2D)
         && STGL_READ_FUNC(glFramebufferTexture3D)
         && STGL_READ_FUNC(glFramebufferRenderbuffer)
         && STGL_READ_FUNC(glGetFramebufferAttachmentParameteriv)
         && STGL_READ_FUNC(glGenerateMipmap)
         && STGL_READ_FUNC(glBlitFramebuffer)
         && STGL_READ_FUNC(glRenderbufferStorageMultisample)
         && STGL_READ_FUNC(glFramebufferTextureLayer);

    // load GL_ARB_vertex_array_object (added to OpenGL 3.0 core)
    const bool hasVAO = (isGlGreaterEqual(3, 0) || stglCheckExtension("GL_ARB_vertex_array_object"))
         && STGL_READ_FUNC(glBindVertexArray)
         && STGL_READ_FUNC(glDeleteVertexArrays)
         && STGL_READ_FUNC(glGenVertexArrays)
         && STGL_READ_FUNC(glIsVertexArray);

    // load GL_ARB_map_buffer_range (added to OpenGL 3.0 core)
    const bool hasMapBufferRange = (isGlGreaterEqual(3, 0) || stglCheckExtension("GL_ARB_map_buffer_range"))
         && STGL_READ_FUNC(glMapBufferRange)
         && STGL_READ_FUNC(glFlushMappedBufferRange);

    // load OpenGL 3.0 new functions
    has30 = isGlGreaterEqual(3, 0)
         && hasFBO
         && hasVAO
         && hasMapBufferRange
         && STGL_READ_FUNC(glColorMaski)
         && STGL_READ_FUNC(glGetBooleani_v)
         && STGL_READ_FUNC(glGetIntegeri_v)
         && STGL_READ_FUNC(glEnablei)
         && STGL_READ_FUNC(glDisablei)
         && STGL_READ_FUNC(glIsEnabledi)
         && STGL_READ_FUNC(glBeginTransformFeedback)
         && STGL_READ_FUNC(glEndTransformFeedback)
         && STGL_READ_FUNC(glBindBufferRange)
         && STGL_READ_FUNC(glBindBufferBase)
         && STGL_READ_FUNC(glTransformFeedbackVaryings)
         && STGL_READ_FUNC(glGetTransformFeedbackVarying)
         && STGL_READ_FUNC(glClampColor)
         && STGL_READ_FUNC(glBeginConditionalRender)
         && STGL_READ_FUNC(glEndConditionalRender)
         && STGL_READ_FUNC(glVertexAttribIPointer)
         && STGL_READ_FUNC(glGetVertexAttribIiv)
         && STGL_READ_FUNC(glGetVertexAttribIuiv)
         && STGL_READ_FUNC(glVertexAttribI1i)
         && STGL_READ_FUNC(glVertexAttribI2i)
         && STGL_READ_FUNC(glVertexAttribI3i)
         && STGL_READ_FUNC(glVertexAttribI4i)
         && STGL_READ_FUNC(glVertexAttribI1ui)
         && STGL_READ_FUNC(glVertexAttribI2ui)
         && STGL_READ_FUNC(glVertexAttribI3ui)
         && STGL_READ_FUNC(glVertexAttribI4ui)
         && STGL_READ_FUNC(glVertexAttribI1iv)
         && STGL_READ_FUNC(glVertexAttribI2iv)
         && STGL_READ_FUNC(glVertexAttribI3iv)
         && STGL_READ_FUNC(glVertexAttribI4iv)
         && STGL_READ_FUNC(glVertexAttribI1uiv)
         && STGL_READ_FUNC(glVertexAttribI2uiv)
         && STGL_READ_FUNC(glVertexAttribI3uiv)
         && STGL_READ_FUNC(glVertexAttribI4uiv)
         && STGL_READ_FUNC(glVertexAttribI4bv)
         && STGL_READ_FUNC(glVertexAttribI4sv)
         && STGL_READ_FUNC(glVertexAttribI4ubv)
         && STGL_READ_FUNC(glVertexAttribI4usv)
         && STGL_READ_FUNC(glGetUniformuiv)
         && STGL_READ_FUNC(glBindFragDataLocation)
         && STGL_READ_FUNC(glGetFragDataLocation)
         && STGL_READ_FUNC(glUniform1ui)
         && STGL_READ_FUNC(glUniform2ui)
         && STGL_READ_FUNC(glUniform3ui)
         && STGL_READ_FUNC(glUniform4ui)
         && STGL_READ_FUNC(glUniform1uiv)
         && STGL_READ_FUNC(glUniform2uiv)
         && STGL_READ_FUNC(glUniform3uiv)
         && STGL_READ_FUNC(glUniform4uiv)
         && STGL_READ_FUNC(glTexParameterIiv)
         && STGL_READ_FUNC(glTexParameterIuiv)
         && STGL_READ_FUNC(glGetTexParameterIiv)
         && STGL_READ_FUNC(glGetTexParameterIuiv)
         && STGL_READ_FUNC(glClearBufferiv)
         && STGL_READ_FUNC(glClearBufferuiv)
         && STGL_READ_FUNC(glClearBufferfv)
         && STGL_READ_FUNC(glClearBufferfi)
         && STGL_READ_FUNC(glGetStringi);

    // load GL_ARB_uniform_buffer_object (added to OpenGL 3.1 core)
    const bool hasUBO = (isGlGreaterEqual(3, 1) || stglCheckExtension("GL_ARB_uniform_buffer_object"))
         && STGL_READ_FUNC(glGetUniformIndices)
         && STGL_READ_FUNC(glGetActiveUniformsiv)
         && STGL_READ_FUNC(glGetActiveUniformName)
         && STGL_READ_FUNC(glGetUniformBlockIndex)
         && STGL_READ_FUNC(glGetActiveUniformBlockiv)
         && STGL_READ_FUNC(glGetActiveUniformBlockName)
         && STGL_READ_FUNC(glUniformBlockBinding);

    // load GL_ARB_copy_buffer (added to OpenGL 3.1 core)
    const bool hasCopyBufSubData = (isGlGreaterEqual(3, 1) || stglCheckExtension("GL_ARB_copy_buffer"))
         && STGL_READ_FUNC(glCopyBufferSubData);

    // load OpenGL 3.1 new functions
    has31 = isGlGreaterEqual(3, 1)
         && hasUBO
         && hasCopyBufSubData
         && STGL_READ_FUNC(glDrawArraysInstanced)
         && STGL_READ_FUNC(glDrawElementsInstanced)
         && STGL_READ_FUNC(glTexBuffer)
         && STGL_READ_FUNC(glPrimitiveRestartIndex);

    // load GL_ARB_draw_elements_base_vertex (added to OpenGL 3.2 core)
    const bool hasDrawElemsBaseVert = (isGlGreaterEqual(3, 2) || stglCheckExtension("GL_ARB_draw_elements_base_vertex"))
         && STGL_READ_FUNC(glDrawElementsBaseVertex)
         && STGL_READ_FUNC(glDrawRangeElementsBaseVertex)
         && STGL_READ_FUNC(glDrawElementsInstancedBaseVertex)
         && STGL_READ_FUNC(glMultiDrawElementsBaseVertex);

    // load GL_ARB_provoking_vertex (added to OpenGL 3.2 core)
    const bool hasProvokingVert = (isGlGreaterEqual(3, 2) || stglCheckExtension("GL_ARB_provoking_vertex"))
         && STGL_READ_FUNC(glProvokingVertex);

    // load GL_ARB_sync (added to OpenGL 3.2 core)
    const bool hasSync = (isGlGreaterEqual(3, 2) || stglCheckExtension("GL_ARB_sync"))
         && STGL_READ_FUNC(glFenceSync)
         && STGL_READ_FUNC(glIsSync)
         && STGL_READ_FUNC(glDeleteSync)
         && STGL_READ_FUNC(glClientWaitSync)
         && STGL_READ_FUNC(glWaitSync)
         && STGL_READ_FUNC(glGetInteger64v)
         && STGL_READ_FUNC(glGetSynciv);

    // load GL_ARB_texture_multisample (added to OpenGL 3.2 core)
    const bool hasTextureMultisample = (isGlGreaterEqual(3, 2) || stglCheckExtension("GL_ARB_texture_multisample"))
         && STGL_READ_FUNC(glTexImage2DMultisample)
         && STGL_READ_FUNC(glTexImage3DMultisample)
         && STGL_READ_FUNC(glGetMultisamplefv)
         && STGL_READ_FUNC(glSampleMaski);

    // load OpenGL 3.2 new functions
    has32 = isGlGreaterEqual(3, 2)
         && hasDrawElemsBaseVert
         && hasProvokingVert
         && hasSync
         && hasTextureMultisample
         && STGL_READ_FUNC(glGetInteger64i_v)
         && STGL_READ_FUNC(glGetBufferParameteri64v)
         && STGL_READ_FUNC(glFramebufferTexture);

    // load GL_ARB_blend_func_extended (added to OpenGL 3.3 core)
    const bool hasBlendFuncExtended = (isGlGreaterEqual(3, 3) || stglCheckExtension("GL_ARB_blend_func_extended"))
         && STGL_READ_FUNC(glBindFragDataLocationIndexed)
         && STGL_READ_FUNC(glGetFragDataIndex);

    // load GL_ARB_sampler_objects (added to OpenGL 3.3 core)
    const bool hasSamplerObjects = (isGlGreaterEqual(3, 3) || stglCheckExtension("GL_ARB_sampler_objects"))
         && STGL_READ_FUNC(glGenSamplers)
         && STGL_READ_FUNC(glDeleteSamplers)
         && STGL_READ_FUNC(glIsSampler)
         && STGL_READ_FUNC(glBindSampler)
         && STGL_READ_FUNC(glSamplerParameteri)
         && STGL_READ_FUNC(glSamplerParameteriv)
         && STGL_READ_FUNC(glSamplerParameterf)
         && STGL_READ_FUNC(glSamplerParameterfv)
         && STGL_READ_FUNC(glSamplerParameterIiv)
         && STGL_READ_FUNC(glSamplerParameterIuiv)
         && STGL_READ_FUNC(glGetSamplerParameteriv)
         && STGL_READ_FUNC(glGetSamplerParameterIiv)
         && STGL_READ_FUNC(glGetSamplerParameterfv)
         && STGL_READ_FUNC(glGetSamplerParameterIuiv);

    // load GL_ARB_timer_query (added to OpenGL 3.3 core)
    const bool hasTimerQuery = (isGlGreaterEqual(3, 3) || stglCheckExtension("GL_ARB_timer_query"))
         && STGL_READ_FUNC(glQueryCounter)
         && STGL_READ_FUNC(glGetQueryObjecti64v)
         && STGL_READ_FUNC(glGetQueryObjectui64v);

    // load GL_ARB_vertex_type_2_10_10_10_rev (added to OpenGL 3.3 core)
    const bool hasVertType21010101rev = (isGlGreaterEqual(3, 3) || stglCheckExtension("GL_ARB_vertex_type_2_10_10_10_rev"))
         && STGL_READ_FUNC(glVertexP2ui)
         && STGL_READ_FUNC(glVertexP2uiv)
         && STGL_READ_FUNC(glVertexP3ui)
         && STGL_READ_FUNC(glVertexP3uiv)
         && STGL_READ_FUNC(glVertexP4ui)
         && STGL_READ_FUNC(glVertexP4uiv)
         && STGL_READ_FUNC(glTexCoordP1ui)
         && STGL_READ_FUNC(glTexCoordP1uiv)
         && STGL_READ_FUNC(glTexCoordP2ui)
         && STGL_READ_FUNC(glTexCoordP2uiv)
         && STGL_READ_FUNC(glTexCoordP3ui)
         && STGL_READ_FUNC(glTexCoordP3uiv)
         && STGL_READ_FUNC(glTexCoordP4ui)
         && STGL_READ_FUNC(glTexCoordP4uiv)
         && STGL_READ_FUNC(glMultiTexCoordP1ui)
         && STGL_READ_FUNC(glMultiTexCoordP1uiv)
         && STGL_READ_FUNC(glMultiTexCoordP2ui)
         && STGL_READ_FUNC(glMultiTexCoordP2uiv)
         && STGL_READ_FUNC(glMultiTexCoordP3ui)
         && STGL_READ_FUNC(glMultiTexCoordP3uiv)
         && STGL_READ_FUNC(glMultiTexCoordP4ui)
         && STGL_READ_FUNC(glMultiTexCoordP4uiv)
         && STGL_READ_FUNC(glNormalP3ui)
         && STGL_READ_FUNC(glNormalP3uiv)
         && STGL_READ_FUNC(glColorP3ui)
         && STGL_READ_FUNC(glColorP3uiv)
         && STGL_READ_FUNC(glColorP4ui)
         && STGL_READ_FUNC(glColorP4uiv)
         && STGL_READ_FUNC(glSecondaryColorP3ui)
         && STGL_READ_FUNC(glSecondaryColorP3uiv)
         && STGL_READ_FUNC(glVertexAttribP1ui)
         && STGL_READ_FUNC(glVertexAttribP1uiv)
         && STGL_READ_FUNC(glVertexAttribP2ui)
         && STGL_READ_FUNC(glVertexAttribP2uiv)
         && STGL_READ_FUNC(glVertexAttribP3ui)
         && STGL_READ_FUNC(glVertexAttribP3uiv)
         && STGL_READ_FUNC(glVertexAttribP4ui)
         && STGL_READ_FUNC(glVertexAttribP4uiv);

    // load OpenGL 3.3 extra functions
    has33 = isGlGreaterEqual(3, 3)
         && hasBlendFuncExtended
         && hasSamplerObjects
         && hasTimerQuery
         && hasVertType21010101rev
         && STGL_READ_FUNC(glVertexAttribDivisor);

    // load GL_ARB_draw_indirect (added to OpenGL 4.0 core)
    const bool hasDrawIndirect = (isGlGreaterEqual(4, 0) || stglCheckExtension("GL_ARB_draw_indirect"))
         && STGL_READ_FUNC(glDrawArraysIndirect)
         && STGL_READ_FUNC(glDrawElementsIndirect);

    // load GL_ARB_gpu_shader_fp64 (added to OpenGL 4.0 core)
    const bool hasShaderFP64 = (isGlGreaterEqual(4, 0) || stglCheckExtension("GL_ARB_gpu_shader_fp64"))
         && STGL_READ_FUNC(glUniform1d)
         && STGL_READ_FUNC(glUniform2d)
         && STGL_READ_FUNC(glUniform3d)
         && STGL_READ_FUNC(glUniform4d)
         && STGL_READ_FUNC(glUniform1dv)
         && STGL_READ_FUNC(glUniform2dv)
         && STGL_READ_FUNC(glUniform3dv)
         && STGL_READ_FUNC(glUniform4dv)
         && STGL_READ_FUNC(glUniformMatrix2dv)
         && STGL_READ_FUNC(glUniformMatrix3dv)
         && STGL_READ_FUNC(glUniformMatrix4dv)
         && STGL_READ_FUNC(glUniformMatrix2x3dv)
         && STGL_READ_FUNC(glUniformMatrix2x4dv)
         && STGL_READ_FUNC(glUniformMatrix3x2dv)
         && STGL_READ_FUNC(glUniformMatrix3x4dv)
         && STGL_READ_FUNC(glUniformMatrix4x2dv)
         && STGL_READ_FUNC(glUniformMatrix4x3dv)
         && STGL_READ_FUNC(glGetUniformdv);

    // load GL_ARB_shader_subroutine (added to OpenGL 4.0 core)
    const bool hasShaderSubroutine = (isGlGreaterEqual(4, 0) || stglCheckExtension("GL_ARB_shader_subroutine"))
         && STGL_READ_FUNC(glGetSubroutineUniformLocation)
         && STGL_READ_FUNC(glGetSubroutineIndex)
         && STGL_READ_FUNC(glGetActiveSubroutineUniformiv)
         && STGL_READ_FUNC(glGetActiveSubroutineUniformName)
         && STGL_READ_FUNC(glGetActiveSubroutineName)
         && STGL_READ_FUNC(glUniformSubroutinesuiv)
         && STGL_READ_FUNC(glGetUniformSubroutineuiv)
         && STGL_READ_FUNC(glGetProgramStageiv);

    // load GL_ARB_tessellation_shader (added to OpenGL 4.0 core)
    const bool hasTessellationShader = (isGlGreaterEqual(4, 0) || stglCheckExtension("GL_ARB_tessellation_shader"))
         && STGL_READ_FUNC(glPatchParameteri)
         && STGL_READ_FUNC(glPatchParameterfv);

    // load GL_ARB_transform_feedback2 (added to OpenGL 4.0 core)
    const bool hasTrsfFeedback2 = (isGlGreaterEqual(4, 0) || stglCheckExtension("GL_ARB_transform_feedback2"))
         && STGL_READ_FUNC(glBindTransformFeedback)
         && STGL_READ_FUNC(glDeleteTransformFeedbacks)
         && STGL_READ_FUNC(glGenTransformFeedbacks)
         && STGL_READ_FUNC(glIsTransformFeedback)
         && STGL_READ_FUNC(glPauseTransformFeedback)
         && STGL_READ_FUNC(glResumeTransformFeedback)
         && STGL_READ_FUNC(glDrawTransformFeedback);

    // load GL_ARB_transform_feedback3 (added to OpenGL 4.0 core)
    const bool hasTrsfFeedback3 = (isGlGreaterEqual(4, 0) || stglCheckExtension("GL_ARB_transform_feedback3"))
         && STGL_READ_FUNC(glDrawTransformFeedbackStream)
         && STGL_READ_FUNC(glBeginQueryIndexed)
         && STGL_READ_FUNC(glEndQueryIndexed)
         && STGL_READ_FUNC(glGetQueryIndexediv);

    // load OpenGL 4.0 new functions
    has40 = isGlGreaterEqual(4, 0)
        && hasDrawIndirect
        && hasShaderFP64
        && hasShaderSubroutine
        && hasTessellationShader
        && hasTrsfFeedback2
        && hasTrsfFeedback3
        && STGL_READ_FUNC(glMinSampleShading)
        && STGL_READ_FUNC(glBlendEquationi)
        && STGL_READ_FUNC(glBlendEquationSeparatei)
        && STGL_READ_FUNC(glBlendFunci)
        && STGL_READ_FUNC(glBlendFuncSeparatei);

    // load GL_ARB_ES2_compatibility (added to OpenGL 4.1 core)
    const bool hasES2Compatibility = (isGlGreaterEqual(4, 1) || stglCheckExtension("GL_ARB_ES2_compatibility"))
         && STGL_READ_FUNC(glReleaseShaderCompiler)
         && STGL_READ_FUNC(glShaderBinary)
         && STGL_READ_FUNC(glGetShaderPrecisionFormat)
         && STGL_READ_FUNC(glDepthRangef)
         && STGL_READ_FUNC(glClearDepthf);

    // load GL_ARB_get_program_binary (added to OpenGL 4.1 core)
    const bool hasGetProgramBinary = (isGlGreaterEqual(4, 1) || stglCheckExtension("GL_ARB_get_program_binary"))
         && STGL_READ_FUNC(glGetProgramBinary)
         && STGL_READ_FUNC(glProgramBinary)
         && STGL_READ_FUNC(glProgramParameteri);


    // load GL_ARB_separate_shader_objects (added to OpenGL 4.1 core)
    const bool hasSeparateShaderObjects = (isGlGreaterEqual(4, 1) || stglCheckExtension("GL_ARB_separate_shader_objects"))
         && STGL_READ_FUNC(glUseProgramStages)
         && STGL_READ_FUNC(glActiveShaderProgram)
         && STGL_READ_FUNC(glCreateShaderProgramv)
         && STGL_READ_FUNC(glBindProgramPipeline)
         && STGL_READ_FUNC(glDeleteProgramPipelines)
         && STGL_READ_FUNC(glGenProgramPipelines)
         && STGL_READ_FUNC(glIsProgramPipeline)
         && STGL_READ_FUNC(glGetProgramPipelineiv)
         && STGL_READ_FUNC(glProgramUniform1i)
         && STGL_READ_FUNC(glProgramUniform1iv)
         && STGL_READ_FUNC(glProgramUniform1f)
         && STGL_READ_FUNC(glProgramUniform1fv)
         && STGL_READ_FUNC(glProgramUniform1d)
         && STGL_READ_FUNC(glProgramUniform1dv)
         && STGL_READ_FUNC(glProgramUniform1ui)
         && STGL_READ_FUNC(glProgramUniform1uiv)
         && STGL_READ_FUNC(glProgramUniform2i)
         && STGL_READ_FUNC(glProgramUniform2iv)
         && STGL_READ_FUNC(glProgramUniform2f)
         && STGL_READ_FUNC(glProgramUniform2fv)
         && STGL_READ_FUNC(glProgramUniform2d)
         && STGL_READ_FUNC(glProgramUniform2dv)
         && STGL_READ_FUNC(glProgramUniform2ui)
         && STGL_READ_FUNC(glProgramUniform2uiv)
         && STGL_READ_FUNC(glProgramUniform3i)
         && STGL_READ_FUNC(glProgramUniform3iv)
         && STGL_READ_FUNC(glProgramUniform3f)
         && STGL_READ_FUNC(glProgramUniform3fv)
         && STGL_READ_FUNC(glProgramUniform3d)
         && STGL_READ_FUNC(glProgramUniform3dv)
         && STGL_READ_FUNC(glProgramUniform3ui)
         && STGL_READ_FUNC(glProgramUniform3uiv)
         && STGL_READ_FUNC(glProgramUniform4i)
         && STGL_READ_FUNC(glProgramUniform4iv)
         && STGL_READ_FUNC(glProgramUniform4f)
         && STGL_READ_FUNC(glProgramUniform4fv)
         && STGL_READ_FUNC(glProgramUniform4d)
         && STGL_READ_FUNC(glProgramUniform4dv)
         && STGL_READ_FUNC(glProgramUniform4ui)
         && STGL_READ_FUNC(glProgramUniform4uiv)
         && STGL_READ_FUNC(glProgramUniformMatrix2fv)
         && STGL_READ_FUNC(glProgramUniformMatrix3fv)
         && STGL_READ_FUNC(glProgramUniformMatrix4fv)
         && STGL_READ_FUNC(glProgramUniformMatrix2dv)
         && STGL_READ_FUNC(glProgramUniformMatrix3dv)
         && STGL_READ_FUNC(glProgramUniformMatrix4dv)
         && STGL_READ_FUNC(glProgramUniformMatrix2x3fv)
         && STGL_READ_FUNC(glProgramUniformMatrix3x2fv)
         && STGL_READ_FUNC(glProgramUniformMatrix2x4fv)
         && STGL_READ_FUNC(glProgramUniformMatrix4x2fv)
         && STGL_READ_FUNC(glProgramUniformMatrix3x4fv)
         && STGL_READ_FUNC(glProgramUniformMatrix4x3fv)
         && STGL_READ_FUNC(glProgramUniformMatrix2x3dv)
         && STGL_READ_FUNC(glProgramUniformMatrix3x2dv)
         && STGL_READ_FUNC(glProgramUniformMatrix2x4dv)
         && STGL_READ_FUNC(glProgramUniformMatrix4x2dv)
         && STGL_READ_FUNC(glProgramUniformMatrix3x4dv)
         && STGL_READ_FUNC(glProgramUniformMatrix4x3dv)
         && STGL_READ_FUNC(glValidateProgramPipeline)
         && STGL_READ_FUNC(glGetProgramPipelineInfoLog);

    // load GL_ARB_vertex_attrib_64bit (added to OpenGL 4.1 core)
    const bool hasVertAttrib64bit = (isGlGreaterEqual(4, 1) || stglCheckExtension("GL_ARB_vertex_attrib_64bit"))
         && STGL_READ_FUNC(glVertexAttribL1d)
         && STGL_READ_FUNC(glVertexAttribL2d)
         && STGL_READ_FUNC(glVertexAttribL3d)
         && STGL_READ_FUNC(glVertexAttribL4d)
         && STGL_READ_FUNC(glVertexAttribL1dv)
         && STGL_READ_FUNC(glVertexAttribL2dv)
         && STGL_READ_FUNC(glVertexAttribL3dv)
         && STGL_READ_FUNC(glVertexAttribL4dv)
         && STGL_READ_FUNC(glVertexAttribLPointer)
         && STGL_READ_FUNC(glGetVertexAttribLdv);

    // load GL_ARB_viewport_array (added to OpenGL 4.1 core)
    const bool hasViewportArray = (isGlGreaterEqual(4, 1) || stglCheckExtension("GL_ARB_viewport_array"))
         && STGL_READ_FUNC(glViewportArrayv)
         && STGL_READ_FUNC(glViewportIndexedf)
         && STGL_READ_FUNC(glViewportIndexedfv)
         && STGL_READ_FUNC(glScissorArrayv)
         && STGL_READ_FUNC(glScissorIndexed)
         && STGL_READ_FUNC(glScissorIndexedv)
         && STGL_READ_FUNC(glDepthRangeArrayv)
         && STGL_READ_FUNC(glDepthRangeIndexed)
         && STGL_READ_FUNC(glGetFloati_v)
         && STGL_READ_FUNC(glGetDoublei_v);

    has41 = isGlGreaterEqual(4, 1)
         && hasES2Compatibility
         && hasGetProgramBinary
         && hasSeparateShaderObjects
         && hasVertAttrib64bit
         && hasViewportArray;

    // load GL_ARB_base_instance (added to OpenGL 4.2 core)
    const bool hasBaseInstance = (isGlGreaterEqual(4, 2) || stglCheckExtension("GL_ARB_base_instance"))
         && STGL_READ_FUNC(glDrawArraysInstancedBaseInstance)
         && STGL_READ_FUNC(glDrawElementsInstancedBaseInstance)
         && STGL_READ_FUNC(glDrawElementsInstancedBaseVertexBaseInstance);

    // load GL_ARB_transform_feedback_instanced (added to OpenGL 4.2 core)
    const bool hasTrsfFeedbackInstanced = (isGlGreaterEqual(4, 2) || stglCheckExtension("GL_ARB_transform_feedback_instanced"))
         && STGL_READ_FUNC(glDrawTransformFeedbackInstanced)
         && STGL_READ_FUNC(glDrawTransformFeedbackStreamInstanced);

    // load GL_ARB_internalformat_query (added to OpenGL 4.2 core)
    const bool hasInternalFormatQuery = (isGlGreaterEqual(4, 2) || stglCheckExtension("GL_ARB_internalformat_query"))
         && STGL_READ_FUNC(glGetInternalformativ);

    // load GL_ARB_shader_atomic_counters (added to OpenGL 4.2 core)
    const bool hasShaderAtomicCounters = (isGlGreaterEqual(4, 2) || stglCheckExtension("GL_ARB_shader_atomic_counters"))
         && STGL_READ_FUNC(glGetActiveAtomicCounterBufferiv);

    // load GL_ARB_shader_image_load_store (added to OpenGL 4.2 core)
    const bool hasShaderImgLoadStore = (isGlGreaterEqual(4, 2) || stglCheckExtension("GL_ARB_shader_image_load_store"))
         && STGL_READ_FUNC(glBindImageTexture)
         && STGL_READ_FUNC(glMemoryBarrier);

    // load GL_ARB_texture_storage (added to OpenGL 4.2 core)
    const bool hasTextureStorage = (isGlGreaterEqual(4, 2) || stglCheckExtension("GL_ARB_texture_storage"))
         && STGL_READ_FUNC(glTexStorage1D)
         && STGL_READ_FUNC(glTexStorage2D)
         && STGL_READ_FUNC(glTexStorage3D)
         && STGL_READ_FUNC(glTextureStorage1DEXT)
         && STGL_READ_FUNC(glTextureStorage2DEXT)
         && STGL_READ_FUNC(glTextureStorage3DEXT);

    has42 = isGlGreaterEqual(4, 2)
         && hasBaseInstance
         && hasTrsfFeedbackInstanced
         && hasInternalFormatQuery
         && hasShaderAtomicCounters
         && hasShaderImgLoadStore
         && hasTextureStorage;

    // It seems that all many problematic cards / drivers
    // (FrameBuffer with non-power-of-two size corrupted / slowdown)
    // are GLSL 1.1 limited (and OpenGL2.0).
    // SURE - this is NOT rootcouse of the problem, but just some detection mechanism
    const StCString aGLSL_old  = stCString("1.10");
    // latest drivers for GeForce FX return GLSL1.2 vc OpenGL2.1 support level
    // but slowdown rendering - so we detect them here.
    const StCString aGeForceFX = stCString("GeForce FX");
    const StString  aGLSLVersion((const char* )core11fwd->glGetString(GL_SHADING_LANGUAGE_VERSION));
    const StString  aGlRenderer ((const char* )core11fwd->glGetString(GL_RENDERER));
    myIsRectFboSupported = !aGLSLVersion.isContains(aGLSL_old) && !aGlRenderer.isContains(aGeForceFX);

    if(aGlRenderer.isContains(stCString("GeForce"))) {
        myGpuName = GPU_GEFORCE;
    } else if(aGlRenderer.isContains(stCString("Quadro"))) {
        myGpuName = GPU_QUADRO;
    } else if(aGlRenderer.isContains(stCString("Radeon"))
           || aGlRenderer.isContains(stCString("RADEON"))) {
        myGpuName = GPU_RADEON;
    } else if(aGlRenderer.isContains(stCString("FireGL"))) {
        myGpuName = GPU_FIREGL;
    } else {
        myGpuName = GPU_UNKNOWN;
    }

    myWasInit = true;

    if(hasFBO) {
        arbFbo = (StGLArbFbo* )(&(*myFuncs));
    }

    if(!has12) {
        myVerMajor = 1;
        myVerMinor = 1;
        return true;
    }

    if(!has13) {
        myVerMajor = 1;
        myVerMinor = 2;
        return true;
    }

    if(!has14) {
        myVerMajor = 1;
        myVerMinor = 3;
        return true;
    }

    if(!has15) {
        myVerMajor = 1;
        myVerMinor = 4;
        return true;
    }

    if(has20) {
        core20    = (StGLCore20*    )(&(*myFuncs));
        core20fwd = (StGLCore20Fwd* )(&(*myFuncs));
    } else {
        myVerMajor = 1;
        myVerMinor = 5;
        return true;
    }

    if(!has21) {
        myVerMajor = 2;
        myVerMinor = 0;
        return true;
    }

    if(!has30) {
        myVerMajor = 2;
        myVerMinor = 1;
        return true;
    }

    if(!has31) {
        myVerMajor = 3;
        myVerMinor = 0;
        return true;
    }

    if(has32) {
        core32     = (StGLCore32*     )(&(*myFuncs));
        core32back = (StGLCore32Back* )(&(*myFuncs));
    } else {
        myVerMajor = 3;
        myVerMinor = 1;
        return true;
    }

    if(!has33) {
        myVerMajor = 3;
        myVerMinor = 2;
        return true;
    }

    if(!has40) {
        myVerMajor = 3;
        myVerMinor = 3;
        return true;
    }

    if(has41) {
        core41     = (StGLCore41*     )(&(*myFuncs));
        core41back = (StGLCore41Back* )(&(*myFuncs));
    } else {
        myVerMajor = 4;
        myVerMinor = 0;
        return true;
    }

    if(has42) {
        core42     = (StGLCore42*     )(&(*myFuncs));
        core42back = (StGLCore42Back* )(&(*myFuncs));
    } else {
        myVerMajor = 4;
        myVerMinor = 1;
        return true;
    }

    return true;
}
