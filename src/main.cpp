/*
MIT License

Copyright (c) 2018 esp-rfid Community
Copyright (c) 2017 Ömer Şiar Baysal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */
#define VERSION "1.3.5"

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TimeLib.h>
#include <Ticker.h>
#include "Ntp.h"
#include <AsyncMqttClient.h>
#include <Bounce2.h>
#include "magicnumbers.h"
#include "config.h"
#include "relay.h"
#include "door.h"

Config config;

Door door;
Bounce *openLockButton = nullptr;
Bounce *doorStatusPin = nullptr;
Relay *relayLock = nullptr;
Relay *relayGreen = nullptr;

#ifdef OFFICIALBOARD

#include <Wiegand.h>

WIEGAND wg;
bool activateRelay[MAX_NUM_RELAYS] = {false};
bool deactivateRelay[MAX_NUM_RELAYS] = {false};

#endif

#ifndef OFFICIALBOARD

#include <MFRC522.h>
#include "PN532.h"
#include <Wiegand.h>
#include "rfid125kHz.h"

MFRC522 mfrc522;
PN532 pn532;
WIEGAND wg;
RFID_Reader RFIDr;

// relay specific variables
bool activateRelay[MAX_NUM_RELAYS] = {false, false, false, false};
bool deactivateRelay[MAX_NUM_RELAYS] = {false, false, false, false};

#endif

// these are from vendors
#include "webh/glyphicons-halflings-regular.woff.gz.h"
#include "webh/required.css.gz.h"
#include "webh/required.js.gz.h"

// these are from us which can be updated and changed
#include "webh/esprfid.js.gz.h"
#include "webh/esprfid.htm.gz.h"
#include "webh/index.html.gz.h"

NtpClient NTP;
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
Ticker wifiReconnectTimer;
WiFiEventHandler wifiDisconnectHandler, wifiConnectHandler, wifiOnStationModeGotIPHandler;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

#define LEDoff HIGH
#define LEDon LOW

#define BEEPERoff HIGH
#define BEEPERon LOW

#define DEBUG_SERIAL \
  if (DEBUG) Serial

// Variables for whole scope
unsigned long cooldown = 0;
unsigned long currentMillis = 0;
unsigned long deltaTime = 0;
bool doEnableWifi = false;
bool formatreq = false;
const char *httpUsername = "admin";
unsigned long keyTimer = 0;

uint8_t lastDoorbellState = 0;
uint8_t lastDoorState = 0;
uint8_t lastTamperState = 0;
unsigned long openDoorMillis = 0;

unsigned long nextbeat = 0;
unsigned long previousLoopMillis = 0;
unsigned long previousMillis = 0;
bool shouldReboot = false;
unsigned long uptime = 0;
unsigned long wifiPinBlink = millis();
unsigned long wiFiUptimeMillis = 0;

#include "led.esp"
#include "beeper.esp"
#include "log.esp"
#include "mqtt.esp"
#include "helpers.esp"
#include "wsResponses.esp"
#include "rfid.esp"
#include "wifi.esp"
#include "config.esp"
#include "websocket.esp"
#include "webserver.esp"
#include "doorbell.esp"

void ICACHE_FLASH_ATTR setup()
{
#ifdef OFFICIALBOARD
	// Set relay pin to LOW signal as early as possible
	pinMode(13, OUTPUT);
	digitalWrite(13, LOW);
	delay(200);
#endif

#ifdef DEBUG
	Serial.begin(9600);
	Serial.println();

	Serial.print(F("[ INFO ] ESP RFID v"));
	Serial.println(VERSION);

	uint32_t realSize = ESP.getFlashChipRealSize();
	uint32_t ideSize = ESP.getFlashChipSize();
	FlashMode_t ideMode = ESP.getFlashChipMode();
	Serial.printf("Flash real id:   %08X\n", ESP.getFlashChipId());
	Serial.printf("Flash real size: %u\n\n", realSize);
	Serial.printf("Flash ide  size: %u\n", ideSize);
	Serial.printf("Flash ide speed: %u\n", ESP.getFlashChipSpeed());
	Serial.printf("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT"
																	: ideMode == FM_DIO	   ? "DIO"
																	: ideMode == FM_DOUT   ? "DOUT"
																						   : "UNKNOWN"));
	if (ideSize != realSize)
	{
		Serial.println("Flash Chip configuration wrong!\n");
	}
	else
	{
		Serial.println("Flash Chip configuration ok.\n");
	}
