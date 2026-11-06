#include "MainFrame.hpp"

#include <wx/app.h>
#include <clocale>

class UnBallManagerApp final : public wxApp {
public:
    bool OnInit() override {
        std::setlocale(LC_CTYPE, "");
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(UnBallManagerApp);
