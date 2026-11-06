#include "MainFrame.hpp"
#include "TestSupport.hpp"
#include <wx/app.h>
#include <clocale>
#include <wx/button.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/gauge.h>
#include <wx/checkbox.h>
#include <fstream>
#include <iterator>
#include <wx/dcscreen.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/notebook.h>
#include <wx/combobox.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/srchctrl.h>
#include <wx/stdpaths.h>
#include <wx/timer.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <unistd.h>

namespace {
int uiTestExitCode = 0;
std::string shellQuote(const std::string& value) {
    std::string result = "'";
    for (char c : value) result += c == '\'' ? "'\"'\"'" : std::string(1, c);
    return result + "'";
}
}

class UiTests final : public wxApp {
public:
    bool OnInit() override {
        std::setlocale(LC_CTYPE, "");
        wxInitAllImageHandlers();
        SetAppName("UnBallManagerUITest-" + std::to_string(::getpid()));
        appData_ = wxStandardPaths::Get().GetUserDataDir().ToStdString();
        require(!std::filesystem::exists(appData_));
        wxString batchTest;
        batchMode_ = wxGetEnv("UNBALL_UI_BATCH_TEST", &batchTest);
        updateMode_ = wxGetEnv("UNBALL_UI_UPDATER_TEST", &batchTest);
        const auto server = temporary_.path / "fake server";
        fakeServer_ = server;
        executable(server, "#!/bin/sh\ntrap 'printf finished; exit 0' INT TERM\nprintf 'ULG6\\n(team 0 left right 0 0)\\n(show 0 ((b) 0 0 0 0))\\n' > ../logs/ui.rcg\nprintf started\\n\nwhile :; do sleep 1; done\n");
        if (batchMode_) executable(server, "#!/bin/sh\nprintf 'ULG6\\n(team 0 left right 0 0)\\n(show 0 ((b) 0 0 0 0))\\n(playmode 0 time_over)\\n' > ../logs/ui.rcg\nprintf 'Game Over. Exiting...\\n'\n");
        wxString realServer, realStrategy;
        realMode_ = batchMode_ && wxGetEnv("UNBALL_UI_REAL_SERVER", &realServer) && wxGetEnv("UNBALL_UI_REAL_STRATEGY", &realStrategy);
        realStrategy_ = realStrategy.ToStdString();
        if (realMode_) executable(server, "#!/bin/sh\nexec " + shellQuote(realServer.ToStdString()) +
            " \"$@\" server::port=16000 server::coach_port=16001 server::olcoach_port=16002 server::half_time=3 server::nr_normal_halfs=2 server::nr_extra_halfs=0 server::penalty_shoot_outs=false server::game_over_wait=1 server::connect_wait=2 server::kick_off_wait=5\n");
        const auto monitor = temporary_.path / "fake monitor";
        executable(monitor, "#!/bin/sh\nprintf started > " + shellQuote((temporary_.path / "monitor-started").string()) + "\n");
        writeTextAtomically(appData_ / "environment.json", nlohmann::json{
            {"baseDirectory", temporary_.path.string()}, {"serverExecutable", server.string()},
            {"monitorExecutable", batchMode_ ? monitor.string() : ""}, {"autoMatchScript", ""}}.dump());
        TeamStore teams(appData_ / "teams.json");
        for (const auto* name : {"left", "right"}) {
            const auto root = temporary_.path / name;
            executable(root / "start.sh", realMode_ ? "#!/bin/sh\ncd " + shellQuote(realStrategy_) +
                "\nexec ./start.sh -p 16000 -P 16002 -t " + name + "\n" : "#!/bin/sh\nexit 0\n");
            teams.add({name, name, root, true});
        }
        writeTextAtomically(appData_ / "ui.json", nlohmann::json{
            {"leftRoot", (temporary_.path / "right").string()}, {"rightRoot", (temporary_.path / "left").string()}}.dump());
        frame_ = new MainFrame();
        frame_->Show();
        start_ = wxDynamicCast(wxWindow::FindWindowByLabel("Iniciar partida", frame_), wxButton);
        stop_ = wxDynamicCast(wxWindow::FindWindowByLabel(wxString::FromUTF8("Encerrar sessão"), frame_), wxButton);
        require(start_ && stop_);
        wxString capture;
        if (wxGetEnv("UNBALL_UI_SCREENSHOT_DIR", &capture)) {
            screenshots_ = capture.ToStdString();
            std::filesystem::create_directories(screenshots_);
        }
        timer_.SetOwner(this);
        Bind(wxEVT_TIMER, &UiTests::tick, this);
        deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(realMode_ ? 75 : 25);
        timer_.Start(200);
        return true;
    }
    int OnExit() override {
        if (checkCloseExport_) {
            try {
                const auto sessions = findDataSessions(temporary_.path);
                require(!sessions.empty() && sessions[0].status == "completed_with_warnings");
                require(sessions[0].exportDirectory != previousExport_);
            } catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; failed_ = true; }
        }
        std::error_code error;
        std::filesystem::remove_all(appData_, error);
        uiTestExitCode = failed_ ? 1 : 0;
        return uiTestExitCode;
    }
