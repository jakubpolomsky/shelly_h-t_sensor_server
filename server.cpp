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
 * Author: github.com/jakubpolomsky
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// main only - HTTP and storage implementations moved to http.cpp and storage.cpp
#include "server.h"
#include "http.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int main(int argc, char **argv) {
    auto print_usage_local = [](const char *prog){
        std::cout << "Simple HTTP Sensor Data Server\n";
        std::cout << "Stores and serves sensor data via HTTP\n\n";
        std::cout << "To send data, use URLs like:\n";
        std::cout << "  http://<server>:<port>/saveSensorInformation?sensor=<id>&temp=<temp>&hum=<hum>&batt=<batt>\n\n";
        std::cout << "Example Action URL for Shelly devices:\n";
        std::cout << "  http://10.0.0.1:8080/saveSensorInformation?sensor=LivingRoom&hum=${status[\"humidity:0\"].rh}&temp=${status[\"temperature:0\"].tC}&batt=${status[\"devicepower:0\"].battery.V}\n\n";
        std::cout << "Usage: " << prog << " [options] [port]\n";
        std::cout << "Options:\n";
        std::cout << "  -h, -help, --help       Show this help message\n";
        std::cout << "  -v, -verbose, --verbose Enable verbose request logging\n";
        std::cout << "Arguments:\n";
        std::cout << "  port                   Optional TCP port to listen on (default " << DEFAULT_PORT << ")\n";
    };

    int port = DEFAULT_PORT;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "-help" || a == "--help") {
            print_usage_local(argv[0]);
            return 0;
        }
        if (a == "-v" || a == "-verbose" || a == "--verbose") {
            verbose = true;
            continue;
        }
        // otherwise try parse as port
        char *endptr = nullptr;
        long p = strtol(argv[i], &endptr, 10);
        if (endptr == argv[i] || *endptr != '\0' || p <= 0 || p > 65535) {
            std::cerr << "Invalid argument: " << argv[i] << "\n";
            print_usage_local(argv[0]);
            return 1;
        }
        port = static_cast<int>(p);
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "Server running on http://localhost:" << port << "/";
    if (verbose) std::cout << "  (verbose)";
    std::cout << "\n";

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        std::string req = read_request(client_fd);
        if (req.empty()) {
            close(client_fd);
            continue;
        }

        if (verbose) std::cout << "Request:\n" << req << "\n";

        std::string response = process_request_and_build_response(req);
        write(client_fd, response.c_str(), response.size());
        close(client_fd);
    }

    close(server_fd);
    return 0;
}

 
