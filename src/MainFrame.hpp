#pragma once

#include "TeamStore.hpp"

#include <wx/frame.h>
#include <wx/process.h>

#include <filesystem>
#include <memory>
#include <vector>
#include <wx/timer.h>

class wxOwnerDrawnComboBox;
class wxListCtrl;
class wxStaticText;
class wxStatusBar;
class wxTextCtrl;
class wxCheckBox;

class MainFrame final : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

private:
    void buildUi();
    void refreshTeams();
    void onAddTeam(wxCommandEvent& event);
    void onImportTeams(wxCommandEvent& event);
    void onValidateTeam(wxCommandEvent& event);
    void onValidateAllTeams(wxCommandEvent& event);
    void onRemoveTeam(wxCommandEvent& event);
    void onPrepareMatch(wxCommandEvent& event);
    void onSaveEnvironment(wxCommandEvent& event);
    void onChooseBaseDirectory(wxCommandEvent& event);
    void onChooseServer(wxCommandEvent& event);
    void onChooseMonitor(wxCommandEvent& event);
    void onChooseAutoMatch(wxCommandEvent& event);
    void onRefreshReplays(wxCommandEvent& event);
    void onWatchSelectedReplay(wxCommandEvent& event);
    void onWatchManualReplay(wxCommandEvent& event);
    void onStartServer(wxCommandEvent& event);
    void onStopAll(wxCommandEvent& event);
    void onProcessTerminated(wxProcessEvent& event);
    void onPollProcessOutput(wxTimerEvent& event);
    std::filesystem::path createRunDirectory() const;
    void startMonitorIfAvailable();
    void stopManagedProcesses();
    void appendEvent(const wxString& text);
    void report(const wxString& text);
    void loadEnvironment();
    void saveEnvironment() const;
    void updateEnvironmentStatus();
    void refreshReplays();

    std::filesystem::path appData_;
    TeamStore store_;
    std::filesystem::path baseDirectory_;
    std::filesystem::path serverExecutable_;
    std::filesystem::path monitorExecutable_;
    std::filesystem::path autoMatchScript_;
    wxListCtrl* teamList_{};
    wxOwnerDrawnComboBox* leftTeam_{};
    wxOwnerDrawnComboBox* rightTeam_{};
    wxCheckBox* coachModeCheck_{};
    wxTextCtrl* events_{};
    wxStaticText* environment_{};
    wxStatusBar* status_{};
    wxTextCtrl* baseDirectoryField_{};
    wxTextCtrl* serverField_{};
    wxTextCtrl* monitorField_{};
    wxTextCtrl* autoMatchField_{};
    wxListCtrl* replayList_{};
    std::vector<std::filesystem::path> currentReplays_;
    std::vector<std::unique_ptr<wxProcess>> processes_;
    wxTimer outputTimer_;
};
