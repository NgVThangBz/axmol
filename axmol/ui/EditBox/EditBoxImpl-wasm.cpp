/****************************************************************************

 Copyright (c) 2019-present Simdsoft Limited.

 https://axmol.dev/

SPDX-License-Identifier: MIT

// Implemented features:
//  [X] single line only based on html <input />

****************************************************************************/

#include "axmol/ui/EditBox/EditBoxImpl-wasm.h"

#if AX_TARGET_PLATFORM == AX_PLATFORM_WASM
#    include "axmol/ui/UIHelper.h"
#    include <emscripten/emscripten.h>
namespace ax
{

namespace ui
{
EditBoxImplWasm* _activeEditBox = nullptr;
extern "C" {
// action: 0 = RETURN, 1 = TAB_TO_NEXT, 2 = TAB_TO_PREVIOUS
EMSCRIPTEN_KEEPALIVE void axmol_editbox_endediting(const char* pszText, int length, int action)
{
    std::string_view text{pszText, static_cast<size_t>(length)};
    AXLOGD("text {} ", text);
    if (_activeEditBox)
    {
        auto endAction = EditBoxDelegate::EditBoxEndAction::RETURN;
        if (action == 1)
            endAction = EditBoxDelegate::EditBoxEndAction::TAB_TO_NEXT;
        else if (action == 2)
            endAction = EditBoxDelegate::EditBoxEndAction::TAB_TO_PREVIOUS;

        auto ended     = _activeEditBox;
        _activeEditBox = nullptr;
        if (ended->isEditingMode())
            ended->editBoxEditingDidEnd(text, endAction);

        // the delegate may have opened another EditBox (tab navigation); ending the previous one hid the shared <input>
        if (_activeEditBox)
            _activeEditBox->refocusNative();
    }
}

EMSCRIPTEN_KEEPALIVE void axmol_editbox_textchange(const char* pszText, int length)
{
    std::string_view text{pszText, static_cast<size_t>(length)};
    AXLOGD("text {} ", text);
    if (_activeEditBox && _activeEditBox->isEditingMode())
    {
        _activeEditBox->editBoxEditingChanged(text);
    }
}

// True while an EditBox is being edited; lets the app suppress hotkeys during text input.
EMSCRIPTEN_KEEPALIVE int axmol_is_editbox_editing()
{
    return _activeEditBox != nullptr ? 1 : 0;
}
}

bool EditBoxImplWasm::s_isInitialized = false;
int EditBoxImplWasm::s_editboxChildID = 100;

EditBoxImpl* __createSystemEditBox(EditBox* pEditBox)
{
    return new EditBoxImplWasm(pEditBox);
}

EditBoxImplWasm::EditBoxImplWasm(EditBox* pEditText) : EditBoxImplCommon(pEditText)
{
    if (!s_isInitialized)
    {
        lazyInit();
    }
    s_editboxChildID++;
}

EditBoxImplWasm::~EditBoxImplWasm()
{
    // this->cleanupEditCtrl();
}

bool EditBoxImplWasm::isEditing()
{
    return _editingMode;
}

void EditBoxImplWasm::createNativeControl()
{
    this->createEditCtrl(ax::ui::EditBox::InputMode::ANY);
}

void EditBoxImplWasm::setNativeFont(std::string_view /*fontName*/, int fontSize)
{
    // clang-format off
    EM_ASM({
        var input = Module.axmol_editbox_input = Module.axmol_editbox_input || document.createElement("input");
        input.style.fontSize                   = $0 + "px";
    },
    fontSize);
    // clang-format on
}

void EditBoxImplWasm::setNativeFontColor(const Color32& /*color*/)
{
    // not implemented yet
}

void EditBoxImplWasm::setNativePlaceholderFont(std::string_view /*fontName*/, int /*fontSize*/)
{
    // not implemented yet
}

void EditBoxImplWasm::setNativePlaceholderFontColor(const Color32& /*color*/)
{
    // not implemented yet
}

void EditBoxImplWasm::setNativeInputMode(EditBox::InputMode inputMode)
{
    this->createEditCtrl(inputMode);
}
void EditBoxImplWasm::setNativeInputFlag(EditBox::InputFlag inputFlag) {}

void EditBoxImplWasm::setNativeReturnType(EditBox::KeyboardReturnType returnType)
{
    // not implemented yet
}

void EditBoxImplWasm::setNativeTextHorizontalAlignment(TextHAlignment alignment) {}

void EditBoxImplWasm::setNativeText(std::string_view text)
{
    if (_activeEditBox != this)
        return;
    // clang-format off
    EM_ASM({
        var input = Module.axmol_editbox_input = Module.axmol_editbox_input || document.createElement("input");
        input.value                            = UTF8ToString($0, $1);
    },
    text.data(), static_cast<int>(text.size()));
    // clang-format off
}

void EditBoxImplWasm::setNativeVisible(bool visible)
{
    // clang-format off
    EM_ASM({
        var input = Module.axmol_editbox_input = Module.axmol_editbox_input || document.createElement("input");
        if ($0 == 0)
            input.style.display = "none";
        else
        {
            var inputMode = $1;
            var inputFlag = $2;
            // set input type
            switch (inputMode)
            {
            case 2:  // NUMERIC
            case 3:  // PHONE_NUMBER
                input.type = 'number';
            default:
                if (inputFlag != 0)
                {
                    input.type = 'text';
                }
                else
                {
                    input.type = 'password';
                }
            }

            input.style.display = "";
            var canvas          = document.getElementById('canvas');
            var inputParent     = input.parentNode;
            var canvasParent    = canvas.parentNode;
            if (inputParent != canvasParent)
            {
                if (inputParent != null)
                {
                    inputParent.removeChild(input);
                }
                canvasParent.insertBefore(input, canvas);
            }
        }
    },
    (int)visible, (int)_editBoxInputMode, (int)_editBoxInputFlag);
    // clang-format on
}

void EditBoxImplWasm::updateNativeFrame(const Rect& rect)
{
    // clang-format off
    EM_ASM({
        var input = Module.axmol_editbox_input = Module.axmol_editbox_input || document.createElement("input");
        var canvas                             = Module["canvas"];
        // set input style
        input.style.position = "absolute";
        input.style.left     = canvas.offsetLeft + $0 + "px";
        input.style.top      = canvas.offsetTop + $1 + "px";
        input.style.width    = $2 + "px";
        input.style.height   = $3 + "px";
    },
    rect.origin.x, rect.origin.y, rect.size.x, rect.size.y);
    // clang-format on
    float designH = _editBox ? _editBox->getContentSize().height : 0.f;
    float scale   = (designH > 0.f) ? (rect.size.y / designH) : 1.f;
    int fontPx    = _fontSize > 0 ? static_cast<int>(_fontSize * scale + 0.5f) : 0;
    int phPx      = _placeholderFontSize > 0 ? static_cast<int>(_placeholderFontSize * scale + 0.5f) : fontPx;
    this->applyNativeStyle(fontPx, phPx, static_cast<int>(rect.size.y + 0.5f));
}

void EditBoxImplWasm::applyNativeStyle(int fontSizePx, int placeholderSizePx, int boxHeightPx)
{
    uint32_t tc = ((uint32_t)_colText.r << 24) | ((uint32_t)_colText.g << 16) | ((uint32_t)_colText.b << 8) |
                  (uint32_t)_colText.a;
    uint32_t pc = ((uint32_t)_colPlaceHolder.r << 24) | ((uint32_t)_colPlaceHolder.g << 16) |
                  ((uint32_t)_colPlaceHolder.b << 8) | (uint32_t)_colPlaceHolder.a;

    Color32 bg   = _editBox ? _editBox->getColor() : Color32::white;
    uint8_t bgA  = _editBox ? _editBox->getDisplayedOpacity() : 255;
    uint32_t bgc = ((uint32_t)bg.r << 24) | ((uint32_t)bg.g << 16) | ((uint32_t)bg.b << 8) | (uint32_t)bgA;

    if (fontSizePx == _appliedFontPx && placeholderSizePx == _appliedPlaceholderPx && boxHeightPx == _appliedBoxPx &&
        tc == _appliedTextColor && pc == _appliedPlaceholderColor && bgc == _appliedBgColor)
        return;
    _appliedFontPx            = fontSizePx;
    _appliedPlaceholderPx     = placeholderSizePx;
    _appliedBoxPx             = boxHeightPx;
    _appliedTextColor         = tc;
    _appliedPlaceholderColor  = pc;
    _appliedBgColor           = bgc;

    // clang-format off
    EM_ASM({
        var input = Module.axmol_editbox_input = Module.axmol_editbox_input || document.createElement("input");
        if ($0 > 0)
            input.style.fontSize = $0 + "px";
        input.style.boxSizing = "border-box";
        input.style.border    = "0";
        input.style.padding   = "0";
        if ($10 > 0)
            input.style.lineHeight = $10 + "px";
        input.style.color           = "rgba(" + $1 + "," + $2 + "," + $3 + "," + ($4 / 255) + ")";
        input.style.backgroundColor = "rgba(" + $11 + "," + $12 + "," + $13 + "," + ($14 / 255) + ")";
        var style = document.getElementById("axmol_editbox_style");
        if (!style)
        {
            style    = document.createElement("style");
            style.id = "axmol_editbox_style";
            (document.head || document.body).appendChild(style);
        }
        var ph = $5 > 0 ? ("font-size:" + $5 + "px;") : "";
        style.textContent = "#axmol_editbox_input::placeholder{color:rgba(" + $6 + "," + $7 + "," + $8 + "," +
                            ($9 / 255) + ");" + ph + "opacity:1;}";
    },
    fontSizePx,
    (int)_colText.r, (int)_colText.g, (int)_colText.b, (int)_colText.a,
    placeholderSizePx,
    (int)_colPlaceHolder.r, (int)_colPlaceHolder.g, (int)_colPlaceHolder.b, (int)_colPlaceHolder.a,
    boxHeightPx,
    (int)bg.r, (int)bg.g, (int)bg.b, (int)bgA);
    // clang-format on
}

void EditBoxImplWasm::refocusNative()
{
    this->setNativeVisible(true);
    // clang-format off
    EM_ASM({
        var input = Module.axmol_editbox_input = Module.axmol_editbox_input || document.createElement("input");
        input.focus();
        try {
            var len = input.value.length;
            input.setSelectionRange(len, len);
        } catch (e) {}
    });
    // clang-format on
}

void EditBoxImplWasm::nativeOpenKeyboard()
{
    if (_activeEditBox && _activeEditBox != this)
    {
        auto previous  = _activeEditBox;
        _activeEditBox = nullptr;
        previous->editBoxEditingDidEnd(std::string{previous->_text}, EditBoxDelegate::EditBoxEndAction::RETURN);
        // ending the previous box hides the shared <input>
        this->setNativeVisible(true);
    }

    _activeEditBox = this;

    this->editBoxEditingDidBegin();

    auto text = this->getText();
    auto ph   = this->getPlaceHolder();

    // clang-format off
    EM_ASM({
        var input = Module.axmol_editbox_input = Module.axmol_editbox_input || document.createElement("input");
        input.value       = UTF8ToString($0, $1);
        input.placeholder = UTF8ToString($3, $4);
        input.maxlength   = $2 != -1 ? $2 : undefined;
        input.focus();
        // put the caret at the end (some browsers focus at position 0)
        try {
            var len = input.value.length;
            input.setSelectionRange(len, len);
        } catch (e) {
            // type=number doesn't support setSelectionRange; re-assign to move caret to end
            var v = input.value;
            input.value = '';
            input.value = v;
        }
    },
    text.data(), (int)text.size(), (int)_maxLength, ph.data(), (int)ph.size());
    // clang-format on

    auto rect = ui::Helper::getNodeNativeWindowRect(_editBox);
    this->updateNativeFrame(rect);
}

void EditBoxImplWasm::lazyInit()
{
    // clang-format off
    EM_ASM({
        var input = Module.axmol_editbox_input = Module.axmol_editbox_input || document.createElement("input");
        input.id = "axmol_editbox_input";
        // set input type
        input.type = "text";
        // set input style
        input.style.position = "absolute";
        input.style.left     = "0px";
        input.style.top      = "0px";
        input.style.width    = "200px";
        input.style.height   = "30px";
        // input.style.border = "1px solid black";
        input.style.padding              = " 0 0 0 0px";
        // document.body.appendChild(input);
        input.addEventListener(
            "keydown", function(event) {
                if (event.key === "Enter")
                {
                    // end editing so the engine fires the RETURN action (native "Done" behavior)
                    event.preventDefault();
                    input.blur();
                    return;
                }
                if (event.key === "Tab")
                {
                    // end editing with a tab action so the delegate can move to the next/previous EditBox
                    event.preventDefault();
                    Module.axmol_editbox_endAction = event.shiftKey ? 2 : 1;
                    input.blur();
                    return;
                }
                if (event.key === "Backspace")
                {
                    // the engine's window-level key handler suppresses the browser's default backspace, delete manually
                    var start = input.selectionStart;
                    var end   = input.selectionEnd;
                    if (start === end)
                    {
                        if (start === 0)
                            return;
                        start -= 1;
                    }
                    input.setRangeText("", start, end, "end");
                    // programmatic edits don't fire 'input', notify the engine ourselves
                    input.dispatchEvent(new Event("input"));
                    return;
                }

                if (input.maxlength !== undefined && input.value.length >= input.maxlength)
                {
                    // prevent max chars
                    event.preventDefault();
                }
            });
        input.addEventListener(
            'input', function() {
                var input = Module.axmol_editbox_input = Module.axmol_editbox_input || document.createElement("input");
                var result = Module.stringToUTF8WithLen(input.value);
                _axmol_editbox_textchange(result.ptr, result.length);
                _free(result.ptr);
            });
        input.addEventListener(
            'blur', function() {
                // handle focus lost
                var input = Module.axmol_editbox_input = Module.axmol_editbox_input || document.createElement("input");
                input.style.display                 = "none";

                var action = Module.axmol_editbox_endAction || 0;
                Module.axmol_editbox_endAction = 0;

                var result = Module.stringToUTF8WithLen(input.value);
                _axmol_editbox_endediting(result.ptr, result.length, action);
                _free(result.ptr)
            });
    });
    // clang-format on
    s_isInitialized = true;
}

void EditBoxImplWasm::createEditCtrl(EditBox::InputMode inputMode)
{
    EM_ASM({ Module.axmol_editbox_input = Module.axmol_editbox_input || document.createElement("input"); });
    this->setNativeFont(this->getNativeDefaultFontName(), this->_fontSize);
    this->setNativeText(this->_text);
}

void EditBoxImplWasm::setNativePlaceHolder(std::string_view text)
{
    if (_activeEditBox != this)
        return;
    EM_ASM(
        {
            var input = Module.axmol_editbox_input = Module.axmol_editbox_input || document.createElement("input");
            // sync input value from native and focus
            input.placeholder = UTF8ToString($0, $1);
            input.focus();
        },
        !text.empty() ? text.data() : "", (int)text.size());
}

std::string_view EditBoxImplWasm::getNativeDefaultFontName()
{
    return "Arial"sv;
}

void EditBoxImplWasm::nativeCloseKeyboard()
{
    // don't need to implement
}

void EditBoxImplWasm::setNativeMaxLength(int /*maxLength*/)
{
    // since we use shared inputbox, we sync maxlength when open inputbox for current editbox
}

}  // namespace ui

}  // namespace ax

#endif
