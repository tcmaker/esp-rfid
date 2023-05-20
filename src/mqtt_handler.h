#ifndef mqtt_handler_h
#define mqtt_handler_h

#include <queue>
#include <Arduino.h>
#include <AsyncMqttClient.h>
#include <Ticker.h>
#include "config.h"
#include "accesscontrol.h"
#include "helpers.h"

#define MAX_MQTT_BUFFER 2048

enum MqttAccessTopic {
    UNSUPPORTED,
    ADD_UID,
    DELETE_UID,
    GET_NUM_UIDS,
    GET_UIDS,
    UNLOCK,
    LOCK,
    GET_CONF
};

struct MqttMessage {
    char topic[128];    // default MQTT maxTopicLength setting is 128 bytes
	char uid[20];
	// char command[20];
	// char user[64];
	// char door[20];
	char serializedMessage[MAX_MQTT_BUFFER];
	// MqttMessage *nextMessage = NULL;
};

void setupMqtt();
void connectToMqtt();
void processMqttQueue();
void processMqttMessage(MqttMessage incomingMessage);
void disconnectMqtt();
MqttAccessTopic decodeMqttTopic(const char *topic);

void mqttPublishEvent(JsonDocument *root);
void mqttPublishEvent(JsonDocument *root, const String topic);

void mqttPublishAck(const char* command, const char* msg);
void mqttPublishNack(const char* command, const char* msg);

void mqttPublishAccess(time_t accesstime, AccessResult const &result, String const &credential, String const &person, String const &uid);
void mqttPublishIo(String const &io, String const &state);
void onMqttPublish(uint16_t packetId);
void mqttPublishHeartbeat(time_t heartbeat, time_t uptime);
void mqttPublishShutdown(time_t heartbeat, time_t uptime);

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttSubscribe(uint16_t packetId, uint8_t qos);

void getDbStatus();
void getUserList();
void deleteAllUserFiles();
void deleteUserID(const char *uid);
void addUserID(MqttMessage& message);

extern void onNewRecord(const String uid, const JsonDocument& payload);

extern AsyncMqttClient mqttClient;
extern Ticker mqttReconnectTimer;
extern boot_info_t bootInfo;
extern bool flagMQTTSendUserList;

#endif