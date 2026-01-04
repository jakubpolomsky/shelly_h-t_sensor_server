#ifndef STORAGE_H
#define STORAGE_H

#include <string>

// Data directory used for storage; defined in storage.cpp
extern std::string DATA_DIR;
// Path to JSON settings file
extern std::string SETTINGS_JSON_FILE;

// Return all settings as JSON object string
std::string all_settings_json();
// Return settings for a single room as JSON string (empty if not found)
std::string room_settings_json(const std::string &room);

// Sensor data storage utilities
bool ensure_data_dir_exists();
std::string sanitize_id(const std::string &id);
bool save_sensor_data(const std::string &id, const std::string &body);
std::string read_sensor_data(const std::string &id);
std::string list_all_sensors_html();
std::string all_sensors_json();

// Settings stored in a single file (per requirement)
// Set desired temperature for a room
bool set_desired_temperature(const std::string &room, double desired);
// Set trigger URL for a room; type is "high" or "low"
bool set_trigger_url(const std::string &room, const std::string &type, const std::string &url);
// Get room settings; returns true if room exists or has settings
bool get_room_settings(const std::string &room, double &desired, bool &has_desired, std::string &high_url, std::string &low_url);

#endif // STORAGE_H
