#include "MainFrame.hpp"
#include "Branding.hpp"
#include <wx/statbmp.h>
#include <wx/scrolwin.h>

#include <wx/button.h>
#include <wx/accel.h>
#include <wx/spinctrl.h>
#include <wx/gauge.h>
#include <wx/srchctrl.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/wrapsizer.h>
#include "FileIO.hpp"
#include <unistd.h>
#include <wx/dirdlg.h>
#include <wx/datetime.h>
#include <wx/dialog.h>
#include <wx/dirctrl.h>
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>
#include <wx/stattext.h>
#include <wx/stream.h>
#include <wx/statusbr.h>
#include <wx/textdlg.h>
#include <wx/textctrl.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>
#include <wx/checkbox.h>

#include <chrono>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace {
enum class ControlId : int {
    StartServer = wxID_HIGHEST + 10,
    StopAll,
    SaveEnvironment,
    AddTeam = wxID_HIGHEST + 20,
    ImportTeams,
    RemoveTeam,
    ValidateTeam,
    ValidateAllTeams,
    PrepareMatch = wxID_HIGHEST + 30,
    StopMatch,
    OpenSessionDirectory,
    ChooseBaseDirectory = wxID_HIGHEST + 40,
    ChooseServer,
    ChooseMonitor,
    ChooseAutoMatch,
    RefreshReplays = wxID_HIGHEST + 50,
    WatchSelectedReplay,
    WatchManualReplay,
    RefreshData = wxID_HIGHEST + 60,
    ReprocessData,
    OpenData,
    DataProblems,
    HistoryReplay,
    HistoryFolder,
    ReuseTeams,
    GoHistory,
    GoEvents,
    OpenBatchDirectory,
    UpdateApplication,
    RestartApplication,
    UpdateLog,
};

constexpr int controlId(ControlId id) { return static_cast<int>(id); }

wxString pathString(const std::filesystem::path& path) { return wxString::FromUTF8(path.string()); }

wxString teamLabel(const Team& team) {
    return wxString::FromUTF8(team.displayName + " (" + team.teamName + ")");
}

wxString findCommandOnPath(const wxString& command) {
    wxString path;
    if (!wxGetEnv("PATH", &path)) return {};

    wxStringTokenizer directories(path, wxString(wxPATH_SEP));
    while (directories.HasMoreTokens()) {
        wxFileName candidate(directories.GetNextToken(), command);
        if (candidate.FileExists()) return candidate.GetFullPath();
    }
    return {};
}

std::filesystem::path defaultAutoMatchPath() {
    const auto home = std::filesystem::path(wxGetHomeDir().ToStdString());
    const std::vector<std::filesystem::path> candidates{
        home / "rcsssetup" / "auto_match.py",
        home / "rcss" / "rcsssetup" / "auto_match.py",
        home / "rcss" / "auto_match.py"
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    return {};
}

wxButton* addButton(wxWindow* parent, wxSizer* sizer, const wxString& label, ControlId id) {
    auto* button = new wxButton(parent, controlId(id), label);
    sizer->Add(button, 0, wxALL, 5);
    return button;
}

// Dialogos genericos permanecem moveis no WSL, ao contrario de alguns seletores nativos.
wxString chooseDirectory(wxWindow* parent, const wxString& defaultPath, const wxString& title) {
    wxDialog dialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(640, 460),
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* directories = new wxGenericDirCtrl(&dialog, wxID_ANY, defaultPath,
        wxDefaultPosition, wxDefaultSize, wxDIRCTRL_DIR_ONLY);
    root->Add(directories, 1, wxEXPAND | wxALL, 8);
    root->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 8);
    dialog.SetSizerAndFit(root);
    dialog.CenterOnParent();
    if (dialog.ShowModal() == wxID_OK) return directories->GetPath();
    return {};
}

wxString chooseFile(wxWindow* parent, const wxString& defaultPath, const wxString& title, const wxString& wildcard) {
    wxFileDialog dialog(parent, title, wxEmptyString, defaultPath, wildcard,
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    dialog.CenterOnParent();
    if (dialog.ShowModal() == wxID_OK) return dialog.GetPath();
    return {};
}
}

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "UnBall 2D Manager", wxDefaultPosition, wxSize(900, 620)),
      appData_(std::filesystem::path(wxStandardPaths::Get().GetUserDataDir().ToStdString())),
      store_(appData_ / "teams.json"),
      outputTimer_(this) {
    try { store_.load(); }
    catch (const std::exception& error) {
        wxMessageBox("Nao foi possivel ler os times: " + wxString::FromUTF8(error.what()), "UnBall 2D Manager", wxICON_WARNING);
    }
    const auto executableDirectory = std::filesystem::path(wxStandardPaths::Get().GetExecutablePath().ToStdString()).parent_path();
    auto extractorDirectory = executableDirectory / "extractor";
    if (!std::filesystem::is_directory(extractorDirectory / "unball_extract"))
        extractorDirectory = executableDirectory.parent_path() / "share/unball-manager/extractor";
    exporter_ = std::make_unique<DataExport>("python3", extractorDirectory);
    auto updaterScript = executableDirectory / "updater/unball_update.py";
    if (!std::filesystem::is_regular_file(updaterScript)) updaterScript = executableDirectory.parent_path() / "share/unball-manager/updater/unball_update.py";
    updater_ = std::make_unique<AppUpdate>(appData_, updaterScript);
    restartTarget_ = installedUpdate(appData_);
    if (!restartTarget_.empty() && std::filesystem::weakly_canonical(restartTarget_) == std::filesystem::weakly_canonical(executableDirectory / "UnBall-Manager2D")) restartTarget_.clear();
    setUnballWindowIcon(*this);
    loadEnvironment();
    buildUi();
    refreshTeams();
    restoreMatch();
    refreshReplays();
    refreshDataSessions();
    if (!environmentWarning_.empty()) report(environmentWarning_);
}

MainFrame::~MainFrame() {
    outputTimer_.Stop();
}

