/****************************************************************************
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2013 Jozef Pridavok
Copyright (c) 2013-2017 zilongshanren
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
Copyright (c) 2019-present Simdsoft Limited.

https://axmol.dev/

SPDX-License-Identifier: MIT
****************************************************************************/
#include "axmol/ui/EditBox/EditBoxImpl-win32.h"

#include "axmol/platform/PlatformConfig.h"
#if (AX_TARGET_PLATFORM == AX_PLATFORM_WIN32)

#    include "axmol/ui/EditBox/EditBox.h"
#    include <tchar.h>
#    include <stdio.h>
#    include <algorithm>
#    include <unordered_map>
#    include "axmol/platform/FileUtils.h"
#    include "axmol/2d/Label.h"
#    include "axmol/base/Director.h"
#    include "axmol/base/text_utils.h"
#    include <Commctrl.h>
#    include <windows.h>
#    include "ntcvt/ntcvt.hpp"
#    include "axmol/ui/UIHelper.h"

namespace ax
{

namespace ui
{

namespace
{
std::wstring resolveNativeFontFace(std::string_view fontName)
{
    auto fileUtils = FileUtils::getInstance();
    if (!fileUtils->isFileExist(fontName))
        return ntcvt::from_chars(fontName);

    static std::unordered_map<std::string, std::wstring> s_faces;
    const std::string key(fontName);
    auto it = s_faces.find(key);
    if (it != s_faces.end())
        return it->second;

    std::string fullPath = fileUtils->fullPathForFilename(fontName);
    std::replace(fullPath.begin(), fullPath.end(), '/', '\\');
    ::AddFontResourceExW(ntcvt::from_chars(fullPath).c_str(), FR_PRIVATE, 0);

    std::string face = fullPath.substr(fullPath.find_last_of('\\') + 1);
    face             = face.substr(0, face.find_first_of(".-"));
    return s_faces.emplace(key, ntcvt::from_chars(face)).first->second;
}
}  // namespace

bool EditBoxImplWin::s_isInitialized       = false;
HMENU EditBoxImplWin::s_editboxChildID     = (HMENU)100;
HWND EditBoxImplWin::s_previousFocusWnd    = 0;
WNDPROC EditBoxImplWin::s_prevAxmolWndProc = 0;
HINSTANCE EditBoxImplWin::s_hInstance      = 0;
HWND EditBoxImplWin::s_hwndAxmol           = 0;

void EditBoxImplWin::lazyInit()
{
    s_hwndAxmol = (HWND)ax::Director::getInstance()->getRenderView()->getNativeWindow();
    LONG style  = ::GetWindowLongW(s_hwndAxmol, GWL_STYLE);
    ::SetWindowLongW(s_hwndAxmol, GWL_STYLE, style | WS_CLIPCHILDREN);
    s_isInitialized    = true;
    s_previousFocusWnd = s_hwndAxmol;

    s_hInstance = ::GetModuleHandleW(nullptr);

    s_prevAxmolWndProc = (WNDPROC)SetWindowLongPtrW(s_hwndAxmol, GWLP_WNDPROC, (LONG_PTR)hookGLFWWindowProc);
}

EditBoxImpl* __createSystemEditBox(EditBox* pEditBox)
{
    return new EditBoxImplWin(pEditBox);
}

EditBoxImplWin::EditBoxImplWin(EditBox* pEditText)
    : EditBoxImplCommon(pEditText), _endAction(EditBoxDelegate::EditBoxEndAction::UNKNOWN)
{
    if (!s_isInitialized)
    {
        lazyInit();
    }

    s_editboxChildID++;
}

EditBoxImplWin::~EditBoxImplWin()
{
    this->cleanupEditCtrl();
}

bool EditBoxImplWin::isEditing()
{
    return _editingMode;
}

void EditBoxImplWin::cleanupEditCtrl()
{
    if (_hwndEdit)
    {
        if (s_previousFocusWnd == _hwndEdit)
        {
            s_previousFocusWnd = s_hwndAxmol;
        }

        SetWindowLongPtrW(_hwndEdit, GWLP_WNDPROC, (LONG_PTR)_prevWndProc);
        ::DestroyWindow(_hwndEdit);
        _hasFocus            = false;
        _changedTextManually = false;
        _editingMode         = false;
        _hwndEdit            = NULL;

        cleanupFont();
    }
}

void EditBoxImplWin::createEditCtrl(bool singleLine)
{
    this->cleanupEditCtrl();
    if (!_hwndEdit)
    {
        const bool password = (_editBoxInputFlag == EditBox::InputFlag::PASSWORD);

        DWORD style = WS_CHILD | ES_LEFT | WS_TABSTOP | ES_AUTOHSCROLL;
        if (!password)
            style |= ES_MULTILINE;
        if (!singleLine)
            style |= ES_AUTOVSCROLL;

        _hwndEdit = ::CreateWindowExW(0, L"EDIT",  // predefined class
                                      NULL,        // no window title
                                      style,
                                      0, 0, 0,
                                      0,                 // set size in WM_SIZE message
                                      s_hwndAxmol,       // parent window
                                      s_editboxChildID,  // edit control ID
                                      s_hInstance,
                                      this);  // pointer not needed

        SetWindowLongPtrW(_hwndEdit, GWLP_USERDATA, (LONG_PTR)this);
        _prevWndProc = (WNDPROC)SetWindowLongPtrW(_hwndEdit, GWLP_WNDPROC, (LONG_PTR)WindowProc);

        ::SendMessageW(_hwndEdit, EM_LIMITTEXT, this->_maxLength, 0);

        this->setNativeFont(_fontName.empty() ? this->getNativeDefaultFontName() : std::string_view(_fontName), this->_fontSize);
        this->setNativeText(this->_text);

        if (password)
            ::SendMessageW(_hwndEdit, EM_SETPASSWORDCHAR, (WPARAM)L'*', 0);
    }
}

void EditBoxImplWin::createNativeControl()
{
    this->createEditCtrl(false);
}

void EditBoxImplWin::setNativeFont(std::string_view fontName, int fontSize)
{
    auto renderView = Director::getInstance()->getRenderView();
    float scale     = renderView ? renderView->getScaleX() : 1.0f;
    if (_editBox)
        scale *= _editBox->getNodeToWorldAffineTransform().a;

    int pixelSize = static_cast<int>(_fontSize * scale + 0.5f);
    if (pixelSize < 1)
        pixelSize = 1;

    auto hFont = ::CreateFontW(-pixelSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                               FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                               VARIABLE_PITCH, resolveNativeFontFace(fontName).c_str());

    if (!hFont)
    {
        AXLOGW("Failed to create font for EditBox, fallback to default font");
        hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        if (hFont)
            return;
    }

    ::SendMessageW(_hwndEdit,           // Handle of edit control
                   WM_SETFONT,          // Message to change the font
                   (WPARAM)hFont,       // handle of the font
                   MAKELPARAM(TRUE, 0)  // Redraw text
    );

    cleanupFont();
    _hEditFont = hFont;
}

void EditBoxImplWin::cleanupFont()
{
    if (_hEditFont && _hEditFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
    {
        DeleteObject(_hEditFont);
        _hEditFont = nullptr;
    }
}

void EditBoxImplWin::setNativeFontColor(const Color32& /*color*/)
{
    // not implemented yet
}

void EditBoxImplWin::setNativePlaceholderFont(std::string_view /*fontName*/, int /*fontSize*/)
{
    // not implemented yet
}

void EditBoxImplWin::setNativePlaceholderFontColor(const Color32& /*color*/)
{
    // not implemented yet
}

void EditBoxImplWin::setNativeInputMode(EditBox::InputMode inputMode)
{
    if (_editBoxInputMode == ax::ui::EditBox::InputMode::ANY)
    {
        this->createEditCtrl(false);
    }
    else if (_editBoxInputMode == ax::ui::EditBox::InputMode::NUMERIC ||
             _editBoxInputMode == ax::ui::EditBox::InputMode::DECIMAL ||
             _editBoxInputMode == ax::ui::EditBox::InputMode::PHONE_NUMBER)
    {
        this->createEditCtrl(true);
        ::SetWindowLongW(_hwndEdit, GWL_STYLE, ::GetWindowLongW(_hwndEdit, GWL_STYLE) | ES_NUMBER);
    }
    else
    {
        this->createEditCtrl(true);
    }

    if (this->_editBoxInputFlag != ax::ui::EditBox::InputFlag::PASSWORD)
    {
        ::PostMessageW(_hwndEdit, EM_SETPASSWORDCHAR, (WPARAM)0, (LPARAM)0);
    }
}
void EditBoxImplWin::setNativeInputFlag(EditBox::InputFlag inputFlag)
{
    if (inputFlag == EditBox::InputFlag::PASSWORD)
    {
        this->createEditCtrl(true);
    }
    else
    {
        if (_editBoxInputMode != ax::ui::EditBox::InputMode::ANY)
        {
            this->createEditCtrl(true);

            if (inputFlag == EditBox::InputFlag::INITIAL_CAPS_ALL_CHARACTERS)
            {
                ::SetWindowLongW(_hwndEdit, GWL_STYLE, ::GetWindowLongW(_hwndEdit, GWL_STYLE) | ES_UPPERCASE);
            }
            // Clear the password style
            ::PostMessageW(_hwndEdit, EM_SETPASSWORDCHAR, (WPARAM)0, (LPARAM)0);
        }
    }
}

void EditBoxImplWin::setNativeReturnType(EditBox::KeyboardReturnType returnType)
{
    // not implemented yet
}

void EditBoxImplWin::setNativeTextHorizontalAlignment(TextHAlignment alignment)
{
    LONG style = ::GetWindowLongW(_hwndEdit, GWL_STYLE);
    switch (alignment)
    {
    case TextHAlignment::LEFT:
        style = (style & ~ES_CENTER & ~ES_RIGHT) | ES_LEFT;
        break;
    case TextHAlignment::CENTER:
        style = (style & ~ES_LEFT & ~ES_RIGHT) | ES_CENTER;
        break;
    case TextHAlignment::RIGHT:
        style = (style & ~ES_LEFT & ~ES_CENTER) | ES_RIGHT;
        break;
    }
    ::SetWindowLongW(_hwndEdit, GWL_STYLE, style);
}

void EditBoxImplWin::setNativeText(std::string_view text)
{
    std::u16string utf16Result;
    ax::text_utils::UTF8ToUTF16(text, utf16Result);
    const bool previousChangedTextManually = _changedTextManually;
    _changedTextManually                   = true;

    ::SetWindowTextW(_hwndEdit, (LPCWSTR)utf16Result.c_str());

    _changedTextManually = previousChangedTextManually;

    int textLen = text.size();
    ::SendMessageW(_hwndEdit, EM_SETSEL, textLen, textLen);

    if (_editBoxInputMode == ax::ui::EditBox::InputMode::ANY)
    {
        ::SendMessageW(_hwndEdit, EM_SCROLLCARET, 0, 0);
    }
}

void EditBoxImplWin::setNativePlaceHolder(std::string_view /*text*/)
{
    // not implemented yet
}

void EditBoxImplWin::setNativeVisible(bool visible)
{
    if (visible)
    {
        ::ShowWindow(_hwndEdit, SW_SHOWNOACTIVATE);
    }
    else
    {
        ::ShowWindow(_hwndEdit, SW_HIDE);
    }
}

void EditBoxImplWin::updateNativeFrame(const Rect& rect)
{
    if (!_hwndEdit)
        return;

    int x = static_cast<int>(rect.origin.x);
    int y = static_cast<int>(rect.origin.y);
    int w = static_cast<int>(rect.size.width);
    int h = static_cast<int>(rect.size.height);

    const bool nonAny   = (_editBoxInputMode != EditBox::InputMode::ANY);
    const bool password = (_editBoxInputFlag == EditBox::InputFlag::PASSWORD);

    auto renderView = Director::getInstance()->getRenderView();
    float scale     = renderView ? renderView->getScaleX() : 1.0f;
    if (_editBox)
        scale *= _editBox->getNodeToWorldAffineTransform().a;
    int margin = static_cast<int>(2 * scale + 0.5f);

    int textHeight = 0;
    if (nonAny && _hEditFont)
    {
        HDC hdc       = ::GetDC(_hwndEdit);
        HFONT oldFont = static_cast<HFONT>(::SelectObject(hdc, _hEditFont));
        TEXTMETRICW tm;
        if (::GetTextMetricsW(hdc, &tm))
            textHeight = tm.tmHeight;
        ::SelectObject(hdc, oldFont);
        ::ReleaseDC(_hwndEdit, hdc);
    }

    if (nonAny && password && textHeight > 0)
    {
        int top = (h - textHeight) / 2;
        if (top < 0)
            top = 0;
        y += top;
        h = textHeight;
    }

    ::SetWindowPos(_hwndEdit, HWND_NOTOPMOST, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

    if (nonAny && textHeight > 0)
    {
        if (password)
        {
            ::SendMessageW(_hwndEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
        }
        else
        {
            RECT rc;
            ::GetClientRect(_hwndEdit, &rc);
            int top = (rc.bottom - rc.top - textHeight) / 2;
            if (top < 0)
                top = 0;
            RECT fmt = {rc.left + margin, rc.top + top, rc.right - margin, rc.bottom};
            ::SendMessageW(_hwndEdit, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&fmt));
        }
    }
}

std::string_view EditBoxImplWin::getNativeDefaultFontName()
{
    return "Arial"sv;
}

void EditBoxImplWin::nativeOpenKeyboard()
{
    auto* renderView   = static_cast<RenderView*>(Director::getInstance()->getRenderView());
    GLFWwindow* window = renderView->getWindow();

    const int previousAutoIconify = glfwGetWindowAttrib(window, GLFW_AUTO_ICONIFY);

    glfwSetWindowAttrib(window, GLFW_AUTO_ICONIFY, GLFW_FALSE);

    this->editBoxEditingDidBegin();

    auto rect = ui::Helper::getNodeNativeWindowRect(_editBox);
    this->updateNativeFrame(rect);

    ::SetFocus(_hwndEdit);

    glfwSetWindowAttrib(window, GLFW_AUTO_ICONIFY, previousAutoIconify);
}

void EditBoxImplWin::nativeCloseKeyboard()
{
    // don't need to implement
}

void EditBoxImplWin::setNativeMaxLength(int maxLength)
{
    ::SendMessageW(_hwndEdit, EM_LIMITTEXT, maxLength, 0);
}

void EditBoxImplWin::hideAndRestoreFocus()
{
    const HWND editWnd = s_previousFocusWnd;
    ::ShowWindow(editWnd, SW_HIDE);
    // Hiding ends editing synchronously, the delegate may have already moved focus to another EditBox.
    if (s_previousFocusWnd == editWnd)
    {
        ::SendMessageW(s_hwndAxmol, WM_SETFOCUS, (WPARAM)editWnd, 0);
        s_previousFocusWnd = s_hwndAxmol;
    }
}

LRESULT EditBoxImplWin::_WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_KEYDOWN:
        if (_hasFocus && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) != 0)  // CTRL+A for "Select All"
        {
            ::SendMessageW(_hwndEdit, EM_SETSEL, 0, -1);
        }
        if (_hasFocus && wParam == VK_ESCAPE)
        {
            _hasFocus  = false;
            _endAction = EditBoxDelegate::EditBoxEndAction::RETURN;
            hideAndRestoreFocus();
        }
        if (_hasFocus && wParam == VK_TAB && _editBoxInputMode != ax::ui::EditBox::InputMode::ANY)
        {
            const bool shiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
            _endAction = shiftDown ? EditBoxDelegate::EditBoxEndAction::TAB_TO_PREVIOUS
                                   : EditBoxDelegate::EditBoxEndAction::TAB_TO_NEXT;
            _hasFocus  = false;
            hideAndRestoreFocus();
        }
        break; 
    case WM_CHAR:
        if (_editBoxInputMode != ax::ui::EditBox::InputMode::ANY && (wParam == VK_TAB || wParam == VK_ESCAPE))
        {
            return 0;
        }
        if (wParam == VK_RETURN)
        {
            if (_editBoxInputMode != ax::ui::EditBox::InputMode::ANY)
            {
                if (s_previousFocusWnd != s_hwndAxmol)
                {
                    switch (_keyboardReturnType)
                    {
                    case EditBox::KeyboardReturnType::DEFAULT:
                        _endAction = EditBoxDelegate::EditBoxEndAction::UNKNOWN;
                        break;
                    case EditBox::KeyboardReturnType::DONE:
                        _endAction = EditBoxDelegate::EditBoxEndAction::RETURN;
                        break;
                    case EditBox::KeyboardReturnType::SEND:
                        _endAction = EditBoxDelegate::EditBoxEndAction::RETURN;
                        break;
                    case EditBox::KeyboardReturnType::SEARCH:
                        _endAction = EditBoxDelegate::EditBoxEndAction::RETURN;
                        break;
                    case EditBox::KeyboardReturnType::GO:
                        _endAction = EditBoxDelegate::EditBoxEndAction::RETURN;
                        break;
                    case EditBox::KeyboardReturnType::NEXT:
                        _endAction = EditBoxDelegate::EditBoxEndAction::TAB_TO_NEXT;
                        break;
                    default:
                        _endAction = EditBoxDelegate::EditBoxEndAction::UNKNOWN;
                        break;
                    }
                    hideAndRestoreFocus();
                }
                return 0;
            }
            else if (s_previousFocusWnd != s_hwndAxmol)
            {
                const auto currentLength = ::GetWindowTextLengthW(s_previousFocusWnd);
                const auto maxLength     = ::SendMessageW(s_previousFocusWnd, EM_GETLIMITTEXT, 0, 0);

                // A Return key needs 2 character spaces (\r\n)
                if (currentLength + 2 > maxLength)
                {
                    return 0;  // Ignore this input
                }
            }
        }
        break;
    case WM_SETFOCUS:
        _hasFocus = true;

        if (hwnd != s_previousFocusWnd)
        {
            ::PostMessageW(hwnd, WM_SETCURSOR, (WPARAM)s_previousFocusWnd, 0);

            if (_initialFocus && _editBoxInputMode != ax::ui::EditBox::InputMode::ANY)
            {
                // The following is a work-around to force the edit box to display
                // as much text as possible when it receives focus for the first time
                int inputLength = ::GetWindowTextLengthW(hwnd);
                if (inputLength > 0)
                {
                    std::u16string wstrResult;
                    wstrResult.resize(inputLength);
                    ::GetWindowTextW(hwnd, (LPWSTR)wstrResult.data(), inputLength + 1);
                    this->_changedTextManually = true;  // We don't want to trigger the editBoxEditingChanged callback
                    ::SetWindowTextW(hwnd, (LPWSTR)wstrResult.data());
                    ::SendMessage(hwnd, EM_SETSEL, inputLength, -1);
                    ::SendMessage(hwnd, EM_SETSEL, -1, -1);
                    ::SendMessageW(_hwndEdit, EM_SETSEL, 0, -1);
                }

                _initialFocus = false;
            }

            ::SendMessageW(hwnd, EM_SETSEL, 0, -1);
            _endAction                 = EditBoxDelegate::EditBoxEndAction::UNKNOWN;
            s_previousFocusWnd         = _hwndEdit;
            this->_changedTextManually = false;
        }
        break;
    case WM_KILLFOCUS:
        _hasFocus = false;
        // focus moved straight to another EditBox, hide this one so its editing ends
        if (const HWND nextWnd = (HWND)wParam; nextWnd && nextWnd != hwnd && ::GetParent(nextWnd) == s_hwndAxmol)
        {
            ::ShowWindow(hwnd, SW_HIDE);
        }
        // when app enter background, this message also be called.
        if (this->_editingMode && !::IsWindowVisible(hwnd))
        {
            this->editBoxEditingDidEnd(this->getNativeText(), _endAction);
        }
        break;
    default:
        break;
    }

