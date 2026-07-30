#include "MainFrame.hpp"

#include <wx/button.h>
#include <wx/odcombo.h>
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
    ChooseBaseDirectory = wxID_HIGHEST + 40,
    ChooseServer,
    ChooseMonitor,
    ChooseAutoMatch,
    RefreshReplays = wxID_HIGHEST + 50,
    WatchSelectedReplay,
    WatchManualReplay,
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

wxString shellQuote(const std::filesystem::path& path) {
    wxString value = pathString(path);
    value.Replace("'", "'\\\"'\\\"'");
    return "'" + value + "'";
}

wxString shellQuote(const std::string& value) {
    wxString escaped = wxString::FromUTF8(value);
    escaped.Replace("'", "'\\\"'\\\"'");
    return "'" + escaped + "'";
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
    : wxFrame(nullptr, wxID_ANY, "UnBall-Manager2D", wxDefaultPosition, wxSize(900, 620)),
      appData_(std::filesystem::path(wxStandardPaths::Get().GetUserDataDir().ToStdString())),
      store_(appData_ / "teams.json"),
      outputTimer_(this) {
    try { store_.load(); }
    catch (const std::exception& error) {
        wxMessageBox("Nao foi possivel ler os times: " + wxString::FromUTF8(error.what()), "UnBall-Manager2D", wxICON_WARNING);
    }
    loadEnvironment();
    buildUi();
    refreshTeams();
    refreshReplays();
}

MainFrame::~MainFrame() {
    stopManagedProcesses();
}

void MainFrame::buildUi() {
    auto* notebook = new wxNotebook(this, wxID_ANY);

    auto* dashboard = new wxPanel(notebook);
    auto* dashboardSizer = new wxBoxSizer(wxVERTICAL);
    environment_ = new wxStaticText(dashboard, wxID_ANY, "Verificando ambiente...");
    dashboardSizer->Add(environment_, 0, wxALL, 12);
    dashboardSizer->Add(new wxStaticText(dashboard, wxID_ANY,
        "Instale times e ferramentas externamente (recomendado: rcsssetup). O gerenciador apenas os utiliza."), 0, wxLEFT | wxRIGHT, 12);
    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    addButton(dashboard, actions, "Iniciar somente servidor", ControlId::StartServer);
    addButton(dashboard, actions, "Encerrar processos da sessao", ControlId::StopAll);
    dashboardSizer->Add(actions, 0, wxALL, 7);
    dashboard->SetSizer(dashboardSizer);
    notebook->AddPage(dashboard, "Painel");

    auto* configuration = new wxPanel(notebook);
    auto* configurationSizer = new wxFlexGridSizer(3, 8, 8);
    configurationSizer->Add(new wxStaticText(configuration, wxID_ANY, "Pasta base do RoboCup:"), 0, wxALIGN_CENTER_VERTICAL);
    baseDirectoryField_ = new wxTextCtrl(configuration, wxID_ANY, pathString(baseDirectory_));
    configurationSizer->Add(baseDirectoryField_, 1, wxEXPAND);
    addButton(configuration, configurationSizer, "Procurar...", ControlId::ChooseBaseDirectory);
    configurationSizer->Add(new wxStaticText(configuration, wxID_ANY, "rcssserver:"), 0, wxALIGN_CENTER_VERTICAL);
    serverField_ = new wxTextCtrl(configuration, wxID_ANY, pathString(serverExecutable_));
    configurationSizer->Add(serverField_, 1, wxEXPAND);
    addButton(configuration, configurationSizer, "Procurar...", ControlId::ChooseServer);
    configurationSizer->Add(new wxStaticText(configuration, wxID_ANY, "rcssmonitor (opcional):"), 0, wxALIGN_CENTER_VERTICAL);
    monitorField_ = new wxTextCtrl(configuration, wxID_ANY, pathString(monitorExecutable_));
    configurationSizer->Add(monitorField_, 1, wxEXPAND);
    addButton(configuration, configurationSizer, "Procurar...", ControlId::ChooseMonitor);
    configurationSizer->Add(new wxStaticText(configuration, wxID_ANY, "auto_match.py:"), 0, wxALIGN_CENTER_VERTICAL);
    autoMatchField_ = new wxTextCtrl(configuration, wxID_ANY, pathString(autoMatchScript_));
    configurationSizer->Add(autoMatchField_, 1, wxEXPAND);
    addButton(configuration, configurationSizer, "Procurar...", ControlId::ChooseAutoMatch);
    configurationSizer->AddGrowableCol(1, 1);
    auto* configurationOuter = new wxBoxSizer(wxVERTICAL);
    configurationOuter->Add(new wxStaticText(configuration, wxID_ANY,
        "Escolha os caminhos da sua instalacao. O UnBall-Manager2D nao instala nem altera essas ferramentas."), 0, wxALL, 12);
    configurationOuter->Add(configurationSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    addButton(configuration, configurationOuter, "Salvar configuracao", ControlId::SaveEnvironment);
    configuration->SetSizer(configurationOuter);
    notebook->AddPage(configuration, "Configuracao");

    auto* teams = new wxPanel(notebook);
    auto* teamsSizer = new wxBoxSizer(wxVERTICAL);
    teamList_ = new wxListCtrl(teams, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    teamList_->AppendColumn("Time", wxLIST_FORMAT_LEFT, 180);
    teamList_->AppendColumn("Nome no servidor", wxLIST_FORMAT_LEFT, 150);
    teamList_->AppendColumn("Protecao", wxLIST_FORMAT_LEFT, 100);
    teamList_->AppendColumn("Pasta", wxLIST_FORMAT_LEFT, 440);
    teamsSizer->Add(teamList_, 1, wxEXPAND | wxALL, 8);
    auto* teamActions = new wxBoxSizer(wxHORIZONTAL);
    addButton(teams, teamActions, "Adicionar time", ControlId::AddTeam);
    addButton(teams, teamActions, "Importar da pasta 'teams'", ControlId::ImportTeams);
    addButton(teams, teamActions, "Validar selecionado", ControlId::ValidateTeam);
    addButton(teams, teamActions, "Validar todos", ControlId::ValidateAllTeams);
    addButton(teams, teamActions, "Remover selecionado", ControlId::RemoveTeam);
    teamsSizer->Add(teamActions, 0, wxLEFT | wxBOTTOM, 3);
    teams->SetSizer(teamsSizer);
    notebook->AddPage(teams, "Times");

    auto* match = new wxPanel(notebook);
    auto* matchSizer = new wxFlexGridSizer(2, 8, 8);
    matchSizer->Add(new wxStaticText(match, wxID_ANY, "Time esquerdo:"), 0, wxALIGN_CENTER_VERTICAL);
    leftTeam_ = new wxOwnerDrawnComboBox(match, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY); matchSizer->Add(leftTeam_, 1, wxEXPAND);
    matchSizer->Add(new wxStaticText(match, wxID_ANY, "Time direito:"), 0, wxALIGN_CENTER_VERTICAL);
    rightTeam_ = new wxOwnerDrawnComboBox(match, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY); matchSizer->Add(rightTeam_, 1, wxEXPAND);
    matchSizer->AddGrowableCol(1, 1);
    auto* matchOuter = new wxBoxSizer(wxVERTICAL);
    matchOuter->Add(new wxStaticText(match, wxID_ANY,
        "O cadastro guarda apenas uma referencia. O time precisa estar pronto e ser reconhecido pelo auto_match.py."), 0, wxALL, 12);
    matchOuter->Add(matchSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    
    coachModeCheck_ = new wxCheckBox(match, wxID_ANY, "Modo Coach (Teste) - Permite arrastar bola/jogadores");
    matchOuter->Add(coachModeCheck_, 0, wxALL, 12);
    
    auto* matchBtns = new wxBoxSizer(wxHORIZONTAL);
    addButton(match, matchBtns, "Preparar sessao", ControlId::PrepareMatch);
    addButton(match, matchBtns, "Finalizar partida", ControlId::StopMatch);
    matchOuter->Add(matchBtns, 0, wxEXPAND | wxLEFT | wxRIGHT, 9);
    
    match->SetSizer(matchOuter);
    notebook->AddPage(match, "Nova partida");

    auto* monitor = new wxPanel(notebook);
    auto* monitorSizer = new wxBoxSizer(wxVERTICAL);
    events_ = new wxTextCtrl(monitor, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
    monitorSizer->Add(events_, 1, wxEXPAND | wxALL, 8);
    monitor->SetSizer(monitorSizer);
    notebook->AddPage(monitor, "Monitor");

    auto* replays = new wxPanel(notebook);
    auto* replaysSizer = new wxBoxSizer(wxVERTICAL);
    replayList_ = new wxListCtrl(replays, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    replayList_->AppendColumn("Data / Hora", wxLIST_FORMAT_LEFT, 160);
    replayList_->AppendColumn("Arquivo de Log", wxLIST_FORMAT_LEFT, 500);
    replaysSizer->Add(replayList_, 1, wxEXPAND | wxALL, 8);
    auto* replayActions = new wxBoxSizer(wxHORIZONTAL);
    addButton(replays, replayActions, "Atualizar lista", ControlId::RefreshReplays);
    addButton(replays, replayActions, "Assistir selecionado", ControlId::WatchSelectedReplay);
    addButton(replays, replayActions, "Procurar log manualmente...", ControlId::WatchManualReplay);
    replaysSizer->Add(replayActions, 0, wxLEFT | wxBOTTOM, 3);
    replays->SetSizer(replaysSizer);
    notebook->AddPage(replays, "Replays");

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
    Bind(wxEVT_BUTTON, &MainFrame::onRefreshReplays, this, controlId(ControlId::RefreshReplays));
    Bind(wxEVT_BUTTON, &MainFrame::onWatchSelectedReplay, this, controlId(ControlId::WatchSelectedReplay));
    Bind(wxEVT_BUTTON, &MainFrame::onWatchManualReplay, this, controlId(ControlId::WatchManualReplay));
    Bind(wxEVT_END_PROCESS, &MainFrame::onProcessTerminated, this);
    Bind(wxEVT_TIMER, &MainFrame::onPollProcessOutput, this);

    updateEnvironmentStatus();
}

void MainFrame::refreshTeams() {
    leftTeam_->Clear(); rightTeam_->Clear();
    teamList_->DeleteAllItems();

    long row = 0;
    for (const Team& team : store_.teams()) {
        const wxString label = teamLabel(team);
        leftTeam_->Append(label);
        rightTeam_->Append(label);
        teamList_->InsertItem(row, wxString::FromUTF8(team.displayName));
        teamList_->SetItem(row, 1, wxString::FromUTF8(team.teamName));
        teamList_->SetItem(row, 2, team.protectedMode ? "Protegido" : "Autorizado");
        teamList_->SetItem(row, 3, pathString(team.root));
        ++row;
    }
    if (!store_.teams().empty()) {
        leftTeam_->SetSelection(0);
        rightTeam_->SetSelection(store_.teams().size() > 1 ? 1 : 0);
    }
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
        baseDirectory_ = data.value("baseDirectory", baseDirectory_.string());
        serverExecutable_ = data.value("serverExecutable", serverExecutable_.string());
        monitorExecutable_ = data.value("monitorExecutable", monitorExecutable_.string());
        autoMatchScript_ = data.value("autoMatchScript", autoMatchScript_.string());
    } catch (...) {
        // A interface informa a ausencia do caminho; um arquivo local invalido nao impede o app de abrir.
    }
}

void MainFrame::saveEnvironment() const {
    std::filesystem::create_directories(appData_);
    std::ofstream output(appData_ / "environment.json");
    output << nlohmann::json{
        {"baseDirectory", baseDirectory_.string()},
        {"serverExecutable", serverExecutable_.string()},
        {"monitorExecutable", monitorExecutable_.string()},
        {"autoMatchScript", autoMatchScript_.string()},
    }.dump(2) << '\n';
}

void MainFrame::updateEnvironmentStatus() {
    const bool serverReady = std::filesystem::is_regular_file(serverExecutable_);
    environment_->SetLabel(serverReady
        ? "Ferramentas configuradas. rcssserver: " + pathString(serverExecutable_)
        : "Configure os caminhos das ferramentas na aba Configuracao.");
    report(serverReady ? "Ambiente carregado." : "Ambiente incompleto: rcssserver nao configurado.");
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
        "Nome do diretorio aceito pelo auto_match.py:", "Adicionar time", suggestedName);
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
    if (!std::filesystem::is_directory(baseDirectory_)) {
        wxMessageBox("Configure a pasta base do RoboCup na aba Configuracao antes de importar.", "Importar times", wxICON_WARNING);
        return;
    }
    const auto teamsFolder = baseDirectory_ / "teams";
    if (!std::filesystem::is_directory(teamsFolder)) {
        wxMessageBox("A pasta 'teams' nao foi encontrada dentro da pasta base.", "Importar times", wxICON_WARNING);
        return;
    }

    report("Procurando times em " + pathString(teamsFolder) + "...");
    int imported = 0;

    auto importTeamDir = [&](const std::filesystem::path& root) {
        const std::string name = root.filename().string();
        Team team{name, name, root, false}; // Origem: detectado automaticamente (Autorizado / false)
        std::string validationReason;
        if (isValidTeam(team, validationReason)) {
            const auto& existing = store_.teams();
            const bool exists = std::any_of(existing.begin(), existing.end(), [&root](const Team& t) {
                return t.root == root;
            });
            if (!exists) {
                try {
                    store_.add(std::move(team));
                    imported++;
                } catch(...) {}
            }
        }
    };

    for (const auto& categoryEntry : std::filesystem::directory_iterator(teamsFolder)) {
        if (!categoryEntry.is_directory()) continue;
        
        // No rcsssetup, a estrutura geralmente eh teams/<categoria>/<time> (ex: teams/rc2024/Helios)
        for (const auto& teamEntry : std::filesystem::directory_iterator(categoryEntry.path())) {
            if (!teamEntry.is_directory()) continue;
            importTeamDir(teamEntry.path());
        }
    }

    if (imported > 0) {
        refreshTeams();
        report(wxString::Format("Importacao concluida: %d novo(s) time(s) importado(s).", imported));
    } else {
        report("Importacao concluida: Nenhum time novo ou valido encontrado.");
    }
}

void MainFrame::onValidateTeam(wxCommandEvent&) {
    const long selection = teamList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selection == -1) { report("Validacao nao executada: nenhum time selecionado."); wxMessageBox("Selecione um time.", "Validar time", wxICON_INFORMATION); return; }
    
    if (static_cast<std::size_t>(selection) >= store_.teams().size()) return;
    
    const auto& team = store_.teams().at(static_cast<std::size_t>(selection));
    
    std::string validationReason;
    const bool valid = isValidTeam(team, validationReason);
    const wxString reason = wxString::FromUTF8(validationReason);

    if (valid) {
        const bool hasStartSh = std::filesystem::exists(team.root / "start.sh");
        const bool hasStart = std::filesystem::exists(team.root / "start");
        const bool hasStartTeam = std::filesystem::exists(team.root / "start_team");
        
        if (!hasStartSh && !hasStart && !hasStartTeam) {
            int answer = wxMessageBox(
                "O time foi encontrado, mas nao possui um script de inicializacao padrao (start.sh, start, ou start_team) exigido pelo rcsssetup.\n\n"
                "Deseja criar um arquivo 'start.sh' generico agora?",
                "Script ausente", wxYES_NO | wxICON_WARNING);
            if (answer == wxYES) {
                try {
                    std::ofstream script(team.root / "start.sh");
                    script << "#!/bin/bash\n"
                           << "# Criado pelo UnBall-Manager2D\n\n"
                           << "if [ -x \"./start\" ]; then\n"
                           << "    ./start \"$@\"\n"
                           << "else\n"
                           << "    echo \"Por favor, substitua isso pelo comando de inicializacao do seu time!\"\n"
                           << "    exit 1\n"
                           << "fi\n";
                    script.close();
                    
                    std::filesystem::permissions(team.root / "start.sh",
                        std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                        std::filesystem::perm_options::add);
                    
                    wxMessageBox("Arquivo 'start.sh' criado com sucesso na pasta do time!", "Sucesso", wxICON_INFORMATION);
                    report("Script start.sh gerado para o time " + wxString::FromUTF8(team.displayName) + ".");
                    return;
                } catch (const std::exception& e) {
                    wxMessageBox("Nao foi possivel criar o arquivo: " + wxString::FromUTF8(e.what()), "Erro", wxICON_ERROR);
                    return;
                }
            }
        }
    }
    
    report(valid ? "Validacao concluida: " + reason : "Validacao encontrou um problema: " + reason);
    wxMessageBox((valid ? "Valido: " : "Invalido: ") + reason, "Validar time", wxICON_INFORMATION);
}

void MainFrame::onValidateAllTeams(wxCommandEvent&) {
    if (store_.teams().empty()) {
        wxMessageBox("A lista de times esta vazia.", "Validar todos", wxICON_INFORMATION);
        return;
    }

    std::vector<const Team*> missingScriptTeams;
    int invalidCount = 0;
    int validCount = 0;

    for (const auto& team : store_.teams()) {
        std::string validationReason;
        if (!isValidTeam(team, validationReason)) {
            invalidCount++;
            continue;
        }
        
        const bool hasStartSh = std::filesystem::exists(team.root / "start.sh");
        const bool hasStart = std::filesystem::exists(team.root / "start");
        const bool hasStartTeam = std::filesystem::exists(team.root / "start_team");
        
        if (!hasStartSh && !hasStart && !hasStartTeam) {
            missingScriptTeams.push_back(&team);
        } else {
            validCount++;
        }
    }

    wxString summary = wxString::Format("Resumo da validacao:\n- %d time(s) totalmente valido(s)\n", validCount);
    if (invalidCount > 0) summary += wxString::Format("- %d time(s) com pasta invalida\n", invalidCount);
    if (!missingScriptTeams.empty()) summary += wxString::Format("- %zu time(s) sem script start.sh\n", missingScriptTeams.size());

    if (missingScriptTeams.empty()) {
        wxMessageBox(summary, "Validar todos", wxICON_INFORMATION);
        report("Validacao de todos os times concluida. Todos possuem scripts de inicializacao.");
        return;
    }

    summary += "\nOs seguintes times precisam de um script 'start.sh':\n";
    for (size_t i = 0; i < std::min(missingScriptTeams.size(), size_t(5)); ++i) {
        summary += "- " + wxString::FromUTF8(missingScriptTeams[i]->displayName) + "\n";
    }
    if (missingScriptTeams.size() > 5) {
        summary += wxString::Format("... e mais %zu time(s).\n", missingScriptTeams.size() - 5);
    }
    summary += "\nDeseja criar scripts genéricos para todos eles agora?";

    int answer = wxMessageBox(summary, "Validacao concluida com pendencias", wxYES_NO | wxICON_WARNING);
    
    if (answer == wxYES) {
        int fixedCount = 0;
        for (const Team* team : missingScriptTeams) {
            try {
                std::ofstream script(team->root / "start.sh");
                script << "#!/bin/bash\n"
                       << "# Criado pelo UnBall-Manager2D\n\n"
                       << "if [ -x \"./start\" ]; then\n"
                       << "    ./start \"$@\"\n"
                       << "else\n"
                       << "    echo \"Por favor, substitua isso pelo comando de inicializacao do seu time!\"\n"
                       << "    exit 1\n"
                       << "fi\n";
                script.close();
                
                std::filesystem::permissions(team->root / "start.sh",
                    std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                    std::filesystem::perm_options::add);
                
                fixedCount++;
            } catch (...) {}
        }
        wxMessageBox(wxString::Format("Scripts criados para %d time(s).", fixedCount), "Sucesso", wxICON_INFORMATION);
        report(wxString::Format("Scripts start.sh gerados para %d time(s).", fixedCount));
    }
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

std::filesystem::path MainFrame::createRunDirectory() const {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream name;
    name << std::put_time(&local, "%Y-%m-%d_%H-%M-%S");
    const auto run = baseDirectory_ / "runs" / "UnBall-Manager2D" / name.str();
    std::filesystem::create_directories(run);
    return run;
}

void MainFrame::startMonitorIfAvailable() {
    if (monitorExecutable_.empty()) {
        report("Aviso: rcssmonitor nao foi configurado.");
        return;
    }
    if (!std::filesystem::is_regular_file(monitorExecutable_)) {
        report("Aviso: o caminho do rcssmonitor nao e valido.");
        return;
    }

    auto monitor = std::make_unique<wxProcess>(this);
    const long pid = wxExecute(shellQuote(monitorExecutable_), wxEXEC_ASYNC | wxEXEC_MAKE_GROUP_LEADER, monitor.get());
    if (pid == 0) {
        report("Aviso: nao foi possivel iniciar o rcssmonitor.");
        return;
    }

    processes_.push_back(std::move(monitor));
    report(wxString::Format("rcssmonitor iniciado (PID %ld).", pid));
}

void MainFrame::stopManagedProcesses() {
    for (const auto& process : processes_) {
        if (process && process->GetPid() > 0) {
            // Em Unix, para garantir que bash e todos os filhos (python, rcssserver, times) 
            // morram juntos, enviamos SIGKILL pro grupo de processos.
            wxExecute(wxString::Format("kill -9 -%ld", process->GetPid()), wxEXEC_SYNC);
            wxProcess::Kill(process->GetPid(), wxSIGKILL, wxKILL_CHILDREN);
        }
    }
}

void MainFrame::onPrepareMatch(wxCommandEvent&) {
    const int left = leftTeam_->GetSelection(), right = rightTeam_->GetSelection();
    report("Preparando partida via auto_match.py...");
    if (left == wxNOT_FOUND || right == wxNOT_FOUND || left == right) { report("Sessao nao criada: selecione dois times diferentes."); wxMessageBox("Selecione dois times diferentes.", "Nova partida", wxICON_WARNING); return; }
    if (autoMatchScript_.empty() || !std::filesystem::is_regular_file(autoMatchScript_)) { report("Partida nao iniciada: configure o caminho de auto_match.py."); wxMessageBox("Configure o caminho de auto_match.py na aba Configuracao.", "Nova partida", wxICON_ERROR); return; }
    if (!std::filesystem::is_directory(baseDirectory_)) { report("Partida nao iniciada: configure a pasta base do RoboCup."); wxMessageBox("Configure a pasta base do RoboCup.", "Nova partida", wxICON_ERROR); return; }
    const auto& a = store_.teams().at(static_cast<std::size_t>(left));
    const auto& b = store_.teams().at(static_cast<std::size_t>(right));
    const auto run = createRunDirectory();
    std::ofstream summary(run / "match.json");
    if (!summary) {
        report("Partida nao iniciada: nao foi possivel criar o resumo da sessao.");
        wxMessageBox("Nao foi possivel criar a pasta da sessao.", "Nova partida", wxICON_ERROR);
        return;
    }
    const nlohmann::json matchSummary{
        {"leftTeam", a.teamName},
        {"rightTeam", b.teamName},
        {"protectedTeams", true},
    };
    summary << matchSummary.dump(2) << '\n';
    const auto base = baseDirectory_;
    const wxString command = "python3 " + shellQuote(autoMatchScript_) + " -d " + shellQuote(base)
        + " -l " + shellQuote(a.teamName) + " -r " + shellQuote(b.teamName) + " -n 1";
    
    const wxString coachVal = coachModeCheck_->GetValue() ? "1" : "0";
    const wxString configCmd = "mkdir -p ~/.rcssserver && touch ~/.rcssserver/server.conf && "
        "sed -i 's/^coach *:.*/coach : " + coachVal + "/' ~/.rcssserver/server.conf && "
        "sed -i 's/^coach_w_referee *:.*/coach_w_referee : " + coachVal + "/' ~/.rcssserver/server.conf";
    
    const wxString shellCommand = "bash -c " + shellQuote(wxString(configCmd + " && cd " + shellQuote(base) + " && " + command).ToStdString());
    report("Comando da partida: " + shellCommand);
    report("Logs esperados em: " + pathString(base / "log_analysis" / "log"));
    
    auto process = std::make_unique<wxProcess>(this);
    process->Redirect();
    const long pid = wxExecute(shellCommand, wxEXEC_ASYNC | wxEXEC_MAKE_GROUP_LEADER, process.get());
    if (pid == 0) { report("Partida nao iniciou: o auto_match.py nao pode ser executado."); wxMessageBox("Nao foi possivel iniciar auto_match.py.", "Nova partida", wxICON_ERROR); return; }
    processes_.push_back(std::move(process));
    outputTimer_.Start(250);
    startMonitorIfAvailable();
    report(wxString::Format("Partida iniciada pelo auto_match.py (PID %ld).", pid));
}

void MainFrame::onSaveEnvironment(wxCommandEvent&) {
    baseDirectory_ = baseDirectoryField_->GetValue().ToStdString();
    serverExecutable_ = serverField_->GetValue().ToStdString();
    monitorExecutable_ = monitorField_->GetValue().ToStdString();
    autoMatchScript_ = autoMatchField_->GetValue().ToStdString();

    if (!std::filesystem::is_directory(baseDirectory_) ||
        !std::filesystem::is_regular_file(serverExecutable_) ||
        !std::filesystem::is_regular_file(autoMatchScript_)) {
        wxMessageBox("Informe uma pasta base, o rcssserver e o auto_match.py validos.",
            "Configuracao", wxICON_WARNING);
        return;
    }
    if (!monitorExecutable_.empty() && !std::filesystem::is_regular_file(monitorExecutable_)) {
        wxMessageBox("O caminho do rcssmonitor e invalido.", "Configuracao", wxICON_WARNING);
        return;
    }

    saveEnvironment();
    updateEnvironmentStatus();
    refreshReplays();
    report("Configuracao de ferramentas salva.");
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

void MainFrame::onStartServer(wxCommandEvent&) {
    if (!std::filesystem::is_regular_file(serverExecutable_)) {
        report("Servidor nao iniciado: rcssserver nao configurado.");
        wxMessageBox("Configure o caminho do rcssserver na aba Configuracao.", "Servidor", wxICON_ERROR);
        return;
    }
    report("Iniciando rcssserver...");
    
    auto process = std::make_unique<wxProcess>(this);
    process->Redirect();
    const long pid = wxExecute(shellQuote(serverExecutable_), wxEXEC_ASYNC, process.get());
    if (pid == 0) { report("Servidor nao iniciou. Consulte o ambiente e os logs."); wxMessageBox("Nao foi possivel iniciar o rcssserver.", "Servidor", wxICON_ERROR); return; }
    processes_.push_back(std::move(process));
    startMonitorIfAvailable();
    report(wxString::Format("rcssserver iniciado (PID %ld). Aguardando conexoes dos times.", pid));
}

void MainFrame::onStopAll(wxCommandEvent&) {
    if (!processes_.empty()) {
        stopManagedProcesses();
        report("Encerramento solicitado para os processos iniciados por esta sessao.");
    }
    else report("Nao ha processos iniciados pelo UnBall-Manager2D para encerrar.");
}

void MainFrame::onProcessTerminated(wxProcessEvent& event) {
    report(wxString::Format("Processo %ld terminou com codigo %d.",
        static_cast<long>(event.GetPid()), static_cast<int>(event.GetExitCode())));
    std::erase_if(processes_, [&event](const std::unique_ptr<wxProcess>& process) {
        return process && process->GetPid() == event.GetPid();
    });
    if (processes_.empty()) outputTimer_.Stop();
}

void MainFrame::onPollProcessOutput(wxTimerEvent&) {
    for (const auto& process : processes_) {
        if (!process) continue;
        auto readStream = [this](wxInputStream* stream) {
            char buffer[512];
            while (stream && stream->CanRead()) {
                stream->Read(buffer, sizeof(buffer) - 1);
                const auto count = stream->LastRead();
                if (count == 0) break;
                buffer[count] = '\0';
                appendEvent(wxString::FromUTF8(buffer));
            }
        };
        if (process->IsInputAvailable()) readStream(process->GetInputStream());
        if (process->IsErrorAvailable()) readStream(process->GetErrorStream());
    }
}

void MainFrame::appendEvent(const wxString& text) {
    if (events_) events_->AppendText(text + "\n");
}

void MainFrame::report(const wxString& text) {
    const wxString message = wxDateTime::Now().FormatISOTime() + "  " + text;
    if (status_) status_->SetStatusText(text);
    appendEvent(message);
}

void MainFrame::refreshReplays() {
    replayList_->DeleteAllItems();
    currentReplays_.clear();

    const std::vector<std::filesystem::path> searchPaths = {
        baseDirectory_ / "log_analysis" / "log",
        baseDirectory_ / "log"
    };

    std::vector<std::filesystem::path> foundFiles;
    for (const auto& path : searchPaths) {
        if (std::filesystem::is_directory(path)) {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_regular_file() && (entry.path().extension() == ".rcg" || entry.path().extension() == ".gz")) {
                    foundFiles.push_back(entry.path());
                }
            }
        }
    }
    // Ordenar do mais recente para o mais antigo
    std::sort(foundFiles.begin(), foundFiles.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        try { return std::filesystem::last_write_time(a) > std::filesystem::last_write_time(b); }
        catch(...) { return false; }
    });

    currentReplays_ = std::move(foundFiles);
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
        replayList_->SetItem(row, 1, pathString(logFile.filename()));
        ++row;
    }
    if (!currentReplays_.empty()) replayList_->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
}

void MainFrame::onRefreshReplays(wxCommandEvent&) {
    refreshReplays();
    report(wxString::Format("Lista de replays atualizada: %lu log(s) encontrado(s).",
        static_cast<unsigned long>(currentReplays_.size())));
}

void MainFrame::onWatchSelectedReplay(wxCommandEvent&) {
    const long selection = replayList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selection == -1 || static_cast<std::size_t>(selection) >= currentReplays_.size()) {
        wxMessageBox("Selecione um replay na lista.", "Assistir Replay", wxICON_INFORMATION);
        return;
    }
    const auto logPath = currentReplays_.at(static_cast<std::size_t>(selection));
    
    if (monitorExecutable_.empty() || !std::filesystem::is_regular_file(monitorExecutable_)) {
        wxMessageBox("Configure o caminho do rcssmonitor na aba Configuracao.", "Assistir Replay", wxICON_ERROR);
        return;
    }
    
    report("Iniciando reproducao de log: " + pathString(logPath.filename()));
    const wxString shellCommand = shellQuote(monitorExecutable_) + " " + shellQuote(logPath);
    
    auto process = std::make_unique<wxProcess>(this);
    const long pid = wxExecute(shellCommand, wxEXEC_ASYNC | wxEXEC_MAKE_GROUP_LEADER, process.get());
    if (pid == 0) {
        wxMessageBox("Nao foi possivel iniciar o rcssmonitor.", "Assistir Replay", wxICON_ERROR);
        return;
    }
    processes_.push_back(std::move(process));
    report(wxString::Format("rcssmonitor reproduzindo log (PID %ld).", pid));
}

