#pragma once
#include <string>
#include "Node.h"
#include "Color.h"

namespace Ling {
    class WinBase;
    class Text;   // 实现在 src/Text.h，Label 只持有指针，头文件无需可见

    // 复合组件：外层是一个标准 flex 容器（支持 flexGrow/setBg/对齐 等所有 Node 特性），
    // 内层是一个私有的 Text 节点，负责实际的文本绘制。
    // surface 尺寸只随文字 metric 走，与容器尺寸无关。
    class Label : public Node
    {
    public:
        Label(WinBase* win);
        ~Label();
        std::wstring getText();
        void setText(const std::wstring& text);
        void setFontSize(float val);
        void setFontFamily(const std::wstring& val);
        void setColor(Color color);
        // 局部文字着色；start/end 都为 0 时清除。
        void setTextRangeColor(int start, int end, Color color);
        void setFontWeight(int weight);
        // 定制版 Ling 兼容接口
        void setSelectable(bool val) { selectable = val; }
        void setHandCursor(bool val) { handCursor = val; }
    public:
        // 文本被点击时触发；idx 为点击处的字符下标（简单命中时返回 0）
        winrt::event<winrt::delegate<Label*, int>> onTextClick;
    private:
        void onDown(POINT pos, bool isRight);
        void onMove(POINT pos);
        void onCursorEvent(bool* handled);
    private:
        Text* text{ nullptr };
        bool selectable{ false }, handCursor{ false }, isHover{ false };
        winrt::event_token downTok{}, moveTok{}, cursorTok{};
    };
}
