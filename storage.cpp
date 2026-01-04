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

#include <fstream>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include <sstream>
#include <filesystem>
#include <string>
#include <map>
#include <optional>
#include <tuple>
#include <cstdio>
#include <cctype>
#include <iterator>

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <chrono>

// define DATA_DIR default
std::string DATA_DIR = "data";
// define SETTINGS_JSON_FILE default
std::string SETTINGS_JSON_FILE = "settings.json";

// in-memory cache for latest readings (sensor id -> JSON payload)
static std::unordered_map<std::string, std::string> in_memory_readings;
static std::mutex in_memory_mutex;

// flusher thread control
static std::thread flusher_thread;
static std::atomic<bool> flusher_running(false);
static std::condition_variable flusher_cv;
static std::mutex flusher_mutex;
static int flusher_interval_seconds = 3600; // default: 1 hour

bool ensure_data_dir_exists() {
    struct stat st{};
    if (stat(DATA_DIR.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(DATA_DIR.c_str(), 0755) == 0) return true;
    return false;
}

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

std::string read_sensor_data(const std::string &id) {
    std::string sid = sanitize_id(id);
    {
        std::lock_guard<std::mutex> lk(in_memory_mutex);
        auto it = in_memory_readings.find(sid);
        if (it != in_memory_readings.end()) return it->second;
    }
    // fallback to disk
    std::string path = DATA_DIR + "/" + sid + ".txt";
    std::ifstream ifs(path);
    if (!ifs) return std::string();
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

std::string list_all_sensors_html() {
    std::ostringstream html;
    html << "<html><body><h1>Sensors</h1><ul>";
    // collect ids from disk and in-memory
    std::unordered_set<std::string> ids;
    DIR *d = opendir(DATA_DIR.c_str());
    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (name.size() > 4 && name.substr(name.size()-4) == ".txt") {
                std::string id = name.substr(0, name.size()-4);
                ids.insert(id);
            }
        }
        closedir(d);
    }
    {
        std::lock_guard<std::mutex> lk(in_memory_mutex);
        for (const auto &kv : in_memory_readings) ids.insert(kv.first);
    }
    for (const auto &id : ids) {
        std::string data = read_sensor_data(id);
        html << "<li><a href=\"/sensor/" << id << "\">" << id << "</a><pre>" << data << "</pre></li>";
    }
    html << "</ul></body></html>";
    return html.str();
}

// Return a JSON object mapping sensor id -> stored JSON payload
std::string all_sensors_json() {
    // Merge in-memory readings (take precedence) with disk readings
    std::ostringstream out;
    out << "{";
    bool first = true;
    // in-memory first
    {
        std::lock_guard<std::mutex> lk(in_memory_mutex);
        for (const auto &kv : in_memory_readings) {
            if (!first) out << ",";
            first = false;
            out << "\"" << kv.first << "\":" << kv.second;
        }
    }
    // then disk entries not present in memory
    DIR *d = opendir(DATA_DIR.c_str());
    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (name.size() > 4 && name.substr(name.size()-4) == ".txt") {
                std::string id = name.substr(0, name.size()-4);
                {
                    std::lock_guard<std::mutex> lk(in_memory_mutex);
                    if (in_memory_readings.find(id) != in_memory_readings.end()) continue;
                }
                std::string data = read_sensor_data(id);
                if (data.empty()) continue;
                size_t json_pos = data.find('{');
                std::string json_val = (json_pos != std::string::npos) ? data.substr(json_pos) : data;
                if (!first) out << ",";
                first = false;
                out << "\"" << id << "\":" << json_val;
            }
        }
        closedir(d);
    }
    out << "}";
    return out.str();
}

// Read settings JSON into map: room -> (optional desired, high, low)
static bool read_settings_map(std::map<std::string, std::tuple<std::optional<double>, std::string, std::string>> &out) {
    std::ifstream ifs(SETTINGS_JSON_FILE);
    out.clear();
    if (!ifs) return false;
    std::string s((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    while (true) {
        size_t q1 = s.find('"', pos);
        if (q1 == std::string::npos) break;
        size_t q2 = s.find('"', q1+1);
        if (q2 == std::string::npos) break;
        std::string room = s.substr(q1+1, q2-q1-1);
        size_t obj_start = s.find('{', q2);
        if (obj_start == std::string::npos) break;
        size_t obj_end = s.find('}', obj_start);
        if (obj_end == std::string::npos) break;
        std::string obj = s.substr(obj_start+1, obj_end-obj_start-1);
        std::optional<double> desired;
        std::string high, low;
        size_t dpos = obj.find("\"desired\"");
        if (dpos != std::string::npos) {
            size_t colon = obj.find(':', dpos);
            if (colon != std::string::npos) {
                size_t val = colon+1;
                while (val < obj.size() && isspace((unsigned char)obj[val])) ++val;
                if (obj.compare(val, 4, "null") != 0) {
                    size_t vend = val;
                    while (vend < obj.size() && (isdigit((unsigned char)obj[vend]) || obj[vend]=='.' || obj[vend]=='-' || obj[vend]=='+' || obj[vend]=='e' || obj[vend]=='E')) ++vend;
                    try { desired = std::stod(obj.substr(val, vend-val)); } catch(...) { desired.reset(); }
                }
            }
        }
        size_t hpos = obj.find("\"high\"");
        if (hpos != std::string::npos) {
            size_t colon = obj.find(':', hpos);
            if (colon != std::string::npos) {
                size_t q3 = obj.find('"', colon);
                if (q3 != std::string::npos) {
                    size_t q4 = obj.find('"', q3+1);
                    if (q4 != std::string::npos) high = obj.substr(q3+1, q4-q3-1);
                }
            }
        }
        size_t lpos = obj.find("\"low\"");
        if (lpos != std::string::npos) {
            size_t colon = obj.find(':', lpos);
            if (colon != std::string::npos) {
                size_t q3 = obj.find('"', colon);
                if (q3 != std::string::npos) {
                    size_t q4 = obj.find('"', q3+1);
                    if (q4 != std::string::npos) low = obj.substr(q3+1, q4-q3-1);
                }
            }
        }
        out[room] = std::make_tuple(desired, high, low);
        pos = obj_end + 1;
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

// Flush current in-memory readings to disk (atomic per-file)
void flush_readings_to_disk() {
    if (!ensure_data_dir_exists()) return;
    std::unordered_map<std::string,std::string> copy;
    {
        std::lock_guard<std::mutex> lk(in_memory_mutex);
        copy = in_memory_readings;
    }
    for (const auto &kv : copy) {
        std::string sid = kv.first;
        std::string path = DATA_DIR + "/" + sid + ".txt";
        std::string tmp = path + ".tmp";
        std::ofstream ofs(tmp, std::ios::trunc);
        if (!ofs) continue;
        ofs << kv.second;
        ofs.close();
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::rename(tmp.c_str(), path.c_str());
        }
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