void MainFrame::buildUi() {
    SetMinSize(FromDIP(wxSize(780, 540)));
    auto* notebook = new wxNotebook(this, wxID_ANY);
    notebook_ = notebook;

    auto* configuration = new wxScrolledWindow(notebook);
    configuration->SetScrollRate(0, FromDIP(10));
    auto* configurationSizer = new wxFlexGridSizer(3, 8, 8);
    configurationSizer->Add(new wxStaticText(configuration, wxID_ANY, "Pasta base do RoboCup:"), 0, wxALIGN_CENTER_VERTICAL);
    baseDirectoryField_ = new wxTextCtrl(configuration, wxID_ANY, pathString(baseDirectory_));
    baseDirectoryField_->SetName("Pasta base do RoboCup");
    configurationSizer->Add(baseDirectoryField_, 1, wxEXPAND);
    addButton(configuration, configurationSizer, "Procurar...", ControlId::ChooseBaseDirectory);
    configurationSizer->Add(new wxStaticText(configuration, wxID_ANY, "rcssserver:"), 0, wxALIGN_CENTER_VERTICAL);
    serverField_ = new wxTextCtrl(configuration, wxID_ANY, pathString(serverExecutable_));
    serverField_->SetName("Executável do servidor");
    configurationSizer->Add(serverField_, 1, wxEXPAND);
    addButton(configuration, configurationSizer, "Procurar...", ControlId::ChooseServer);
    configurationSizer->Add(new wxStaticText(configuration, wxID_ANY, "rcssmonitor (opcional):"), 0, wxALIGN_CENTER_VERTICAL);
    monitorField_ = new wxTextCtrl(configuration, wxID_ANY, pathString(monitorExecutable_));
    monitorField_->SetName("Executável do monitor");
    configurationSizer->Add(monitorField_, 1, wxEXPAND);
    addButton(configuration, configurationSizer, "Procurar...", ControlId::ChooseMonitor);
    configurationSizer->Add(new wxStaticText(configuration, wxID_ANY, "auto_match.py (legado, opcional):"), 0, wxALIGN_CENTER_VERTICAL);
    autoMatchField_ = new wxTextCtrl(configuration, wxID_ANY, pathString(autoMatchScript_));
    autoMatchField_->SetName("Auto match opcional");
    configurationSizer->Add(autoMatchField_, 1, wxEXPAND);
    addButton(configuration, configurationSizer, "Procurar...", ControlId::ChooseAutoMatch);
    configurationSizer->AddGrowableCol(1, 1);
    auto* configurationOuter = new wxBoxSizer(wxVERTICAL);
    configurationOuter->Add(new wxStaticText(configuration, wxID_ANY,
        "Escolha os caminhos da sua instalação. O UnBall 2D Manager não instala nem altera essas ferramentas."), 0, wxALL, 12);
    configurationOuter->Add(configurationSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    addButton(configuration, configurationOuter, "Salvar configuração", ControlId::SaveEnvironment);
    environment_ = new wxStaticText(configuration, wxID_ANY, "Verificando ambiente...");
    configurationOuter->Add(environment_, 0, wxALL, 12);
    addButton(configuration, configurationOuter, "Iniciar somente servidor", ControlId::StartServer);
    configurationOuter->Add(new wxStaticText(configuration, wxID_ANY,
        "Versão instalada: " + wxString::FromUTF8(applicationRevision().substr(0, 12))), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    updateText_ = new wxStaticText(configuration, wxID_ANY, wxString::FromUTF8(updater_->message()));
    updateText_->SetName("Estado da atualização");
    updateText_->Wrap(FromDIP(650));
    configurationOuter->Add(updateText_, 0, wxEXPAND | wxALL, 12);
    updateGauge_ = new wxGauge(configuration, wxID_ANY, 100);
    updateGauge_->SetName("Progresso da atualização");
    configurationOuter->Add(updateGauge_, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    auto* updateActions = new wxWrapSizer(wxHORIZONTAL);
    addButton(configuration, updateActions, "Atualizar aplicação", ControlId::UpdateApplication);
    addButton(configuration, updateActions, "Reiniciar agora", ControlId::RestartApplication);
    addButton(configuration, updateActions, "Ver log da atualização", ControlId::UpdateLog);
    configurationOuter->Add(updateActions, 0, wxEXPAND | wxALL, 7);
    Bind(wxEVT_BUTTON, &MainFrame::onUpdateApplication, this, controlId(ControlId::UpdateApplication));
    Bind(wxEVT_BUTTON, &MainFrame::onRestartApplication, this, controlId(ControlId::RestartApplication));
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (!wxLaunchDefaultApplication(pathString(updater_->log()))) report("Não foi possível abrir o log da atualização."); }, controlId(ControlId::UpdateLog));
    configuration->SetSizer(configurationOuter);
    notebook->AddPage(configuration, "Configuração");

    auto* teams = new wxPanel(notebook);
    auto* teamsSizer = new wxBoxSizer(wxVERTICAL);
    teamList_ = new wxListCtrl(teams, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    teamList_->AppendColumn("Time", wxLIST_FORMAT_LEFT, 180);
    teamList_->AppendColumn("Nome no servidor", wxLIST_FORMAT_LEFT, 150);
    teamList_->AppendColumn("Inicialização", wxLIST_FORMAT_LEFT, 100);
    teamList_->AppendColumn("Pasta", wxLIST_FORMAT_LEFT, 440);
    teamsSizer->Add(teamList_, 1, wxEXPAND | wxALL, 8);
    auto* teamActions = new wxWrapSizer(wxHORIZONTAL);
    addButton(teams, teamActions, "Adicionar time", ControlId::AddTeam);
    addButton(teams, teamActions, "Importar da pasta 'teams'", ControlId::ImportTeams);
    addButton(teams, teamActions, "Validar selecionado", ControlId::ValidateTeam);
    addButton(teams, teamActions, "Validar todos", ControlId::ValidateAllTeams);
    addButton(teams, teamActions, "Remover selecionado", ControlId::RemoveTeam);
    teamsSizer->Add(teamActions, 0, wxEXPAND | wxLEFT | wxBOTTOM, 3);
    teams->SetSizer(teamsSizer);
    notebook->AddPage(teams, "Times");

    auto* match = new wxPanel(notebook);
    auto* matchSizer = new wxFlexGridSizer(2, 8, 8);
    matchSizer->Add(new wxStaticText(match, wxID_ANY, "Time esquerdo:"), 0, wxALIGN_CENTER_VERTICAL);
    leftTeam_ = new wxComboBox(match, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY); matchSizer->Add(leftTeam_, 1, wxEXPAND);
    matchSizer->Add(new wxStaticText(match, wxID_ANY, "Time direito:"), 0, wxALIGN_CENTER_VERTICAL);
    rightTeam_ = new wxComboBox(match, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY); matchSizer->Add(rightTeam_, 1, wxEXPAND);
    matchSizer->AddGrowableCol(1, 1);
    auto* matchOuter = new wxBoxSizer(wxVERTICAL);
    sessionState_ = new wxStaticText(match, wxID_ANY, "Escolha os times para iniciar uma partida.");
    auto* identity = new wxBoxSizer(wxHORIZONTAL);
    auto* mark = new wxStaticBitmap(match, wxID_ANY, unballLogo());
    mark->SetName("Logo da UnBall");
    mark->SetToolTip("UnBall, Universidade de Brasília");
    identity->Add(mark, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    auto* identityText = new wxBoxSizer(wxVERTICAL);
    auto* title = new wxStaticText(match, wxID_ANY, "UnBall 2D Manager");
    auto font = title->GetFont();
    font.SetWeight(wxFONTWEIGHT_BOLD);
    font.SetPointSize(font.GetPointSize() + 2);
    title->SetFont(font);
    identityText->Add(title, 0, wxBOTTOM, 3);
    identityText->Add(sessionState_, 0);
    identity->Add(identityText, 1, wxALIGN_CENTER_VERTICAL);
    matchOuter->Add(identity, 0, wxEXPAND | wxALL, 8);
    sessionState_->SetName("Estado da sessão");
    leftTeam_->SetName("Time esquerdo");
    rightTeam_->SetName("Time direito");
    matchOuter->Add(matchSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    
    coachModeCheck_ = new wxCheckBox(match, wxID_ANY, "Permitir mover bola e jogadores no monitor (modo Coach)");
    matchOuter->Add(coachModeCheck_, 0, wxALL, 8);
    
    auto* batchOptions = new wxWrapSizer(wxHORIZONTAL);
    batchOptions->Add(new wxStaticText(match, wxID_ANY, "Quantidade de jogos:"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    gameCount_ = new wxSpinCtrl(match, wxID_ANY, "1", wxDefaultPosition, FromDIP(wxSize(85, -1)), wxSP_ARROW_KEYS, 1, 1000, 1);
    gameCount_->SetName("Quantidade de jogos");
    batchOptions->Add(gameCount_, 0, wxALL, 4);
    batchOptions->Add(new wxStaticText(match, wxID_ANY, "Limite por jogo (min):"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    gameTimeout_ = new wxSpinCtrl(match, wxID_ANY, "30", wxDefaultPosition, FromDIP(wxSize(85, -1)), wxSP_ARROW_KEYS, 1, 240, 30);
    gameTimeout_->SetName("Tempo limite por jogo");
    gameTimeout_->SetToolTip("Tempo máximo de uma partida em lote. Ao exceder, salva os logs e segue para a próxima.");
    batchOptions->Add(gameTimeout_, 0, wxALL, 4);
    matchOuter->Add(batchOptions, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    showMonitor_ = new wxCheckBox(match, wxID_ANY, "Abrir monitor em cada jogo");
    showMonitor_->SetValue(true);
    matchOuter->Add(showMonitor_, 0, wxALL, 8);
    batchGauge_ = new wxGauge(match, wxID_ANY, 1);
    batchGauge_->SetName("Progresso do lote");
    matchOuter->Add(batchGauge_, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    batchText_ = new wxStaticText(match, wxID_ANY, "Escolha mais de um jogo para executar uma fila automática.");
    batchText_->SetName("Estado do lote");
    matchOuter->Add(batchText_, 0, wxEXPAND | wxALL, 8);
    gameCount_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { updateSessionState(); });

    auto* matchBtns = new wxWrapSizer(wxHORIZONTAL);
    auto* startMatch = addButton(match, matchBtns, "Iniciar partida", ControlId::PrepareMatch);
    startMatch->SetToolTip("Executar a quantidade de jogos escolhida (Ctrl+Enter)");
    addButton(match, matchBtns, "Encerrar sessão", ControlId::StopMatch)->SetToolTip("Em um lote, encerra o jogo atual, preserva seus dados e cancela os seguintes.");
    addButton(match, matchBtns, "Abrir pasta da sessão", ControlId::OpenSessionDirectory);
    addButton(match, matchBtns, "Abrir pasta do lote", ControlId::OpenBatchDirectory);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (batch_ && !wxLaunchDefaultApplication(pathString(batch_->directory()))) report("Não foi possível abrir a pasta do lote.");
    }, controlId(ControlId::OpenBatchDirectory));
    matchOuter->Add(matchBtns, 0, wxEXPAND | wxLEFT | wxRIGHT, 9);
    
    match->SetSizer(matchOuter);
    notebook->AddPage(match, "Nova partida");

    auto* monitor = new wxPanel(notebook);
    auto* monitorSizer = new wxBoxSizer(wxVERTICAL);
    events_ = new wxTextCtrl(monitor, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
    monitorSizer->Add(events_, 1, wxEXPAND | wxALL, 8);
    monitor->SetSizer(monitorSizer);
    notebook->AddPage(monitor, "Eventos");

    auto* replays = new wxPanel(notebook);
    auto* replaysSizer = new wxBoxSizer(wxVERTICAL);
    replayList_ = new wxListCtrl(replays, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    replayList_->AppendColumn("Data / Hora", wxLIST_FORMAT_LEFT, 160);
    replayList_->AppendColumn("Arquivo de Log", wxLIST_FORMAT_LEFT, 500);
    replaysSizer->Add(replayList_, 1, wxEXPAND | wxALL, 8);
    auto* replayActions = new wxWrapSizer(wxHORIZONTAL);
    addButton(replays, replayActions, "Atualizar lista", ControlId::RefreshReplays);
    addButton(replays, replayActions, "Assistir selecionado", ControlId::WatchSelectedReplay);
    addButton(replays, replayActions, "Procurar log manualmente...", ControlId::WatchManualReplay);
    replaysSizer->Add(replayActions, 0, wxEXPAND | wxLEFT | wxBOTTOM, 3);
    replays->SetSizer(replaysSizer);
    notebook->AddPage(replays, "Replays");

    auto* data = new wxPanel(notebook);
    auto* dataSizer = new wxBoxSizer(wxVERTICAL);
    auto* dataHelp = new wxStaticText(data, wxID_ANY,
        "Encontre uma partida para abrir os dados, assistir ao replay ou usar os mesmos times.");
    dataHelp->Wrap(FromDIP(700));
    dataSizer->Add(dataHelp, 0, wxALL, 10);
    auto* filters = new wxBoxSizer(wxHORIZONTAL);
    historySearch_ = new wxSearchCtrl(data, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(320, -1)));
    historySearch_->SetMinSize(FromDIP(wxSize(220, -1)));
    historySearch_->SetName("Buscar partidas");
    historySearch_->SetDescriptiveText("Buscar por time, data ou identificador");
    historySearch_->ShowCancelButton(false);
    filters->Add(historySearch_, 1, wxEXPAND | wxRIGHT, 8);
    historyFilter_ = new wxChoice(data, wxID_ANY);
    historyFilter_->SetName("Filtrar histórico");
    for (const auto* label : {"Todas", "Com CSVs", "Avisos ou falhas", "Sem CSVs"}) historyFilter_->Append(wxString::FromUTF8(label));
    historyFilter_->SetSelection(0);
    filters->Add(historyFilter_, 0, wxALIGN_CENTER_VERTICAL);
    dataSizer->Add(filters, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    dataList_ = new wxListCtrl(data, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    dataList_->SetName("Sessões e exportações de dados");
    dataList_->AppendColumn("Data / hora", wxLIST_FORMAT_LEFT, 155);
    dataList_->AppendColumn("Confronto", wxLIST_FORMAT_LEFT, 230);
    dataList_->AppendColumn("Extração", wxLIST_FORMAT_LEFT, 180);
    dataList_->AppendColumn("Cobertura", wxLIST_FORMAT_LEFT, 120);
    historyDetail_ = new wxStaticText(data, wxID_ANY, "Inicie uma partida para criar seu histórico.");
    historyDetail_->SetMinSize(FromDIP(wxSize(-1, 42)));
    dataSizer->Add(dataList_, 1, wxEXPAND | wxALL, 8);
    dataSizer->Add(historyDetail_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    auto* dataActions = new wxWrapSizer(wxHORIZONTAL);

    addButton(data, dataActions, "Abrir CSVs", ControlId::OpenData);
    addButton(data, dataActions, "Assistir replay", ControlId::HistoryReplay);
    addButton(data, dataActions, "Usar estes times", ControlId::ReuseTeams);
    addButton(data, dataActions, "Abrir pasta", ControlId::HistoryFolder);
    addButton(data, dataActions, "Reprocessar dados", ControlId::ReprocessData);
    addButton(data, dataActions, "Ver problemas", ControlId::DataProblems);
    addButton(data, dataActions, "Atualizar (F5)", ControlId::RefreshData);
    dataSizer->Add(dataActions, 0, wxEXPAND | wxALL, 4);
    data->SetSizer(dataSizer);
    notebook->AddPage(data, "Histórico");
    while (notebook->GetPageCount() > 0) notebook->RemovePage(0);
    notebook->AddPage(match, "Partida");
    notebook->AddPage(data, "Histórico");
    notebook->AddPage(teams, "Times");
    notebook->AddPage(replays, "Replays antigos");
    notebook->AddPage(monitor, "Eventos");
    notebook->AddPage(configuration, "Configuração");
    historySearch_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { historySearch_->ShowCancelButton(!historySearch_->GetValue().empty()); filterDataSessions(); });
    historySearch_->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent&) { historySearch_->Clear(); });
    historyFilter_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { filterDataSessions(); });
    dataList_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) {
        wxCommandEvent event;
        const long selected = dataList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (selected >= 0 && static_cast<std::size_t>(selected) < dataSessions_.size() && !dataSessions_[selected].exportDirectory.empty()) onOpenData(event);
        else onHistoryReplay(event);
    });
    Bind(wxEVT_BUTTON, &MainFrame::onHistoryReplay, this, controlId(ControlId::HistoryReplay));
    Bind(wxEVT_BUTTON, &MainFrame::onHistoryFolder, this, controlId(ControlId::HistoryFolder));
    Bind(wxEVT_BUTTON, &MainFrame::onReuseTeams, this, controlId(ControlId::ReuseTeams));
    Bind(wxEVT_MENU, &MainFrame::onPrepareMatch, this, controlId(ControlId::PrepareMatch));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { notebook_->SetSelection(1); historySearch_->SetFocus(); }, controlId(ControlId::GoHistory));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { notebook_->SetSelection(4); }, controlId(ControlId::GoEvents));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { refreshDataSessions(); refreshReplays(); }, controlId(ControlId::RefreshData));
    const wxAcceleratorEntry shortcuts[]{
        {wxACCEL_CTRL, WXK_RETURN, controlId(ControlId::PrepareMatch)},
        {wxACCEL_CTRL, 'H', controlId(ControlId::GoHistory)},
        {wxACCEL_CTRL, 'L', controlId(ControlId::GoEvents)},
        {wxACCEL_NORMAL, WXK_F5, controlId(ControlId::RefreshData)}
    };
    SetAcceleratorTable(wxAcceleratorTable(4, shortcuts));
    dataList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { updateSessionState(); updateHistoryDetail(); });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refreshDataSessions(); }, controlId(ControlId::RefreshData));
    Bind(wxEVT_BUTTON, &MainFrame::onReprocessData, this, controlId(ControlId::ReprocessData));
    Bind(wxEVT_BUTTON, &MainFrame::onOpenData, this, controlId(ControlId::OpenData));
    Bind(wxEVT_BUTTON, &MainFrame::onDataProblems, this, controlId(ControlId::DataProblems));

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(notebook, 1, wxEXPAND);
    SetSizer(root);
    status_ = CreateStatusBar(1);

    Bind(wxEVT_BUTTON, &MainFrame::onStartServer, this, controlId(ControlId::StartServer));
    Bind(wxEVT_BUTTON, &MainFrame::onStopAll, this, controlId(ControlId::StopAll));
    Bind(wxEVT_BUTTON, &MainFrame::onSaveEnvironment, this, controlId(ControlId::SaveEnvironment));
    Bind(wxEVT_BUTTON, &MainFrame::onChooseBaseDirectory, this, controlId(ControlId::ChooseBaseDirectory));
    Bind(wxEVT_BUTTON, &MainFrame::onChooseServer, this, controlId(ControlId::ChooseServer));
    Bind(wxEVT_BUTTON, &MainFrame::onChooseMonitor, this, controlId(ControlId::ChooseMonitor));
    Bind(wxEVT_BUTTON, &MainFrame::onChooseAutoMatch, this, controlId(ControlId::ChooseAutoMatch));
    Bind(wxEVT_BUTTON, &MainFrame::onAddTeam, this, controlId(ControlId::AddTeam));
    Bind(wxEVT_BUTTON, &MainFrame::onImportTeams, this, controlId(ControlId::ImportTeams));
    Bind(wxEVT_BUTTON, &MainFrame::onValidateTeam, this, controlId(ControlId::ValidateTeam));
    Bind(wxEVT_BUTTON, &MainFrame::onValidateAllTeams, this, controlId(ControlId::ValidateAllTeams));
    Bind(wxEVT_BUTTON, &MainFrame::onRemoveTeam, this, controlId(ControlId::RemoveTeam));
    Bind(wxEVT_BUTTON, &MainFrame::onPrepareMatch, this, controlId(ControlId::PrepareMatch));
    Bind(wxEVT_BUTTON, &MainFrame::onStopAll, this, controlId(ControlId::StopMatch));
    Bind(wxEVT_BUTTON, &MainFrame::onOpenSessionDirectory, this, controlId(ControlId::OpenSessionDirectory));
    Bind(wxEVT_BUTTON, &MainFrame::onRefreshReplays, this, controlId(ControlId::RefreshReplays));
    Bind(wxEVT_BUTTON, &MainFrame::onWatchSelectedReplay, this, controlId(ControlId::WatchSelectedReplay));
    Bind(wxEVT_BUTTON, &MainFrame::onWatchManualReplay, this, controlId(ControlId::WatchManualReplay));
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
    Bind(wxEVT_TIMER, &MainFrame::onPollProcessOutput, this);

    updateEnvironmentStatus();
    updateSessionState();
    notebook->SetSelection(std::filesystem::is_regular_file(serverExecutable_) ? 0 : 5);
}

