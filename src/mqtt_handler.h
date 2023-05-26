#ifndef mqtt_handler_h
#define mqtt_handler_h

#include <memory>
#include <queue>
#include <Arduino.h>
#include <AsyncMqttClient.h>
#include <Ticker.h>
#include "config.h"
#include "accesscontrol.h"
#include "helpers.h"

#define MAX_MQTT_BUFFER 2048

#define SEMAPHORE_FS_TAKE(X) while (_xSemaphore) { /*ESP.wdtFeed();*/ } _xSemaphore = true
#define SEMAPHORE_FS_GIVE(X) _xSemaphore = false

bool _xSemaphore;

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


/**
 * @brief MQTT payloads are loaded into a static buffer and when complete
 * are copied to a MqttMessage on a queue that is then processed outside
 * of the MQTT onMessage callback.
 * 
 * This class dynamically allocates the memeory necessary to store the
 * payload.
 * 
 */
class MqttMessage {
    public:
    char topic[128];
    char uid[20];
    std::unique_ptr<char[]> serializedMessage;
    unsigned msgLen = 0;

    MqttMessage(const char mqttTopic[], const char mqttPayload[]);
    MqttMessage(const MqttMessage&) = default;
};


/**
 * @brief This sends the database using qos = 1 (broker replies with ACK) and waits to send the
 * next packet until an ACK is received so that we don't overwhelm the IP subsystem with packets.
 * 
 */
class MqttDatabaseSender {
    public:
    MqttDatabaseSender();
    ~MqttDatabaseSender();

    /**
     * @brief Stores the last pkt_id returned by the MQTT subsystem.
     * This is compared against the PUBACKs received from the broker.
     *   1 means ready to send
     *   0 means packet not sent (publish() will retuen 0 when not sent)
     *  >1 is a real pkt_id
     * 
     * This is reset to 1 when the PUBACK is received.
     * 
     * This is static as it is acced by the static function onMqttPublish().
     * 
     */
    static uint16_t pkt_id;

    /**
     * @brief This callback is registerd with the MQTT onPublish()
     * 
     * @param packetId ID of the PUBACK received
     */
    static void onMqttPublish(uint16_t packetId);

    /**
     * @brief Run this from the main loop until the return value is false.
     * 
     * @return true 
     * @return false 
     */
    bool run();

    /**
     * @brief Tracks total number of packets sent
     * 
     */
    unsigned long count = 0;

    /**
     * @brief Set one the entire DB has been sent
     * 
     */
    bool done = false;

    private:
    DynamicJsonDocument *root;
    uint32_t lastSend = 0;
    Dir *dir = nullptr;
	bool _available = false;
};

void setupMqtt();
void connectToMqtt();
void processMqttQueue();
void processMqttMessage(MqttMessage& incomingMessage);
void disconnectMqtt();
MqttAccessTopic decodeMqttTopic(const char *topic);

void mqttPublishEvent(JsonDocument *root);
uint16_t mqttPublishEvent(JsonDocument *root, const String topic, const uint8_t qos = 0);
uint16_t mqttPublishEvent(const String payload, const String topic, const uint8_t qos = 0);

void mqttPublishAck(const char* command, const char* msg);
void mqttPublishNack(const char* command, const char* msg);

void mqttPublishAccess(time_t accesstime, AccessResult const &result, String const &detail, String const &credential, String const &person);

void mqttPublishIo(String const &io, String const &state);
void onMqttPublish(uint16_t packetId);
void mqttPublishHeartbeat(time_t heartbeat, time_t uptime);
void mqttPublishShutdown(time_t heartbeat, time_t uptime);

void onMqttMessage(char *topic, const char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttSubscribe(uint16_t packetId, uint8_t qos);

void getDbStatus();
void getUserList();
void deleteAllUserFiles();
void deleteUserID(const char *uid);
void addUserID(const MqttMessage& message);

extern void onNewRecord(const String uid, const JsonDocument& payload);

extern AsyncMqttClient mqttClient;
extern Ticker mqttReconnectTimer;
extern boot_info_t bootInfo;
extern bool flagMQTTSendUserList;
extern bool FS_IN_USE;


#endif