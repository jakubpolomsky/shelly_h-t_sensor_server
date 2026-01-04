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

#include "../http.h"
#include "../storage.h"
#include <iostream>
#include <cassert>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

void test_parse_query() {
    auto m = parse_query("a=1&b=hello%20world+plus&empty=&encoded=%7B%22k%22%3A%22v%22%7D");
    assert(m["a"] == "1");
    assert(m["b"] == "hello world plus");
    assert(m["empty"] == "");
    assert(m["encoded"] == "{\"k\":\"v\"}");
}

void test_sanitize_id() {
    string s = sanitize_id("Te!st@ID#123\n");
    assert(s == "TestID123");
    string s2 = sanitize_id("..///");
    assert(s2 == "unknown");
}

void test_storage_roundtrip() {
    // use a temp test directory
    DATA_DIR = "test_data";
    if (fs::exists(DATA_DIR)) fs::remove_all(DATA_DIR);
    bool ok = ensure_data_dir_exists();
    assert(ok);

    string id = "sensor-test";
    string payload = "{\"timestamp\":\"t\",\"sensor\":\"sensor-test\",\"temp\":\"22.5\"}";
    bool wrote = save_sensor_data(id, payload);
    assert(wrote);
    string read = read_sensor_data(id);
    assert(read == payload);

    string all = all_sensors_json();
    // should contain "sensor-test":{...}
    assert(all.find("\"sensor-test\":") != string::npos);

    // cleanup
    fs::remove_all(DATA_DIR);
}

void test_settings() {
    DATA_DIR = "test_data";
    if (fs::exists(DATA_DIR)) fs::remove_all(DATA_DIR);
    bool ok = ensure_data_dir_exists();
    assert(ok);
    // place settings inside test_data for isolation
    SETTINGS_JSON_FILE = DATA_DIR + "/settings.json";

    string room = "living-room";
    // set desired temp
    bool s1 = set_desired_temperature(room, 21.5);
    assert(s1);
    // set triggers
    bool s2 = set_trigger_url(room, "high", "http://example.com/high");
    bool s3 = set_trigger_url(room, "low", "http://example.com/low");
    assert(s2 && s3);

    double desired = 0.0; bool has_desired = false; string high; string low;
    bool got = get_room_settings(room, desired, has_desired, high, low);
    assert(got);
    assert(has_desired);
    assert(desired == 21.5);
    assert(high == "http://example.com/high");
    assert(low == "http://example.com/low");

    // cleanup
    fs::remove_all(DATA_DIR);
}

int main() {
    try {
        test_parse_query();
        test_sanitize_id();
        test_storage_roundtrip();
        test_settings();
        cout << "All tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        cerr << "Test failed with exception: " << e.what() << '\n';
        return 2;
    } catch (...) {
        cerr << "Test failed with unknown exception" << '\n';
        return 2;
    }
}