private:
    void capture(const std::string& name) {
        if (screenshots_.empty()) return;
        frame_->Refresh();
        frame_->Update();
        wxBitmap bitmap(frame_->GetClientSize());
        wxMemoryDC destination(bitmap);
        wxScreenDC source;
        const auto origin = frame_->ClientToScreen(wxPoint(0, 0));
        destination.Blit(0, 0, bitmap.GetWidth(), bitmap.GetHeight(), &source, origin.x, origin.y);
        destination.SelectObject(wxNullBitmap);
        require(bitmap.SaveFile((screenshots_ / name).string(), wxBITMAP_TYPE_PNG));
    }
    void press(wxButton* button) {
        wxCommandEvent event(wxEVT_BUTTON, button->GetId());
        event.SetEventObject(button);
        frame_->GetEventHandler()->ProcessEvent(event);
    }
    void tick(wxTimerEvent&) {
        try {
            require(std::chrono::steady_clock::now() < deadline_);
            if (batchMode_) { tickBatch(); return; }
            if (updateMode_) { tickUpdate(); return; }
            if (stage_ == 0) {
                require(start_->IsEnabled() && !stop_->IsEnabled());
                auto* update = wxDynamicCast(wxWindow::FindWindowByLabel(wxString::FromUTF8("Atualizar aplicação"), frame_), wxButton);
                require(update && update->IsEnabled());
                capture("900x620.png");
                frame_->SetSize(780, 540);
                ++stage_;
            } else if (stage_ == 1) {
                capture("780x540.png");
                require(start_->IsShownOnScreen());
                press(start_);
                require(!start_->IsEnabled() && stop_->IsEnabled());
                auto* update = wxDynamicCast(wxWindow::FindWindowByLabel(wxString::FromUTF8("Atualizar aplicação"), frame_), wxButton);
                require(update && !update->IsEnabled());
                // The handler itself rejects a second session, even if an event is injected.
                press(start_);
                require(findDataSessions(temporary_.path).size() == 1);
                ++stage_;
            } else if (stage_ == 2) {
                capture("em-execucao.png");
                press(stop_);
                require(!stop_->IsEnabled());
                ++stage_;
            } else if (stage_ == 3 && start_->IsEnabled()) {
                require(!stop_->IsEnabled());
                require(findReplays(temporary_.path).size() == 1);
                ++stage_;
            } else if (stage_ == 4) {
                const auto sessions = findDataSessions(temporary_.path);
                if (sessions.empty() || sessions[0].exportDirectory.empty() || sessions[0].status == "queued" || sessions[0].status == "extracting") return;
                require(sessions[0].status == "completed_with_warnings");
                auto* list = wxWindow::FindWindowByName(wxString::FromUTF8("Sessões e exportações de dados"), frame_);
                require(list);
                auto* notebook = dynamic_cast<wxNotebook*>(list->GetParent()->GetParent());
                require(notebook);
                notebook->SetSelection(1);
                require(notebook->GetSelection() == 1 && list->IsShownOnScreen());
                auto* search = dynamic_cast<wxSearchCtrl*>(wxWindow::FindWindowByName("Buscar partidas", frame_));
                auto* history = dynamic_cast<wxListCtrl*>(list);
                require(search && history);
                search->SetValue("nonexistent-team");
                require(history->GetItemCount() == 0);
                auto* reprocessHidden = wxDynamicCast(wxWindow::FindWindowByLabel("Reprocessar dados", frame_), wxButton);
                require(!reprocessHidden->IsEnabled());
                search->Clear();
                require(history->GetItemCount() == 1);
                auto* filter = dynamic_cast<wxChoice*>(wxWindow::FindWindowByName(wxString::FromUTF8("Filtrar histórico"), frame_));
                require(filter);
                filter->SetSelection(3);
                wxCommandEvent filterEvent(wxEVT_CHOICE, filter->GetId());
                filterEvent.SetEventObject(filter);
                filter->GetEventHandler()->ProcessEvent(filterEvent);
                require(history->GetItemCount() == 0);
                filter->SetSelection(0);
                filter->GetEventHandler()->ProcessEvent(filterEvent);
                require(history->GetItemCount() == 1);
                auto* reuse = wxDynamicCast(wxWindow::FindWindowByLabel("Usar estes times", frame_), wxButton);
                require(reuse && reuse->IsEnabled());
                press(reuse);
                require(notebook->GetSelection() == 0 && start_->IsEnabled());
                auto* left = dynamic_cast<wxComboBox*>(wxWindow::FindWindowByName("Time esquerdo", frame_));
                auto* right = dynamic_cast<wxComboBox*>(wxWindow::FindWindowByName("Time direito", frame_));
                require(left && right && left->GetSelection() == 1 && right->GetSelection() == 0);
                notebook->SetSelection(1);
                frame_->SetSize(900, 620);
                previousExport_ = sessions[0].exportDirectory;
                auto* reprocess = wxDynamicCast(wxWindow::FindWindowByLabel("Reprocessar dados", frame_), wxButton);
                require(reprocess && reprocess->IsEnabled());
                press(reprocess);
                require(!reprocess->IsEnabled());
                ++stage_;
            } else if (stage_ == 5) {
                const auto sessions = findDataSessions(temporary_.path);
                if (sessions.empty() || sessions[0].exportDirectory == previousExport_ || sessions[0].status == "queued" || sessions[0].status == "extracting") return;
                require(sessions[0].status == "completed_with_warnings");
                previousExport_ = sessions[0].exportDirectory;
                ++stage_;
            } else if (stage_ == 6) {
                if (++settleTicks_ < 3) return;
                capture("historico.png");
                auto* reprocess = wxDynamicCast(wxWindow::FindWindowByLabel("Reprocessar dados", frame_), wxButton);
                press(reprocess);
                checkCloseExport_ = true;
                timer_.Stop();
                std::cout << "GUI: exportação automática, reprocessamento e espera ao fechar verificados.\n";
                frame_->Close();
            }
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            failed_ = true;
            timer_.Stop();
            frame_->Destroy();
        }
    }
    void tickUpdate() {
        auto* gauge = dynamic_cast<wxGauge*>(wxWindow::FindWindowByName(wxString::FromUTF8("Progresso da atualização"), frame_));
        auto* state = dynamic_cast<wxStaticText*>(wxWindow::FindWindowByName(wxString::FromUTF8("Estado da atualização"), frame_));
        require(gauge && state);
        if (stage_ == 0) {
            auto* update = wxDynamicCast(wxWindow::FindWindowByLabel(wxString::FromUTF8("Atualizar aplicação"), frame_), wxButton);
            require(update && update->IsEnabled());
            auto* notebook = dynamic_cast<wxNotebook*>(state->GetParent()->GetParent());
            require(notebook);
            notebook->SetSelection(5);
            press(update);
            require(!start_->IsEnabled());
            ++stage_;
        } else if (stage_ == 1 && start_->IsEnabled()) {
            require(gauge->GetValue() == 100);
            require(state->GetLabel().Contains(wxString::FromUTF8("já está atualizada")));
            capture("atualizacao.png");
            timer_.Stop();
            std::cout << "GUI: botão de atualização consultou o GitHub e confirmou a versão atual.\n";
            frame_->Close();
        }
    }
    nlohmann::json read(const std::filesystem::path& path) {
        std::ifstream input(path); return nlohmann::json::parse(input);
    }
    void tickBatch() {
        auto* count = dynamic_cast<wxSpinCtrl*>(wxWindow::FindWindowByName("Quantidade de jogos", frame_));
        auto* gauge = dynamic_cast<wxGauge*>(wxWindow::FindWindowByName("Progresso do lote", frame_));
        require(count && gauge);
        if (stage_ == 0) {
            auto* monitor = dynamic_cast<wxCheckBox*>(wxWindow::FindWindowByLabel("Abrir monitor em cada jogo", frame_));
            require(monitor);
            monitor->SetValue(false);
            count->SetValue(3);
            press(start_);
            require(!start_->IsEnabled() && !count->IsEnabled());
            press(start_); // A second event must not replace a running batch.
            ++stage_;
        } else if (stage_ == 1 && start_->IsEnabled()) {
            const auto sessions = findDataSessions(temporary_.path);
            require(sessions.size() == 3);
            require(gauge->GetRange() == 3 && gauge->GetValue() == 3);
            std::string batchId;
            for (const auto& session : sessions) {
                require(!session.exportDirectory.empty());
                require(session.batchTotal == 3 && session.batchIndex >= 1 && session.batchIndex <= 3);
                if (batchId.empty()) batchId = session.batchId;
                require(session.batchId == batchId);
                if (realMode_) {
                    std::ifstream console(sessionRuntimeDirectory(session.directory) / "console.log");
                    const std::string output((std::istreambuf_iterator<char>(console)), {});
                    int players = 0;
                    for (std::size_t position = 0; (position = output.find(" player (", position)) != std::string::npos; ++position) ++players;
                    require(players == 22);
                }
            }
            const auto metadata = read(sessions[0].directory / "match.json");
            const auto summary = read(std::filesystem::path(metadata["batch_directory"].get<std::string>()) / "lote.json");
            require(summary["settled"] == 3 && summary["status"] == "completed");
            require(!std::filesystem::exists(temporary_.path / "monitor-started"));
            // Next batch is cancelled while its first game is active; pending games must never start.
            executable(fakeServer_, "#!/bin/sh\ntrap 'exit 0' INT TERM\nprintf 'ULG6\\n(team 0 left right 0 0)\\n(show 0 ((b) 0 0 0 0))\\n' > ../logs/ui.rcg\nwhile :; do sleep 1; done\n");
            count->SetValue(5);
            press(start_);
            require(!start_->IsEnabled());
            ++stage_;
        } else if (stage_ == 2) {
            press(stop_);
            ++stage_;
        } else if (stage_ == 3 && start_->IsEnabled()) {
            const auto sessions = findDataSessions(temporary_.path);
            require(sessions.size() == 4 && gauge->GetValue() == 1);
            const auto metadata = read(sessions[0].directory / "match.json");
            const auto summary = read(std::filesystem::path(metadata["batch_directory"].get<std::string>()) / "lote.json");
            require(summary["status"] == "cancelled" && summary["started"] == 1 && summary["total"] == 5);
            timer_.Stop();
            std::cout << "GUI: lote de três jogos com CSVs e cancelamento sem iniciar pendentes verificados.\n";
            frame_->Close();
        }
    }
    TemporaryDirectory temporary_;
    std::filesystem::path appData_, screenshots_, previousExport_;
    MainFrame* frame_{};
    wxButton* start_{};
    wxButton* stop_{};
    wxTimer timer_;
    std::chrono::steady_clock::time_point deadline_;
    int stage_{};
    int settleTicks_{};
    bool failed_{};
    bool updateMode_{};
    bool batchMode_{};
    bool realMode_{};
    std::string realStrategy_;
    std::filesystem::path fakeServer_;
    bool checkCloseExport_{};
};

wxIMPLEMENT_APP_NO_MAIN(UiTests);

int main(int argc, char** argv) {
    const int result = wxEntry(argc, argv);
    return uiTestExitCode != 0 ? uiTestExitCode : result;
}