void MainFrame::refreshTeams() {
    const auto previousLeft = leftTeam_->GetValue();
    const auto previousRight = rightTeam_->GetValue();
    leftTeam_->Clear(); rightTeam_->Clear();
    teamList_->DeleteAllItems();

    long row = 0;
    for (const Team& team : store_.teams()) {
        const wxString label = teamLabel(team);
        leftTeam_->Append(label);
        rightTeam_->Append(label);
        teamList_->InsertItem(row, wxString::FromUTF8(team.displayName));
        teamList_->SetItem(row, 1, wxString::FromUTF8(team.teamName));
        teamList_->SetItem(row, 2, findTeamLauncher(team.root).empty() ? "Pendente" : "Pronto");
        teamList_->SetItem(row, 3, pathString(team.root));
        ++row;
    }
    if (!store_.teams().empty()) {
        leftTeam_->SetSelection(0);
        rightTeam_->SetSelection(store_.teams().size() > 1 ? 1 : 0);
    }
    if (leftTeam_->FindString(previousLeft) != wxNOT_FOUND) leftTeam_->SetStringSelection(previousLeft);
    if (rightTeam_->FindString(previousRight) != wxNOT_FOUND) rightTeam_->SetStringSelection(previousRight);
    updateSessionState();
    report(wxString::Format("Catalogo atualizado: %lu time(s) cadastrado(s).",
        static_cast<unsigned long>(store_.teams().size())));
}

