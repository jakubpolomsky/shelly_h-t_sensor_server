#include <iostream>
#include <cstdlib>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    std::string *s = (std::string*)userdata;
    s->append((char*)ptr, size*nmemb);
    return size*nmemb;
}

struct HttpResult { long code; std::string body; };

static HttpResult http_request(const std::string &method, const std::string &url) {
    HttpResult r{0, ""};
    CURL *c = curl_easy_init();
    if (!c) return r;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
    if (method == "POST") curl_easy_setopt(c, CURLOPT_POST, 1L);
    CURLcode ec = curl_easy_perform(c);
    if (ec == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.code);
    curl_easy_cleanup(c);
    return r;
}

int main() {
    // start server in background
    int rc = system("./server > /tmp/shelly_server_test.log 2>&1 & echo $! > /tmp/shelly_server_test.pid");
    if (rc == -1) { std::cerr << "Failed to start server" << std::endl; return 2; }

    // wait for server to start up (try for up to 5s)
    bool up = false;
    for (int i=0;i<50;i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        HttpResult r = http_request("GET", "http://localhost:8080/");
        if (r.code == 200) { up = true; break; }
    }
    if (!up) {
        std::cerr << "Server did not respond in time" << std::endl;
        // try to stop
        std::ifstream pidifs("/tmp/shelly_server_test.pid");
        if (pidifs) { pid_t pid; pidifs >> pid; kill(pid, SIGINT); }
        return 2;
    }

    // perform a few smoke requests
    HttpResult root = http_request("GET", "http://localhost:8080/");
    if (root.code != 200) { std::cerr << "GET / returned " << root.code << std::endl; return 2; }

    HttpResult sensors = http_request("GET", "http://localhost:8080/sensors");
    if (sensors.code != 200) { std::cerr << "GET /sensors returned " << sensors.code << std::endl; return 2; }

    HttpResult ph = http_request("POST", "http://localhost:8080/triggerAllHigh");
    if (ph.code != 200) { std::cerr << "POST /triggerAllHigh returned " << ph.code << std::endl; return 2; }

    HttpResult pl = http_request("POST", "http://localhost:8080/triggerAllLow");
    if (pl.code != 200) { std::cerr << "POST /triggerAllLow returned " << pl.code << std::endl; return 2; }

    // stop server
    std::ifstream pidifs("/tmp/shelly_server_test.pid");
    if (pidifs) {
        pid_t pid; pidifs >> pid;
        kill(pid, SIGINT);
        // wait for process to exit
        for (int i=0;i<50;i++) {
            if (kill(pid, 0) != 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        unlink("/tmp/shelly_server_test.pid");
    }

    std::cout << "Integration smoke tests passed" << std::endl;
    return 0;
}
