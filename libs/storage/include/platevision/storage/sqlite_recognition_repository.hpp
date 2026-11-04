#pragma once

#include "platevision/interfaces.hpp"

#include <filesystem>
#include <string>

struct sqlite3;

namespace platevision {

class SqliteRecognitionRepository final
    : public IRecognitionRepository {
public:
    explicit SqliteRecognitionRepository(
        std::filesystem::path database_path);
    ~SqliteRecognitionRepository() override;

    bool initialize(std::string& error) override;
    bool save(
        const RecognitionEvent& event,
        std::string& error) override;
    std::vector<RecognitionEvent> query(
        const RecordQuery& query,
        std::string& error) override;

private:
    bool execute(const char* sql, std::string& error);

    std::filesystem::path database_path_;
    sqlite3* database_{nullptr};
};

}  // namespace platevision

