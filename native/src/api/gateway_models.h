#pragma once

#include <map>
#include <string>
#include <vector>

struct GatewayConfig {
    std::string listenAddress = "127.0.0.1";
    int port = 24104;
    std::string authToken;
    std::string logLevel = "info";
    std::string logDir;
    std::string stateFile = "vision-realtime-state.json";
    bool noColor = false;
};

struct TagConfig {
    std::string tagId;
    int ioa = 0;
    std::string visionType = "Number";
    std::string deviceDataType = "M_ME_NC_1";
    bool writable = false;
    bool selectBeforeOperate = false;
    int qualifier = 0;
};

struct ConnectionConfig {
    std::string remoteAddress = "127.0.0.1";
    int remotePort = 2404;
    int commonAddress = 1;
    int originatorAddress = 0;
    int cotSize = 2;
    int caSize = 2;
    int ioaSize = 3;
    int timeoutMs = 10000;
    int reconnectMs = 5000;
    int apciT0Sec = 10;
    int apciT1Sec = 15;
    int apciT2Sec = 10;
    int apciT3Sec = 20;
    int apciK = 12;
    int apciW = 8;
    bool interrogationOnConnect = true;
    bool clockSyncOnConnect = false;
};

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};
