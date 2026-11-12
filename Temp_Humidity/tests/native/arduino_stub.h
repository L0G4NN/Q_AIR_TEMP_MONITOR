#ifndef TEST_ARDUINO_STUB_H
#define TEST_ARDUINO_STUB_H
#include <math.h>
#include <stddef.h>
#include <string>

extern unsigned long test_millis;
inline unsigned long millis() { return test_millis; }
inline void delay(unsigned long duration) { test_millis += duration; }

struct TestClient {
    std::string request;
    std::string response;
    size_t offset = 0;
    bool connected = true;
    bool stalled = false;
    unsigned long read_cost_ms = 0;
};
extern TestClient *test_client;
class WiFiClient {
    TestClient *client;
public:
    WiFiClient() : client(test_client) {}
    explicit operator bool() const { return client != nullptr; }
    bool connected() const { return client && client->connected; }
    bool available() const { return connected() && !client->stalled && client->offset < client->request.size(); }
    int read() {
        test_millis += client->read_cost_ms;
        return static_cast<unsigned char>(client->request[client->offset++]);
    }
    template<typename T> void print(T value) { client->response += std::to_string(value); }
    void print(const char *value) { client->response += value; }
    void print(float value, int) { print(value); }
    void println(const char *value) { print(value); print("\n"); }
    void println() { print("\n"); }
    void stop() { if (client) client->connected = false; }
};
class WiFiServer {
public:
    explicit WiFiServer(int) {}
    void begin() {}
    WiFiClient available() { return WiFiClient(); }
};
enum { WL_CONNECTED = 3 };
struct TestWiFi {
    int state = WL_CONNECTED;
    unsigned reconnects = 0;
    void begin(const char *, const char *) {}
    int status() const { return state; }
    const char *localIP() const { return "192.168.1.2"; }
    void reconnect() { reconnects++; }
};
extern TestWiFi WiFi;
struct TestSerial {
    void begin(int) {}
    template<typename T> void print(T) {}
    template<typename T> void println(T) {}
    void println() {}
};
extern TestSerial Serial;
#define DHT11 11
extern float test_temperature;
extern unsigned sensor_reads;
class DHT {
public:
    DHT(int, int) {}
    void begin() {}
    float readHumidity() { return 50; }
    float readTemperature() { sensor_reads++; return test_temperature; }
    float convertCtoF(float celsius) { return celsius * 1.8f + 32; }
};
#endif