#endif

	if (!SPIFFS.begin())
	{
		if (SPIFFS.format())
		{
			writeEvent("WARN", "sys", "Filesystem formatted", "");
		}
		else
		{
#ifdef DEBUG
			Serial.println(F(" failed!"));
			Serial.println(F("[ WARN ] Could not format filesystem!"));
#endif
		}
	}

	bool configured = false;
	configured = loadConfiguration(config);


	if (config.doorstatpin != 255)
	{
		DEBUG_SERIAL.printf("milliseconds: %lu - setting up doorStatusPin (pin %d)\n", millis(), config.doorstatpin);
		doorStatusPin = new Bounce();
		doorStatusPin->interval(2000);
		doorStatusPin->attach(config.doorstatpin, INPUT);
	}


	if (config.relayPin[0] != 255) {
		DEBUG_SERIAL.printf("milliseconds: %lu - setting up relayLock (pin %d)\n", millis(), config.relayPin[0]);
		relayLock = new Relay(
			(uint8_t) config.relayPin[0],
		 	config.relayType[0] ? Relay::ControlType::activeHigh : Relay::ControlType::activeLow,
			config.activateTime[0]
		);
	}

	if (config.relayPin[1] != 255 && config.relayPin[1] != config.relayPin[0])
	{
		DEBUG_SERIAL.printf("milliseconds: %lu - setting up doorStatusPin (pin %d)\n", millis(), config.relayPin[1]);
		relayGreen = new Relay(
			(uint8_t) config.relayPin[1],
			config.relayType[1] ? Relay::ControlType::activeHigh : Relay::ControlType::activeLow,
			config.activateTime[1]
		);
	}
	

    DEBUG_SERIAL.printf("milliseconds: %lu - setting up door\n", millis());

	door = Door(relayLock, doorStatusPin);
	door.begin();

	setupMqtt();
	setupWebServer();
	setupWifi(configured);
	writeEvent("INFO", "sys", "System setup completed, running", "");
}

void ICACHE_RAM_ATTR loop()
{
	currentMillis = millis();
	deltaTime = currentMillis - previousLoopMillis;
	uptime = NTP.getUptimeSec();
	previousLoopMillis = currentMillis;

	if (openLockButton)
	{
		openLockButton->update();
		if (openLockButton->fell())
		{
			writeLatest(" ", "Button", 1);
			mqttPublishAccess(now(), "true", "Always", "Button", " ");
			// door.activate();
			activateRelay[0] = true;
		}
	}
	ledWifiStatus();
	ledAccessDeniedOff();
	beeperBeep();

	// Door::update() handles relay and status pin updates
	door.update();
	
	doorbellStatus();

	if (currentMillis >= cooldown)
	{
		rfidLoop();
	}

	// activateRelay[] is set by:
	// 1. rfidLoop for authorized access
	// 2. WebSocket through UI
	// 3. MQTT?
	if (activateRelay[0])
	{
		door.activate();
		// if (relayGreen)
		// 	relayGreen->activate();
		mqttPublishIo("lock", "UNLOCKED");
		activateRelay[0] = false;
	}

	// if ((relayLock->state == Relay::OperationState::inactive) &&
	// 	(relayGreen) &&
	// 	(relayGreen->state != Relay::OperationState::inactive))
	// 	{
	// 		relayGreen->deactivate();
	// 	}

	if (formatreq)
	{
#ifdef DEBUG
		Serial.println(F("[ WARN ] Factory reset initiated..."));
#endif
		SPIFFS.end();
		ws.enable(false);
		SPIFFS.format();
		ESP.restart();
	}

	if (config.autoRestartIntervalSeconds > 0 && uptime > config.autoRestartIntervalSeconds * 1000)
	{
		writeEvent("WARN", "sys", "Auto restarting...", "");
		shouldReboot = true;
	}

	if (shouldReboot)
	{
		writeEvent("INFO", "sys", "System is going to reboot", "");
		ESP.restart();
	}

	if (WiFi.isConnected())
	{
		wiFiUptimeMillis += deltaTime;
	}

	if (config.wifiTimeout > 0 && wiFiUptimeMillis > (config.wifiTimeout * 1000) && WiFi.isConnected())
	{
		writeEvent("INFO", "wifi", "WiFi is going to be disabled", "");
		disableWifi();
	}

	// don't try connecting to WiFi when waiting for pincode
	if (doEnableWifi == true && keyTimer == 0)
	{
		if (!WiFi.isConnected())
		{
			enableWifi();
			writeEvent("INFO", "wifi", "Enabling WiFi", "");
			doEnableWifi = false;
		}
	}

	if (config.mqttEnabled && mqttClient.connected())
	{
		if ((unsigned)now() > nextbeat)
		{
			mqttPublishHeartbeat(now(), uptime);
			nextbeat = (unsigned)now() + config.mqttInterval;
#ifdef DEBUG
			Serial.print("[ INFO ] Nextbeat=");
			Serial.println(nextbeat);
#endif
		}
		processMqttQueue();
	}
}
