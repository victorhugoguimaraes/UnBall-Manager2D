#include "MainFrame.hpp"

#include <wx/app.h>

class UnBallManagerApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(UnBallManagerApp);
