#include <WiFi.h>
#include <DHT.h>

#define DHT_PIN 4
#define DHT_TYPE DHT11

const char *ssid = "REPLACE_WITH_YOUR_SSID";
const char *password = "REPLACE_WITH_YOUR_PASSWORD";

DHT dht(DHT_PIN, DHT_TYPE);
WiFiServer server(80);

float temperatureC = NAN;
float temperatureF = NAN;
float humidity = NAN;
unsigned long lastSensorReadMs = 0;

static void readSensor()
{
  if (millis() - lastSensorReadMs < 2000) {
    return;
  }

  lastSensorReadMs = millis();

  float newHumidity = dht.readHumidity();
  float newTemperatureC = dht.readTemperature();
  float newTemperatureF = dht.readTemperature(true);

  if (isnan(newHumidity) || isnan(newTemperatureC) || isnan(newTemperatureF)) {
    Serial.println("Failed to read from DHT11 sensor");
    return;
  }

  humidity = newHumidity;
  temperatureC = newTemperatureC;
  temperatureF = newTemperatureF;

  Serial.print("Temperature: ");
  Serial.print(temperatureC);
  Serial.print(" C / ");
  Serial.print(temperatureF);
  Serial.print(" F, Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");
}

static void sendWebPage(WiFiClient &client)
{
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println("Refresh: 5");
  client.println();

  client.println("<!DOCTYPE html>");
  client.println("<html lang=\"en\">");
  client.println("<head>");
  client.println("<meta charset=\"utf-8\">");
  client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  client.println("<title>Desk Temperature Sensor</title>");
  client.println("<style>");
  client.println("body{font-family:Arial,sans-serif;margin:2rem;background:#f6f7f9;color:#202124}");
  client.println("main{max-width:520px;margin:auto;background:white;padding:1.5rem;border:1px solid #ddd;border-radius:8px}");
  client.println("h1{font-size:1.5rem;margin-top:0}");
  client.println(".reading{font-size:2rem;font-weight:700;margin:.5rem 0}");
  client.println(".label{color:#5f6368}");
  client.println("</style>");
  client.println("</head>");
  client.println("<body><main>");
  client.println("<h1>Desk Temperature Sensor</h1>");

  if (isnan(temperatureC) || isnan(humidity)) {
    client.println("<p>Waiting for a valid DHT11 reading...</p>");
  } else {
    client.println("<p class=\"label\">Temperature</p>");
    client.print("<p class=\"reading\">");
    client.print(temperatureC, 1);
    client.print(" &deg;C / ");
    client.print(temperatureF, 1);
    client.println(" &deg;F</p>");

    client.println("<p class=\"label\">Humidity</p>");
    client.print("<p class=\"reading\">");
    client.print(humidity, 1);
    client.println(" %</p>");
  }

  client.println("</main></body></html>");
}

void setup()
{
  Serial.begin(115200);
  dht.begin();

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("Open http://");
  Serial.println(WiFi.localIP());

  server.begin();
}

void loop()
{
  readSensor();

  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  unsigned long timeoutMs = millis() + 2000;
  while (client.connected() && millis() < timeoutMs) {
    if (!client.available()) {
      delay(1);
      continue;
    }

    String requestLine = client.readStringUntil('\n');
    if (requestLine == "\r") {
      sendWebPage(client);
      break;
    }
  }

  client.stop();
}
