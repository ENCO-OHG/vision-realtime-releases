extern "C" {
#include "cs104_connection.h"
}

bool visionOneIec104Lib60870Probe()
{
    CS104_Connection connection = CS104_Connection_create("127.0.0.1", IEC_60870_5_104_DEFAULT_PORT);
    if (connection == nullptr) {
        return false;
    }

    CS104_Connection_destroy(connection);
    return true;
}
