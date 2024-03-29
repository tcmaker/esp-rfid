#ifndef config_h
#define config_h

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <FS.h>
#include <ArduinoJson.h>
#include "magicnumbers.h"
#include "helpers.h"

struct Config {
    int relayPin[MAX_NUM_RELAYS];
    uint8_t accessdeniedpin = 255;
    bool accessPointMode = false;
    IPAddress accessPointIp;
    IPAddress accessPointSubnetIp;
    /**
     * @brief For momentary relays, this is the minimum time (in ms) that the relay will
     * be in the active state.
     */
    unsigned long activateTime[MAX_NUM_RELAYS];
    unsigned long autoRestartIntervalSeconds = 0;
    unsigned long beeperInterval = 0;
    unsigned long beeperOffTime = 0;
    uint8_t beeperpin = 255;
    byte bssid[6] = {0, 0, 0, 0, 0, 0};
    char *deviceHostname = NULL;
    bool dhcpEnabled = true;
    IPAddress dnsIp;
    uint8_t doorbellpin = 255;
    uint8_t doorstatpin = 255;
    bool fallbackMode = false;
    IPAddress gatewayIp;
    char *httpPass = NULL;
    IPAddress ipAddress;
    uint8_t ledwaitingpin = 255;
    int lockType[MAX_NUM_RELAYS];
    uint8_t maxOpenDoorTime = 0;

    bool mqttEnabled = false;
    bool mqttHA = false; // Sends events over simple MQTT topics and AutoDiscovery
    bool mqttEvents = false;	  // Sends events over MQTT disables SPIFFS file logging
    char *mqttHost = NULL;
    int mqttPort;
    char *mqttUser = NULL;
    char *mqttPass = NULL;
    char *mqttTopic = NULL;
    bool mqttAutoTopic = false;
    unsigned long mqttInterval = 180; // Add to GUI & json config

    bool networkHidden = false;
    char *ntpServer = NULL;
	int ntpInterval = 0;
    int numRelays = 1;
    char *openingHours[7];
    uint8_t openlockpin = 255;
    bool pinCodeRequested = true;
    bool pinCodeOnly = false;
    bool wiegandReadHex = true;
    bool present = false;
    int readertype;
    int relayType[MAX_NUM_RELAYS];
    IPAddress subnetIp;
    const char *ssid;
    int timeZone = 0;
    const char *wifiApIp = NULL;
    const char *wifiApSubnet = NULL;
	uint8_t wifipin = 255;
    const char *wifiPassword = NULL;
    unsigned long wifiTimeout = 0;
};

bool ICACHE_FLASH_ATTR loadConfiguration();

extern Config config;
#endif