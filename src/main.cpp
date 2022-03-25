#include <Arduino.h>
#include <SPISniffer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include "secrets.h"
#include <InfluxDbClient.h>

const char *ssid = STASSID;
const char *password = STAPSK;

const uint8_t enablePin = D5;
const uint8_t dataPin = D6;
const uint8_t clkPin = D7;

SPISniffer spiSniffer;
Point sensor("dmm_data");
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_DB_NAME);

void IRAM_ATTR isr_handleEnable()
{
    spiSniffer.handleEnable();
}

void IRAM_ATTR isr_handleClk()
{
    spiSniffer.handleClk();
}

void setupSnifferInterrupts()
{
    attachInterrupt(spiSniffer._enablePin, isr_handleEnable, CHANGE);
    attachInterrupt(spiSniffer._clkPin, isr_handleClk, FALLING);
}

void detachSnifferInterrupts()
{
    detachInterrupt(enablePin);
    detachInterrupt(clkPin);
}

int counter = 0;
void handleSnifferData(DmmData data)
{
    // content of spi packet doesn't change every display update
    if (counter < 6)
    {
        counter++;
        return;
    }
    else
    {
        counter = 0;
    }

    digitalWrite(LED_BUILTIN, LOW);
    if (data.overloaded)
    {
        //Serial.println("Overloaded!");
    }
    else
    {
        if (data.mode != Unknown)
        {
            //detachSnifferInterrupts();
            sensor.clearFields();
            sensor.addField("mode", data.mode);
            sensor.addField("measuredValue", data.parsedNumber, 9);
            if (data.secondParsedNumber != 0.0)
            {
                sensor.addField("measuredSecondaryValue", data.secondParsedNumber, 3);
            }

            if (!client.isBufferFull())
            {
                if (!client.writePoint(sensor))
                {
                    //Serial.print("InfluxDB write failed: ");
                    //Serial.println(client.getLastErrorMessage());
                }
            }
            else
            {
                //Serial.println("Full buffer! Prevent writing as to not flush in interrupt...");
            }

            //Serial.println(data.parsedNumber);
            //setupSnifferInterrupts();
        }
        else
        {
            //Serial.println("Unknown mode!");
        }
    }
    digitalWrite(LED_BUILTIN, HIGH);
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Booting...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
        Serial.println("Connection Failed! Rebooting...");
        delay(5000);
        ESP.restart();
    }

    ArduinoOTA.setHostname("DmmEsp8266");
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
        {
            type = "sketch";
        }
        else
        {
            type = "filesystem";
        }
        Serial.println("Start updating " + type);
        detachInterrupt(clkPin);
        detachInterrupt(enablePin);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
        {
            Serial.println("Auth Failed");
        }
        else if (error == OTA_BEGIN_ERROR)
        {
            Serial.println("Begin Failed");
        }
        else if (error == OTA_CONNECT_ERROR)
        {
            Serial.println("Connect Failed");
        }
        else if (error == OTA_RECEIVE_ERROR)
        {
            Serial.println("Receive Failed");
        }
        else if (error == OTA_END_ERROR)
        {
            Serial.println("End Failed");
        }
    });
    ArduinoOTA.begin();

    Serial.println("WiFi ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nis.gov");
    client.setConnectionParamsV1(INFLUXDB_URL, INFLUXDB_DB_NAME, INFLUXDB_USER, INFLUXDB_PASSWORD);
    client.setWriteOptions(WriteOptions().writePrecision(WritePrecision::MS).batchSize(10).bufferSize(50));

    //  enable, data, clock, bufferSize
    spiSniffer.setup(enablePin, dataPin, clkPin, 137);
    setupSnifferInterrupts();
    spiSniffer._handleData = *handleSnifferData;

    pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
    // We're mostly working with callbacks and interrupts so all that's left to do here is handling ota updates and flushing the influxdb client buffer whenever we get the chance
    ArduinoOTA.handle();

    if (!client.isBufferEmpty())
    {
        client.flushBuffer();
    }
}