void MainFrame::loadEnvironment() {
    const auto file = appData_ / "environment.json";
    baseDirectory_ = std::filesystem::path(wxGetHomeDir().ToStdString()) / "rcss";
    serverExecutable_ = findCommandOnPath("rcssserver").ToStdString();
    monitorExecutable_ = findCommandOnPath("rcssmonitor").ToStdString();
    autoMatchScript_ = defaultAutoMatchPath();
    if (!std::filesystem::is_regular_file(file)) return;
    try {
        std::ifstream input(file);
        const auto data = nlohmann::json::parse(input);
        const std::filesystem::path base = data.value("baseDirectory", baseDirectory_.string());
        const std::filesystem::path server = data.value("serverExecutable", serverExecutable_.string());
        const std::filesystem::path monitor = data.value("monitorExecutable", monitorExecutable_.string());
        const std::filesystem::path script = data.value("autoMatchScript", autoMatchScript_.string());
        baseDirectory_ = base; serverExecutable_ = server; monitorExecutable_ = monitor; autoMatchScript_ = script;
    } catch (const std::exception& error) {
        environmentWarning_ = "Configuração não carregada. Revise os caminhos na aba Configuração: " + wxString::FromUTF8(error.what());
    }
}



void MainFrame::updateEnvironmentStatus() {
    const bool ready = std::filesystem::is_regular_file(serverExecutable_) && ::access(serverExecutable_.c_str(), X_OK) == 0;
    environment_->SetLabel(ready ? "Servidor configurado. Escolha os times em Partida."
        : "Escolha um servidor executável na aba Configuração.");
}
void MainFrame::updateSessionState() {
    const bool busy = sessionPid_ != 0 || batchBusy() || updater_->busy();
    for (const auto id : {ControlId::StartServer, ControlId::PrepareMatch, ControlId::SaveEnvironment,
                         ControlId::AddTeam, ControlId::ImportTeams, ControlId::RemoveTeam}) {
        if (auto* control = FindWindow(controlId(id))) control->Enable(!busy && !stopping_ && !closing_);
    }
    if (auto* control = FindWindow(controlId(ControlId::PrepareMatch)))
        control->Enable(!busy && !stopping_ && !closing_ && store_.teams().size() >= 2);
    for (const auto id : {ControlId::StopAll, ControlId::StopMatch}) {
        if (auto* control = FindWindow(controlId(id))) control->Enable((!processes_.empty() && !stopping_) || (batchBusy() && !batch_->cancelled()));
    }
    if (auto* control = FindWindow(controlId(ControlId::OpenSessionDirectory))) control->Enable(!activeSession_.directory.empty());
    leftTeam_->Enable(!busy); rightTeam_->Enable(!busy); coachModeCheck_->Enable(!busy);
    gameCount_->Enable(!busy && !closing_);
    gameTimeout_->Enable(!busy && !closing_ && gameCount_->GetValue() > 1);
    showMonitor_->Enable(!busy && !closing_);
    FindWindow(controlId(ControlId::OpenBatchDirectory))->Enable(static_cast<bool>(batch_));
    if (auto* start = wxDynamicCast(FindWindow(controlId(ControlId::PrepareMatch)), wxButton)) {
        const wxString label = gameCount_->GetValue() > 1 ? "Iniciar lote" : "Iniciar partida";
        if (start->GetLabel() != label) start->SetLabel(label);
    }
    if (auto* stop = wxDynamicCast(FindWindow(controlId(ControlId::StopMatch)), wxButton)) {
        const wxString label = batchBusy() ? "Cancelar lote" : "Encerrar sessão";
        if (stop->GetLabel() != label) stop->SetLabel(label);
    }
    updateBatchProgress();
    refreshUpdateState();
    const long selectedData = dataList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    const bool hasData = selectedData >= 0 && static_cast<std::size_t>(selectedData) < dataSessions_.size();
    const DataSession* selectedSession = hasData ? &dataSessions_.at(static_cast<std::size_t>(selectedData)) : nullptr;
    FindWindow(controlId(ControlId::ReprocessData))->Enable(selectedSession && !closing_ && !updater_->busy() &&
        !(busy && selectedSession->directory == std::filesystem::weakly_canonical(activeSession_.directory)) && !exporter_->contains(selectedSession->directory));
    FindWindow(controlId(ControlId::OpenData))->Enable(selectedSession && !selectedSession->exportDirectory.empty());
    FindWindow(controlId(ControlId::DataProblems))->Enable(hasData);
    FindWindow(controlId(ControlId::HistoryFolder))->Enable(hasData);
    FindWindow(controlId(ControlId::HistoryReplay))->Enable(selectedSession && !selectedSession->replay.empty() && !monitorExecutable_.empty() && !(busy && selectedSession->directory == std::filesystem::weakly_canonical(activeSession_.directory)));
    FindWindow(controlId(ControlId::ReuseTeams))->Enable(selectedSession && !selectedSession->leftRoot.empty() && !selectedSession->rightRoot.empty() && !busy && !stopping_ && !closing_);
    const wxString sessionText = stopping_ ? "Encerrando os processos da sessão e aguardando a gravação dos logs…"
        : sessionPid_ != 0 ? "Sessão em execução. Consulte a saída na aba Eventos."
        : batchBusy() ? "Lote em andamento. Aguardando os dados para continuar."
        : "Nenhuma sessão em execução. Escolha dois times em Partida.";
    if (sessionState_->GetLabel() != sessionText) sessionState_->SetLabel(sessionText);
}

void MainFrame::onAddTeam(wxCommandEvent&) {
    report("Adicionando time por uma pasta escolhida pelo usuario.");
    const wxString path = chooseDirectory(this, "", "Selecione a pasta raiz do time");
    if (path.empty()) { report("Cadastro de time cancelado."); return; }

    const std::filesystem::path selectedRoot = path.ToStdString();
    const wxString suggestedName = pathString(selectedRoot.filename());
    wxTextEntryDialog name(this, "Nome exibido do time:", "Adicionar time", suggestedName);
    if (name.ShowModal() != wxID_OK || name.GetValue().empty()) { report("Cadastro de time cancelado."); return; }
    wxTextEntryDialog serverName(this,
        "Nome de identificação do time (distinto do adversário):", "Adicionar time", suggestedName);
    if (serverName.ShowModal() != wxID_OK || serverName.GetValue().empty()) { report("Cadastro de time cancelado."); return; }
    Team team{name.GetValue().ToStdString(), serverName.GetValue().ToStdString(), selectedRoot, true};

    std::string reason;
    if (!isValidTeam(team, reason)) {
        report("Cadastro de time nao concluido: " + wxString::FromUTF8(reason));
        wxMessageBox(wxString::FromUTF8(reason), "Adicionar time", wxICON_WARNING);
        return;
    }
    try {
        store_.add(std::move(team));
    } catch (const std::exception& error) {
        report("Cadastro de time nao concluido: " + wxString::FromUTF8(error.what()));
        wxMessageBox("Nao foi possivel salvar o time.", "Adicionar time", wxICON_ERROR);
        return;
    }
    refreshTeams();
    report("Time adicionado em modo protegido. Nenhum arquivo do time foi alterado.");
}

void MainFrame::onImportTeams(wxCommandEvent&) {
    try {
        const auto folder = baseDirectory_ / "teams";
        if (!std::filesystem::is_directory(folder)) throw std::runtime_error("Configure uma pasta base com o diretório teams.");
        int imported = 0;
        for (auto team : discoverTeams(folder)) {
            const auto& existing = store_.teams();
            if (std::any_of(existing.begin(), existing.end(), [&](const Team& item) { return item.root == team.root; })) continue;
            store_.add(std::move(team));
            ++imported;
        }
        refreshTeams();
        report(wxString::Format("Importação concluída: %d time(s) com inicializador executável.", imported));
    } catch (const std::exception& error) {
        report("Importação interrompida: " + wxString::FromUTF8(error.what()));
        wxMessageBox(wxString::FromUTF8(error.what()), "Importar times", wxICON_ERROR);
    }
}

