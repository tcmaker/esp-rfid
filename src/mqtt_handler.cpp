#include "mqtt_handler.h"

#define DEBUG_SERIAL if(DEBUG)Serial

char mqttBuffer[MAX_MQTT_BUFFER];
DynamicJsonDocument mqttIncomingJson(4096);
std::queue<MqttMessage*> MqttMessages;

void connectToMqtt()
{
	if (!config.mqttEnabled || mqttClient.connected()) {
		return;
	}
	DEBUG_SERIAL.println("[ DEBUG ] Connecting MQTT");
	mqttClient.connect();
}

void disconnectMqtt()
{
	if (!config.mqttEnabled || !mqttClient.connected()) {
		return;
	}
	DEBUG_SERIAL.println("[ INFO ] Disconnecting MQTT");
	
	mqttClient.disconnect(true);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
	String reasonstr = "";
	switch (reason)
	{
	case (AsyncMqttClientDisconnectReason::TCP_DISCONNECTED):
		reasonstr = "TCP_DISCONNECTED";
		break;
	case (AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION):
		reasonstr = "MQTT_UNACCEPTABLE_PROTOCOL_VERSION";
		break;
	case (AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED):
		reasonstr = "MQTT_IDENTIFIER_REJECTED";
		break;
	case (AsyncMqttClientDisconnectReason::MQTT_SERVER_UNAVAILABLE):
		reasonstr = "MQTT_SERVER_UNAVAILABLE";
		break;
	case (AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS):
		reasonstr = "MQTT_MALFORMED_CREDENTIALS";
		break;
	case (AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED):
		reasonstr = "MQTT_NOT_AUTHORIZED";
		break;
	case (AsyncMqttClientDisconnectReason::ESP8266_NOT_ENOUGH_SPACE):
		reasonstr = "ESP8266_NOT_ENOUGH_SPACE";
		break;
	default:
		reasonstr = "Unknown";
		break;
	}
	// writeEvent("WARN", "mqtt", "Disconnected from MQTT server", reasonstr);
	DEBUG_SERIAL.printf("Disconnected from MQTT server: %s", reasonstr.c_str());

	// Do this in all cases in main loop.
	// if (WiFi.isConnected())
	// {
	// 	mqttReconnectTimer.once(60, connectToMqtt);
	// }
}

void mqttPublishEvent(JsonDocument *root, const String topic)
{
	if (config.mqttEnabled && mqttClient.connected())
	{
		String full_topic(config.mqttTopic);
		full_topic = full_topic + "/" + topic;
		String payload;
		serializeJson(*root, payload);
		mqttClient.publish(full_topic.c_str(), 0, false, payload.c_str());
		DEBUG_SERIAL.printf("[ INFO ] Mqtt Publish: %s | ", full_topic.c_str());
		DEBUG_SERIAL.println(payload);
	}
}

// void mqttPublishEventHA(JsonDocument *root, String topic)
// {
// 	if (config.mqttEnabled && mqttClient.connected())
// 	{
// 		String stopic = "homeassistant/";
// 		stopic = stopic + topic;
// 		String mqttBuffer;
// 		serializeJson(*root, mqttBuffer);
// 		mqttClient.publish(stopic.c_str(), 0, true, mqttBuffer.c_str());
// #ifdef DEBUG
// 		Serial.print("[ INFO ] Mqtt Publish:");
// 		Serial.println(mqttBuffer);
// #endif
// 	}
// }

void mqttPublishEvent(JsonDocument *root)
{
	mqttPublishEvent(root, "send");
}

void mqttPublishAck(const char* topic, const char* msg) 
{
	DynamicJsonDocument root(512);
	String ack_topic(topic);
	ack_topic = ack_topic + "/ack";
	root["id"] = WiFi.localIP().toString();
	root["msg"] = msg;
	mqttPublishEvent(&root, ack_topic);
}

void mqttPublishNack(const char* topic, const char* msg) 
{
	DynamicJsonDocument root(512);
	String nack_topic(topic);
	nack_topic = nack_topic + "/nack";
	root["id"] = WiFi.localIP().toString();
	root["msg"] = msg;
	mqttPublishEvent(&root, nack_topic);
}

