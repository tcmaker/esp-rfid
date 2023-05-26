#include "mqtt_handler.h"

#define DEBUG_SERIAL if(DEBUG)Serial

char mqttBuffer[MAX_MQTT_BUFFER];
DynamicJsonDocument mqttIncomingJson(4096);
std::queue<MqttMessage> MqttMessages;

MqttMessage::MqttMessage(const char mqttTopic[], const char mqttPayload[]) {
	strlcpy(topic, mqttTopic, sizeof(topic));
	msgLen = strlen(mqttPayload);
	serializedMessage = std::unique_ptr<char[]>(new char[msgLen + 1]);
	// DEBUG_SERIAL.println(mqttPayload);
	strlcpy(serializedMessage.get(), mqttPayload, msgLen + 1);
	// DEBUG_SERIAL.println(serializedMessage.get());
}

void processMqttQueue()
{
	if (MqttMessages.empty())
		return;

	MqttMessage &m = MqttMessages.front();
	processMqttMessage(m);
	MqttMessages.pop();
	// delete &m;
}

void processMqttMessage(MqttMessage& incomingMessage)
{
	char *topic = incomingMessage.topic;
	// const char *message = incomingMessage.serializedMessage.get();

	// DEBUG_SERIAL.printf("[ INFO ] %lu us - Processing MQTT message: %s\n", micros(), topic);

	mqttIncomingJson.clear();
	// DynamicJsonDocument mqttIncomingJson(4096);
	// DEBUG_SERIAL.println(incomingMessage.serializedMessage.get());
	// incomingMessage.serializedMessage is modified by the deserializeJson function
	auto error = deserializeJson(mqttIncomingJson, incomingMessage.serializedMessage.get());
	if (error)
	{
		DEBUG_SERIAL.printf("[ INFO ] Failed deserializing MQTT message: %s\n", error.c_str());
		mqttPublishNack("notify/error", "invalid JSON");
		return;
	}

	if (!mqttIncomingJson.containsKey("id"))
	{
		DEBUG_SERIAL.println("[ ERROR ] No `id` key.");
		mqttPublishNack("notify/error", "no `id` provided");
		return;
	}
	
	const char* id = mqttIncomingJson["id"];
	if (id && !((strcmp(id, config.deviceHostname)) == 0))
	{
		// TODO: reject messages without id match
		DEBUG_SERIAL.printf("[ INFO ] recv id mismatch: %s\n", id);
		// return
	}

	MqttAccessTopic topicID = decodeMqttTopic(topic);
	// String filename = "/P/";
	File f;
	// char *uid;

	switch (topicID)
	{
	case ADD_UID:
		if (!mqttIncomingJson.containsKey("credential")) {
			mqttPublishNack(topic, "invalid schema");
			return;
		}


		strlcpy(incomingMessage.uid, mqttIncomingJson["credential"], 20);
		// DEBUG_SERIAL.print("[ INFO ] Adding credential: ");
		// DEBUG_SERIAL.println(incomingMessage.uid);
		if (DEBUG && mqttIncomingJson.memoryUsage() > 180) {
			DEBUG_SERIAL.printf(" json_memory_usage: %u\n", mqttIncomingJson.memoryUsage());
		}

		onNewRecord(incomingMessage.uid, mqttIncomingJson);
		addUserID(incomingMessage);

		break;
	case DELETE_UID:
		if (!mqttIncomingJson.containsKey("credential")) {
			mqttPublishNack(topic, "invalid format");
			return;
		}
		DEBUG_SERIAL.print("[ INFO ] Deleting credential: ");
		strncpy(incomingMessage.uid, mqttIncomingJson["credential"], 20);
		DEBUG_SERIAL.println(incomingMessage.uid);
		deleteUserID(incomingMessage.uid);
		break;
	case GET_UIDS:
		DEBUG_SERIAL.println("[ INFO ] Get User List");
		getUserList();
		break;
	case GET_NUM_UIDS:
		DEBUG_SERIAL.println("[ INFO ] Get DB status");
		getDbStatus();
		break;
	case GET_CONF:
		DEBUG_SERIAL.println("[ INFO ] Get configuration");
		f = SPIFFS.open("/config.json", "r");
		if (f)
		{
			int fileSize = f.size();
			char *buf = (char *)malloc(fileSize + 1);
			f.readBytes(buf, fileSize);
			f.close();

			buf[fileSize] = '\0';
			DynamicJsonDocument root(2048);
			// root["type"] = "getconf";
			root["ip"] = WiFi.localIP().toString();
			root["hostname"] = config.deviceHostname;
			// DynamicJsonDocument configFile(2048);
			// deserializeJson(configFile, buf, fileSize + 1);
			root["configfile"] = serialized(buf);
			mqttPublishEvent(&root, "notify/conf");
			free(buf);
		}
		break;
	default:
		DEBUG_SERIAL.println("[ ERROR ] Unsupported message");
		break;
	}
	return;
}

