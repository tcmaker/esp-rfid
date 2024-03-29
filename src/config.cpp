#include "config.h"

#define DEBUG_SERIAL if(DEBUG)Serial

Config config;

bool ICACHE_FLASH_ATTR loadConfiguration()
{
	File configFile = SPIFFS.open("/config.json", "r");
	if (!configFile)
	{
#ifdef DEBUG
		Serial.println(F("[ WARN ] Failed to open config file"));
#endif
		return false;
	}
	size_t size = configFile.size();
	std::unique_ptr<char[]> buf(new char[size + 1]);
	configFile.readBytes(buf.get(), size);
	buf[size] = '\0';
	DynamicJsonDocument json(2048);
	auto error = deserializeJson(json, buf.get());
	if (error)
	{
#ifdef DEBUG
		Serial.println(F("[ WARN ] Failed to parse config file"));
#endif
		return false;
	}
#ifdef DEBUG
	Serial.println(F("[ INFO ] Config file found"));
#endif
	JsonObject network = json["network"];
	JsonObject hardware = json["hardware"];
	JsonObject general = json["general"];
	JsonObject mqtt = json["mqtt"];
	JsonObject ntp = json["ntp"];
#ifdef DEBUG
	Serial.println(F("[ INFO ] Trying to setup RFID Hardware"));
#endif

	config.wifipin = hardware["wifipin"] | 255;
	if (config.wifipin != 255)
	{
		pinMode(config.wifipin, OUTPUT);
		digitalWrite(config.wifipin, LEDoff);
	}

	config.doorstatpin = hardware["doorstatpin"] | 255;
	if (config.doorstatpin != 255)
	{
		pinMode(config.doorstatpin, INPUT);
	}

	config.maxOpenDoorTime = hardware["maxOpenDoorTime"] | 0;

	config.doorbellpin = hardware["doorbellpin"] | 255;
	if (config.doorbellpin != 255)
	{
		pinMode(config.doorbellpin, INPUT);
	}

	if (hardware.containsKey("accessdeniedpin"))
	{
		config.accessdeniedpin = hardware["accessdeniedpin"];
		if (config.accessdeniedpin != 255)
		{
			pinMode(config.accessdeniedpin, OUTPUT);
			digitalWrite(config.accessdeniedpin, LOW);
		}
	}

	if (hardware.containsKey("beeperpin"))
	{
		config.beeperpin = hardware["beeperpin"];
		if (config.beeperpin != 255)
		{
			pinMode(config.beeperpin, OUTPUT);
			digitalWrite(config.beeperpin, BEEPERoff);
		}
	}

	if (hardware.containsKey("ledwaitingpin"))
	{
		config.ledwaitingpin = hardware["ledwaitingpin"];
		if (config.ledwaitingpin != 255)
		{
			pinMode(config.ledwaitingpin, OUTPUT);
			digitalWrite(config.ledwaitingpin, LEDoff);
		}
	}

	if (hardware.containsKey("openlockpin"))
	{
		config.openlockpin = hardware["openlockpin"];
	}

	if (hardware.containsKey("numrelays"))
	{
		config.numRelays = hardware["numrelays"];
	}
	else
		config.numRelays = 1;

	config.readertype = hardware["readertype"];
	int rfidss;
	if (config.readertype == READER_WIEGAND || config.readertype == READER_WIEGAND_RDM6300)
	{
		int wgd0pin = hardware["wgd0pin"];
		int wgd1pin = hardware["wgd1pin"];
		config.pinCodeRequested = hardware["requirepincodeafterrfid"];
		config.pinCodeOnly = hardware["allowpincodeonly"];
		config.wiegandReadHex = hardware["useridstoragemode"] == "hexadecimal";
		// setupWiegandReader(wgd0pin, wgd1pin); // also some other settings like weather to use keypad or not, LED pin, BUZZER pin, Wiegand 26/34 version
	}
	else if (config.readertype == READER_MFRC522 || config.readertype == READER_MFRC522_RDM6300)
	{
		rfidss = 15;
		if (hardware.containsKey("sspin"))
		{
			rfidss = hardware["sspin"];
		}
		int rfidgain = hardware["rfidgain"];
		// setupMFRC522Reader(rfidss, rfidgain);
	}
	else if (config.readertype == READER_PN532 || config.readertype == READER_PN532_RDM6300)
	{
		rfidss = hardware["sspin"];
		// setupPN532Reader(rfidss);
	}
	config.fallbackMode = network["fallbackmode"] == 1;
	config.autoRestartIntervalSeconds = general["restart"];
	config.wifiTimeout = network["offtime"];
	const char *bssidmac = network["bssid"];
	if (strlen(bssidmac) > 0)
		parseBytes(bssidmac, ':', config.bssid, 6, 16);
	config.deviceHostname = strdup(general["hostnm"]);
	config.ntpServer = strdup(ntp["server"]);
	config.ntpInterval = ntp["interval"];
	config.timeZone = ntp["timezone"];
	config.activateTime[0] = hardware["rtime"];
	config.lockType[0] = hardware["ltype"];
	config.relayType[0] = hardware["rtype"];

	config.relayPin[0] = hardware["rpin"];
	pinMode(config.relayPin[0], OUTPUT);
	digitalWrite(config.relayPin[0], !config.relayType[0]);

	for (int i = 1; i < config.numRelays; i++)
	{
		JsonObject relay = hardware["relay" + String((i + 1))];
		config.activateTime[i] = relay["rtime"];
		config.lockType[i] = relay["ltype"];
		config.relayType[i] = relay["rtype"];
		config.relayPin[i] = relay["rpin"];
		pinMode(config.relayPin[i], OUTPUT);
		digitalWrite(config.relayPin[i], !config.relayType[i]);
	}

	config.ssid = strdup(network["ssid"]);
	config.wifiPassword = strdup(network["pswd"]);
	config.accessPointMode = network["wmode"] == 1;
	config.wifiApIp = strdup(network["apip"]);
	config.wifiApSubnet = strdup(network["apsubnet"]);
	config.networkHidden = network["hide"] == 1;
	config.httpPass = strdup(general["pswd"]);
	config.dhcpEnabled = network["dhcp"] == 1;
	config.ipAddress.fromString(network["ip"].as<const char*>());
	config.subnetIp.fromString(network["subnet"].as<const char*>());
	config.gatewayIp.fromString(network["gateway"].as<const char*>());
	config.dnsIp.fromString(network["dns"].as<const char*>());

	const char *apipch;
	if (config.wifiApIp)
	{
		apipch = config.wifiApIp;
	}
	else
	{
		apipch = "192.168.4.1";
	}
	const char *apsubnetch;
	if (config.wifiApSubnet)
	{
		apsubnetch = config.wifiApSubnet;
	}
	else
	{
		apsubnetch = "255.255.255.0";
	}
	config.accessPointIp.fromString(apipch);
	config.accessPointSubnetIp.fromString(apsubnetch);

	for (int d = 0; d < 7; d++)
	{
		if (general["openinghours"])
		{
			config.openingHours[d] = strdup(general["openinghours"][d].as<const char *>());
		}
		else
		{
			config.openingHours[d] = strdup("111111111111111111111111");
		}
	}

	config.mqttEnabled = mqtt["enabled"] == 1;

	if (config.mqttEnabled)
	{
		String mhsString = mqtt["host"];
		config.mqttHost = strdup(mhsString.c_str());
		config.mqttPort = mqtt["port"];
		String muserString = mqtt["user"];
		config.mqttUser = strdup(muserString.c_str());
		String mpasString = mqtt["pswd"];
		config.mqttPass = strdup(mpasString.c_str());
		String mqttTopicString = mqtt["topic"];
		config.mqttTopic = strdup(mqttTopicString.c_str());
		config.mqttAutoTopic = mqtt["autotopic"];
		if (config.mqttAutoTopic)
		{
			uint8_t macAddr[6];
			WiFi.softAPmacAddress(macAddr);
			char topicSuffix[7];
			sprintf(topicSuffix, "-%02x%02x%02x", macAddr[3], macAddr[4], macAddr[5]);
			char *newTopic = (char *)malloc(sizeof(char) * 80);
			strcpy(newTopic, config.mqttTopic);
			strcat(newTopic, topicSuffix);
			config.mqttTopic = newTopic;
		}
		config.mqttInterval = mqtt["syncrate"];

		if (mqtt["mqttlog"] == 1)
			config.mqttEvents = true;
		else
			config.mqttEvents = false;

		if (mqtt["mqttha"] == 1)
			config.mqttHA = true;
		else
			config.mqttHA = false;
	}
#ifdef DEBUG
	Serial.println(F("[ INFO ] Configuration done."));
#endif
	config.present = true;
	return true;
}
