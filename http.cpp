/*
 * Copyright (C) 2026 Jakub
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
 * Author: Jakub
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "http.h"
#include "storage.h"

#include <sstream>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <chrono>
#include <ctime>
#include <iomanip>

// Forward declarations: functions declared in server.h are implemented here.

std::string read_request(int client_fd) {
    std::string req;
    char buffer[4096];
    ssize_t received;

    while ((received = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
        req.append(buffer, received);
        size_t header_end = req.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            size_t cl_pos = req.find("Content-Length:");
            if (cl_pos != std::string::npos) {
                size_t line_end = req.find('\r', cl_pos);
                std::string cl_line = req.substr(cl_pos, (line_end==std::string::npos? req.size(): line_end)-cl_pos);
                int content_length = 0;
                std::istringstream iss(cl_line);
                std::string tmp;
                iss >> tmp >> content_length;
                size_t body_len = req.size() - (header_end + 4);
                while ((int)body_len < content_length) {
                    received = recv(client_fd, buffer, sizeof(buffer), 0);
                    if (received <= 0) break;
                    req.append(buffer, received);
                    body_len = req.size() - (header_end + 4);
                }
            }
            break;
        }
    }

    return req;
}

// URL-decode a string (handles %XX and +)
static std::string url_decode(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < s.size()) {
            std::string hex = s.substr(i+1, 2);
            char decoded = (char) strtol(hex.c_str(), nullptr, 16);
            out.push_back(decoded);
            i += 2;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// Parse query string like "a=1&b=2" into a map with URL-decoded keys/values
std::map<std::string,std::string> parse_query(const std::string &query) {
    std::map<std::string,std::string> params;
    if (query.empty()) return params;
    size_t start = 0;
    while (start < query.size()) {
        size_t eq = query.find('=', start);
        if (eq == std::string::npos) break;
        std::string key = query.substr(start, eq - start);
        size_t amp = query.find('&', eq + 1);
        std::string val = (amp == std::string::npos) ? query.substr(eq + 1) : query.substr(eq + 1, amp - eq - 1);
        params[url_decode(key)] = url_decode(val);
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return params;
}

RequestLine parse_request_line(const std::string &req) {
    RequestLine rl;
    std::istringstream request_stream(req);
    std::string request_line;
    std::getline(request_stream, request_line);
    if (!request_line.empty() && request_line.back() == '\r') request_line.pop_back();
    std::istringstream line_stream(request_line);
    line_stream >> rl.method >> rl.path >> rl.version;
    return rl;
}

std::string build_response(const std::string &content_type, const std::string &body) {
    std::ostringstream resp;
    resp << "HTTP/1.1 200 OK\r\n"
         << "Content-Type: " << content_type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "\r\n"
         << body;
    return resp.str();
}

std::string process_request_and_build_response(const std::string &req) {
    RequestLine rl = parse_request_line(req);
    if (rl.method == "GET") {
        if (rl.path == "/" || rl.path == "") {
            std::string body = list_all_sensors_html();
            return build_response("text/html", body);
        } else if (rl.path.rfind("/sensor/", 0) == 0) {
            std::string id = rl.path.substr(std::string("/sensor/").size());
            std::string data = read_sensor_data(id);
            if (data.empty()) {
                return std::string("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            }
            return build_response("application/json", data);
        } else if (rl.path.rfind("/saveSensorInformation", 0) == 0) {
            std::string path = rl.path;
            size_t qpos = path.find('?');
            std::string query = (qpos!=std::string::npos) ? path.substr(qpos+1) : std::string();
            std::map<std::string,std::string> params = parse_query(query);

            std::string sensor = "unknown";
            if (params.count("sensor")) sensor = params["sensor"];
            else if (params.count("id")) sensor = params["id"];
            std::string hum = params.count("hum") ? params["hum"] : std::string();
            std::string temp = params.count("temp") ? params["temp"] : std::string();
            std::string batt = params.count("batt") ? params["batt"] : std::string();

            std::ostringstream payload;
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            payload << "{\"timestamp\":\"" << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S") << "\"";
            payload << ",\"sensor\":\"" << sensor << "\"";
            if (!temp.empty()) payload << ",\"temp\":\"" << temp << "\"";
            if (!hum.empty()) payload << ",\"hum\":\"" << hum << "\"";
            if (!batt.empty()) payload << ",\"batt\":\"" << batt << "\"";
            payload << "}";

            bool ok = save_sensor_data(sensor, payload.str());
            std::string resp_body = ok ? (std::string("Stored sensor data for: ") + sensor) : (std::string("Failed to store data for: ") + sensor);
            return build_response("text/plain", resp_body);
        } else if (rl.path == "/sensors" || rl.path == "/allSensors") {
            std::string json = all_sensors_json();
            return build_response("application/json", json);
        } else {
            std::string body = list_all_sensors_html();
            return build_response("text/html", body);
        }
    } else if (rl.method == "POST") {
        size_t header_end = req.find("\r\n\r\n");
        std::string body = (header_end != std::string::npos) ? req.substr(header_end + 4) : "";
        bool ok = true;
        std::string resp_body = ok ? ("Stored sensor data for: ") : ("Failed to store data for: ");
        return build_response("text/plain", resp_body);
    } else {
        return std::string("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n");
    }
}
