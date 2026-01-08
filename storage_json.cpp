// storage_json.cpp
// Consolidated JSON storage for sensor readings (moved from storage.cpp)

#include "storage_json.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <iterator>
#include <cctype>

// helpers (internal)
static bool extract_json_value_at(const std::string &s, size_t pos, std::string &out, size_t &new_pos) {
    while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
    if (pos >= s.size()) return false;
    char c = s[pos];
    if (c == '{' || c == '[') {
        char open = c;
        char close = (c == '{') ? '}' : ']';
        size_t i = pos;
        int depth = 0;
        while (i < s.size()) {
            char ch = s[i];
            if (ch == '"') {
                ++i;
                while (i < s.size()) {
                    if (s[i] == '\\') { i += 2; continue; }
                    if (s[i] == '"') { ++i; break; }
                    ++i;
                }
                continue;
            } else if (ch == open) {
                ++depth;
            } else if (ch == close) {
                --depth;
                if (depth == 0) { ++i; break; }
            }
            ++i;
        }
        if (depth != 0) return false;
        out = s.substr(pos, i - pos);
        new_pos = i;
        return true;
    } else if (c == '"') {
        size_t i = pos + 1;
        while (i < s.size()) {
            if (s[i] == '\\') { i += 2; continue; }
            if (s[i] == '"') { ++i; break; }
            ++i;
        }
        if (i > s.size()) return false;
        out = s.substr(pos, i - pos);
        new_pos = i;
        return true;
    } else {
        size_t i = pos;
        while (i < s.size()) {
            if (s[i] == ',' || s[i] == '}' || s[i] == ']') break;
            ++i;
        }
        if (i == pos) return false;
        size_t r = i;
        while (r > pos && std::isspace((unsigned char)s[r-1])) --r;
        out = s.substr(pos, r - pos);
        new_pos = i;
        return true;
    }
}

std::string json_escape(const std::string &s) {
    std::string o; o.reserve(s.size()*2);
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default: o.push_back(c);
        }
    }
    return o;
}