void mqttPublishBoot(time_t boot_time)
{
	DynamicJsonDocument root(512);
	String topic = String("notify/boot");
	root["time"] = boot_time;
	root["uptime"] = "0";
	root["id"] = WiFi.localIP().toString();
	mqttPublishEvent(&root, topic);
}

// void mqttPublishDiscovery()
// {
// 	String mtopic(config.mqttTopic);
// 	String topic;
// 	String deviceName = config.deviceHostname;
// 	String deviceIP = WiFi.localIP().toString();
// 	DynamicJsonDocument via(512);
// 	via["ids"] = WiFi.macAddress();

// 	DynamicJsonDocument dev(512);
// 	dev["ids"] = WiFi.macAddress();
// 	dev["name"] = config.deviceHostname;
// 	dev["mf"] = "esp-rfid";
// 	dev["sw"] = VERSION;
// 	String num;
// 	for (int n = 0; n < config.numRelays; n++)
// 	{
// 		DynamicJsonDocument root(512);
// 		num = String(n);
// 		topic = "lock/" + deviceName + "/lock" + num + "/config";
// 		root["name"] = deviceName + " Lock" + num;
// 		root["uniq_id"] = deviceName + "/lock" + num;
// 		root["stat_t"] = mtopic + "/io/lock" + num;
// 		root["cmd_t"] = mtopic + "/cmd";
// 		root["pl_unlk"] = "{cmd:'open', door:'" + num + "', doorip:'" + deviceIP + "'}";
// 		root["pl_lock"] = "{cmd:'close', door:'" + num + "', doorip:'" + deviceIP + "'}";
// 		root["avty_t"] = mtopic + "/avty";
// 		if (n == 0)
// 		{
// 			root["dev"] = dev;
// 		}
// 		else
// 		{
// 			root["dev"] = via;
// 		}
// 		mqttPublishEventHA(&root, topic);
// 	}

// 	if (config.doorstatpin != 255)
// 	{
// 		DynamicJsonDocument door(512);
// 		topic = "binary_sensor/" + deviceName + "/door/config";
// 		door["name"] = "Door";
// 		door["uniq_id"] = deviceName + "/door";
// 		door["stat_t"] = mtopic + "/io/door";
// 		door["avty_t"] = mtopic + "/avty";
// 		door["dev_cla"] = "door";
// 		door["dev"] = via;
// 		mqttPublishEventHA(&door, topic);

// 		DynamicJsonDocument tamper(512);
// 		topic = "binary_sensor/" + deviceName + "/tamper/config";
// 		tamper["name"] = "Door tamper";
// 		tamper["uniq_id"] = deviceName + "/tamper";
// 		tamper["stat_t"] = mtopic + "/io/tamper";
// 		tamper["avty_t"] = mtopic + "/avty";
// 		tamper["dev_cla"] = "safety";
// 		tamper["dev"] = via;
// 		mqttPublishEventHA(&tamper, topic);
// 	}

// 	if (config.doorbellpin != 255)
// 	{
// 		DynamicJsonDocument doorbell(512);
// 		topic = "binary_sensor/" + deviceName + "/doorbell/config";
// 		doorbell["name"] = "Doorbell";
// 		doorbell["uniq_id"] = deviceName + "/doorbell";
// 		doorbell["stat_t"] = mtopic + "/io/doorbell";
// 		doorbell["avty_t"] = mtopic + "/avty";
// 		doorbell["dev_cla"] = "sound";
// 		doorbell["icon"] = "mdi:bell";
// 		doorbell["dev"] = via;
// 		mqttPublishEventHA(&doorbell, topic);
// 	}

// 	DynamicJsonDocument tag(512);
// 	topic = "sensor/" + deviceName + "/tag/config";
// 	tag["name"] = "Tag";
// 	tag["uniq_id"] = deviceName + "/tag";
// 	tag["stat_t"] = mtopic + "/tag";
// 	tag["avty_t"] = mtopic + "/avty";
// 	tag["val_tpl"] = "{{ value_json.uid }}";
// 	tag["json_attr_t"] = mtopic + "/tag";
// 	tag["icon"] = "mdi:key";
// 	tag["dev"] = via;
// 	mqttPublishEventHA(&tag, topic);