void MainFrame::onValidateTeam(wxCommandEvent&) {
    const long selection = teamList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selection == -1) { report("Selecione um time na lista para validar."); return; }
    const auto& team = store_.teams().at(static_cast<std::size_t>(selection));
    std::string reason;
    const bool validRoot = isValidTeam(team, reason);
    const auto launcher = validRoot ? findTeamLauncher(team.root) : std::filesystem::path{};
    const wxString message = !validRoot ? wxString::FromUTF8(reason) : launcher.empty()
        ? "Não foi encontrado um inicializador executável. Selecione a pasta que contém start.sh, start ou o diretório bin do time."
        : "Inicializador encontrado: " + pathString(launcher);
    report(message);
    wxMessageBox(message, "Validar time", launcher.empty() ? wxICON_WARNING : wxICON_INFORMATION);
}

void MainFrame::onValidateAllTeams(wxCommandEvent&) {
    wxString summary;
    unsigned ready = 0;
    for (const auto& team : store_.teams()) {
        const bool valid = !findTeamLauncher(team.root).empty();
        ready += valid ? 1 : 0;
        summary += wxString::FromUTF8(team.displayName) + (valid ? ": pronto\n" : ": inicializador executável ausente\n");
    }
    if (summary.empty()) summary = "Adicione ou importe um time para começar.";
    report(wxString::Format("Validação: %u de %zu times prontos. Nenhum arquivo de time foi alterado.", ready, store_.teams().size()));
    wxMessageBox(summary, "Validar times", wxICON_INFORMATION);
}

void MainFrame::onRemoveTeam(wxCommandEvent&) {
    const long selection = teamList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selection == -1) { 
        report("Remocao nao executada: nenhum time selecionado."); 
        wxMessageBox("Selecione um time para remover.", "Remover time", wxICON_INFORMATION); 
        return; 
    }
    
    if (static_cast<std::size_t>(selection) >= store_.teams().size()) return;
    
    const auto& team = store_.teams().at(static_cast<std::size_t>(selection));
    
    int answer = wxMessageBox("Deseja realmente remover o time '" + wxString::FromUTF8(team.displayName) + "' da lista?\n\nOs arquivos originais do time NAO serao apagados.", 
                              "Remover time", wxYES_NO | wxICON_QUESTION);
    if (answer != wxYES) return;

    try {
        store_.remove(static_cast<std::size_t>(selection));
        refreshTeams();
        report("Time removido do catalogo.");
    } catch (const std::exception& e) {
        wxMessageBox("Erro ao remover o time: " + wxString::FromUTF8(e.what()), "Erro", wxICON_ERROR);
    }
}

long MainFrame::launchProcess(const std::vector<std::string>& arguments, ProcessRole role,
                              const std::filesystem::path& directory,
                              const std::map<std::string, std::string>& environment) {
    auto child = std::make_unique<ChildProcess>(arguments, directory, environment);
    const auto pid = child->pid();
    processes_.push_back({std::move(child), {}, role});
    outputTimer_.Start(100);
    return pid;
}

void MainFrame::startMonitorIfAvailable() {
    if (monitorExecutable_.empty()) return;
    try {
        launchProcess({monitorExecutable_.string()}, ProcessRole::Monitor);
    } catch (const std::exception& error) {
        report("Partida mantida sem monitor: " + wxString::FromUTF8(error.what()));
    }
}

void MainFrame::stopManagedProcesses() {
    if (batchBusy()) { try { batch_->cancel(); } catch (const std::exception& error) { persistBatchFailure(error.what()); } }
    if (sessionPid_ != 0) stopRequested_ = true;
    for (auto& entry : processes_) entry.process->stop();
    stopping_ = !processes_.empty();
    updateSessionState();
}

bool MainFrame::launchSession(const Team* left, const Team* right) {
    if (closing_ || updater_->busy()) return false;
    if (sessionPid_ != 0 || stopping_) { report("Encerre a sessão atual antes de iniciar outra."); return false; }
    try {
        if (!std::filesystem::is_directory(baseDirectory_)) throw std::runtime_error("Escolha uma pasta base válida na aba Configuração.");
        if (!std::filesystem::is_regular_file(serverExecutable_) || ::access(serverExecutable_.c_str(), X_OK) != 0)
            throw std::runtime_error("Escolha um rcssserver executável na aba Configuração.");
        activeSession_ = prepareSession(baseDirectory_, left && coachModeCheck_->GetValue(), left, right);
        if (batchBusy() && batch_->ready()) batch_->startGame(activeSession_.directory);
        sessionLog_.open(activeSession_.runtime / "console.log", std::ios::binary);
        if (!sessionLog_) throw std::runtime_error("Não foi possível gravar a saída da sessão.");
        sessionPid_ = launchProcess({std::filesystem::absolute(serverExecutable_).string(), "include=config/session.conf"}, ProcessRole::Session,
                                   activeSession_.runtime, {{"RCSS_CONF_DIR", activeSession_.configuration.string()}});
        stopRequested_ = false;
        gameTimedOut_ = false; gameEndedNormally_ = false; endEvidence_.clear();
        gameStarted_ = std::chrono::steady_clock::now();
        if (left) rememberMatch();
        try {
            writeTextAtomically(sessionRuntimeDirectory(activeSession_.directory) / "session-state.json",
                nlohmann::json{{"active", true}, {"pid", sessionPid_}, {"stop_requested", false}}.dump());
        } catch (const std::exception& error) { report("Aviso ao registrar estado da sessão: " + wxString::FromUTF8(error.what())); }
        refreshDataSessions();
        stopping_ = false;
        report("Servidor iniciado. Logs da sessão: " + pathString(activeSession_.logs));
        if (showMonitor_->GetValue()) startMonitorIfAvailable();
        updateSessionState();
        return true;
    } catch (const std::exception& error) {
        sessionLog_.close(); sessionLog_.clear();
        if (batchBusy()) persistBatchFailure(error.what());
        report("Sessão não iniciada: " + wxString::FromUTF8(error.what()));
        wxMessageBox(wxString::FromUTF8(error.what()), "Iniciar sessão", wxICON_ERROR);
        return false;
    }
}

void MainFrame::onPrepareMatch(wxCommandEvent&) {
    if (batchBusy() || updater_->busy() || sessionPid_ != 0 || stopping_ || closing_) return;
    const int left = leftTeam_->GetSelection(), right = rightTeam_->GetSelection();
    if (left == wxNOT_FOUND || right == wxNOT_FOUND || left == right) {
        report("Escolha dois times diferentes para iniciar a partida."); return;
    }
    const auto& first = store_.teams().at(static_cast<std::size_t>(left));
    const auto& second = store_.teams().at(static_cast<std::size_t>(right));
    if (gameCount_->GetValue() == 1) { launchSession(&first, &second); return; }
    try {
        if (first.teamName == second.teamName || findTeamLauncher(first.root).empty() || findTeamLauncher(second.root).empty())
            throw std::runtime_error("Escolha times com nomes distintos e inicializadores executáveis.");
        if (!std::filesystem::is_directory(baseDirectory_) || !std::filesystem::is_regular_file(serverExecutable_) || ::access(serverExecutable_.c_str(), X_OK) != 0)
            throw std::runtime_error("Revise a pasta base e o servidor na aba Configuração.");
        batch_ = std::make_unique<BatchRun>(baseDirectory_, first, second, gameCount_->GetValue(),
            gameTimeout_->GetValue() * 60, coachModeCheck_->GetValue(), showMonitor_->GetValue());
        batchStarted_ = std::chrono::steady_clock::now();
        batchGauge_->SetRange(batch_->total());
        report("Lote iniciado: " + pathString(batch_->directory()));
        outputTimer_.Start(100);
        advanceBatch();
        updateSessionState();
    } catch (const std::exception& error) {
        report("Lote não iniciado: " + wxString::FromUTF8(error.what()));
        wxMessageBox(wxString::FromUTF8(error.what()), "Iniciar lote", wxICON_WARNING);
    }
}

