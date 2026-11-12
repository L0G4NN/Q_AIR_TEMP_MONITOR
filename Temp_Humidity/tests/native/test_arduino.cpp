#include <cassert>
#include <climits>
#include <iostream>
#include "arduino_stub.h"

unsigned long test_millis;
TestClient *test_client;
TestWiFi WiFi;
TestSerial Serial;
float test_temperature = 21;
unsigned sensor_reads;

#include "../../../sketch_jul16a/sketch_jul16a.ino"

int main()
{
    /* An unreachable router must not block setup or later sensor sampling. */
    WiFi.state = 0;
    setup();
    assert(test_millis == 10000);
    loop();
    assert(sensor_reads == 1 && WiFi.reconnects == 0);
    test_millis += 30000;
    loop();
    assert(sensor_reads == 2 && WiFi.reconnects == 1);

    WiFi.state = WL_CONNECTED;
    loop();
    assert(wifiReported);
    TestClient stalled;
    stalled.stalled = true;
    test_client = &stalled;
    test_millis = ULONG_MAX - 1000;
    lastSensorReadMs = test_millis;
    const unsigned long start = test_millis;
    loop();
    assert(test_millis - start == 2000 && !stalled.connected);

    TestClient oversized;
    oversized.request = std::string(1100, 'a');
    test_client = &oversized;
    loop();
    assert(oversized.offset == 1024 && oversized.response.empty());

    TestClient valid;
    valid.request = "GET / HTTP/1.1\r\nHost: monitor\r\n\r\n";
    test_client = &valid;
    test_millis += 2500;
    loop();
    assert(valid.response.find("HTTP/1.1 200 OK") != std::string::npos);

    test_temperature = NAN;
    TestClient stale;
    stale.request = valid.request;
    test_client = &stale;
    test_millis += 2500;
    loop();
    assert(stale.response.find("Showing the last good values") != std::string::npos);
    assert(stale.response.find("nan") == std::string::npos);
    std::cout << "PASS: legacy Arduino offline sampling, retry, request rollover/size bounds and stale readings\n";
}