MqttDatabaseSender::MqttDatabaseSender() {
	// DEBUG_SERIAL.printf("[ INFO ] MqttDatabaseSender - Free heap:%u\n", ESP.getFreeHeap());
	pkt_id = 1;
	root = new DynamicJsonDocument(4096);
	// mqttClient.onPublish((std::function<void(uint16_t packetId)>) std::bind(&MqttDatabaseSender::onMqttPublish, this, std::placeholders::_1));
	// mqttClient.onPublish(&MqttDatabaseSender::onMqttPublish);
}

MqttDatabaseSender::~MqttDatabaseSender() {
	delete root;
	// DEBUG_SERIAL.println("~MqttDatabaseSender");
	// DEBUG_SERIAL.println((unsigned long) dir);
	if (dir != nullptr) {
		FS_IN_USE = false;
		delete dir;
		// dir = nullptr;
	}
}

uint16_t MqttDatabaseSender::pkt_id = 1;

void MqttDatabaseSender::onMqttPublish(uint16_t packetId) {
	// DEBUG_SERIAL.printf("[ DEBUG ] %lu us - packet ack'd: %u\n", micros(), packetId);
	if (pkt_id > 1 && pkt_id == packetId) {
		pkt_id = 1;
	}
}

bool MqttDatabaseSender::run() {
	if (count == 0) {
		while (FS_IN_USE) {
		}
		FS_IN_USE = true;

		// starting out, open filesystem
		dir = new Dir;
		*dir = SPIFFS.openDir("/P/");
		DEBUG_SERIAL.println((unsigned long) dir);

		// load first file
		_available = dir->next();
	}

	if (pkt_id == 1) {
		// last packet cleared, build new packet
		
		unsigned long i = 0;
		
		root->clear();
		//previous packet was accepted by mqttClient
		//move to next packet

		JsonArray users = root->createNestedArray("userlist");

		while (_available && i < 10)	{
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
			_available = dir->next();
		}

		(*root)["size"] = i;
		(*root)["index"] = count - i;

		if (!_available) {
			// no next file, so we're done
			// last json payload includes overall data
			FSInfo fsinfo;

			(*root)["total"] = count;
			SPIFFS.info(fsinfo);
			(*root)["flash_used"] = fsinfo.usedBytes;
			(*root)["flash_available"] = fsinfo.totalBytes - fsinfo.usedBytes;
			(*root)["complete"] = true;
		} else {
			(*root)["complete"] = false;
		}

		(*root)["json_memory_usage"] = (*root).memoryUsage();
	} else if (millis() - lastSend > 2000) {
		DEBUG_SERIAL.println("[ DEBUG ] MqttDatabaseSender timeout");
		// timeout -- die
		return false;
	} else if (pkt_id > 1) {
		// do nothing now, get called again later
		return true;
	} // else if pkt_id == 0 , then last packet did not send, so send again

	// send packet with qos 1 to keep track that packets were sent
	pkt_id = mqttPublishEvent(root, "notify/db/list", 1);
	if (pkt_id > 1) {
		lastSend = millis();
	}

	// if (pkt_id != 0) {
	DEBUG_SERIAL.printf("[ DEBUG ] %lu us - pkt_id: %u\n", micros(), pkt_id);
	// }

	if (!_available) {
		done = true;
		return false;
	}

	// keep calling as long as files available or pkt_id is zero (packet not accepted)
	return true;
}

