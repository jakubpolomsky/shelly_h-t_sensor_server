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

#ifndef HTTP_H
#define HTTP_H

#include <string>
#include <map>

struct RequestLine {
    std::string method;
    std::string path;
    std::string version;
};

// Read full HTTP request from a connected socket (reads headers and body if Content-Length present)
std::string read_request(int client_fd);

// Parse the request line (first line) into method, path and version
RequestLine parse_request_line(const std::string &req);

// Build a full HTTP response given content type and body
std::string build_response(const std::string &content_type, const std::string &body);

// Process the incoming raw request and return a full HTTP response string
std::string process_request_and_build_response(const std::string &req);

// Parse a URL query string into a map of key->value (URL-decoded)
std::map<std::string,std::string> parse_query(const std::string &query);

#endif // HTTP_H
