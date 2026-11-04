#include "platevision/storage/sqlite_recognition_repository.hpp"

#include <winsqlite/winsqlite3.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace platevision {
namespace {

std::int64_t nowMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string sqliteError(sqlite3* database) {
    return database == nullptr
        ? "SQLite database is not open"
        : sqlite3_errmsg(database);
}

class Statement final {
public:
    Statement(sqlite3* database, const char* sql, std::string& error)
        : database_(database) {
        if (sqlite3_prepare_v2(
                database_,
                sql,
                -1,
                &statement_,
                nullptr) != SQLITE_OK) {
            error = sqliteError(database_);
        }
    }

    ~Statement() {
        if (statement_ != nullptr) {
            sqlite3_finalize(statement_);
        }
    }

    sqlite3_stmt* get() const noexcept {
        return statement_;
    }

    bool valid() const noexcept {
        return statement_ != nullptr;
    }

private:
    sqlite3* database_;
    sqlite3_stmt* statement_{nullptr};
};

}  // namespace

SqliteRecognitionRepository::SqliteRecognitionRepository(
    std::filesystem::path database_path)
    : database_path_(std::move(database_path)) {}

SqliteRecognitionRepository::~SqliteRecognitionRepository() {
    if (database_ != nullptr) {
        sqlite3_close(database_);
    }
}

bool SqliteRecognitionRepository::initialize(std::string& error) {
    if (database_ != nullptr) {
        error.clear();
        return true;
    }

    if (database_path_.has_parent_path()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(
            database_path_.parent_path(),
            filesystem_error);
        if (filesystem_error) {
            error = "failed to create database directory: " +
                filesystem_error.message();
            return false;
        }
    }

    const std::wstring native_path = database_path_.wstring();
    if (sqlite3_open16(
            native_path.c_str(),
            &database_) != SQLITE_OK) {
        error = "failed to open SQLite database: " +
            sqliteError(database_);
        return false;
    }

    sqlite3_busy_timeout(database_, 2000);
    if (!execute("PRAGMA journal_mode=WAL;", error) ||
        !execute("PRAGMA foreign_keys=ON;", error) ||
        !execute(
            "CREATE TABLE IF NOT EXISTS cameras ("
            "id INTEGER PRIMARY KEY,"
            "name TEXT NOT NULL,"
            "source_uri TEXT NOT NULL,"
            "enabled INTEGER NOT NULL DEFAULT 1);",
            error) ||
        !execute(
            "CREATE TABLE IF NOT EXISTS recognition_events ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "camera_id INTEGER NOT NULL,"
            "stream_id INTEGER NOT NULL,"
            "track_id INTEGER NOT NULL,"
            "plate_number TEXT NOT NULL,"
            "plate_color TEXT NOT NULL,"
            "confidence REAL NOT NULL,"
            "captured_at_ms INTEGER NOT NULL,"
            "snapshot_path TEXT,"
            "bbox_json TEXT,"
            "created_at_ms INTEGER NOT NULL,"
            "FOREIGN KEY (camera_id) REFERENCES cameras(id));",
            error) ||
        !execute(
            "CREATE INDEX IF NOT EXISTS idx_events_time "
            "ON recognition_events(captured_at_ms DESC);",
            error) ||
        !execute(
            "CREATE INDEX IF NOT EXISTS idx_events_plate "
            "ON recognition_events(plate_number, captured_at_ms DESC);",
            error) ||
        !execute(
            "CREATE INDEX IF NOT EXISTS idx_events_camera_time "
            "ON recognition_events(camera_id, captured_at_ms DESC);",
            error) ||
        !execute(
            "CREATE TABLE IF NOT EXISTS schema_meta ("
            "key TEXT PRIMARY KEY,"
            "value TEXT NOT NULL);",
            error) ||
        !execute(
            "INSERT OR REPLACE INTO schema_meta(key, value) "
            "VALUES('schema_version', '1');",
            error) ||
        !execute(
            "INSERT OR IGNORE INTO cameras(id, name, source_uri, enabled) "
            "VALUES(1, 'Local CLI', 'local', 1);",
            error)) {
        return false;
    }
    return true;
}