void setupMqtt()
{
  if (!config.mqttEnabled)
	{
		return;
	}

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

void connectToMqtt()
{
	// mqttReconnectTimer.detach();
	if (!config.mqttEnabled || mqttClient.connected()) {
		return;
	}
	DEBUG_SERIAL.println("[ DEBUG ] Connecting MQTT");
	DynamicJsonDocument root(512);
	
	// String topic(config.mqttTopic);

	// root["base_topic"] = topic;
	root["id"] = config.deviceHostname;
	root["ip"] = WiFi.localIP().toString();

	// static variables are necessary as the mqttClient
	// does not copy the strings, just stores the pointers
	static char topic[64];
	strlcpy(topic, config.mqttTopic, 64);
	strlcat(topic, "/notify/lastwill", 64);

	static char payload[64];
	size_t x = serializeJson(root, payload, 64);
	payload[63] = '\0';

	DEBUG_SERIAL.printf("[ DEBUG ] lastwill json payload size: %u / 64\n", x);
	// mqttClient.setWill(topic.c_str(), 0, false, payload.c_str());
	mqttClient.setWill(topic, 0, false, payload);
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

MqttAccessTopic decodeMqttTopic(const char *topic) {
	// all subscribed topics should use the same prefix
	const char* subTopic = topic + strlen(config.mqttTopic) + 1;

	if (strcmp(subTopic, "db/add") == 0) {
		// DEBUG_SERIAL.println("[ INFO ] db/add");
 		return ADD_UID;
	} else if (strcmp(subTopic, "db/delete") == 0) {
		DEBUG_SERIAL.println("[ INFO ] db/delete");
 		return DELETE_UID;
	} else if (strcmp(subTopic, "db/status") == 0) {
		DEBUG_SERIAL.println("[ INFO ] db/status");
 		return GET_NUM_UIDS;
	} else if (strcmp(subTopic, "db/get") == 0) {
		DEBUG_SERIAL.println("[ INFO ] db/get");
 		return GET_UIDS;
	} else if (strcmp(subTopic, "set/unlock") == 0) {
		DEBUG_SERIAL.println("[ INFO ] set/unlock");
 		return UNLOCK;
	} else if (strcmp(subTopic, "set/lock") == 0) {
		DEBUG_SERIAL.println("[ INFO ] set/lock");
 		return LOCK;
	} else if (strcmp(subTopic, "conf/get") == 0) {
		DEBUG_SERIAL.println("[ INFO ] conf/get");
		return GET_CONF;
	} else {
		return UNSUPPORTED;
	}
}

// holdover from original implementation.
void mqttPublishEvent(JsonDocument *root)
{
	mqttPublishEvent(root, "notify/send");
}

uint16_t mqttPublishEvent(JsonDocument *root, const String topic, const uint8_t qos)
{
	if (!config.mqttEnabled || !mqttClient.connected()) {
		return 0;
	}

	(*root)["id"] = config.deviceHostname;

	String payload;
	serializeJson(*root, payload);
	return mqttPublishEvent(payload, topic, qos);
}

uint16_t mqttPublishEvent(const String payload, const String topic, const uint8_t qos) {
	if (!config.mqttEnabled || !mqttClient.connected()) {
		return 0;
	}

	String full_topic(config.mqttTopic);
	full_topic = full_topic + "/" + topic;

	// auto pkt_id = mqttClient.publish(full_topic.c_str(), 0, false, payload.c_str());
	return mqttClient.publish(full_topic.c_str(), qos, false, payload.c_str());
	// DEBUG_SERIAL.printf("[ INFO ] Mqtt publish to %s: (%u bytes at %lu us, id: %u)\n", full_topic.c_str(), payload.length(), micros(), pkt_id);
	// DEBUG_SERIAL.printf("Free mem: %u\n", ESP.getFreeHeap());
	// DEBUG_SERIAL.println(payload);
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

void mqttPublishAck(const char* topic, const char* msg) 
{
	DynamicJsonDocument root(512);
	String ack_topic(topic);
	// DEBUG_SERIAL.println(topic);
	// String ack_topic(topic + strlen(config.mqttTopic) + 1);
	// ack_topic = "notify/" + ack_topic;
	root["result"] = "ack";
	root["msg"] = msg;
	mqttPublishEvent(&root, ack_topic);
}

void mqttPublishNack(const char* topic, const char* msg) 
{
	DynamicJsonDocument root(512);
	String nack_topic(topic);
	// nack_topic = nack_topic + "/nack";
	// root["id"] = config.deviceHostname;
	root["result"] = "nack";
	root["msg"] = msg;
	mqttPublishEvent(&root, nack_topic);
}

void mqttPublishConnect(time_t boot_time)
{
	DynamicJsonDocument root(512);
	String topic = String(F("notify/connected"));
	// root["time"] = boot_time;
	root["device"] = F("ESP-RFID");
	root["version"] = bootInfo.version;
	root["debug"] = bootInfo.debug ? F("true") : F("false");
	root["chip_id"] = bootInfo.chipid;
	root["formatted"] = bootInfo.formatted;
	root["configured"] = bootInfo.configured;
	root["flash_size"] = bootInfo.fsinfo.totalBytes;
	root["flash_used"] = bootInfo.fsinfo.usedBytes;
	root["uptime_millis"] = millis();
	root["ip"] = WiFi.localIP().toString();
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
	root["free_ram"] = ESP.getFreeHeap();
	// root["id"] = WiFi.localIP().toString();
	mqttPublishEvent(&root, topic);
}

void mqttPublishShutdown(time_t heartbeat, time_t uptime) {
	DynamicJsonDocument root(512);
	String topic("notify/shutdown");
	root["time"] = heartbeat;
	root["uptime"] = uptime;
	// root["id"] = WiFi.localIP().toString();
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
	case not_yet_valid:
		root["result"] = "not yet valid";
		break;
	case time_not_valid:
		root["result"] = "local time not valid";
		break;
	default:
		root["result"] = "unknown";
		break;
	}

	root["time"] = accesstime;
	root["detail"] = detail;
	root["credential"] = credential;

	if (result != unrecognized) {
		root["username"] = person;
	}

	mqttPublishEvent(&root, topic);
}

void mqttPublishIo(String const &io, String const &state)
{
	if (config.mqttEnabled && mqttClient.connected())
	{
		String topic("notify/io/");
		topic += io;

		DynamicJsonDocument root(512);

		root["state"] = state;
		root["time"] = now();
		mqttPublishEvent(&root, topic);

		DEBUG_SERIAL.print("[ INFO ] Mqtt Publish: ");
		DEBUG_SERIAL.println(state + " @ " + topic);
	}
}


void getUserList() {
	if (flagMQTTSendUserList) {
		mqttPublishNack("notify/db/get", "already running");
	} else {
		flagMQTTSendUserList = true;
	}
}


void addUserID(const MqttMessage& message) {
	String filename("/P/");
	filename += message.uid;
	SEMAPHORE_FS_TAKE();
	File f = SPIFFS.open(filename, "w");

	if (f)
	{
		mqttIncomingJson["record_time"] = now();
		mqttIncomingJson["source"] = "MQTT";
		mqttIncomingJson["uid"] = message.uid;
		mqttIncomingJson.remove("id");
		serializeJson(mqttIncomingJson, f);
		f.close();
		mqttPublishAck("notify/db/add", filename.c_str());
	} else {
		mqttPublishNack("notify/db/add", "could not create file");
	}
	SEMAPHORE_FS_GIVE();
}

void deleteAllUserFiles()
{
	SEMAPHORE_FS_TAKE();
	Dir dir = SPIFFS.openDir("/P/");
	while (dir.next())
	{
		String uid = dir.fileName();
		uid.remove(0, 3);
		SPIFFS.remove(dir.fileName());
	}
	SEMAPHORE_FS_GIVE();
	mqttPublishAck("notify/db/drop", "complete");
}

void deleteUserID(const char *uid)
{
	// only do this if a user id has been provided
	if (uid)
	{
		String myuid = String(uid);
		myuid = "/P/" + myuid;

		SEMAPHORE_FS_TAKE();

		if (SPIFFS.exists(myuid.c_str()) && SPIFFS.remove(myuid.c_str())) {
			mqttPublishAck("notify/db/delete", String(uid).c_str());
		} else {
			mqttPublishNack("notify/db/delete", String(uid).c_str());
		}
		SEMAPHORE_FS_GIVE();
	}
}

void getDbStatus() {
	DynamicJsonDocument root(512);
	SEMAPHORE_FS_TAKE();
	Dir dir = SPIFFS.openDir("/P/");
	int i = 0;
	// DEBUG_SERIAL.println("[ INFO ] getDbStatus");
	while (dir.next())
	{
		++i;
	}
	// root["id"] = config.deviceHostname;
	root["total"] = i;
	FSInfo fsinfo;
	SPIFFS.info(fsinfo);
	SEMAPHORE_FS_GIVE();
	root["flash_used"] = fsinfo.usedBytes;
	root["flash_available"] = fsinfo.totalBytes - fsinfo.usedBytes;
	mqttPublishEvent(&root, String("notify/db/count"));
}

void onMqttPublish(uint16_t packetId)
{
	DEBUG_SERIAL.printf("[ DEBUG ] %lu - publish acknowledged, id: %u\n", micros(), packetId);
	// writeEvent("INFO", "mqtt", "MQTT publish acknowledged", String(packetId));
}

void onMqttMessage(char *topic, const char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
	// The AsyncMQTTClient library will pass partial payloads as the TCP packets are received
	// * len is the length of this payload
	// * index is starting location of this payload
	// * total is the total size of the entire payload.

	if (total > MAX_MQTT_BUFFER - 1) {
		// payload too big for our buffer, skip it.
		DEBUG_SERIAL.println("[ WARN ] Oversized payload received by onMqttMessage!");
		return;
	}

	// DEBUG_SERIAL.printf("[ INFO ] %lu - MQTT message incoming: %s (%u %u %u)\n", micros(), topic, index, len, total);

	if (index + len > MAX_MQTT_BUFFER - 1)
	{
		// index and len would exceed our buffer
		return;
	}

	// copy payload into our own buffer (+1 is required for '\0')
	strlcpy(mqttBuffer + index, payload, len + 1);

	if (index + len < total)  //payload incomplete
	{
		return;
	}

	// DEBUG_SERIAL.printf("[ INFO ] JSON msg (%s): ", topic);
	// DEBUG_SERIAL.println(mqttBuffer);

	// emplace makes a new item and pushes it on to the back of the
	// queue to handle outside of callback.
	MqttMessages.emplace(topic, mqttBuffer);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
	String reasonstr("");
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
	DEBUG_SERIAL.printf("[ WARN ] Disconnected from MQTT server: %s\n", reasonstr.c_str());
	// the disconnect will be noticed in main loop and reconnect scheduled
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos)
{
#ifdef DEBUG
	Serial.print("[ INFO ] Subscribe acknowledged - ");
	Serial.print("packetId: ");
	Serial.print(packetId);
	Serial.print(", qos: ");
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
	
	mqttPublishConnect(now());

	String base_topic(config.mqttTopic);
	String stopic("/db/+");
	stopic = base_topic + stopic;
	mqttClient.subscribe(stopic.c_str(), 2);

	stopic = base_topic + "/set/+";
	mqttClient.subscribe(stopic.c_str(), 2);

	stopic = base_topic + "/conf/+";
	mqttClient.subscribe(stopic.c_str(), 2);

	mqttReconnectTimer.detach();
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



