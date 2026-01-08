/*
 * Copyright (C) 2026 github.com/jakubpolomsky
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Author: github.com/jakubpolomsky
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "storage.h"
#include "storage_json.h"

// define SETTINGS_JSON_FILE default
std::string SETTINGS_JSON_FILE = "settings.json";
// define TRIGGERS_LOG_FILE default
std::string TRIGGERS_LOG_FILE = "triggers.log";
// define SENSOR_DATA_JSON_FILE default
std::string SENSOR_DATA_JSON_FILE = "sensor_data.json";

// in-memory cache for latest readings (sensor id -> JSON payload)
// defined here and exposed via `extern` in storage.h so JSON helpers
// implemented in storage_json.cpp can access it.
std::unordered_map<std::string, std::string> in_memory_readings;
std::mutex in_memory_mutex;
// in-memory queue for trigger events (to be flushed periodically)
std::deque<std::string> in_memory_triggers;
std::mutex in_memory_triggers_mutex;

// maximum triggers to hold in memory before dropping oldest entries
std::atomic<int> MAX_TRIGGER_EVENTS(100);
// triggers enabled by default
std::atomic<bool> TRIGGERS_ENABLED(true);

std::map<std::string, std::string> get_all_trigger_urls(const std::string &type) {
    std::map<std::string, std::tuple<std::optional<double>, std::string, std::string>> m;
    read_settings_map(m);
    std::map<std::string, std::string> res;
    for (const auto &kv : m) {
        const std::string &room = kv.first;
        const auto &tpl = kv.second;
        const std::string &high = std::get<1>(tpl);
        const std::string &low = std::get<2>(tpl);
        if (type == "high" && !high.empty()) res[room] = high;
        if (type == "low" && !low.empty()) res[room] = low;
    }
    return res;
}

// flusher thread control
static std::thread flusher_thread;
static std::atomic<bool> flusher_running(false);
static std::condition_variable flusher_cv;
static std::mutex flusher_mutex;
static int flusher_interval_seconds = 3600; // default: 1 hour

std::string sanitize_id(const std::string &id) {
    std::string out;
    for (char c : id) {
        if (std::isalnum((unsigned char)c) || c=='-' || c=='_' ) out.push_back(c);
    }
    if (out.empty()) out = "unknown";
    return out;
}

bool save_sensor_data(const std::string &id, const std::string &body) {
    // store latest reading in memory; flusher will persist to disk periodically
    std::string sid = sanitize_id(id);
    std::lock_guard<std::mutex> lk(in_memory_mutex);
    in_memory_readings[sid] = body;
    return true;
}

// read_sensor_data: implemented in storage_json.cpp

// Return a JSON object mapping sensor id -> stored JSON payload
// all_sensors_json: implemented in storage_json.cpp

// Read settings JSON into map: room -> (optional desired, high, low)
bool read_settings_map(std::map<std::string, std::tuple<std::optional<double>, std::string, std::string>> &out) {
    std::ifstream ifs(SETTINGS_JSON_FILE);
    out.clear();
    if (!ifs) return false;
    std::string s((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    try {
        std::regex entry_re(R"RE("([^"]+)"\s*:\s*\{([^}]*)\})RE");
        auto begin = std::sregex_iterator(s.begin(), s.end(), entry_re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::smatch match = *it;
            std::string room = match[1].str();
            std::string body = match[2].str();
            std::optional<double> desired;
            std::string high, low;
            std::smatch sub;
            if (std::regex_search(body, sub, std::regex(R"RE("desired"\s*:\s*(null|[-0-9.+eE]+))RE"))) {
                std::string val = sub[1].str();
                if (val != "null") {
                    try { desired = std::stod(val); } catch(...) { desired.reset(); }
                }
            }
            if (std::regex_search(body, sub, std::regex(R"RE("high"\s*:\s*"([^"]*)")RE"))) {
                high = sub[1].str();
            }
            if (std::regex_search(body, sub, std::regex(R"RE("low"\s*:\s*"([^"]*)")RE"))) {
                low = sub[1].str();
            }
            out[room] = std::make_tuple(desired, high, low);
        }
    } catch(...) {
        return false;
    }
    return true;
}

static bool write_settings_map(const std::map<std::string, std::tuple<std::optional<double>, std::string, std::string>> &m) {
    auto esc = [](const std::string &s){
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
    };
    std::ostringstream js;
    js << "{";
    bool first = true;
    for (const auto &kv : m) {
        if (!first) js << ",";
        first = false;
        const std::string &room = kv.first;
        const auto &tpl = kv.second;
        js << '"' << esc(room) << '"' << ":" << "{";
        if (std::get<0>(tpl).has_value()) js << "\"desired\":" << std::get<0>(tpl).value();
        else js << "\"desired\":null";
        js << ",\"high\":\"" << esc(std::get<1>(tpl)) << "\",\"low\":\"" << esc(std::get<2>(tpl)) << "\"}";
    }
    js << "}";
    // atomic write: write to temp then rename
    std::filesystem::path p(SETTINGS_JSON_FILE);
    auto parent = p.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::string tmp = SETTINGS_JSON_FILE + ".tmp";
    std::ofstream ofs(tmp, std::ios::trunc);
    if (!ofs) return false;
    ofs << js.str();
    ofs.close();
    std::error_code ec;
    std::filesystem::rename(tmp, SETTINGS_JSON_FILE, ec);
    if (ec) {
        // fallback: try std::rename
        std::rename(tmp.c_str(), SETTINGS_JSON_FILE.c_str());
    }
    return true;
}

// flush_readings_to_disk: implemented in storage_json.cpp

void log_trigger_event(const std::string &sensor, const std::string &type, const std::string &url) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ts;
    ts << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    std::string obj = "{";
    obj += "\"timestamp\":\"" + json_escape(ts.str()) + "\",";
    obj += "\"sensor\":\"" + json_escape(sensor) + "\",";
    obj += "\"type\":\"" + json_escape(type) + "\",";
    obj += "\"url\":\"" + json_escape(url) + "\"}";

    // push into in-memory trigger queue; flusher will persist to disk
    {
        std::lock_guard<std::mutex> lk(in_memory_triggers_mutex);
        in_memory_triggers.push_back(obj);
        // enforce maximum size (drop oldest)
        int maxv = MAX_TRIGGER_EVENTS.load();
        while ((int)in_memory_triggers.size() > maxv) in_memory_triggers.pop_front();
    }
}

std::string all_trigger_events_json() {
    // Read persisted file entries first
    std::ostringstream out;
    out << "[";
    bool first = true;
    std::ifstream ifs(TRIGGERS_LOG_FILE);
    if (ifs) {
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            if (!first) out << ",";
            first = false;
            out << line;
        }
    }
    // Append in-memory (not-yet-flushed) trigger events
    {
        std::lock_guard<std::mutex> lk(in_memory_triggers_mutex);
        for (const auto &t : in_memory_triggers) {
            if (!first) out << ",";
            first = false;
            out << t;
        }
    }
    out << "]";
    return out.str();
}

bool clear_trigger_events_log() {
    // clear persisted file
    std::ofstream ofs(TRIGGERS_LOG_FILE, std::ios::trunc);
    bool ok = ofs.good();
    // clear in-memory queue as well
    {
        std::lock_guard<std::mutex> lk(in_memory_triggers_mutex);
        in_memory_triggers.clear();
    }
    return ok;
}

void load_triggers_from_disk() {
    std::deque<std::string> loaded;
    std::ifstream ifs(TRIGGERS_LOG_FILE);
    if (ifs) {
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            loaded.push_back(line);
        }
    }
    int maxv = MAX_TRIGGER_EVENTS.load();
    if (maxv > 0 && (int)loaded.size() > maxv) {
        std::deque<std::string> tmp;
        size_t start = loaded.size() - maxv;
        for (size_t i = start; i < loaded.size(); ++i) tmp.push_back(loaded[i]);
        loaded.swap(tmp);
    }
    {
        std::lock_guard<std::mutex> lk(in_memory_triggers_mutex);
        in_memory_triggers = std::move(loaded);
    }
}


static void flusher_loop() {
    while (flusher_running.load()) {
        std::unique_lock<std::mutex> lk(flusher_mutex);
        flusher_cv.wait_for(lk, std::chrono::seconds(flusher_interval_seconds));
        if (!flusher_running.load()) break;
        try {
            flush_readings_to_disk();
        } catch(...) {}
    }
}

void start_periodic_flusher(int seconds) {
    if (seconds > 0) flusher_interval_seconds = seconds;
    if (flusher_running.load()) return;
    flusher_running.store(true);
    flusher_thread = std::thread(flusher_loop);
}

void stop_periodic_flusher() {
    if (!flusher_running.load()) return;
    flusher_running.store(false);
    flusher_cv.notify_all();
    if (flusher_thread.joinable()) flusher_thread.join();
    // final flush
    flush_readings_to_disk();
}

std::string all_settings_json() {
    std::ifstream ifs(SETTINGS_JSON_FILE);
    if (!ifs) return std::string("{}");
    std::string s((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return s.empty() ? std::string("{}") : s;
}

std::string room_settings_json(const std::string &room) {
    std::map<std::string, std::tuple<std::optional<double>, std::string, std::string>> m;
    if (!read_settings_map(m)) return std::string();
    std::string sid = sanitize_id(room);
    auto it = m.find(sid);
    if (it == m.end()) return std::string();
    const auto &tpl = it->second;
    auto esc = [](const std::string &s){
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
    };
    std::ostringstream js;
    js << "{";
    if (std::get<0>(tpl).has_value()) js << "\"desired\":" << std::get<0>(tpl).value();
    else js << "\"desired\":null";
    js << ",\"high\":\"" << esc(std::get<1>(tpl)) << "\",\"low\":\"" << esc(std::get<2>(tpl)) << "\"}";
    return js.str();
}

bool set_desired_temperature(const std::string &room, double desired) {
    std::map<std::string, std::tuple<std::optional<double>, std::string, std::string>> m;
    read_settings_map(m);
    std::string sid = sanitize_id(room);
    m[sid] = std::make_tuple(std::optional<double>(desired), std::get<1>(m[sid]), std::get<2>(m[sid]));
    write_settings_map(m);
    return true;
}

bool delete_room_settings(const std::string &room) {
    std::map<std::string, std::tuple<std::optional<double>, std::string, std::string>> m;
    read_settings_map(m);
    std::string sid = sanitize_id(room);
    auto it = m.find(sid);
    if (it != m.end()) {
        m.erase(it);
        write_settings_map(m);
    }
    return true;
}

bool set_trigger_url(const std::string &room, const std::string &type, const std::string &url) {
    std::map<std::string, std::tuple<std::optional<double>, std::string, std::string>> m;
    read_settings_map(m);
    std::string sid = sanitize_id(room);
    auto &entry = m[sid];
    std::optional<double> desired = std::get<0>(entry);
    std::string high = std::get<1>(entry);
    std::string low = std::get<2>(entry);
    if (type == "high") high = url;
    else if (type == "low") low = url;
    m[sid] = std::make_tuple(desired, high, low);
    write_settings_map(m);
    return true;
}

bool get_room_settings(const std::string &room, double &desired, bool &has_desired, std::string &high_url, std::string &low_url) {
    std::map<std::string, std::tuple<std::optional<double>, std::string, std::string>> m;
    if (!read_settings_map(m)) return false;
    std::string sid = sanitize_id(room);
    auto it = m.find(sid);
    if (it == m.end()) return false;
    const auto &tpl = it->second;
    if (std::get<0>(tpl).has_value()) { desired = std::get<0>(tpl).value(); has_desired = true; }
    else { desired = 0.0; has_desired = false; }
    high_url = std::get<1>(tpl);
    low_url = std::get<2>(tpl);
    return true;
}
