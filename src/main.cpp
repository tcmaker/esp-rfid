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


#include "Arduino.h"
#include <ESP8266WiFi.h>
// #include <SPI.h>
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

#define DEBUG_SERIAL if(DEBUG)Serial

// Variables for whole scope
unsigned long cooldown = 0;
unsigned long currentMillis = 0;
unsigned long deltaTime = 0;
bool flagMQTTSendUserList = false;
bool doEnableWifi = false;
bool formatreq = false;
const char *httpUsername = "admin";
unsigned long keyTimer = 0;

uint8_t lastDoorbellState = 0;
uint8_t lastDoorState = 0;
uint8_t lastTamperState = 0;
unsigned long openDoorMillis = 0;

unsigned long lastbeat = 0;
unsigned long previousLoopMillis = 0;
unsigned long previousMillis = 0;
bool shouldReboot = false;
unsigned long uptimeSeconds = 0;
unsigned long wifiPinBlink = millis();
unsigned long wiFiUptimeMillis = 0;

#include "config.h"
#include "log.esp"
#include "mqtt_handler.h"
#include "helpers.h"
#include "magicnumbers.h"
#include "relay.h"
#include "door.h"
#include "accesscontrol.h"

Door door;
Bounce *openLockButton = nullptr;
Bounce *doorStatusPin = nullptr;
Relay *relayLock = nullptr;
Relay *relayGreen = nullptr;

bool networkFirstUp = false;

// relay specific variables
bool activateRelay[MAX_NUM_RELAYS] = {false, false, false, false};
bool deactivateRelay[MAX_NUM_RELAYS] = {false, false, false, false};

#include "led.esp"
#include "beeper.esp"
#include "wsResponses.esp"
// #include "rfid.esp"
#include "wifi.esp"
#include "websocket.esp"
#include "webserver.esp"
#include "doorbell.esp"

bool mqttSendUserList();
void armRemoteLookup(String uid);

void accessGranted_wrapper(AccessResult result, String detail, String credential, String name)
{
	DEBUG_SERIAL.printf("Granted: %s\n", detail.c_str());
	DEBUG_SERIAL.printf("Wi-Fi: %d, MQTT: %d, Timer: %d\n", WiFi.isConnected(), mqttClient.connected(), mqttReconnectTimer.active());
	mqttPublishAccess(now(), result, detail, credential, name);
	door.activate();
}

void accessDenied_wrapper(AccessResult result, String detail, String credential, String name)
{
	DEBUG_SERIAL.printf("Denied: %s\n", detail.c_str());
	mqttPublishAccess(now(), result, detail, credential, name);
}



