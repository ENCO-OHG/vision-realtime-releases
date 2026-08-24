#include "json_utils.h"
#include "state_store.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void parserHandlesNestedAndEscapedValues() {
    const JsonValue value = parseJson(R"({"name":"line\n\u0041","items":[true,-1.25e2,{"id":3}]})");
    require(value.find("name") && *value.find("name")->string() == "line\nA", "escaped string was not decoded");
    require(value.find("items") && value.find("items")->array()->size() == 3, "nested array was not parsed");
    require(parseJson(serializeJson(value)).find("items") != nullptr, "serialized JSON was not parseable");
    bool rejected = false;
    try { (void)parseJson("{\"broken\":]"); } catch (const std::exception&) { rejected = true; }
    require(rejected, "invalid JSON was accepted");
}

void stateRoundTripsAndPreservesDesiredRunning() {
    const auto path = std::filesystem::temp_directory_path() / "vision-realtime-state-store-test.json";
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    std::filesystem::remove(path);
    std::filesystem::remove(temporary);

    ConnectionConfig connection;
    connection.remoteAddress = "10.0.0.7";
    connection.remotePort = 2405;
    const std::vector<TagConfig> tags{{.tagId = "tag-1", .ioa = 17, .deviceDataType = "M_SP_NA_1"}};
    StateStore writer(path.string());
    writer.load();
    writer.updateConfig("device-1", tags, connection);
    writer.updateDesiredRunning("device-1", true);
    require(!std::filesystem::exists(temporary), "temporary state file remained after save");

    StateStore reader(path.string());
    reader.load();
    const auto devices = reader.devices();
    require(devices.size() == 1, "persisted device count differs");
    require(devices[0].deviceId == "device-1" && devices[0].desiredRunning, "desired-running state differs");
    require(devices[0].connection.remoteAddress == "10.0.0.7" && devices[0].tags[0].ioa == 17, "device configuration differs");
    std::filesystem::remove(path);
}

void invalidStateReportsItsPath() {
    const auto path = std::filesystem::temp_directory_path() / "vision-realtime-invalid-state-test.json";
    {
        std::ofstream output(path, std::ios::trunc);
        output << "{\"version\":1,\"devices\":[{";
    }
    bool rejected = false;
    try {
        StateStore store(path.string());
        store.load();
    } catch (const std::exception& e) {
        rejected = std::string(e.what()).find(path.string()) != std::string::npos;
    }
    std::filesystem::remove(path);
    require(rejected, "invalid state did not report a clear path-specific error");
}

} // namespace

int main() {
    try {
        parserHandlesNestedAndEscapedValues();
        stateRoundTripsAndPreservesDesiredRunning();
        invalidStateReportsItsPath();
        std::cout << "state store tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "state store test failed: " << e.what() << '\n';
        return 1;
    }
}