void MainFrame::onWatchManualReplay(wxCommandEvent&) {
    const wxString path = chooseFile(this, pathString(baseDirectory_), "Selecione o arquivo de log (.rcg)", "Logs do RoboCup (*.rcg;*.rcg.gz)|*.rcg;*.rcg.gz|Todos os arquivos (*)|*");
    if (path.empty()) return;
    
    if (monitorExecutable_.empty() || !std::filesystem::is_regular_file(monitorExecutable_)) {
        wxMessageBox("Configure o caminho do rcssmonitor na aba Configuracao.", "Assistir Replay", wxICON_ERROR);
        return;
    }
    
    report("Iniciando reproducao de log: " + path);
    const wxString shellCommand = shellQuote(monitorExecutable_) + " " + shellQuote(std::filesystem::path(path.ToStdString()));
    
    auto process = std::make_unique<wxProcess>(this);
    const long pid = wxExecute(shellCommand, wxEXEC_ASYNC | wxEXEC_MAKE_GROUP_LEADER, process.get());
    if (pid == 0) {
        wxMessageBox("Nao foi possivel iniciar o rcssmonitor.", "Assistir Replay", wxICON_ERROR);
        return;
    }
    processes_.push_back(std::move(process));
    report(wxString::Format("rcssmonitor reproduzindo log manual (PID %ld).", pid));
}