    return ::CallWindowProcW(_prevWndProc, hwnd, uMsg, wParam, lParam);
}

std::string EditBoxImplWin::getNativeText() const
{
    std::u16string wstrResult;
    std::string utf8Result;

    int inputLength = ::GetWindowTextLengthW(_hwndEdit);
    wstrResult.resize(inputLength);

    ::GetWindowTextW(_hwndEdit, (LPWSTR)&wstrResult[0], inputLength + 1);
    bool conversionResult = ax::text_utils::UTF16ToUTF8(wstrResult, utf8Result);
    if (!conversionResult)
    {
        AXLOGW("warning, editbox input text conversion error.");
    }
    return utf8Result;
}

LRESULT EditBoxImplWin::hookGLFWWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE)
        {
            EditBoxImplWin* pThis = (EditBoxImplWin*)GetWindowLongPtrW((HWND)lParam, GWLP_USERDATA);
            if (pThis && !pThis->_changedTextManually)
            {
                pThis->editBoxEditingChanged(pThis->getNativeText());
            }
        }
        break;
    case WM_LBUTTONDOWN:
        if (s_previousFocusWnd != s_hwndAxmol)
        {
            const HWND editWnd    = s_previousFocusWnd;
            EditBoxImplWin* pThis = (EditBoxImplWin*)GetWindowLongPtrW(editWnd, GWLP_USERDATA);
            ::ShowWindow(editWnd, SW_HIDE);

            if (pThis != nullptr && !pThis->_hasFocus)
            {
                if (pThis->_editingMode && !IsWindowVisible(editWnd))
                {
                    pThis->editBoxEditingDidEnd(pThis->getNativeText());
                }
            }
            else
            {
                ::PostMessageW(s_hwndAxmol, WM_SETFOCUS, (WPARAM)editWnd, 0);
            }
            // the delegate may have already moved focus to another EditBox
            if (s_previousFocusWnd == editWnd)
            {
                s_previousFocusWnd = s_hwndAxmol;
            }
        }

        break;
    default:
        break;
    }

    return ::CallWindowProcW(s_prevAxmolWndProc, hwnd, uMsg, wParam, lParam);
}

LRESULT EditBoxImplWin::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    EditBoxImplWin* pThis = (EditBoxImplWin*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (pThis)
    {
        return pThis->_WindowProc(hwnd, uMsg, wParam, lParam);
    }

    return ::CallWindowProcW(pThis->_prevWndProc, hwnd, uMsg, wParam, lParam);
}
}  // namespace ui

}  // namespace ax

#endif /* (AX_TARGET_PLATFORM == AX_PLATFORM_WIN32) */
