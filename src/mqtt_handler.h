#ifndef mqtt_handler_h
#define mqtt_handler_h

#include <queue>
#include <Arduino.h>
#include <AsyncMqttClient.h>
#include <Ticker.h>
#include "config.h"
#include "accesscontrol.h"

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
	// char command[20];
	// char uid[20];
	// char user[64];
	// char door[20];
	char serializedMessage[MAX_MQTT_BUFFER];
	// MqttMessage *nextMessage = NULL;
};

void connectToMqtt();
void disconnectMqtt();
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void mqttPublishEvent(JsonDocument *root);
void mqttPublishEvent(JsonDocument *root, const String topic);
void mqttPublishAck(String command, const char* msg);
void mqttPublishNack(String command, const char* msg);
void deleteUserID(const char *uid);
void processMqttMessage(MqttMessage incomingMessage);
void setupMqtt();
void processMqttQueue();
void mqttPublishAccess(time_t accesstime, AccessResult const &result, String const &credential, String const &person, String const &uid);
void mqttPublishIo(String const &io, String const &state);
void mqttPublishHeartbeat(time_t heartbeat, time_t uptime);
void mqttPublishShutdown(time_t heartbeat, time_t uptime);

extern AsyncMqttClient mqttClient;
extern Ticker mqttReconnectTimer;

#endif