bool SqliteRecognitionRepository::save(
    const RecognitionEvent& event,
    std::string& error) {
    if (database_ == nullptr) {
        error = "SQLite repository is not initialized";
        return false;
    }

    Statement statement(
        database_,
        "INSERT INTO recognition_events("
        "camera_id, stream_id, track_id, plate_number, plate_color,"
        "confidence, captured_at_ms, snapshot_path, bbox_json,"
        "created_at_ms) VALUES(?,?,?,?,?,?,?,?,?,?);",
        error);
    if (!statement.valid()) {
        return false;
    }

    std::ostringstream bbox;
    bbox << "{\"x\":" << event.bounding_box.x
         << ",\"y\":" << event.bounding_box.y
         << ",\"width\":" << event.bounding_box.width
         << ",\"height\":" << event.bounding_box.height << "}";
    const std::string bbox_json = bbox.str();

    sqlite3_bind_int64(
        statement.get(),
        1,
        static_cast<sqlite3_int64>(event.camera_id));
    sqlite3_bind_int64(
        statement.get(),
        2,
        static_cast<sqlite3_int64>(event.stream_id));
    sqlite3_bind_int64(
        statement.get(),
        3,
        static_cast<sqlite3_int64>(event.track_id));
    sqlite3_bind_text(
        statement.get(),
        4,
        event.plate_number.c_str(),
        -1,
        SQLITE_TRANSIENT);
    sqlite3_bind_text(
        statement.get(),
        5,
        toString(event.color),
        -1,
        SQLITE_STATIC);
    sqlite3_bind_double(statement.get(), 6, event.confidence);
    sqlite3_bind_int64(
        statement.get(),
        7,
        event.captured_at_us / 1000);
    sqlite3_bind_text(
        statement.get(),
        8,
        event.snapshot_path.c_str(),
        -1,
        SQLITE_TRANSIENT);
    sqlite3_bind_text(
        statement.get(),
        9,
        bbox_json.c_str(),
        -1,
        SQLITE_TRANSIENT);
    sqlite3_bind_int64(
        statement.get(),
        10,
        nowMilliseconds());

    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        error = sqliteError(database_);
        return false;
    }
    error.clear();
    return true;
}

std::vector<RecognitionEvent> SqliteRecognitionRepository::query(
    const RecordQuery& query,
    std::string& error) {
    std::vector<RecognitionEvent> events;
    if (database_ == nullptr) {
        error = "SQLite repository is not initialized";
        return events;
    }

    Statement statement(
        database_,
        "SELECT id, camera_id, stream_id, track_id, plate_number,"
        "plate_color, confidence, captured_at_ms, snapshot_path "
        "FROM recognition_events "
        "WHERE (? IS NULL OR camera_id = ?) "
        "AND (? IS NULL OR plate_number = ?) "
        "AND captured_at_ms BETWEEN ? AND ? "
        "ORDER BY captured_at_ms DESC LIMIT ? OFFSET ?;",
        error);
    if (!statement.valid()) {
        return events;
    }

    if (query.camera_id) {
        sqlite3_bind_int64(
            statement.get(),
            1,
            static_cast<sqlite3_int64>(*query.camera_id));
        sqlite3_bind_int64(
            statement.get(),
            2,
            static_cast<sqlite3_int64>(*query.camera_id));
    } else {
        sqlite3_bind_null(statement.get(), 1);
        sqlite3_bind_null(statement.get(), 2);
    }
    if (query.plate_number) {
        sqlite3_bind_text(
            statement.get(),
            3,
            query.plate_number->c_str(),
            -1,
            SQLITE_TRANSIENT);
        sqlite3_bind_text(
            statement.get(),
            4,
            query.plate_number->c_str(),
            -1,
            SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(statement.get(), 3);
        sqlite3_bind_null(statement.get(), 4);
    }
    sqlite3_bind_int64(statement.get(), 5, query.begin_ms);
    sqlite3_bind_int64(
        statement.get(),
        6,
        query.end_ms > 0
            ? query.end_ms
            : std::numeric_limits<std::int64_t>::max());
    sqlite3_bind_int(statement.get(), 7, query.limit);
    sqlite3_bind_int(statement.get(), 8, query.offset);

    while (sqlite3_step(statement.get()) == SQLITE_ROW) {
        RecognitionEvent event;
        event.event_id = sqlite3_column_int64(statement.get(), 0);
        event.camera_id = static_cast<std::uint64_t>(
            sqlite3_column_int64(statement.get(), 1));
        event.stream_id = static_cast<std::uint64_t>(
            sqlite3_column_int64(statement.get(), 2));
        event.track_id = static_cast<std::uint64_t>(
            sqlite3_column_int64(statement.get(), 3));
        const unsigned char* plate =
            sqlite3_column_text(statement.get(), 4);
        event.plate_number = plate == nullptr
            ? std::string{}
            : reinterpret_cast<const char*>(plate);
        const unsigned char* color =
            sqlite3_column_text(statement.get(), 5);
        const std::string color_text = color == nullptr
            ? std::string{}
            : reinterpret_cast<const char*>(color);
        event.color = color_text == "green"
            ? PlateColor::Green
            : (color_text == "blue"
                   ? PlateColor::Blue
                   : (color_text == "yellow"
                          ? PlateColor::Yellow
                          : PlateColor::Unknown));
        event.confidence =
            static_cast<float>(
                sqlite3_column_double(statement.get(), 6));
        event.captured_at_us =
            sqlite3_column_int64(statement.get(), 7) * 1000;
        const unsigned char* snapshot =
            sqlite3_column_text(statement.get(), 8);
        event.snapshot_path = snapshot == nullptr
            ? std::string{}
            : reinterpret_cast<const char*>(snapshot);
        events.push_back(std::move(event));
    }
    error.clear();
    return events;
}

bool SqliteRecognitionRepository::execute(
    const char* sql,
    std::string& error) {
    char* message = nullptr;
    const int code = sqlite3_exec(
        database_,
        sql,
        nullptr,
        nullptr,
        &message);
    if (code != SQLITE_OK) {
        error = message == nullptr
            ? sqliteError(database_)
            : message;
        sqlite3_free(message);
        return false;
    }
    error.clear();
    return true;
}

}  // namespace platevision