// Return latest reading for `id`. Prefer in-memory cache; fallback to consolidated JSON file.
std::string read_sensor_data(const std::string &id) {
    std::string sid = sanitize_id(id);
    {
        std::lock_guard<std::mutex> lk(in_memory_mutex);
        auto it = in_memory_readings.find(sid);
        if (it != in_memory_readings.end()) return it->second;
    }
    std::ifstream ifs(SENSOR_DATA_JSON_FILE);
    if (!ifs) return std::string();
    std::string s((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    size_t obj_start = s.find('{');
    if (obj_start == std::string::npos) return std::string();
    size_t pos = obj_start + 1;
    while (pos < s.size()) {
        while (pos < s.size() && (std::isspace((unsigned char)s[pos]) || s[pos] == ',')) ++pos;
        if (pos >= s.size() || s[pos] == '}') break;
        if (s[pos] != '"') break;
        size_t key_start = pos + 1;
        size_t i = key_start;
        while (i < s.size()) {
            if (s[i] == '\\') { i += 2; continue; }
            if (s[i] == '"') break;
            ++i;
        }
        if (i >= s.size()) break;
        std::string key = s.substr(key_start, i - key_start);
        pos = i + 1;
        while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
        if (pos >= s.size() || s[pos] != ':') break;
        ++pos;
        std::string val; size_t val_end;
        if (!extract_json_value_at(s, pos, val, val_end)) break;
        if (key == sid) return val;
        pos = val_end;
    }
    return std::string();
}

// Return JSON object mapping sensor id -> payload. In-memory override file entries.
std::string all_sensors_json() {
    std::ostringstream out;
    out << "{";
    bool first = true;
    {
        std::lock_guard<std::mutex> lk(in_memory_mutex);
        for (const auto &kv : in_memory_readings) {
            if (!first) out << ",";
            first = false;
            out << "\"" << kv.first << "\":" << kv.second;
        }
    }
    std::ifstream ifs(SENSOR_DATA_JSON_FILE);
    if (ifs) {
        std::string s((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        size_t obj_start = s.find('{');
        if (obj_start != std::string::npos) {
            size_t pos = obj_start + 1;
            while (pos < s.size()) {
                while (pos < s.size() && (std::isspace((unsigned char)s[pos]) || s[pos] == ',')) ++pos;
                if (pos >= s.size() || s[pos] == '}') break;
                if (s[pos] != '"') break;
                size_t key_start = pos + 1;
                size_t i = key_start;
                while (i < s.size()) {
                    if (s[i] == '\\') { i += 2; continue; }
                    if (s[i] == '"') break;
                    ++i;
                }
                if (i >= s.size()) break;
                std::string key = s.substr(key_start, i - key_start);
                pos = i + 1;
                while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
                if (pos >= s.size() || s[pos] != ':') break;
                ++pos;
                std::string val; size_t val_end;
                if (!extract_json_value_at(s, pos, val, val_end)) break;
                {
                    std::lock_guard<std::mutex> lk(in_memory_mutex);
                    if (in_memory_readings.find(key) == in_memory_readings.end()) {
                        if (!first) out << ",";
                        first = false;
                        out << "\"" << key << "\":" << val;
                    }
                }
                pos = val_end;
            }
        }
    }
    out << "}";
    return out.str();
}

// Flush in-memory readings to the consolidated JSON file atomically.
// Legacy per-file writes removed.
void flush_readings_to_disk() {
    std::unordered_map<std::string, std::string> copy;
    {
        std::lock_guard<std::mutex> lk(in_memory_mutex);
        copy = in_memory_readings;
    }
    // Read existing file into map
    std::unordered_map<std::string, std::string> combined;
    std::ifstream ifs(SENSOR_DATA_JSON_FILE);
    if (ifs) {
        std::string s((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        size_t obj_start = s.find('{');
        if (obj_start != std::string::npos) {
            size_t pos = obj_start + 1;
            while (pos < s.size()) {
                while (pos < s.size() && (std::isspace((unsigned char)s[pos]) || s[pos] == ',')) ++pos;
                if (pos >= s.size() || s[pos] == '}') break;
                if (s[pos] != '"') break;
                size_t key_start = pos + 1;
                size_t i = key_start;
                while (i < s.size()) {
                    if (s[i] == '\\') { i += 2; continue; }
                    if (s[i] == '"') break;
                    ++i;
                }
                if (i >= s.size()) break;
                std::string key = s.substr(key_start, i - key_start);
                pos = i + 1;
                while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
                if (pos >= s.size() || s[pos] != ':') break;
                ++pos;
                std::string val; size_t val_end;
                if (!extract_json_value_at(s, pos, val, val_end)) break;
                combined[key] = val;
                pos = val_end;
            }
        }
    }
    // Merge with in-memory
    for (const auto &kv : copy) combined[kv.first] = kv.second;

    // Build JSON
    std::ostringstream js;
    js << "{";
    bool first = true;
    for (const auto &kv : combined) {
        if (!first) js << ",";
        first = false;
        js << "\"" << json_escape(kv.first) << "\":" << kv.second;
    }
    js << "}";

    // atomic write
    std::filesystem::path p(SENSOR_DATA_JSON_FILE);
    auto parent = p.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::string tmp = SENSOR_DATA_JSON_FILE + ".tmp";
    std::ofstream ofs(tmp, std::ios::trunc);
    if (!ofs) return;
    ofs << js.str();
    ofs.close();
    std::error_code ec;
    std::filesystem::rename(tmp, SENSOR_DATA_JSON_FILE, ec);
    if (ec) std::rename(tmp.c_str(), SENSOR_DATA_JSON_FILE.c_str());

    // Flush pending trigger events (append) and clear in-memory queue
        std::deque<std::string> pending;
    {
        std::lock_guard<std::mutex> lk(in_memory_triggers_mutex);
        pending.swap(in_memory_triggers);
    }
    if (!pending.empty()) {
        // read existing persisted triggers
        std::deque<std::string> existing;
        std::ifstream ifs2(TRIGGERS_LOG_FILE);
        if (ifs2) {
            std::string line;
            while (std::getline(ifs2, line)) {
                if (line.empty()) continue;
                existing.push_back(line);
            }
        }
        // append pending in-memory events
        for (const auto &line : pending) existing.push_back(line);

        // trim to keep only latest MAX_TRIGGER_EVENTS entries
        int maxv = MAX_TRIGGER_EVENTS.load();
        if (maxv > 0 && (int)existing.size() > maxv) {
            std::deque<std::string> tmp;
            size_t start = existing.size() - maxv;
            for (size_t i = start; i < existing.size(); ++i) tmp.push_back(existing[i]);
            existing.swap(tmp);
        }

        // atomic write the trimmed list back to disk
        std::filesystem::path tp(TRIGGERS_LOG_FILE);
        auto parent = tp.parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        std::string ttmp = TRIGGERS_LOG_FILE + ".tmp";
        std::ofstream ofs2(ttmp, std::ios::trunc);
        if (ofs2) {
            for (const auto &line : existing) ofs2 << line << "\n";
            ofs2.close();
            std::error_code ec2;
            std::filesystem::rename(ttmp, TRIGGERS_LOG_FILE, ec2);
            if (ec2) std::rename(ttmp.c_str(), TRIGGERS_LOG_FILE.c_str());
        } else {
            // write failed: requeue pending back into memory
            std::lock_guard<std::mutex> lk(in_memory_triggers_mutex);
            for (const auto &line : pending) in_memory_triggers.push_back(line);
        }
    }
}
