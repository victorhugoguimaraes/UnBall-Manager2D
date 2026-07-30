#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct Team {
    std::string displayName;
    std::string teamName;
    std::filesystem::path root;
    bool protectedMode{true};
};

class TeamStore {
public:
    explicit TeamStore(std::filesystem::path file);

    void load();
    void save() const;
    const std::vector<Team>& teams() const noexcept { return teams_; }
    void add(Team team);
    void remove(size_t index);

private:
    std::filesystem::path file_;
    std::vector<Team> teams_;
};

bool isValidTeam(const Team& team, std::string& reason);
