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

#define DEBUG_SERIAL if(DEBUG)Serial

const char* Relay::OperationState_Label[4] = 
  {
      "active",
      "activating",
      "inactive",
      "deactivating"
  };

const char* Relay::OverrideState_Label[3] = 
  {
      "normal",
      "holding",
      "lockedout"
  };

Relay::Relay(uint8_t pin, ControlType type , unsigned long actuation_ms, unsigned long delay_ms)
  : controlPin(pin)
  , controlType(type)
  , actuationTime(actuation_ms)
  , delayTime(delay_ms)
  , lastMillis(0)
  , state(inactive)
  , override(normal)
  , onStateChangeCB(nullptr)
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

void Relay::activate(bool report)
{
  DEBUG_SERIAL.printf("Relay(%d)::activate()\n", controlPin);
  if (override != normal)
    return;

  digitalWrite(controlPin, controlType);
  lastMillis = millis();
  if (delayTime > 0) {
    state = activating;
  } else {
    state = active;
  }

  if (report && onStateChangeCB) {
    onStateChangeCB(state, override);
  }
}

// void Relay::deactivate() {
//   deactivate()
// }

void Relay::deactivate(bool report)
{
  DEBUG_SERIAL.printf("Relay(%d)::deactivate()\n", controlPin);
  if (override != normal)
    return;
  
  digitalWrite(controlPin, !controlType);
  lastMillis = millis();
  if (delayTime > 0) {
    state = deactivating;
  } else {
    state = inactive;
  }

  if (report && onStateChangeCB) {
    onStateChangeCB(state, override);
  }
}

void Relay::hold() {
  activate(false);
  override = holding;
  if (onStateChangeCB) {
    onStateChangeCB(state, override);
  }
}

void Relay::lockout() {
  deactivate(false);
  override = lockedout;
  if (onStateChangeCB) {
    onStateChangeCB(state, override);
  }
}

void Relay::release() {
  if (override != normal && onStateChangeCB) {
    onStateChangeCB(state, normal);
  }
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
  const unsigned long currentMillis = millis();
  static auto last_override = this->override;

  switch (state)
  {
    case activating:
    case deactivating:
      if (currentMillis - lastMillis > delayTime) 
      {
          state = (state == activating) ? active : inactive;
      }
      break;
    case active:
      if (override == normal && (actuationTime > 0) && (currentMillis - lastMillis > actuationTime))
      {
        deactivate();
        acted = true;
      }
      break;
    case inactive:
      // if (override == normal && last_override != override)
      // {
      //   deactivate();
      //   acted = true;
      //   // digitalWrite(controlPin, !controlType);
      // }
    default: // inactive
        break;
  }
  last_override = this->override;
  return acted;
}