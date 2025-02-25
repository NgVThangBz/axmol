/****************************************************************************
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
Copyright (c) 2021 Bytedance Inc.

 https://axmolengine.github.io/

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/
#include "platform/Application.h"
#include "base/Director.h"
#include <algorithm>
#include "platform/FileUtils.h"
#include <shellapi.h>
#include <WinVer.h>
#include <timeapi.h>

#include "yasio/string_view.hpp"
#include "ntcvt/ntcvt.hpp"
/**
@brief    This function change the PVRFrame show/hide setting in register.
@param  bEnable If true show the PVRFrame window, otherwise hide.
*/
static void PVRFrameEnableControlWindow(bool bEnable);

NS_AX_BEGIN

// sharedApplication pointer
Application* Application::sm_pSharedApplication = nullptr;

Application::Application() : _instance(nullptr), _accelTable(nullptr)
{
    _instance                   = GetModuleHandle(nullptr);
    _animationInterval.QuadPart = 0;
    AX_ASSERT(!sm_pSharedApplication);
    sm_pSharedApplication = this;
}

Application::~Application()
{
    AX_ASSERT(this == sm_pSharedApplication);
    sm_pSharedApplication = nullptr;
}

int Application::run()
{
    PVRFrameEnableControlWindow(false);

    ///////////////////////////////////////////////////////////////////////////
    /////////////// changing timer resolution
    ///////////////////////////////////////////////////////////////////////////
    UINT TARGET_RESOLUTION = 1;  // 1 millisecond target resolution
    TIMECAPS tc;
    UINT wTimerRes = 0;
    if (TIMERR_NOERROR == timeGetDevCaps(&tc, sizeof(TIMECAPS)))
    {
        wTimerRes = std::min(std::max(tc.wPeriodMin, TARGET_RESOLUTION), tc.wPeriodMax);
        timeBeginPeriod(wTimerRes);
    }

    // Main message loop:
    LARGE_INTEGER nLast;
    LARGE_INTEGER nNow;

    QueryPerformanceCounter(&nLast);

    initGLContextAttrs();

    // Initialize instance and cocos2d.
    if (!applicationDidFinishLaunching())
    {
        return 1;
    }

    auto director = Director::getInstance();
    auto glView   = director->getGLView();

    // Retain glView to avoid glView being released in the while loop
    glView->retain();

    LONGLONG interval = 0LL;
    LONG waitMS       = 0L;

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    while (!glView->windowShouldClose())
    {
        QueryPerformanceCounter(&nNow);
        interval = nNow.QuadPart - nLast.QuadPart;
        if (interval >= _animationInterval.QuadPart)
        {
            nLast.QuadPart = nNow.QuadPart;
            director->mainLoop();
            glView->pollEvents();
        }
        else
        {
            // The precision of timer on Windows is set to highest (1ms) by 'timeBeginPeriod' from above code,
            // but it's still not precise enough. For example, if the precision of timer is 1ms,
            // Sleep(3) may make a sleep of 2ms or 4ms. Therefore, we subtract 1ms here to make Sleep time shorter.
            // If 'waitMS' is equal or less than 1ms, don't sleep and run into next loop to
            // boost CPU to next frame accurately.
            waitMS = static_cast<LONG>((_animationInterval.QuadPart - interval) * 1000LL / freq.QuadPart - 1L);
            if (waitMS > 1L)
                Sleep(waitMS);
        }
    }

    // Director should still do a cleanup if the window was closed manually.
    if (glView->isOpenGLReady())
    {
        director->end();
        director = nullptr;
    }
    glView->release();

    ///////////////////////////////////////////////////////////////////////////
    /////////////// restoring timer resolution
    ///////////////////////////////////////////////////////////////////////////
    if (wTimerRes != 0)
    {
        timeEndPeriod(wTimerRes);
    }
    return 0;
}

void Application::setAnimationInterval(float interval)
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    _animationInterval.QuadPart = (LONGLONG)(interval * freq.QuadPart);
}

//////////////////////////////////////////////////////////////////////////
// static member function
//////////////////////////////////////////////////////////////////////////
Application* Application::getInstance()
{
    AX_ASSERT(sm_pSharedApplication);
    return sm_pSharedApplication;
}

