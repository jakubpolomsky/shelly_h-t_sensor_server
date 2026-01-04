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
#include <chrono>
#include <iomanip>
#include <dirent.h>
#include <sys/stat.h>
#include <sstream>
#include <string>
#include <cstdlib>

// define DATA_DIR default
std::string DATA_DIR = "data";

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
    if (!ensure_data_dir_exists()) return false;
    std::string sid = sanitize_id(id);
    std::string path = DATA_DIR + "/" + sid + ".txt";
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs) return false;
    ofs << body;
    return true;
}

std::string read_sensor_data(const std::string &id) {
    std::string sid = sanitize_id(id);
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
    DIR *d = opendir(DATA_DIR.c_str());
    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (name.size() > 4 && name.substr(name.size()-4) == ".txt") {
                std::string id = name.substr(0, name.size()-4);
                std::string data = read_sensor_data(id);
                html << "<li><a href=\"/sensor/" << id << "\">" << id << "</a><pre>" << data << "</pre></li>";
            }
        }
        closedir(d);
    }
    html << "</ul></body></html>";
    return html.str();
}

// Return a JSON object mapping sensor id -> stored JSON payload
std::string all_sensors_json() {
    std::ostringstream out;
    out << "{";
    DIR *d = opendir(DATA_DIR.c_str());
    bool first = true;
    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (name.size() > 4 && name.substr(name.size()-4) == ".txt") {
                std::string id = name.substr(0, name.size()-4);
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