void ICACHE_FLASH_ATTR setup()
{
#ifdef DEBUG
	Serial.begin(115200);
	Serial.println();

	Serial.print(F("[ INFO ] ESP RFID v"));
	Serial.println(bootInfo.version);
	Serial.print(F("[ INFO ] ENV: "));
	Serial.println(bootInfo.debug ? "DEBUG" : "PROD");

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

	bootInfo.formatted = true;
	if (!SPIFFS.begin())
	{
		bootInfo.formatted = false;
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

	if (!SPIFFS.info(bootInfo.fsinfo)) {
		DEBUG_SERIAL.println(F("[ ERROR ] Failed to retrieve SPIFFS info"));
	}

	bootInfo.configured = loadConfiguration();

	ws.setAuthentication("admin", config.httpPass);

	if (config.openlockpin != 255)
	{
		DEBUG_SERIAL.printf("microseconds: %lu - setting up openLockButton (pin %d)\n", micros(), config.openlockpin);
		openLockButton = new Bounce();
		openLockButton->attach(config.openlockpin, INPUT_PULLUP);
		openLockButton->interval(30);
	}

	if (config.doorstatpin != 255)
	{
		DEBUG_SERIAL.printf("microseconds: %lu - setting up doorStatusPin (pin %d)\n", micros(), config.doorstatpin);
		doorStatusPin = new Bounce();
		doorStatusPin->interval(2000);
		doorStatusPin->attach(config.doorstatpin, INPUT);
	}

	if (config.relayPin[0] != 255)
	{
		DEBUG_SERIAL.printf("microseconds: %lu - setting up relayLock (pin %d)\n", micros(), config.relayPin[0]);
		relayLock = new Relay(
			(uint8_t)config.relayPin[0],
			config.relayType[0] ? Relay::ControlType::activeHigh : Relay::ControlType::activeLow,
			config.activateTime[0]);
	}

	if (config.relayPin[1] != 255 && config.relayPin[1] != config.relayPin[0])
	{
		DEBUG_SERIAL.printf("microseconds: %lu - setting up relayGreen (pin %d)\n", micros(), config.relayPin[1]);
		relayGreen = new Relay(
			(uint8_t)config.relayPin[1],
			config.relayType[1] ? Relay::ControlType::activeHigh : Relay::ControlType::activeLow,
			config.activateTime[1]);
	}

	DEBUG_SERIAL.printf("microseconds: %lu - setting up reader\n", micros());

	TCMWiegand.begin(13, 12);
	// pinMode(13, INPUT_PULLUP);
	// pinMode(12, INPUT_PULLUP);

	DEBUG_SERIAL.printf("microseconds: %lu - setting up door\n", micros());

	// door = Door(relayLock, doorStatusPin);
	door = Door(doorStatusPin, relayLock, relayGreen);
	// if (config.maxOpenDoorTime)
	door.maxOpenTime = config.maxOpenDoorTime;
	door.begin();

	AccessControl.accessGranted = accessGranted_wrapper;
	AccessControl.accessDenied = accessDenied_wrapper;
	AccessControl.lookupRemote = armRemoteLookup;
	
	setupMqtt();
	//mqtt.onNewRecord = onNewRecord;
	setupWebServer();
	setupWifi(bootInfo.configured);
	writeEvent("INFO", "sys", "System setup completed, running", "");
}

static String localUid;
// static JsonDocument *localScan;

/**
 * @brief Call this to copy the uid and allow matching against new records that arrive.
 * 
 * @param uid 
 */
void armRemoteLookup(String uid) {
	localUid = uid;
	
}

/**
 * @brief Called when a ADD_UID message is received over MQTT. If the AccessControl object is
 * in the `wait_remote` state, then this will copy over the relevent data from the MQTT message to the
 * AccessControl record and update the AccessControl state.
 * 
 * 
 * Note that this could be called asynchronously on the via the onMqttMessage() call back, but the
 * current implementation processes the MQTT payloads via the main loop.
 * 
 * @param uid The "credential" from the new payload
 * @param payload A reference to the MQTT JSON payload
 */
void onNewRecord(const String uid, const JsonDocument& payload) {
	if (!localUid.isEmpty() && AccessControl.state == ControlState::wait_remote) {
		// this state means that localUid has been set
		if (uid == localUid) {
			// set next state asynchronously to stop timeout
			AccessControl.state = ControlState::process_record_remote;
			localUid.clear();
			AccessControl.jsonRecord["person"] = payload["person"].as<char>();
			AccessControl.jsonRecord["credential"] = payload["credential"].as<char>();
			AccessControl.jsonRecord["validuntil"] = payload["validuntil"].as<unsigned long>();
			AccessControl.jsonRecord["validsince"] = payload["validsince"].as<unsigned long>();
			AccessControl.jsonRecord["is_banned"] = payload["is_banned"].as<unsigned long>();
		}
	} else if (AccessControl.state == ControlState::cool_down || AccessControl.state == ControlState::wait_read) {
		localUid.clear();
	}

}

void ICACHE_RAM_ATTR loop()
{
	currentMillis = millis();
	deltaTime = currentMillis - previousLoopMillis;
	previousLoopMillis = currentMillis;

	if (openLockButton)
	{
		openLockButton->update();
		if (openLockButton->fell())
		{
			writeLatest(" ", "Button", 1);
			mqttPublishAccess(now(), AccessResult::granted, String("N/A"), "Button", " ");
			// door.activate();
			activateRelay[0] = true;
		}
	}

	// digitalWrite(9, 1);

	ledWifiStatus();
	ledAccessDeniedOff();
	beeperBeep();

	// Door::update() handles relay and status pin updates
	door.update();

	doorbellStatus();

	// if (currentMillis >= cooldown)
	// {
	// 	rfidLoop();
	// }

	TCMWiegand.loop();
	// HidProxWiegand.loop();
	AccessControl.loop();

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

	if (formatreq) {
		DEBUG_SERIAL.println(F("[ WARN ] Factory reset initiated..."));
		SPIFFS.end();
		ws.enable(false);
		SPIFFS.format();
		ESP.restart();
	}

	if (networkFirstUp) {
		networkFirstUp = false;
		Serial.println("[ INFO ] Trying to setup NTP Server");
		NTP.Ntp(config.ntpServer, config.timeZone, config.ntpInterval * 60);
	}

	uptimeSeconds = NTP.getUptimeSec();

	if (config.autoRestartIntervalSeconds > 0 && uptimeSeconds > config.autoRestartIntervalSeconds)
	{
		writeEvent("WARN", "sys", "Auto restarting...", "");
		shouldReboot = true;
	}

	if (shouldReboot)
	{
		writeEvent("INFO", "sys", "System is going to reboot", "");
		if (config.mqttEnabled and mqttClient.connected()) {
			mqttPublishShutdown(now(), uptimeSeconds);
		}
		SPIFFS.end();
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
		doEnableWifi = false;
		if (!WiFi.isConnected())
		{
			enableWifi();
			writeEvent("INFO", "wifi", "Enabling WiFi", "");
		}
	}

	

	if (config.mqttEnabled) {
		if (mqttClient.connected()) {
			if ((unsigned long) now() - lastbeat > config.mqttInterval)
			{
				mqttPublishHeartbeat(now(), uptimeSeconds);
				lastbeat = (unsigned)now();
				// + config.mqttInterval;
				DEBUG_SERIAL.print("[ INFO ] Nextbeat=");
				DEBUG_SERIAL.println(lastbeat + config.mqttInterval);
			}
			processMqttQueue();
			if (flagMQTTSendUserList) {
				flagMQTTSendUserList = mqttSendUserList();
			}
		} else if (WiFi.isConnected() && !mqttReconnectTimer.active()) {
			mqttReconnectTimer.once(20, connectToMqtt);
		}
	}
}

bool mqttSendUserList()
{
	static unsigned long count = 0;
	static Dir *dir = nullptr;
	static bool available;

	// should this be a static document to avoid heap fragmentation?
	DynamicJsonDocument root(4096);

	FSInfo fsinfo;

	unsigned long i = 0;

	if (count == 0) {
		// starting out, open filesystem
		dir = new Dir;
		*dir = SPIFFS.openDir("/P/");

		// load first file
		available = dir->next();
		// otherwise filerecord is loaded from lass call
	}

	JsonArray users = root.createNestedArray("userlist");

	while (available && i < 10)	{
		++count;
		++i;

		JsonObject item = users.createNestedObject();

		String uid = dir->fileName();
		uid.remove(0, 3);  // remove "/P/" from filename
		item["uid"] = uid;

		File f = SPIFFS.open(dir->fileName(), "r");
		size_t size = f.size();
		item["filesize"] = (unsigned) size;

		std::unique_ptr<char[]> buf(new char[size + 1]);
		f.readBytes(buf.get(), size);
		f.close();

		// ensure buffer is null terminated
		buf[size] = '\0';

		// presume that file is already JSON
		item["record"] = serialized(buf.get());

		// prepare for next file
		available = dir->next();
	}

	root["size"] = i;
	root["index"] = count - i;

	if (!available) {
		// no next file, so we're done
		// last json payload includes overall data
		root["total"] = count;
		SPIFFS.info(fsinfo);
		root["flash_used"] = fsinfo.usedBytes;
		root["flash_available"] = fsinfo.totalBytes - fsinfo.usedBytes;
		root["complete"] = true;
		// complete = true;
		count = 0;
		delete dir;
		dir = nullptr;
	} else {
		root["complete"] = false;
	}

	root["json_memory_usage"] = root.memoryUsage();
	mqttPublishEvent(&root, "notify/db/list");

	return available;
}
