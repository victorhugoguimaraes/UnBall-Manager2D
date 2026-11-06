#pragma once

#include "TeamStore.hpp"
#include "ChildProcess.hpp"
#include "Session.hpp"
#include "DataExport.hpp"
#include "BatchRun.hpp"
#include "AppUpdate.hpp"

#include <wx/frame.h>

#include <filesystem>
#include <memory>
#include <vector>
#include <fstream>
#include <wx/timer.h>

class wxComboBox;
class wxListCtrl;
class wxStaticText;
class wxStatusBar;
class wxTextCtrl;
class wxCheckBox;
class wxNotebook;
class wxSearchCtrl;
class wxChoice;
class wxSpinCtrl;
class wxGauge;

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
    void onClose(wxCloseEvent& event);
    void onOpenSessionDirectory(wxCommandEvent& event);
    void onPollProcessOutput(wxTimerEvent& event);
    void startMonitorIfAvailable();
    void stopManagedProcesses();
    void appendEvent(const wxString& text);
    void report(const wxString& text);
    void loadEnvironment();
    void updateEnvironmentStatus();
    void refreshReplays();
    void refreshDataSessions();
    void filterDataSessions();
    void updateHistoryDetail();
    void onHistoryReplay(wxCommandEvent& event);
    void onHistoryFolder(wxCommandEvent& event);
    void onReuseTeams(wxCommandEvent& event);
    void rememberMatch();
    void restoreMatch();
    void onReprocessData(wxCommandEvent& event);
    void onOpenData(wxCommandEvent& event);
    void onDataProblems(wxCommandEvent& event);
    void markSessionEnded(bool stopRequested);
    enum class ProcessRole { Session, Monitor, Replay };
    struct ProcessEntry {
        std::unique_ptr<ChildProcess> process;
        std::string pendingOutput;
        ProcessRole role;
    };
    long launchProcess(const std::vector<std::string>& arguments, ProcessRole role,
                       const std::filesystem::path& directory = {},
                       const std::map<std::string, std::string>& environment = {});
    bool launchSession(const Team* left = nullptr, const Team* right = nullptr);
    void watchReplay(const std::filesystem::path& path);
    void updateSessionState();
    bool batchBusy() const noexcept;
    void advanceBatch();
    void updateBatchProgress();
    void onUpdateApplication(wxCommandEvent& event);
    void onRestartApplication(wxCommandEvent& event);
    void refreshUpdateState();
    void persistBatchFailure(const std::string& reason);

    std::filesystem::path appData_;
    TeamStore store_;
    std::filesystem::path baseDirectory_;
    std::filesystem::path serverExecutable_;
    std::filesystem::path monitorExecutable_;
    std::filesystem::path autoMatchScript_;
    wxListCtrl* teamList_{};
    wxComboBox* leftTeam_{};
    wxComboBox* rightTeam_{};
    wxCheckBox* coachModeCheck_{};
    wxCheckBox* showMonitor_{};
    wxSpinCtrl* gameCount_{};
    wxSpinCtrl* gameTimeout_{};
    wxGauge* batchGauge_{};
    wxStaticText* batchText_{};
    wxString batchCaption_;
    std::unique_ptr<BatchRun> batch_;
    std::chrono::steady_clock::time_point gameStarted_, batchStarted_;
    bool gameTimedOut_ = false, gameEndedNormally_ = false;
    std::string endEvidence_;
    wxTextCtrl* events_{};
    wxStaticText* environment_{};
    wxStatusBar* status_{};
    wxTextCtrl* baseDirectoryField_{};
    wxTextCtrl* serverField_{};
    wxTextCtrl* monitorField_{};
    wxTextCtrl* autoMatchField_{};
    wxListCtrl* replayList_{};
    wxListCtrl* dataList_{};
    wxNotebook* notebook_{};
    wxSearchCtrl* historySearch_{};
    wxChoice* historyFilter_{};
    wxStaticText* historyDetail_{};
    std::vector<DataSession> allDataSessions_;
    std::vector<DataSession> dataSessions_;
    std::unique_ptr<DataExport> exporter_;
    std::unique_ptr<AppUpdate> updater_;
    wxStaticText* updateText_{};
    wxGauge* updateGauge_{};
    std::filesystem::path restartTarget_;
    bool stopRequested_{};
    std::vector<std::filesystem::path> currentReplays_;
    std::vector<ProcessEntry> processes_;
    SessionFiles activeSession_;
    std::ofstream sessionLog_;
    long sessionPid_{};
    bool stopping_{};
    bool closing_{};
    wxStaticText* sessionState_{};
    wxString environmentWarning_;
    wxTimer outputTimer_;
};
