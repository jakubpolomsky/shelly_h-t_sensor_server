#include "../http.h"
#include "../storage.h"

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

int main() {
    try {
        test_parse_query();
        test_sanitize_id();
        test_storage_roundtrip();
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
