/**
 * @file relay.cpp
 * @author Brad Ferugson (brad.ferguson@tcmaker.org)
 * @brief Handles relay on/off timing
 * @version 0.1
 * @date 2022-10-01
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "relay.h"

void Relay::debugPrint(const char* info) {
  Serial.printf("milliseconds: %lu - %s\n", millis(), info);
}

Relay::Relay() 
  : controlType(unknown)
  , actuationTime(2000)
  {}

Relay::Relay(uint8_t pin, ControlType type , unsigned long actuation_ms, unsigned long delay_ms)
  : controlPin(pin)
  , controlType(type)
  , actuationTime(actuation_ms)
  , delayTime(delay_ms)
  {}

/**
 * @brief Has the relay been configured to operate?
 * 
 * @return true if ready
 */
bool Relay::isConfigured()
{
  return controlType != 255;
}

void Relay::activate()
{
  Serial.printf("Relay(%d)::activate()\n", controlPin);
  if (override != normal)
    return;

  digitalWrite(controlPin, controlType);
  lastMillis = millis();
  if (delayTime > 0) {
    state = activating;
  } else {
    state = active;
  }
}

void Relay::deactivate()
{
  Serial.printf("Relay(%d)::deactivate()\n", controlPin);
  if (override != normal)
    return;
  
  digitalWrite(controlPin, !controlType);
  lastMillis = millis();
  if (delayTime > 0) {
    state = deactivating;
  } else {
    state = inactive;
  }
}

void Relay::hold() {
  activate();
  override = holding;
}

void Relay::lockout() {
  deactivate();
  override = lockedout;
}

void Relay::release() {
  override = normal;
}

/**
 * @brief Stop any delayed changes, reset relay state to default.
 * 
 */
void Relay::begin()
{
  override = normal;
  this->deactivate();
  state = inactive;
}

bool Relay::update() 
{
  bool acted = false;
  const unsigned long now = millis();

  switch (state)
  {
    case activating:
    case deactivating:
        if (now > lastMillis + delayTime) 
        {
        state = (state == activating) ? active : inactive;
        }
        break;
    case active:
        if (override == normal && (actuationTime > 0) && (now > lastMillis + actuationTime))
        {
        deactivate();
        acted = true;
        }
        break;
    case inactive:
        if (override == normal) 
        {
            digitalWrite(controlPin, !controlType);
        }
    default: // inactive
        break;
  }
  return acted;
}