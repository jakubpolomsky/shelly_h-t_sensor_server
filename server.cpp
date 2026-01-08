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
#include "storage.h"
#include <curl/curl.h>


static volatile sig_atomic_t keep_running = 1;
static int g_server_fd = -1;

// notifier thread: when shutdown starts, repeatedly echo a message until shutdown completes
static std::atomic<bool> shutdown_in_progress(false);
static std::atomic<bool> shutdown_complete(false);
static std::thread notifier_thread;

static void signal_handler(int sig) {
    (void)sig;
    // minimal async-signal-safe message
    const char msg[] = "Shutdown requested; waiting for server to stop...\n";
    write(STDERR_FILENO, msg, sizeof(msg)-1);
    shutdown_in_progress.store(true);
    keep_running = 0;
    if (g_server_fd != -1) {
        // try to shutdown socket to interrupt accept()/connections
        shutdown(g_server_fd, SHUT_RDWR);
        close(g_server_fd);
    }
}

static void notifier_loop() {
    // wait until shutdown is requested
    while (!shutdown_in_progress.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // while shutdown is in progress and not complete, print message every second
    while (!shutdown_complete.load()) {
        std::cerr << "Shutdown in progress... waiting to finish (press Ctrl+C again to force)\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
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
        std::cout << "  -m, --max-triggers <n>         Maximum in-memory trigger events to keep (default 100)\n";
        std::cout << "Arguments:\n";
        std::cout << "  port                   Optional TCP port to listen on (default " << DEFAULT_PORT << ")\n";
    };

    int port = DEFAULT_PORT;
    bool verbose = false;
    int flush_interval = 3600; // seconds
    int max_triggers = 100;
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
        if (a == "-m" || a == "--max-triggers") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << a << "\n";
                return 1;
            }
            char *endptr = nullptr;
            long v = strtol(argv[i+1], &endptr, 10);
            if (endptr == argv[i+1] || *endptr != '\0' || v <= 0) {
                std::cerr << "Invalid max-triggers value: " << argv[i+1] << "\n";
                return 1;
            }
            max_triggers = static_cast<int>(v);
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

    // allow immediate reuse of the address after the server is killed
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
    }
#ifdef SO_REUSEPORT
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        // non-fatal
    }
#endif

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

    // start notifier thread (will wait until shutdown requested)
    notifier_thread = std::thread(notifier_loop);

    // start periodic flusher
    start_periodic_flusher(flush_interval);
    // apply configured max triggers
    MAX_TRIGGER_EVENTS.store(max_triggers);
    // load existing triggers from disk into memory (trimmed to max)
    load_triggers_from_disk();
    // initialize libcurl (required for threaded use)
    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::cout << "Server running on http://localhost:" << port << "/";
    if (verbose) std::cout << "  (verbose)";
    std::cout << "  (flush-interval=" << flush_interval << "s)";
    std::cout << "  (max-triggers=" << max_triggers << ")";
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

    // mark shutdown complete so notifier stops
    shutdown_complete.store(true);
    if (notifier_thread.joinable()) notifier_thread.join();

    close(server_fd);
    g_server_fd = -1;
    // cleanup libcurl
    curl_global_cleanup();
    return 0;
}

 