// 	DynamicJsonDocument user(512);
// 	topic = "sensor/" + deviceName + "/user/config";
// 	user["name"] = "User";
// 	user["uniq_id"] = deviceName + "/name";
// 	user["stat_t"] = mtopic + "/tag";
// 	user["avty_t"] = mtopic + "/avty";
// 	user["val_tpl"] = "{{ value_json.username }}";
// 	user["json_attr_t"] = mtopic + "/tag";
// 	user["icon"] = "mdi:human";
// 	user["dev"] = via;
// 	mqttPublishEventHA(&user, topic);

// 	DynamicJsonDocument dellog(512);
// 	topic = "button/" + deviceName + "/dellog/config";
// 	dellog["name"] = deviceName + " Delete Log";
// 	dellog["uniq_id"] = deviceName + "/deletlog";
// 	dellog["cmd_t"] = mtopic + "/cmd";
// 	dellog["payload_press"] = "{cmd:'deletlog', doorip:'" + deviceIP + "'}";
// 	dellog["avty_t"] = mtopic + "/avty";
// 	dellog["dev"] = via;
// 	mqttPublishEventHA(&dellog, topic);
// }

// void mqttPublishAvty()
// {
// 	String mtopic(config.mqttTopic);
// 	String avty_topic = mtopic + "/avty";
// 	String payloadString = "online";
// 	mqttClient.publish(avty_topic.c_str(), 0, true, payloadString.c_str());
// 	DEBUG_SERIAL.println("[ INFO ] Mqtt Publish: online @ " + avty_topic);
// }

void mqttPublishHeartbeat(time_t heartbeat, time_t uptime)
{
	DynamicJsonDocument root(512);
	String topic("notify/heartbeat");
	root["time"] = heartbeat;
	root["uptime"] = uptime;
	root["id"] = WiFi.localIP().toString();
	mqttPublishEvent(&root, topic);
}

void mqttPublishShutdown(time_t heartbeat, time_t uptime) {
	DynamicJsonDocument root(512);
	String topic("notify/shutdown");
	root["time"] = heartbeat;
	root["uptime"] = uptime;
	root["id"] = WiFi.localIP().toString();
	mqttPublishEvent(&root, topic);
}

// void mqttPublishAccess(time_t accesstime, String const &isknown, String const &type, String const &user, String const &uid)
void mqttPublishAccess(time_t accesstime, AccessResult const &result, String const &detail, String const &credential, String const &person)
{
	DynamicJsonDocument root(512);
	const String topic = String("notify/scan");

	switch (result)
	{
	case unrecognized:
		root["result"] = "unrecognized";
		break;
	case banned:
		root["result"] = "banned";
		break;
	case expired:
		root["result"] = "expired";
		break;
	case granted:
		root["result"] = "granted";
		break;
	default:
		root["result"] = "unknown";
		break;
	}

	root["id"] = config.deviceHostname;
	root["time"] = accesstime;
	root["detail"] = detail;
	root["credential"] = credential;

	if (result != unrecognized) {
		root["person"] = person;
	}

	mqttPublishEvent(&root, topic);
}

void mqttPublishIo(String const &io, String const &state)
{
	if (config.mqttHA && config.mqttEnabled && mqttClient.connected())
	{
		String mtopic(config.mqttTopic);
		String topic = mtopic + "/io/" + io;

		mqttClient.publish(topic.c_str(), 0, false, state.c_str());

#ifdef DEBUG
		Serial.print("[ INFO ] Mqtt Publish: ");
		Serial.println(state + " @ " + topic);
#endif
	}
}

void onMqttPublish(uint16_t packetId)
{
	// writeEvent("INFO", "mqtt", "MQTT publish acknowledged", String(packetId));
}

void getUserCount() {
	DynamicJsonDocument root(512);
	Dir dir = SPIFFS.openDir("/P/");
	int i = 0;
	DEBUG_SERIAL.println("[ INFO ] getUserList");
	while (dir.next())
	{
		++i;
	}
	root["id"] = WiFi.localIP().toString();
	root["count"] = i;
	mqttPublishEvent(&root, String("getnumaccounts"));
}

