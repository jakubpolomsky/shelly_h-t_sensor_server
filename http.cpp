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

#include "http.h"
#include "storage.h"
#include <curl/curl.h>
#include <set>
#include <algorithm>


// Forward declarations: functions declared in server.h are implemented here.

// (implementation moved earlier)

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
                // If client used Expect: 100-continue, send interim response so client will send body
                std::string req_l = req;
                std::transform(req_l.begin(), req_l.end(), req_l.begin(), [](unsigned char c){ return std::tolower(c); });
                size_t expect_pos = req_l.find("expect:");
                if (expect_pos != std::string::npos) {
                    size_t expect_end = req_l.find('\r', expect_pos);
                    std::string expect_line = req_l.substr(expect_pos, (expect_end==std::string::npos? req_l.size(): expect_end)-expect_pos);
                    if (expect_line.find("100-continue") != std::string::npos) {
                        const char *cont = "HTTP/1.1 100 Continue\r\n\r\n";
                        send(client_fd, cont, strlen(cont), 0);
                    }
                }
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

// forward declaration for background executor used below
static void execute_url_background(const std::string &url);

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
         << "Access-Control-Allow-Origin: *\r\n"
         << "\r\n"
         << body;
    return resp.str();
}

// Determine allowed methods for a given request path
static std::string get_allowed_methods_for_path(const RequestLine &rl) {
    std::set<std::string> methods;
    methods.insert("OPTIONS");
    if (rl.path == "/" || rl.path.empty() || rl.path == "/sensors" || rl.path.rfind("/sensor/", 0) == 0 || rl.path == "/triggers" || rl.path == "/settings") {
        methods.insert("GET");
    }
    if (rl.path.rfind("/settings", 0) == 0 || rl.path == "/settings") {
        methods.insert("DELETE");
    }
    if (rl.path == "/setDesiredTemperature" || rl.path == "/setHighTrigger" || rl.path == "/setLowTrigger" || rl.path == "/triggerAllHigh" || rl.path == "/triggerAllLow" || rl.path == "/disableTriggers" || rl.path == "/enableTriggers") {
        methods.insert("POST");
    }
    if (rl.path == "/triggerLog") methods.insert("DELETE");

    std::string out;
    for (const auto &m : methods) {
        if (!out.empty()) out += ", ";
        out += m;
    }
    return out;
}

// Build an OPTIONS response with Allow and CORS headers
static std::string process_options_request(const RequestLine &rl, const std::string &req) {
    std::string allow = get_allowed_methods_for_path(rl);
    // find Access-Control-Request-Headers if present
    std::string acrh;
    std::string lower = req;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
    size_t pos = lower.find("access-control-request-headers:");
    if (pos != std::string::npos) {
        size_t eol = lower.find('\r', pos);
        if (eol == std::string::npos) eol = lower.find('\n', pos);
        size_t start = pos + std::string("access-control-request-headers:").size();
        acrh = req.substr(start, (eol==std::string::npos? req.size(): eol) - start);
        while (!acrh.empty() && (acrh.front()==' ' || acrh.front()=='\t')) acrh.erase(acrh.begin());
        while (!acrh.empty() && (acrh.back()==' ' || acrh.back()=='\t' || acrh.back()=='\r' || acrh.back()=='\n')) acrh.pop_back();
    }

    std::ostringstream resp;
    resp << "HTTP/1.1 204 No Content\r\n";
    resp << "Allow: " << allow << "\r\n";
    resp << "Content-Length: 0\r\n";
    resp << "Access-Control-Allow-Origin: *\r\n";
    resp << "Access-Control-Allow-Methods: " << allow << "\r\n";
    if (!acrh.empty()) resp << "Access-Control-Allow-Headers: " << acrh << "\r\n";
    else resp << "Access-Control-Allow-Headers: Content-Type, Accept\r\n";
    resp << "Access-Control-Max-Age: 3600\r\n";
    resp << "\r\n";
    return resp.str();
}

std::string process_request_and_build_response(const std::string &req) {
    RequestLine rl = parse_request_line(req);
    
    if (rl.method == "GET") {
        return process_get_request(rl);
    } else if (rl.method == "POST") {
        return process_post_request(rl, req);
    } else if (rl.method == "DELETE") {
        return process_delete_request(rl);
    } else if (rl.method == "OPTIONS") {
        return process_options_request(rl, req);
    }

    return std::string("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n");
}

