#pragma once

#include "ChildProcess.hpp"
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Serializes exports independently of match processes. Each state is persisted for the UI.
class DataExport final {
public:
    DataExport(std::string interpreter, std::filesystem::path moduleDirectory);
    bool enqueue(const std::filesystem::path& session);
    bool poll();
    bool busy() const noexcept;
    bool contains(const std::filesystem::path& session) const;
    const std::string& message() const noexcept { return message_; }
private:
    void state(const std::filesystem::path& session, const std::string& value, const std::string& detail = {});
    std::string interpreter_;
    std::filesystem::path moduleDirectory_;
    std::deque<std::filesystem::path> queue_;
    std::unique_ptr<ChildProcess> process_;
    std::filesystem::path current_;
    std::string output_, message_;
};

struct DataSession {
    std::filesystem::path directory;
    std::string label;
    std::string createdAt;
    std::string batchId;
    int batchIndex = 0, batchTotal = 0;
    std::filesystem::path leftRoot, rightRoot, replay;
    std::string status;
    std::string coverage;
    std::string detail;
    std::filesystem::path exportDirectory;
};
std::vector<DataSession> findDataSessions(const std::filesystem::path& base);

std::filesystem::path csvDirectory(const std::filesystem::path& bundle);
