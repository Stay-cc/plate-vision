#include "platevision/storage/sqlite_recognition_repository.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;
using namespace platevision;

struct Options {
    fs::path database{"data/plate_vision.db"};
    fs::path output_csv;
    std::optional<std::string> plate;
    std::optional<std::uint64_t> camera_id;
    int limit{100};
    bool show_help{false};
};

bool parseInt(const std::string& text, long long& value) {
    try {
        std::size_t consumed = 0;
        value = std::stoll(text, &consumed);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

bool parseOptions(
    const std::vector<std::string>& arguments,
    Options& options,
    std::string& error) {
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        auto requireValue = [&](std::string& value) {
            if (index + 1 >= arguments.size()) {
                error = "missing value for option: " + argument;
                return false;
            }
            value = arguments[++index];
            return true;
        };

        std::string value;
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
        } else if (argument == "--database") {
            if (!requireValue(value)) {
                return false;
            }
            options.database = fs::u8path(value);
        } else if (argument == "--output-csv") {
            if (!requireValue(value)) {
                return false;
            }
            options.output_csv = fs::u8path(value);
        } else if (argument == "--plate") {
            if (!requireValue(value)) {
                return false;
            }
            options.plate = value;
        } else if (argument == "--camera") {
            if (!requireValue(value)) {
                return false;
            }
            long long camera = 0;
            if (!parseInt(value, camera) || camera <= 0) {
                error = "invalid camera id";
                return false;
            }
            options.camera_id = static_cast<std::uint64_t>(camera);
        } else if (argument == "--limit") {
            if (!requireValue(value)) {
                return false;
            }
            long long limit = 0;
            if (!parseInt(value, limit) || limit <= 0 ||
                limit > 10000) {
                error = "invalid record limit";
                return false;
            }
            options.limit = static_cast<int>(limit);
        } else {
            error = "unknown option: " + argument;
            return false;
        }
    }
    return true;
}

void printUsage() {
    std::cout
        << "Usage:\n"
        << "  plate_records [options]\n\n"
        << "Options:\n"
        << "  --database <path>    SQLite database path\n"
        << "  --plate <text>       filter by plate number\n"
        << "  --camera <id>        filter by camera id\n"
        << "  --limit <n>          maximum rows, default 100\n"
        << "  --output-csv <path>  export result to CSV\n";
}

std::string timestampText(std::int64_t timestamp_us) {
    const std::time_t seconds = static_cast<std::time_t>(
        timestamp_us / 1000000);
    std::tm time{};
#ifdef _WIN32
    localtime_s(&time, &seconds);
#else
    localtime_r(&seconds, &time);
#endif
    std::ostringstream stream;
    stream << std::put_time(&time, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::string csvField(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) {
        return value;
    }
    std::string escaped = "\"";
    for (const char character : value) {
        if (character == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(character);
        }
    }
    escaped.push_back('"');
    return escaped;
}

bool writeCsv(
    const fs::path& path,
    const std::vector<RecognitionEvent>& events,
    std::string& error) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "failed to open CSV output: " + path.string();
        return false;
    }
    output << "id,camera_id,stream_id,track_id,plate_number,color,"
              "confidence,captured_at,snapshot_path\n";
    for (const RecognitionEvent& event : events) {
        output
            << event.event_id << ","
            << event.camera_id << ","
            << event.stream_id << ","
            << event.track_id << ","
            << csvField(event.plate_number) << ","
            << toString(event.color) << ","
            << event.confidence << ","
            << csvField(timestampText(event.captured_at_us)) << ","
            << csvField(event.snapshot_path) << "\n";
    }
    error.clear();
    return true;
}

int run(const std::vector<std::string>& arguments) {
    Options options;
    std::string error;
    if (!parseOptions(arguments, options, error)) {
        std::cerr << error << "\n\n";
        printUsage();
        return 1;
    }
    if (options.show_help) {
        printUsage();
        return 0;
    }

    SqliteRecognitionRepository repository(options.database);
    if (!repository.initialize(error)) {
        std::cerr << error << "\n";
        return 2;
    }

    RecordQuery query;
    query.plate_number = options.plate;
    query.camera_id = options.camera_id;
    query.limit = options.limit;
    const std::vector<RecognitionEvent> events =
        repository.query(query, error);
    if (!error.empty()) {
        std::cerr << error << "\n";
        return 3;
    }

    std::cout << "records=" << events.size() << "\n";
    for (const RecognitionEvent& event : events) {
        std::cout
            << event.event_id << " | "
            << timestampText(event.captured_at_us) << " | "
            << event.plate_number << " | "
            << toString(event.color) << " | "
            << event.snapshot_path << "\n";
    }

    if (!options.output_csv.empty()) {
        if (!writeCsv(options.output_csv, events, error)) {
            std::cerr << error << "\n";
            return 4;
        }
        std::cout << "csv=" << options.output_csv.string() << "\n";
    }
    return 0;
}

#ifdef _WIN32
std::vector<std::string> narrowArguments(
    int argc,
    wchar_t** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(std::max(0, argc - 1)));
    for (int index = 1; index < argc; ++index) {
        const int required = WideCharToMultiByte(
            CP_UTF8,
            0,
            argv[index],
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);
        std::string value(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            argv[index],
            -1,
            value.data(),
            required,
            nullptr,
            nullptr);
        if (!value.empty() && value.back() == '\0') {
            value.pop_back();
        }
        arguments.push_back(std::move(value));
    }
    return arguments;
}
#endif

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    return run(narrowArguments(argc, argv));
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return run(arguments);
}
#endif

