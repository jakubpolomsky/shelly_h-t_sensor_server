// storage_json.h
// JSON-related storage implementations (consolidated sensor data file)
#ifndef STORAGE_JSON_H
#define STORAGE_JSON_H

#include "storage.h"

// Implementations moved from storage.cpp:
// - read_sensor_data
// - all_sensors_json
// - flush_readings_to_disk

// Utility exported for other modules
std::string json_escape(const std::string &s);

#endif // STORAGE_JSON_H