void getUserList()
{
	DynamicJsonDocument root(512);
	JsonArray users = root.createNestedArray("list");
	Dir dir = SPIFFS.openDir("/P/");
#ifdef DEBUG
	Serial.println("[ INFO ] getUserList");
#endif
	while (dir.next())
	{
		JsonObject item = users.createNestedObject();
		String uid = dir.fileName();
		uid.remove(0, 3);
		item["uid"] = uid;
		File f = SPIFFS.open(dir.fileName(), "r");
		size_t size = f.size();
		std::unique_ptr<char[]> buf(new char[size]);
		f.readBytes(buf.get(), size);
		DynamicJsonDocument json(512);
		auto error = deserializeJson(json, buf.get());
		if (!error)
		{
			mqttPublishEvent(&root);
		}
	}
}

void deleteAllUserFiles()
{
	Dir dir = SPIFFS.openDir("/P/");
	while (dir.next())
	{
		String uid = dir.fileName();
		uid.remove(0, 3);
		SPIFFS.remove(dir.fileName());
	}
}

void deleteUserID(const char *uid)
{
	// only do this if a user id has been provided
	if (uid)
	{
		String myuid = String(uid);
		myuid = "/P/" + myuid;
		if (SPIFFS.exists(myuid.c_str())) {
			SPIFFS.remove(myuid.c_str());
		}
		// Dir dir = SPIFFS.openDir("/P/");
		// while (dir.next())
		// {
		// 	String user_id = dir.fileName();
		// 	String myuid = uid;
		// 	user_id.remove(0, 3);
		// 	if (myuid == user_id)
		// 	{
		// 		SPIFFS.remove(dir.fileName());
		// 	}
		// }
	}
}


void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
	// The AsyncMQTTClient library will pass partial payloads as the TCP packets are received
	// * len is the length of this payload
	// * index is starting location of this payload
	// * total is the total size of the entire payload.

	if (total > MAX_MQTT_BUFFER - 1) {
		// payload too big for our buffer, skip it.
		return;
	}

	size_t n = 0;
	size_t i = index;
	while(n < len) {
		mqttBuffer[i] = payload[n];
		n++;
		i++;
	}
	if (index + len == total) { //payload complete
		mqttBuffer[i] = '\0';
	} else {
		return;
	}

	MqttMessage *incomingMessage = new MqttMessage;

	DEBUG_SERIAL.printf("[ INFO ] JSON msg (%s): ", topic);
	DEBUG_SERIAL.println(mqttBuffer);

	strlcpy(incomingMessage->topic, topic, sizeof(incomingMessage->topic));
	strlcpy(incomingMessage->serializedMessage, mqttBuffer, MAX_MQTT_BUFFER);


	// push message to queue to handle outside of callback
	MqttMessages.push(incomingMessage);
}

void onMqttMessage_HandleRecord(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
	if (total > MAX_MQTT_BUFFER - 1) {
		// payload too big for our buffer, skip it.
		return;
	}

	size_t n = 0;
	size_t i = index;
	while(n < len) {
		mqttBuffer[i] = payload[n];
		n++;
		i++;
	}
	if (index + len == total) { //payload complete
		mqttBuffer[i] = '\0';
	} else {
		return;
	}

	MqttMessage *incomingMessage = new MqttMessage;

	DEBUG_SERIAL.printf("[ INFO ] JSON msg (%s): ", topic);
	DEBUG_SERIAL.println(mqttBuffer);

	strlcpy(incomingMessage->topic, topic, sizeof(incomingMessage->topic));
	strlcpy(incomingMessage->serializedMessage, mqttBuffer, MAX_MQTT_BUFFER);


	// push message to queue to handle outside of callback
	MqttMessages.push(incomingMessage);

}

