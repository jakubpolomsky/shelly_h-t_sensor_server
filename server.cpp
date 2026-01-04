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

#include "server.h"
#include "http.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <atomic>
#include "storage.h"

static volatile sig_atomic_t keep_running = 1;
static int g_server_fd = -1;

static void signal_handler(int sig) {
    (void)sig;
    keep_running = 0;
    if (g_server_fd != -1) close(g_server_fd);
}

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
        std::cout << "  -i, --flush-interval <seconds>  Periodic flush interval in seconds (default 3600)\n";
        std::cout << "Arguments:\n";
        std::cout << "  port                   Optional TCP port to listen on (default " << DEFAULT_PORT << ")\n";
    };

    int port = DEFAULT_PORT;
    bool verbose = false;
    int flush_interval = 3600; // seconds
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
        if (a == "-i" || a == "--flush-interval") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << a << "\n";
                return 1;
            }
            char *endptr = nullptr;
            long v = strtol(argv[i+1], &endptr, 10);
            if (endptr == argv[i+1] || *endptr != '\0' || v <= 0) {
                std::cerr << "Invalid flush interval: " << argv[i+1] << "\n";
                return 1;
            }
            flush_interval = static_cast<int>(v);
            ++i;
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

    // register signal handlers for clean shutdown
    g_server_fd = server_fd;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // start periodic flusher
    start_periodic_flusher(flush_interval);

    std::cout << "Server running on http://localhost:" << port << "/";
    if (verbose) std::cout << "  (verbose)";
    std::cout << "  (flush-interval=" << flush_interval << "s)";
    std::cout << "\n";
    while (keep_running) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (!keep_running) break;
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

    // shutdown sequence
    stop_periodic_flusher();
    // ensure final flush
    flush_readings_to_disk();

    close(server_fd);
    g_server_fd = -1;
    return 0;
}

 
