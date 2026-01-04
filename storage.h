#ifndef STORAGE_H
#define STORAGE_H

#include <string>

// Data directory used for storage; defined in storage.cpp
extern std::string DATA_DIR;

// Sensor data storage utilities
bool ensure_data_dir_exists();
std::string sanitize_id(const std::string &id);
bool save_sensor_data(const std::string &id, const std::string &body);
std::string read_sensor_data(const std::string &id);
std::string list_all_sensors_html();
std::string all_sensors_json();

#endif // STORAGE_H