MqttAccessTopic decodeMqttTopic(const char *topic) {
	// all subscribed topics should use the same prefix
	const char* subTopic = topic + strlen(config.mqttTopic);

	if (strcmp(subTopic, "addperson") == 0) {
		DEBUG_SERIAL.println(F("[ INFO ] addperson"));
 		return ADD_UID;
	} else if (strcmp(subTopic, "deleteperson") == 0) {
		DEBUG_SERIAL.println(F("[ INFO ] deleteperson"));
 		return DELETE_UID;
	} else if (strcmp(subTopic, "getnumaccounts") == 0) {
		DEBUG_SERIAL.println(F("[ INFO ] getnumaccounts"));
 		return GET_NUM_UIDS;
	} else if (strcmp(subTopic, "getusers") == 0) {
		DEBUG_SERIAL.println(F("[ INFO ] getusers"));
 		return GET_UIDS;
	} else if (strcmp(subTopic, "unlock") == 0) {
		DEBUG_SERIAL.println(F("[ INFO ] unlock"));
 		return UNLOCK;
	} else if (strcmp(subTopic, "lock") == 0) {
		DEBUG_SERIAL.println(F("[ INFO ] lock"));
 		return LOCK;
	} else {
		return UNSUPPORTED;
	}
}

void processMqttMessage(MqttMessage incomingMessage)
{
	char *topic = incomingMessage.topic;

	mqttIncomingJson.clear();
	// DynamicJsonDocument mqttIncomingJson(4096);
	// note that the serializedMessage is modified here rather than dupicating it into the JSON document.
	auto error = deserializeJson(mqttIncomingJson, incomingMessage.serializedMessage);
	if (error)
	{
		DEBUG_SERIAL.print("[ INFO ] Failed deserializing MQTT message.");
		return;
	}

	if (!mqttIncomingJson.containsKey("id"))
	{
		return;
	}
	
	const char *id = mqttIncomingJson["id"];
	// String espIp = WiFi.localIP().toString();
	// if (!((strcmp(ipadr, espIp.c_str()) == 0) && (ipadr != NULL)))
	// {
		DEBUG_SERIAL.print("[ INFO ] recv id: ");
		DEBUG_SERIAL.println(id);
	// 	return;
	// }


	// char *command = incomingMessage->command;
	MqttAccessTopic topicID = decodeMqttTopic(topic);
	String filename = "/P/";
	File f;
	const char *uid;

	switch (topicID)
	{
	case ADD_UID:
		if (!mqttIncomingJson.containsKey("credential")) {
			mqttPublishNack(topic, "invalid format");
			return;
		}
		DEBUG_SERIAL.print(F("[ INFO ] Adding credential: "));

		uid = mqttIncomingJson["credential"];
		DEBUG_SERIAL.println(uid);
		DEBUG_SERIAL.printf(" memory_usage: %u\n", mqttIncomingJson.memoryUsage());
		filename += uid;
		f = SPIFFS.open(filename, "w");
		// Check if we created the file
		if (f)
		{
			mqttIncomingJson["local_update"] = now();
			serializeJson(mqttIncomingJson, f);
			f.close();
			mqttPublishAck(topic, uid);
		} else {
			mqttPublishNack(topic, "could not create file");
		}
		break;
	case DELETE_UID:
		if (!mqttIncomingJson.containsKey("credential")) {
			mqttPublishNack(topic, "invalid format");
			return;
		}
		DEBUG_SERIAL.print(F("[ INFO ] Deleting credential: "));
		uid = mqttIncomingJson["credential"];
		DEBUG_SERIAL.println(uid);
		deleteUserID(uid);
		break;
	case GET_NUM_UIDS:
		DEBUG_SERIAL.println("[ INFO ] Get User List");
		getUserList();
		break;
	case GET_CONF:
		DEBUG_SERIAL.println("[ INFO ] Get configuration");
		f = SPIFFS.open("/config.json", "r");
		// char *buf;
		if (f)
		{
			// int fileSize = f.size();
			// buf = (char *)malloc(fileSize + 1);
			// f.readBytes(buf, fileSize);
			// f.close();
			// buf[fileSize] = '\0';

			// DynamicJsonDocument root(2048);
			// root["type"] = "getconf";
			// root["ip"] = WiFi.localIP().toString();
			// root["hostname"] = config.deviceHostname;
			// DynamicJsonDocument configFile(2048);
			// deserializeJson(configFile, buf, fileSize + 1);
			// root["configfile"] = configFile;
			// mqttPublishEvent(&root);
			// free(buf);
		}
		break;
	default:
		break;
	}
		// writeLatest(" ", "MQTT", 1);
		// mqttPublishAccess(now(), "true", "Always", "MQTT", " ");
		// for (int currentRelay = 0; currentRelay < config.numRelays; currentRelay++)
		// {
		// 	activateRelay[currentRelay] = true;
		// }
		// previousMillis = millis();
		// 	else if (strcmp(command, "updateconf") == 0)
		// 	{
		// #ifdef DEBUG
		// 		Serial.println("[ INFO ] Update configuration");
		// #endif
		// 		DynamicJsonDocument mqttIncomingJson(4096);
		// 		auto error = deserializeJson(mqttIncomingJson, incomingMessage->serializedMessage);
		// 		if (error)
		// 		{
		// #ifdef DEBUG
		// 			Serial.println("[ INFO ] Failed parsing MQTT message!!!");
		// #endif
		// 			return;
		// 		}
		// 		File f = SPIFFS.open("/config.json", "w");
		// 		if (f)
		// 		{
		// 			serializeJsonPretty(mqttIncomingJson["configfile"], f);
		// 			f.close();
		// 			mqttPublishAck("updateconf");
		// 			shouldReboot = true;
		// 		}
		// 	}

	// free(incomingMessage);
	return;
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos)
{
#ifdef DEBUG
	Serial.println("[ INFO ] Subscribe acknowledged.");
	Serial.print("[ INFO ] packetId: ");
	Serial.println(packetId);
	Serial.print("[ INFO ] qos: ");
	Serial.println(qos);
#endif
}