void MainFrame::onSaveEnvironment(wxCommandEvent&) {
    if (batchBusy() || updater_->busy()) return;
    try {
        const std::filesystem::path base = baseDirectoryField_->GetValue().ToStdString();
        const std::filesystem::path server = serverField_->GetValue().ToStdString();
        const std::filesystem::path monitor = monitorField_->GetValue().ToStdString();
        const std::filesystem::path script = autoMatchField_->GetValue().ToStdString();
        if (!std::filesystem::is_directory(base)) throw std::runtime_error("Escolha uma pasta base existente.");
        if (!std::filesystem::is_regular_file(server) || ::access(server.c_str(), X_OK) != 0)
            throw std::runtime_error("Escolha um rcssserver executável.");
        if (!monitor.empty() && (!std::filesystem::is_regular_file(monitor) || ::access(monitor.c_str(), X_OK) != 0))
            throw std::runtime_error("Escolha um monitor executável ou deixe o campo vazio.");
        if (!script.empty() && !std::filesystem::is_regular_file(script))
            throw std::runtime_error("O auto_match.py é opcional. Corrija o caminho ou deixe o campo vazio.");
        writeTextAtomically(appData_ / "environment.json", nlohmann::json{
            {"baseDirectory", base.string()}, {"serverExecutable", server.string()},
            {"monitorExecutable", monitor.string()}, {"autoMatchScript", script.string()}}.dump(2) + '\n');
        baseDirectory_ = base; serverExecutable_ = server; monitorExecutable_ = monitor; autoMatchScript_ = script;
        updateEnvironmentStatus(); refreshReplays(); refreshDataSessions();
        report("Configuração salva.");
    } catch (const std::exception& error) {
        report("Configuração não salva: " + wxString::FromUTF8(error.what()));
        wxMessageBox(wxString::FromUTF8(error.what()), "Configuração", wxICON_WARNING);
    }
}

void MainFrame::onChooseBaseDirectory(wxCommandEvent&) {
    const wxString path = chooseDirectory(this, baseDirectoryField_->GetValue(), "Selecione a pasta base do RoboCup");
    if (!path.empty()) baseDirectoryField_->SetValue(path);
}

void MainFrame::onChooseServer(wxCommandEvent&) {
    const wxString path = chooseFile(this, serverField_->GetValue(), "Selecione rcssserver", "Todos os arquivos (*)|*");
    if (!path.empty()) serverField_->SetValue(path);
}

void MainFrame::onChooseMonitor(wxCommandEvent&) {
    const wxString path = chooseFile(this, monitorField_->GetValue(), "Selecione rcssmonitor", "Todos os arquivos (*)|*");
    if (!path.empty()) monitorField_->SetValue(path);
}

void MainFrame::onChooseAutoMatch(wxCommandEvent&) {
    const wxString path = chooseFile(this, autoMatchField_->GetValue(), "Selecione auto_match.py", "Python (*.py)|*.py|Todos os arquivos (*)|*");
    if (!path.empty()) autoMatchField_->SetValue(path);
}

void MainFrame::onStartServer(wxCommandEvent&) { if (!batchBusy() && !updater_->busy()) launchSession(); }

void MainFrame::onStopAll(wxCommandEvent&) {
    const bool cancellingBatch = batchBusy();
    stopManagedProcesses();
    if (cancellingBatch) { report("Cancelamento do lote solicitado. Aguardando o jogo atual e seus dados."); return; }
    report(processes_.empty() ? "Nenhuma sessão para encerrar." : "Encerramento solicitado. Aguardando os logs…");
}

void MainFrame::onOpenSessionDirectory(wxCommandEvent&) {
    if (!activeSession_.directory.empty() && !wxLaunchDefaultApplication(pathString(activeSession_.directory)))
        report("Não foi possível abrir a pasta: " + pathString(activeSession_.directory));
}

void MainFrame::onClose(wxCloseEvent& event) {
    if ((!processes_.empty() || exporter_->busy() || batchBusy() || updater_->busy()) && event.CanVeto()) {
        closing_ = true;
        updater_->cancel();
        stopManagedProcesses();
        outputTimer_.Start(100);
        report("Encerrando a sessão e aguardando a extração dos dados…");
        event.Veto();
        return;
    }
    event.Skip();
}

void MainFrame::onPollProcessOutput(wxTimerEvent&) {
    bool sessionEnded = false;
    int sessionExitCode = -1;
    if (batchBusy() && sessionPid_ != 0 && !gameTimedOut_ && !stopRequested_ &&
        batch_->timedOut(std::chrono::duration<double>(std::chrono::steady_clock::now() - gameStarted_).count())) {
        gameTimedOut_ = true; stopRequested_ = true; stopping_ = true;
        for (auto& entry : processes_) if (entry.role == ProcessRole::Session || entry.role == ProcessRole::Monitor) entry.process->stop();
        report("Tempo limite atingido. Encerrando o jogo e preservando os dados antes de continuar o lote.");
    }
    for (auto& entry : processes_) {
        const auto output = entry.process->poll();
        if (entry.role == ProcessRole::Session && sessionLog_.is_open()) {
            sessionLog_ << output; sessionLog_.flush();
        }
        if (entry.role == ProcessRole::Session) {
            endEvidence_ += output;
            if (endEvidence_.find("Game Over") != std::string::npos) gameEndedNormally_ = true;
            if (endEvidence_.size() > 32) endEvidence_.erase(0, endEvidence_.size() - 32);
        }
        entry.pendingOutput += output;
        std::size_t end;
        while ((end = entry.pendingOutput.find('\n')) != std::string::npos) {
            auto line = entry.pendingOutput.substr(0, end);
            auto text = wxString::FromUTF8(line);
            if (text.empty() && !line.empty()) text = wxString::From8BitData(line.data(), line.size());
            appendEvent(text);
            entry.pendingOutput.erase(0, end + 1);
        }
        if (entry.pendingOutput.size() > 16384 || entry.process->finished()) {
            if (!entry.pendingOutput.empty()) appendEvent(wxString::FromUTF8(entry.pendingOutput));
            entry.pendingOutput.clear();
        }
        if (entry.process->finished()) {
            report(wxString::Format("Processo %d encerrado (código %d).", entry.process->pid(), entry.process->exitCode().value_or(-1)));
            if (entry.role == ProcessRole::Session) {
                sessionExitCode = entry.process->exitCode().value_or(-1);
                sessionPid_ = 0; sessionEnded = true;
                sessionLog_.close(); sessionLog_.clear();
            }
        }
    }
    if (sessionEnded) {
        if (batchBusy() && batch_->gameRunning()) {
            try {
                const auto outcome = gameTimedOut_ ? "timeout" : sessionExitCode == 0 && gameEndedNormally_ ? "completed" :
                    stopRequested_ ? "cancelled" : "failed";
                batch_->endGame(outcome, sessionExitCode,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - gameStarted_).count());
            } catch (const std::exception& error) { persistBatchFailure(error.what()); }
        }
        markSessionEnded(stopRequested_);
        try { exporter_->enqueue(activeSession_.directory); }
        catch (const std::exception& error) { report("Extração não agendada: " + wxString::FromUTF8(error.what())); }
        stopping_ = true;
        for (auto& entry : processes_) if (entry.role == ProcessRole::Monitor) entry.process->stop();
        refreshReplays();
    }
    std::erase_if(processes_, [](const ProcessEntry& entry) { return entry.process->finished(); });
    if (sessionPid_ == 0 && !stopRequested_) {
        stopping_ = std::any_of(processes_.begin(), processes_.end(), [](const ProcessEntry& entry) { return entry.role == ProcessRole::Monitor; });
    }
    const bool updateChanged = updater_->poll();
    if (updateChanged) {
        if (!updater_->busy()) {
            restartTarget_ = installedUpdate(appData_);
            const auto current = std::filesystem::path(wxStandardPaths::Get().GetExecutablePath().ToStdString());
            if (!restartTarget_.empty() && std::filesystem::weakly_canonical(restartTarget_) == std::filesystem::weakly_canonical(current)) restartTarget_.clear();
        }
        refreshUpdateState();
    }
    const bool exportChanged = exporter_->poll();
    if (exportChanged) { report(wxString::FromUTF8(exporter_->message())); refreshDataSessions(); }
    if (sessionEnded) refreshDataSessions();
    if (sessionPid_ == 0 && std::none_of(processes_.begin(), processes_.end(), [](const ProcessEntry& entry) { return entry.role != ProcessRole::Replay; })) stopping_ = false;
    advanceBatch();
    if (processes_.empty()) {
        stopping_ = false;
        if (!exporter_->busy() && !batchBusy() && !updater_->busy()) outputTimer_.Stop();
        if (closing_ && !exporter_->busy() && !batchBusy() && !updater_->busy()) { Destroy(); return; }
    }
    updateSessionState();
}

void MainFrame::appendEvent(const wxString& text) {
    if (!events_) return;
    constexpr long maximumLogLength = 200000;
    if (events_->GetLastPosition() > maximumLogLength) events_->Remove(0, events_->GetLastPosition() - maximumLogLength / 2);
    events_->AppendText(text + "\n");
}

void MainFrame::report(const wxString& text) {
    const wxString message = wxDateTime::Now().FormatISOTime() + "  " + text;
    if (status_) status_->SetStatusText(text);
    appendEvent(message);
}

void MainFrame::refreshReplays() {
    replayList_->DeleteAllItems();
    currentReplays_.clear();

    try { currentReplays_ = findReplays(baseDirectory_); }
    catch (const std::exception& error) { report("Replays indisponíveis: " + wxString::FromUTF8(error.what())); }
    long row = 0;
    for (const auto& logFile : currentReplays_) {
        std::string dateStr = "Desconhecido";
        try {
            const auto ftime = std::filesystem::last_write_time(logFile);
            const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            const std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
            std::tm local{};
#ifdef _WIN32
            localtime_s(&local, &cftime);
#else
            localtime_r(&cftime, &local);
#endif
            std::ostringstream dateStream;
            dateStream << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
            dateStr = dateStream.str();
        } catch(...) {}

        replayList_->InsertItem(row, wxString::FromUTF8(dateStr));
        replayList_->SetItem(row, 1, pathString(logFile.lexically_relative(baseDirectory_)));
        ++row;
    }
    if (!currentReplays_.empty()) replayList_->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
}

