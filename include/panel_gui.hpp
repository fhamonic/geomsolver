#pragma once

#include <imgui.h>

class PanelGUI {
public:
    virtual ~PanelGUI() = default;
    virtual void show(ImVec2 pos, ImVec2 size) = 0;
    virtual const char * name() const = 0;
};
