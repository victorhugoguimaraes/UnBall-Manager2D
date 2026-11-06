#include "MainFrame.hpp"

#include <wx/app.h>
#include <clocale>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <unistd.h>
#include "AppUpdate.hpp"

class UnBallManagerApp final : public wxApp {
public:
    bool OnInit() override {
        std::setlocale(LC_CTYPE, "");
        SetClassName("unball-manager2d");
        const auto data = std::filesystem::path(wxStandardPaths::Get().GetUserDataDir().ToStdString());
        const auto executable = std::filesystem::path(wxStandardPaths::Get().GetExecutablePath().ToStdString());
        wxString skip;
        const bool skipManaged = wxGetEnv("UNBALL_SKIP_MANAGED_UPDATE", &skip) && skip == "1";
        const auto update = skipManaged ? std::filesystem::path{} : startupUpdate(data, executable, applicationRevision());
        if (!update.empty()) {
            std::vector<std::string> arguments{update.string()};
            for (int i = 1; i < argc; ++i) arguments.push_back(wxString(argv[i]).ToStdString());
            std::vector<char*> pointers;
            for (auto& argument : arguments) pointers.push_back(argument.data());
            pointers.push_back(nullptr);
            ::execv(update.c_str(), pointers.data()); // Fall back to the base app if exec fails.
        }
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(UnBallManagerApp);