void onMqttConnect(bool sessionPresent)
{
#ifdef DEBUG
	Serial.println("[ INFO ] MQTT Connected session");
#endif
	if (sessionPresent == true)
	{
#ifdef DEBUG
		Serial.println("[ INFO ] MQTT session Present: True");
#endif
		// writeEvent("INFO", "mqtt", "Connected to MQTT Server", "Session Present");
	}
	mqttPublishBoot(now());

	String base_topic(config.mqttTopic);
	String stopic("/+");
	stopic = base_topic + stopic;
	mqttClient.subscribe(stopic.c_str(), 2);

	// if (config.mqttHA)
	// {
	// 	mqttPublishDiscovery();
	// 	mqttPublishAvty();
	// 	if (config.doorstatpin != 255)
	// 	{
	// 		mqttPublishIo("door", digitalRead(config.doorstatpin) == HIGH ? "OFF" : "ON");
	// 	}
	// 	if (config.doorbellpin != 255)
	// 	{
	// 		mqttPublishIo("doorbell", digitalRead(config.doorbellpin) == HIGH ? "ON" : "OFF");
	// 	}
	// 	for (int i = 0; i < config.numRelays; i++)
	// 	{
	// 		mqttPublishIo("lock" + String(i), digitalRead(config.relayPin[i]) == config.relayType[i] ? "UNLOCKED" : "LOCKED");
	// 	}
	// 	mqttPublishIo("tamper", "OFF");
	// }
}

void processMqttQueue()
{
	MqttMessage *m;
	if (MqttMessages.empty())
		return;
	m = MqttMessages.front();
	MqttMessages.pop();
	processMqttMessage(*m);
	free(m);
	// while(messageQueue != NULL) {
	// 	MqttMessage *messageToProcess = messageQueue;
	// 	messageQueue = messageToProcess->nextMessage;
	// 	processMqttMessage(messageToProcess);
	// }
}

void setupMqtt()
{
  if (!config.mqttEnabled)
	{
		return;
	}

	// DEBUG_SERIAL.println("[ INFO ] Trying to setup MQTT");

	// if (config.mqttHA)
	// {
	// 	String stopic(config.mqttTopic);
	// 	String topicString = stopic + "/avty";
	// 	String payloadString = "offline";
	// 	char *topicLWT = strdup(topicString.c_str());
	// 	char *payloadLWT = strdup(payloadString.c_str());
	// 	mqttClient.setWill(topicLWT, 2, true, payloadLWT);
	// }
	mqttClient.setServer(config.mqttHost, config.mqttPort);
	mqttClient.setCredentials(config.mqttUser, config.mqttPass);
	mqttClient.onDisconnect(onMqttDisconnect);
	mqttClient.onPublish(onMqttPublish);
	mqttClient.onSubscribe(onMqttSubscribe);
	mqttClient.onConnect(onMqttConnect);
	mqttClient.onMessage(onMqttMessage);
}
