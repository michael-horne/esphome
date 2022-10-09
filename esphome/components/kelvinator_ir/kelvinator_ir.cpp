#include "kelvinator_ir.h"
#include "esphome/components/remote_base/kelvinator_protocol.h"
#include "esphome/core/log.h"

namespace esphome {
namespace kelvinator_ir {

static const char *const TAG = "climate.kelvinator_ir";

uint8_t KelvinatorIR::getMode() {
  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL:
      return KELVINATOR_MODE_COOL;
    case climate::CLIMATE_MODE_DRY:
      return KELVINATOR_MODE_DRY;
    case climate::CLIMATE_MODE_FAN_ONLY:
      return KELVINATOR_MODE_FAN;
    case climate::CLIMATE_MODE_HEAT:
      return KELVINATOR_MODE_HEAT;
    case climate::CLIMATE_MODE_HEAT_COOL:
      return KELVINATOR_MODE_AUTO;
    default:
      return KELVINATOR_MODE_AUTO;
  }
}

uint8_t KelvinatorIR::getTemperature() { return this->target_temperature - KELVINATOR_TEMPERATURE_MIN; }

uint8_t KelvinatorIR::getPower() { return this->mode != climate::CLIMATE_MODE_OFF; }

uint8_t KelvinatorIR::getBasicFan() {
  if (this->fan_mode.has_value()) {
    switch (this->fan_mode.value()) {
      case climate::CLIMATE_FAN_AUTO:
        return KELVINATOR_BASICFAN_AUTO;
      case climate::CLIMATE_FAN_HIGH:
        return KELVINATOR_BASICFAN_MAX;
      case climate::CLIMATE_FAN_MEDIUM:
        return KELVINATOR_BASICFAN_MEDIUM;
      case climate::CLIMATE_FAN_LOW:
        return KELVINATOR_BASICFAN_MIN;
      default:
        return KELVINATOR_BASICFAN_AUTO;
    }
  }
  return KELVINATOR_BASICFAN_AUTO;
}

uint8_t KelvinatorIR::getSwingMode() {
  switch (this->swing_mode) {
    case climate::CLIMATE_SWING_OFF:
      return KELVINATOR_VSWING_OFF;
    case climate::CLIMATE_SWING_VERTICAL:
      return KELVINATOR_VSWING_AUTO;
    default:
      return KELVINATOR_VSWING_OFF;
  }

  return KELVINATOR_VSWING_OFF;
}

void KelvinatorIR::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    auto mode = call.get_mode().value();

    // swing resets after unit powered off
    if (mode == climate::CLIMATE_MODE_OFF) {
      this->swing_mode = climate::CLIMATE_SWING_OFF;
    }

    // temperature resets for auto and fan only modes
    if ((mode == climate::CLIMATE_MODE_AUTO || mode == climate::CLIMATE_MODE_FAN_ONLY) &&
        this->target_temperature != KELVINATOR_TEMPERATURE_AUTO) {
      this->target_temperature = KELVINATOR_TEMPERATURE_AUTO;
      ESP_LOGD(TAG, "Resetting temperature to %d degrees.", KELVINATOR_TEMPERATURE_AUTO);
    }
  }
  climate_ir::ClimateIR::control(call);
}

void KelvinatorIR::transmit_state() {
  auto command = KelvinatorCommand();

  command.Power = this->getPower();
  command.Mode = this->getMode();
  command.Temp = this->getTemperature();
  command.Light = this->light_;
  command.BasicFan = this->getBasicFan();

  command.rawBytes[8] = command.rawBytes[0];
  command.rawBytes[9] = command.rawBytes[1];
  command.rawBytes[10] = command.rawBytes[2];

  command.rawBytes[3] = 0x50;
  command.rawBytes[11] = 0x70;

  remote_base::KelvinatorData kelvinatorData;
  kelvinatorData.data.push_back(command.raw[0]);
  kelvinatorData.data.push_back(command.raw[1]);
  kelvinatorData.applyChecksum();

  kelvinatorData.log();

  auto transmit = this->transmitter_->transmit();
  auto *data = transmit.get_data();
  remote_base::KelvinatorProtocol().encode(data, kelvinatorData);
  transmit.perform();
}

bool KelvinatorIR::on_receive(remote_base::RemoteReceiveData data) {
  auto decoded = remote_base::KelvinatorProtocol().decode(data);
  if (!decoded.has_value()) {
    return false;
  }

  auto kelvinatorData = decoded.value();

  if (kelvinatorData.data.size() < 2) {
    return false;
  }

  kelvinatorData.log();

  KelvinatorCommand command;
  command.raw[0] = kelvinatorData.data[0];
  command.raw[1] = kelvinatorData.data[1];

  if (command.Power != 0) {
    switch (command.Mode) {
      case KELVINATOR_MODE_AUTO:
        mode = climate::CLIMATE_MODE_HEAT_COOL;
        break;
      case KELVINATOR_MODE_COOL:
        mode = climate::CLIMATE_MODE_COOL;
        break;
      case KELVINATOR_MODE_DRY:
        mode = climate::CLIMATE_MODE_DRY;
        break;
      case KELVINATOR_MODE_FAN:
        mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case KELVINATOR_MODE_HEAT:
        mode = climate::CLIMATE_MODE_HEAT;
        break;
    }
  } else {
    mode = climate::CLIMATE_MODE_OFF;
  }

  this->target_temperature = command.Temp + KELVINATOR_TEMPERATURE_MIN;

  switch (command.BasicFan) {
    case KELVINATOR_BASICFAN_AUTO:
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
      break;
    case KELVINATOR_BASICFAN_MAX:
      this->fan_mode = climate::CLIMATE_FAN_HIGH;
      break;
    case KELVINATOR_BASICFAN_MEDIUM:
      this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
      break;
    case KELVINATOR_BASICFAN_MIN:
      this->fan_mode = climate::CLIMATE_FAN_LOW;
      break;
    default:
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
  }

  this->publish_state();

  return true;
}

void KelvinatorIR::set_light(const bool enabled) { this->light_ = enabled; }

}  // namespace kelvinator_ir
}  // namespace esphome
