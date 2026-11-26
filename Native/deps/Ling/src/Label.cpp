#include "pch.h"
#include "../include/Label.h"
#include "../include/WinBase.h"
#include "../include/Text.h"

namespace Ling {

    Label::Label(WinBase* win) : Node(win)
    {
        // 默认让 Text 在容器内居中；使用者可通过 Node::setJustify/setAlign 覆盖。
        text = makeChild<Text>();
        auto weakThis = getWeakThis();
        downTok   = win->onMouseDown .add([this, weakThis](POINT pos, bool isRight) { if (!weakThis.lock()) return; onDown(pos, isRight); });
        moveTok   = win->onMouseMove .add([this, weakThis](POINT pos)               { if (!weakThis.lock()) return; onMove(pos); });
        cursorTok = win->onCursor    .add([this, weakThis](bool* handled)           { if (!weakThis.lock()) return; onCursorEvent(handled); });
    }

    Label::~Label()
    {
        win->onMouseDown.remove(downTok);
        win->onMouseMove.remove(moveTok);
        win->onCursor.remove(cursorTok);
    }

    std::wstring Label::getText()
    {
        return text->getText();
    }

    void Label::setText(const std::wstring& s)       { text->setText(s); }
    void Label::setFontSize(float v)                 { text->setFontSize(v); }
    void Label::setFontFamily(const std::wstring& f) { text->setFontFamily(f); }
    void Label::setColor(Color c)                    { text->setColor(c); }
    void Label::setTextRangeColor(int s, int e, Color c) { text->setTextRangeColor(s, e, c); }
    void Label::setFontWeight(int w)                 { text->setFontWeight(w); }

    void Label::onDown(POINT pos, bool isRight)
    {
        if (!selectable || isRight || !visual.IsVisible()) return;
        if (!isPosIn(pos)) return;
        onTextClick(this, 0);
    }

    void Label::onMove(POINT pos)
    {
        isHover = visual.IsVisible() && isPosIn(pos);
    }

    void Label::onCursorEvent(bool* handled)
    {
        if (!handCursor || !isHover) return;
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        *handled = true;
    }
}