std::string process_get_request(const RequestLine &rl) {
    if (rl.path == "/" || rl.path == "") {
            std::string body = all_sensors_json();
            return build_response("application/json", body);
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

            // After storing, check desired temperature and triggers
            if (ok && !temp.empty()) {
                try {
                    double measured = std::stod(temp);
                    double desired = 0.0;
                    bool has_desired = false;
                    std::string high_url, low_url;
                    if (get_room_settings(sensor, desired, has_desired, high_url, low_url) && has_desired) {
                        if (measured > desired && !high_url.empty()) {
                            log_trigger_event(sensor, "high", high_url);
                            if (TRIGGERS_ENABLED.load()) execute_url_background(high_url);
                        } else if (measured < desired && !low_url.empty()) {
                            log_trigger_event(sensor, "low", low_url);
                            if (TRIGGERS_ENABLED.load()) execute_url_background(low_url);
                        }
                    }
                } catch(...) {
                    // ignore parse errors
                }
            }

            return build_response("text/plain", resp_body);
        } else if (rl.path == "/sensors" || rl.path == "/allSensors") {
            std::string json = all_sensors_json();
            return build_response("application/json", json);
        } else if (rl.path == "/triggers" || rl.path == "/triggerEvents") {
            std::string json = all_trigger_events_json();
            return build_response("application/json", json);
        } else if (rl.path == "/triggersEnabled") {
            std::string js = TRIGGERS_ENABLED.load() ? "{\"enabled\":true}" : "{\"enabled\":false}";
            return build_response("application/json", js);
        } else if (rl.path == "/settings") {
            std::string json = all_settings_json();
            return build_response("application/json", json);
        } else if (rl.path.rfind("/settings/", 0) == 0) {
            std::string room = rl.path.substr(std::string("/settings/").size());
            if (room.empty()) return std::string("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
            std::string js = room_settings_json(room);
            if (js.empty()) return std::string("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            return build_response("application/json", js);
        } else {
            std::string body = all_sensors_json();
            return build_response("application/json", body);
        }
}

std::string process_delete_request(const RequestLine &rl) {
    if (rl.path.rfind("/settings", 0) == 0) {
        std::string room = rl.path.substr(std::string("/settings/").size());
        if (room.empty()) return build_response("text/plain", "Missing room name");
        bool ok = delete_room_settings(room);
        return build_response("text/plain", ok ? "OK" : "Failed");
    } if (rl.path == "/triggerLog") {
        bool ec = clear_trigger_events_log();
        return build_response("text/plain", ec ? "OK" : "Failed");

    }else {
        return std::string("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    }
}

std::string process_post_request(const RequestLine &rl, const std::string &req) {
    size_t header_end = req.find("\r\n\r\n");
        std::string body = (header_end != std::string::npos) ? req.substr(header_end + 4) : "";
        // parse POST body as application/x-www-form-urlencoded
        auto params = parse_query(body);
        // Route: set desired temperature
        if (rl.path == "/setDesiredTemperature") {
            std::string room = params.count("room") ? params["room"] : (params.count("sensor") ? params["sensor"] : "");
            std::string desired_s = params.count("desired") ? params["desired"] : params["value"];
            if (room.empty() || desired_s.empty()) {
                return build_response("text/plain", "Missing room or desired parameter");
            }
            try {
                double d = std::stod(desired_s);
                bool ok = set_desired_temperature(room, d);
                return build_response("text/plain", ok ? "OK" : "Failed");
            } catch (...) {
                return build_response("text/plain", "Invalid desired value");
            }
        }

        // Route: set high trigger URL
        if (rl.path == "/setHighTrigger") {
            std::string room = params.count("room") ? params["room"] : (params.count("sensor") ? params["sensor"] : "");
            std::string url = params.count("url") ? params["url"] : params["trigger"];
            if (room.empty() || url.empty()) return build_response("text/plain", "Missing room or url");
            bool ok = set_trigger_url(room, "high", url);
            return build_response("text/plain", ok ? "OK" : "Failed");
        }

        // Route: set low trigger URL
        if (rl.path == "/setLowTrigger") {
            std::string room = params.count("room") ? params["room"] : (params.count("sensor") ? params["sensor"] : "");
            std::string url = params.count("url") ? params["url"] : params["trigger"];
            if (room.empty() || url.empty()) return build_response("text/plain", "Missing room or url");
            bool ok = set_trigger_url(room, "low", url);
            return build_response("text/plain", ok ? "OK" : "Failed");
        }

        // Route: trigger all high triggers immediately
        if (rl.path == "/triggerAllHigh") {
            auto m = get_all_trigger_urls("high");
            int count = 0;
            for (const auto &kv : m) {
                const std::string &room = kv.first;
                const std::string &url = kv.second;
                log_trigger_event(room, "high", url);
                if (TRIGGERS_ENABLED.load()) execute_url_background(url);
                ++count;
            }
            return build_response("text/plain", std::string("Triggered high for: ") + std::to_string(count));
        }

        // Route: trigger all low triggers immediately
        if (rl.path == "/triggerAllLow") {
            auto m = get_all_trigger_urls("low");
            int count = 0;
            for (const auto &kv : m) {
                const std::string &room = kv.first;
                const std::string &url = kv.second;
                log_trigger_event(room, "low", url);
                if (TRIGGERS_ENABLED.load()) execute_url_background(url);
                ++count;
            }
            return build_response("text/plain", std::string("Triggered low for: ") + std::to_string(count));
        }

        // Route: disable triggers
        if (rl.path == "/disableTriggers") {
            TRIGGERS_ENABLED.store(false);
            return build_response("text/plain", "Triggers disabled");
        }

        // Route: enable triggers
        if (rl.path == "/enableTriggers") {
            TRIGGERS_ENABLED.store(true);
            return build_response("text/plain", "Triggers enabled");
        }

        

        return build_response("text/plain", "Unknown POST route");
}

// execute URL in background using libcurl (no fork/exec)
static void execute_url_background(const std::string &url) {
    std::thread([url]{
        CURL *c = curl_easy_init();
        if (!c) return;
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
        // suppress output
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, +[](char*, size_t sz, size_t nmemb, void*){ return sz*nmemb; });
        curl_easy_perform(c);
        curl_easy_cleanup(c);
    }).detach();
}
