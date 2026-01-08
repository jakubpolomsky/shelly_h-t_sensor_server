#ifndef STORAGE_H
#define STORAGE_H

#include <string>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include <sstream>
#include <filesystem>
#include <string>
#include <map>
#include <optional>
#include <tuple>
#include <cstdio>
#include <cctype>
#include <iterator>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <deque>

// Path to JSON settings file (stores room settings)
extern std::string SETTINGS_JSON_FILE;

// Path to triggers log file (line-separated JSON objects)
extern std::string TRIGGERS_LOG_FILE;

// Path to single JSON file storing all sensor readings (new consolidated storage)
extern std::string SENSOR_DATA_JSON_FILE;

// In-memory cache for latest readings (sensor id -> JSON payload)
// Exposed so JSON helpers can access and merge with disk state.
extern std::unordered_map<std::string, std::string> in_memory_readings;
extern std::mutex in_memory_mutex;
// In-memory queue for trigger events (each item is a JSON object string)
// In-memory queue for trigger events (each item is a JSON object string)
extern std::deque<std::string> in_memory_triggers;
extern std::mutex in_memory_triggers_mutex;

// Maximum number of trigger events kept in memory before older events are dropped.
extern std::atomic<int> MAX_TRIGGER_EVENTS;

// Global flag to enable/disable trigger execution
extern std::atomic<bool> TRIGGERS_ENABLED;

// Return a map of room -> trigger url for given type ("high" or "low").
std::map<std::string, std::string> get_all_trigger_urls(const std::string &type);
// (TRIGGERS_LOG_FILE and SENSOR_DATA_JSON_FILE declared above)

// Return all settings as JSON object string
std::string all_settings_json();
// Return settings for a single room as JSON string (empty if not found)
std::string room_settings_json(const std::string &room);

// Periodic flusher: writes in-memory sensor readings to disk at an interval.
void start_periodic_flusher(int seconds);
void stop_periodic_flusher();

// Force immediate flush of in-memory readings to disk (atomic).
void flush_readings_to_disk();

// Trigger event logging
// Record that a trigger URL was executed for `sensor` with type `high` or `low` and the URL called
void log_trigger_event(const std::string &sensor, const std::string &type, const std::string &url);
// Return trigger events as a JSON array string (each entry is an object with timestamp, sensor, type, url)
std::string all_trigger_events_json();
// Clear all trigger events log
bool clear_trigger_events_log();

// Load trigger events from disk into the in-memory queue (keeps only latest `MAX_TRIGGER_EVENTS`)
void load_triggers_from_disk();

// Sensor data storage utilities
// - sanitize_id: produce safe filename/id from arbitrary input
// - save_sensor_data: store latest reading in memory (flusher persists to disk)
// - read_sensor_data: return latest reading (prefer in-memory, then file)
// - all_sensors_json: return JSON mapping sensor id -> payload
std::string sanitize_id(const std::string &id);
bool save_sensor_data(const std::string &id, const std::string &body);
std::string read_sensor_data(const std::string &id);
std::string all_sensors_json();

// Settings stored in a single file (per requirement)
// Set desired temperature for a room
bool set_desired_temperature(const std::string &room, double desired);
// Set trigger URL for a room; type is "high" or "low"
bool set_trigger_url(const std::string &room, const std::string &type, const std::string &url);
// Get room settings; returns true if room exists or has settings
bool get_room_settings(const std::string &room, double &desired, bool &has_desired, std::string &high_url, std::string &low_url);
// Clear settings for a room
bool delete_room_settings(const std::string &room);

// Read settings JSON into map: room -> (optional desired, high, low)
// Exposed for callers that need to inspect raw settings map.
bool read_settings_map(std::map<std::string, std::tuple<std::optional<double>, std::string, std::string>> &out);

#endif // STORAGE_H