void MainFrame::onRefreshReplays(wxCommandEvent&) {
    refreshReplays();
    report(wxString::Format("Lista de replays atualizada: %lu log(s) encontrado(s).",
        static_cast<unsigned long>(currentReplays_.size())));
}


void MainFrame::watchReplay(const std::filesystem::path& path) {
    try {
        if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("O replay não foi encontrado. Atualize a lista.");
        launchProcess({monitorExecutable_.string(), std::filesystem::absolute(path).string()}, ProcessRole::Replay);
        report("Replay aberto: " + pathString(path.filename()));
        updateSessionState();
    } catch (const std::exception& error) {
        report("Replay não aberto: " + wxString::FromUTF8(error.what()));
        wxMessageBox(wxString::FromUTF8(error.what()), "Abrir replay", wxICON_ERROR);
    }
}

void MainFrame::onWatchSelectedReplay(wxCommandEvent&) {
    const long selection = replayList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selection == -1 || static_cast<std::size_t>(selection) >= currentReplays_.size()) {
        report("Selecione um replay na lista."); return;
    }
    watchReplay(currentReplays_.at(static_cast<std::size_t>(selection)));
}

void MainFrame::onWatchManualReplay(wxCommandEvent&) {
    const auto path = chooseFile(this, pathString(baseDirectory_), "Escolha um replay", "Replays (*.rcg;*.rcg.gz)|*.rcg;*.rcg.gz");
    if (!path.empty()) watchReplay(std::filesystem::path(path.ToStdString()));
}

void MainFrame::markSessionEnded(bool stopRequested) {
    try {
        writeTextAtomically(sessionRuntimeDirectory(activeSession_.directory) / "session-state.json",
            nlohmann::json{{"active", false}, {"stop_requested", stopRequested}}.dump());
    } catch (const std::exception& error) { report("Estado da sessão não salvo: " + wxString::FromUTF8(error.what())); }
}

void MainFrame::refreshDataSessions() {
    try { allDataSessions_ = findDataSessions(baseDirectory_); filterDataSessions(); }
    catch (const std::exception& error) { report("Histórico indisponível: " + wxString::FromUTF8(error.what())); }
}

void MainFrame::filterDataSessions() {
    std::filesystem::path previous;
    const long selected = dataList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selected >= 0 && static_cast<std::size_t>(selected) < dataSessions_.size())
        previous = dataSessions_.at(static_cast<std::size_t>(selected)).directory;
    const auto search = historySearch_->GetValue().Lower();
    const int filter = historyFilter_->GetSelection();
    dataList_->Freeze();
    dataList_->DeleteAllItems();
    dataSessions_.clear();
    for (const auto& session : allDataSessions_) {
        const auto text = wxString::FromUTF8(session.label + " " + session.createdAt + " " + session.batchId + " " + session.directory.filename().string()).Lower();
        if (!search.empty() && !text.Contains(search)) continue;
        if (filter == 1 && session.exportDirectory.empty()) continue;
        if (filter == 2 && session.status != "failed" && session.status != "completed_with_warnings" && !((session.status == "queued" || session.status == "extracting") && !exporter_->contains(session.directory))) continue;
        if (filter == 3 && !session.exportDirectory.empty()) continue;
        dataSessions_.push_back(session);
    }
    long row = 0;
    bool restored = false;
    for (const auto& session : dataSessions_) {
        wxString state = "Aguardando extração";
        if (sessionPid_ != 0 && session.directory == std::filesystem::weakly_canonical(activeSession_.directory)) state = "Gravando logs";
        else if (session.status == "queued") state = exporter_->contains(session.directory) ? "Na fila" : "Interrompida; reprocessar";
        else if (session.status == "extracting") state = exporter_->contains(session.directory) ? "Extraindo" : "Interrompida; reprocessar";
        else if (session.status == "completed") state = "Concluída";
        else if (session.status == "completed_with_warnings") state = "Concluída com avisos";
        else if (session.status == "failed") state = "Falhou; ver problemas";
        auto date = session.createdAt.substr(0, 19);
        std::replace(date.begin(), date.end(), 'T', ' ');
        dataList_->InsertItem(row, wxString::FromUTF8(date));
        dataList_->SetItem(row, 1, wxString::FromUTF8(session.label));
        dataList_->SetItem(row, 2, state);
        dataList_->SetItem(row, 3, wxString::FromUTF8(session.coverage));
        if (session.directory == previous) {
            dataList_->SetItemState(row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            restored = true;
        }
        ++row;
    }
    if (!restored && !dataSessions_.empty()) dataList_->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    dataList_->Thaw();
    updateSessionState();
    updateHistoryDetail();
}

void MainFrame::updateHistoryDetail() {
    const long selected = dataList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    wxString text;
    if (allDataSessions_.empty()) text = "Seu histórico está vazio. Inicie uma partida na aba Partida; os dados serão gerados ao terminar.";
    else if (dataSessions_.empty()) text = "Nenhuma partida encontrada. Limpe a busca ou escolha outro filtro.";
    else if (selected >= 0 && static_cast<std::size_t>(selected) < dataSessions_.size()) {
        const auto& session = dataSessions_.at(static_cast<std::size_t>(selected));
        text = wxString::Format("%zu partida(s) na lista. ", dataSessions_.size()) +
            (session.exportDirectory.empty() ? "CSV ainda não disponível. " : "Duplo clique abre os CSVs. ") +
            "\nLogs originais e versões anteriores ficam na pasta da partida.";
        if (!session.batchId.empty()) text += wxString::Format(" Jogo %d/%d do lote ", session.batchIndex, session.batchTotal) + wxString::FromUTF8(session.batchId);
        historyDetail_->SetToolTip(pathString(session.directory));
    }
    historyDetail_->SetLabel(text);
    historyDetail_->Wrap(FromDIP(700));
    historyDetail_->GetParent()->Layout();
}

void MainFrame::onReprocessData(wxCommandEvent&) {
    const long selected = dataList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selected < 0 || static_cast<std::size_t>(selected) >= dataSessions_.size() || closing_ || updater_->busy()) return;
    const auto directory = dataSessions_.at(static_cast<std::size_t>(selected)).directory;
    if (sessionPid_ != 0 && directory == std::filesystem::weakly_canonical(activeSession_.directory)) {
        report("Aguarde a sessão encerrar antes de extrair os dados."); return;
    }
    try {
        if (exporter_->enqueue(directory)) {
            outputTimer_.Start(100);
            report("Extração agendada. Os logs e a última exportação válida serão preservados.");
        }
        refreshDataSessions();
    } catch (const std::exception& error) { report("Extração não agendada: " + wxString::FromUTF8(error.what())); }
}

void MainFrame::onOpenData(wxCommandEvent&) {
    const long selected = dataList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selected < 0 || static_cast<std::size_t>(selected) >= dataSessions_.size()) return;
    const auto& directory = dataSessions_.at(static_cast<std::size_t>(selected)).exportDirectory;
    if (!directory.empty() && !wxLaunchDefaultApplication(pathString(csvDirectory(directory)))) report("Não foi possível abrir os CSVs: " + pathString(directory));
}

void MainFrame::onDataProblems(wxCommandEvent&) {
    const long selected = dataList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selected < 0 || static_cast<std::size_t>(selected) >= dataSessions_.size()) return;
    const auto& session = dataSessions_.at(static_cast<std::size_t>(selected));
    std::string text = session.detail;
    if (!session.exportDirectory.empty()) {
        text += "\nÚltima exportação válida: " + session.exportDirectory.string() + "\nCobertura: " + session.coverage + "\n\n";
        std::ifstream quality(csvDirectory(session.exportDirectory) / "qualidade.csv");
        // Preview is bounded; the complete file remains available in the export folder.
        std::string line;
        for (int count = 0; count < 100 && std::getline(quality, line); ++count) text += line + '\n';
    }
    if (text.empty()) text = "Ainda não há diagnóstico. Encerre a partida ou use Reprocessar dados. Consulte a aba Eventos em caso de falha.";
    wxDialog dialog(this, wxID_ANY, "Problemas e cobertura da extração", wxDefaultPosition, FromDIP(wxSize(720, 440)), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* content = new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(text), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
    sizer->Add(content, 1, wxEXPAND | wxALL, 8);
    sizer->Add(dialog.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, 8);
    dialog.SetSizer(sizer);
    dialog.ShowModal();
}