LanguageType Application::getCurrentLanguage()
{
    LanguageType ret = LanguageType::ENGLISH;

    LCID localeID                    = GetUserDefaultLCID();
    unsigned short primaryLanguageID = localeID & 0xFF;

    switch (primaryLanguageID)
    {
    case LANG_CHINESE:
        ret = LanguageType::CHINESE;
        break;
    case LANG_ENGLISH:
        ret = LanguageType::ENGLISH;
        break;
    case LANG_FRENCH:
        ret = LanguageType::FRENCH;
        break;
    case LANG_ITALIAN:
        ret = LanguageType::ITALIAN;
        break;
    case LANG_GERMAN:
        ret = LanguageType::GERMAN;
        break;
    case LANG_SPANISH:
        ret = LanguageType::SPANISH;
        break;
    case LANG_DUTCH:
        ret = LanguageType::DUTCH;
        break;
    case LANG_RUSSIAN:
        ret = LanguageType::RUSSIAN;
        break;
    case LANG_KOREAN:
        ret = LanguageType::KOREAN;
        break;
    case LANG_JAPANESE:
        ret = LanguageType::JAPANESE;
        break;
    case LANG_HUNGARIAN:
        ret = LanguageType::HUNGARIAN;
        break;
    case LANG_PORTUGUESE:
        ret = LanguageType::PORTUGUESE;
        break;
    case LANG_ARABIC:
        ret = LanguageType::ARABIC;
        break;
    case LANG_NORWEGIAN:
        ret = LanguageType::NORWEGIAN;
        break;
    case LANG_POLISH:
        ret = LanguageType::POLISH;
        break;
    case LANG_TURKISH:
        ret = LanguageType::TURKISH;
        break;
    case LANG_UKRAINIAN:
        ret = LanguageType::UKRAINIAN;
        break;
    case LANG_ROMANIAN:
        ret = LanguageType::ROMANIAN;
        break;
    case LANG_BULGARIAN:
        ret = LanguageType::BULGARIAN;
        break;
    case LANG_BELARUSIAN:
        ret = LanguageType::BELARUSIAN;
        break;
    }

    return ret;
}

const char* Application::getCurrentLanguageCode()
{
    LANGID lid           = GetUserDefaultUILanguage();
    const LCID locale_id = MAKELCID(lid, SORT_DEFAULT);
    static char code[3]  = {0};
    GetLocaleInfoA(locale_id, LOCALE_SISO639LANGNAME, code, sizeof(code));
    code[2] = '\0';
    return code;
}

Application::Platform Application::getTargetPlatform()
{
    return Platform::Win32;
}

std::string Application::getVersion()
{
    char verString[256] = {0};
    TCHAR szVersionFile[MAX_PATH];
    GetModuleFileName(NULL, szVersionFile, MAX_PATH);
    DWORD verHandle = NULL;
    UINT size       = 0;
    LPBYTE lpBuffer = NULL;
    DWORD verSize   = GetFileVersionInfoSize(szVersionFile, &verHandle);

    if (verSize != NULL)
    {
        LPSTR verData = new char[verSize];

        if (GetFileVersionInfo(szVersionFile, verHandle, verSize, verData))
        {
            if (VerQueryValue(verData, L"\\", (VOID FAR * FAR*)&lpBuffer, &size))
            {
                if (size)
                {
                    VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
                    if (verInfo->dwSignature == 0xfeef04bd)
                    {

                        // Doesn't matter if you are on 32 bit or 64 bit,
                        // DWORD is always 32 bits, so first two revision numbers
                        // come from dwFileVersionMS, last two come from dwFileVersionLS
                        sprintf(verString, "%d.%d.%d.%d", (verInfo->dwFileVersionMS >> 16) & 0xffff,
                                (verInfo->dwFileVersionMS >> 0) & 0xffff, (verInfo->dwFileVersionLS >> 16) & 0xffff,
                                (verInfo->dwFileVersionLS >> 0) & 0xffff);
                    }
                }
            }
        }
        delete[] verData;
    }
    return verString;
}

bool Application::openURL(std::string_view url)
{
    std::wstring wURL = ntcvt::from_chars(url, CP_UTF8);
    HINSTANCE r       = ShellExecuteW(NULL, L"open", wURL.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return (size_t)r > 32;
}

void Application::setStartupScriptFilename(std::string_view startupScriptFile)
{
    _startupScriptFilename = startupScriptFile;
    std::replace(_startupScriptFilename.begin(), _startupScriptFilename.end(), '\\', '/');
}

NS_AX_END

//////////////////////////////////////////////////////////////////////////
// Local function
//////////////////////////////////////////////////////////////////////////
static void PVRFrameEnableControlWindow(bool bEnable)
{
    HKEY hKey = 0;

    // Open PVRFrame control key, if not exist create it.
    if (ERROR_SUCCESS != RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Imagination Technologies\\PVRVFRame\\STARTUP\\",
                                         0, 0, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, 0, &hKey, nullptr))
    {
        return;
    }

    using namespace cxx17;

    const WCHAR* wszValue = L"hide_gui";
    const auto svNewData  = (bEnable) ? L"NO"_sv : L"YES"_sv;
    WCHAR wszOldData[256] = {0};
    DWORD dwSize          = static_cast<DWORD>(sizeof(wszOldData));
    LSTATUS status        = RegQueryValueExW(hKey, wszValue, 0, nullptr, (LPBYTE)wszOldData, &dwSize);
    if (ERROR_FILE_NOT_FOUND == status    // the key not exist
        || (ERROR_SUCCESS == status       // or the hide_gui value is exist
            && svNewData != wszOldData))  // but new data and old data not equal
    {
        dwSize = static_cast<DWORD>(sizeof(WCHAR) * (svNewData.length() + 1));
        RegSetValueEx(hKey, wszValue, 0, REG_SZ, (const BYTE*)svNewData.data(), dwSize);
    }

    RegCloseKey(hKey);
}