void MainFrame::onHistoryReplay(wxCommandEvent&) {
    const long selected = dataList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selected < 0 || static_cast<std::size_t>(selected) >= dataSessions_.size()) return;
    const auto& session = dataSessions_.at(static_cast<std::size_t>(selected));
    if (sessionPid_ != 0 && session.directory == std::filesystem::weakly_canonical(activeSession_.directory)) {
        report("Aguarde a partida encerrar para abrir seu replay."); return;
    }
    const auto& replay = session.replay;
    if (!replay.empty()) watchReplay(replay);
    else report("Não há um replay único nesta sessão. Use a aba Replays antigos para escolher um arquivo.");
}

void MainFrame::onHistoryFolder(wxCommandEvent&) {
    const long selected = dataList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selected < 0 || static_cast<std::size_t>(selected) >= dataSessions_.size()) return;
    const auto directory = dataSessions_.at(static_cast<std::size_t>(selected)).directory;
    if (!wxLaunchDefaultApplication(pathString(directory))) report("Não foi possível abrir a pasta da partida.");
}

void MainFrame::onReuseTeams(wxCommandEvent&) {
    const long selected = dataList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selected < 0 || static_cast<std::size_t>(selected) >= dataSessions_.size() || sessionPid_ != 0 || batchBusy() || updater_->busy() || stopping_ || closing_) return;
    const auto& session = dataSessions_.at(static_cast<std::size_t>(selected));
    int left = wxNOT_FOUND, right = wxNOT_FOUND;
    for (std::size_t i = 0; i < store_.teams().size(); ++i) {
        if (store_.teams()[i].root == session.leftRoot) left = static_cast<int>(i);
        if (store_.teams()[i].root == session.rightRoot) right = static_cast<int>(i);
    }
    if (left == wxNOT_FOUND || right == wxNOT_FOUND || left == right) {
        report("Cadastre novamente as pastas dos dois times na aba Times para usar este confronto."); return;
    }
    leftTeam_->SetSelection(left); rightTeam_->SetSelection(right);
    notebook_->SetSelection(0);
    FindWindow(controlId(ControlId::PrepareMatch))->SetFocus();
    rememberMatch();
    report("Times carregados. Use Iniciar partida ou Ctrl+Enter para jogar.");
}

void MainFrame::rememberMatch() {
    const int left = leftTeam_->GetSelection(), right = rightTeam_->GetSelection();
    if (left == wxNOT_FOUND || right == wxNOT_FOUND) return;
    try {
        writeTextAtomically(appData_ / "ui.json", nlohmann::json{
            {"leftRoot", store_.teams().at(static_cast<std::size_t>(left)).root.string()},
            {"rightRoot", store_.teams().at(static_cast<std::size_t>(right)).root.string()},
            {"coach", coachModeCheck_->GetValue()}}.dump(2));
    } catch (const std::exception& error) { report("Não foi possível lembrar os times: " + wxString::FromUTF8(error.what())); }
}

void MainFrame::restoreMatch() {
    try {
        const auto file = appData_ / "ui.json";
        if (!std::filesystem::exists(file)) return;
        std::ifstream input(file);
        const auto settings = nlohmann::json::parse(input);
        const std::filesystem::path left = settings.value("leftRoot", ""), right = settings.value("rightRoot", "");
        for (std::size_t i = 0; i < store_.teams().size(); ++i) {
            if (store_.teams()[i].root == left) leftTeam_->SetSelection(static_cast<int>(i));
            if (store_.teams()[i].root == right) rightTeam_->SetSelection(static_cast<int>(i));
        }
        coachModeCheck_->SetValue(settings.value("coach", false));
    } catch (const std::exception& error) { report("Preferências não carregadas: " + wxString::FromUTF8(error.what())); }
}

bool MainFrame::batchBusy() const noexcept { return batch_ && !batch_->finished(); }

void MainFrame::persistBatchFailure(const std::string& reason) {
    if (!batch_) return;
    try { batch_->abort(reason); }
    catch (const std::exception& error) { report("Lote interrompido; falha ao salvar o resumo: " + wxString::FromUTF8(error.what())); }
}

void MainFrame::advanceBatch() {
    if (!batchBusy()) return;
    try {
        if (batch_->waitingForExport() && !exporter_->contains(activeSession_.directory)) {
            std::string status = "failed";
            std::optional<bool> normalEnd;
            try {
                std::ifstream input(sessionRuntimeDirectory(activeSession_.directory) / "extraction.json");
                const auto result = nlohmann::json::parse(input);
                const auto value = result.value("status", "failed");
                if (value == "completed" || value == "completed_with_warnings") status = value;
            } catch (const std::exception&) { /* Missing/invalid state is an export failure, never a success. */ }
            if (status != "failed") {
                try {
                    const auto sessions = findDataSessions(baseDirectory_);
                    const auto session = std::find_if(sessions.begin(), sessions.end(), [this](const DataSession& item) { return item.directory == activeSession_.directory; });
                    if (session != sessions.end() && !session->exportDirectory.empty()) {
                        std::ifstream input(session->exportDirectory / "manifest.json");
                        normalEnd = nlohmann::json::parse(input).value("ending", "") == "normal";
                    }
                } catch (const std::exception&) { status = "failed"; }
            }
            batch_->endExport(status, normalEnd);
            if (batch_->finished()) report("Lote finalizado. Resumo e arquivos: " + pathString(batch_->directory()));
        }
        if (batch_->ready() && !closing_ && sessionPid_ == 0 && !stopping_) {
            launchSession(&batch_->left(), &batch_->right());
        }
    } catch (const std::exception& error) {
        persistBatchFailure(error.what());
        report("Lote interrompido: " + wxString::FromUTF8(error.what()));
    }
}

void MainFrame::updateBatchProgress() {
    if (!batch_) return;
    if (batchGauge_->GetRange() != batch_->total()) batchGauge_->SetRange(batch_->total());
    if (batchGauge_->GetValue() != batch_->settled()) batchGauge_->SetValue(batch_->settled());
    wxString text;
    if (batch_->finished()) {
        text = batch_->status() == "cancelled" ? "Lote cancelado: " : batch_->status() == "failed" ? "Lote interrompido por erro: " : "Lote concluído: ";
        text += wxString::Format("%d de %d processados; %d falhas de jogo e %d de extração.",
            batch_->settled(), batch_->total(), batch_->failures(), batch_->exportFailures());
    } else {
        text = batch_->cancelled() ? "Cancelando lote. " : "";
        text += wxString::Format("%d de %d finalizados. Jogo %d: ", batch_->settled(), batch_->total(), batch_->started());
        text += batch_->waitingForExport() ? "exportando os CSVs." : "em execução.";
        if (batch_->settled() > 0 && !batch_->cancelled()) {
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - batchStarted_).count();
            const auto remaining = static_cast<long>(seconds / batch_->settled() * (batch_->total() - batch_->settled()));
            text += wxString::Format(" Restante estimado: %ld min %ld s.", remaining / 60, remaining % 60);
        }
    }
    if (batchCaption_ != text) {
        batchCaption_ = text;
        batchText_->SetLabel(text);
        batchText_->Wrap(FromDIP(680));
        batchText_->GetParent()->Layout();
    }
}

void MainFrame::refreshUpdateState() {
    if (!updateText_) return;
    const wxString text = wxString::FromUTF8(updater_->message());
    if (updateText_->GetLabel() != text) { updateText_->SetLabel(text); updateText_->Wrap(FromDIP(650)); updateText_->GetParent()->Layout(); }
    if (updateGauge_->GetValue() != updater_->progress()) updateGauge_->SetValue(updater_->progress());
    const bool occupied = sessionPid_ != 0 || batchBusy() || !processes_.empty() || exporter_->busy();
    auto* button = wxDynamicCast(FindWindow(controlId(ControlId::UpdateApplication)), wxButton);
    const wxString label = updater_->busy() ? "Cancelar atualização" : "Atualizar aplicação";
    if (button->GetLabel() != label) button->SetLabel(label);
    button->Enable(!closing_ && (!occupied || updater_->busy()));
    FindWindow(controlId(ControlId::RestartApplication))->Enable(!closing_ && !occupied && !updater_->busy() && !restartTarget_.empty());
    FindWindow(controlId(ControlId::UpdateLog))->Enable(std::filesystem::is_regular_file(updater_->log()));
}

void MainFrame::onUpdateApplication(wxCommandEvent&) {
    if (updater_->busy()) { updater_->cancel(); refreshUpdateState(); return; }
    if (closing_ || sessionPid_ != 0 || batchBusy() || !processes_.empty() || exporter_->busy()) return;
    try {
        updater_->start();
        outputTimer_.Start(100);
        updateSessionState();
    } catch (const std::exception& error) { report("Atualização não iniciada: " + wxString::FromUTF8(error.what())); }
}

void MainFrame::onRestartApplication(wxCommandEvent&) {
    if (closing_ || sessionPid_ != 0 || batchBusy() || !processes_.empty() || exporter_->busy() || updater_->busy()) return;
    const auto executable = installedUpdate(appData_);
    if (executable.empty()) { report("Não há atualização instalada para abrir."); return; }
    const char* arguments[]{executable.c_str(), nullptr};
    if (wxExecute(arguments, wxEXEC_ASYNC | wxEXEC_MAKE_GROUP_LEADER) > 0) Close();
    else report("Não foi possível abrir a versão atualizada.");
}
