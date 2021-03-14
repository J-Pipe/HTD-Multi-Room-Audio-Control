#include "esphome.h"

class MCA66 : public Component, public UARTDevice, public CustomAPIDevice {
 public:
  MCA66(UARTComponent *parent) : UARTDevice(parent) {}

  static const int TOTAL_ZONES = 7;
  static const int COMMAND_SIZE = 6; // Command size in bytes
  static const int TX_RESPONSE_SIZE = 14; // Response size in bytes
  static const int MINIMUM_VOLUME = 0xc3; // Volume ranges from (0xC3 - (0xFF + 1))
  static const int MAXIMUM_VOLUME = 0x100; 

  struct Zone {
    unsigned char power1      : 1;
    unsigned char mute        : 1;
    unsigned char mode        : 1;
    unsigned char power2      : 1;
    unsigned char party       : 1;
    unsigned char party_input : 3;
    unsigned char volume      : 8;
    unsigned char treble      : 8;
    unsigned char bass        : 8;
    unsigned char balance     : 8;
  };
  Zone zones[TOTAL_ZONES];

  // This will be called once to set up the component
  void setup() override {   

    // Subscribe to Home Assistant states
    //  - Each time the ESP connects or Home Assistant updates the state, the function
    //    on_state_changed will be called
    subscribe_homeassistant_state(&MCA66::on_downstairs_music_power_changed, "input_boolean.downstairs_music_power");
    subscribe_homeassistant_state(&MCA66::on_upstairs_music_power_changed, "input_boolean.upstairs_music_power");
    subscribe_homeassistant_state(&MCA66::on_patio_music_power_changed, "input_boolean.patio_music_power");
    subscribe_homeassistant_state(&MCA66::on_garage_music_power_changed, "input_boolean.garage_music_power");
    subscribe_homeassistant_state(&MCA66::on_bathroom_music_power_changed, "input_boolean.bathroom_music_power");

    subscribe_homeassistant_state(&MCA66::on_downstairs_music_volume_changed, "input_number.downstairs_music_volume");
    subscribe_homeassistant_state(&MCA66::on_upstairs_music_volume_changed, "input_number.upstairs_music_volume");
    subscribe_homeassistant_state(&MCA66::on_patio_music_volume_changed, "input_number.patio_music_volume");
    subscribe_homeassistant_state(&MCA66::on_garage_music_volume_changed, "input_number.garage_music_volume");
    subscribe_homeassistant_state(&MCA66::on_bathroom_music_volume_changed, "input_number.bathroom_music_volume");
    
    register_service(&MCA66::on_send_command, "send_command", {"zoneNumber", "command", "data"});
  }

  void on_send_command(int zoneNumber, int command, int data) {
    uint8_t buf[COMMAND_SIZE]{0x02,0x00,zoneNumber,command,data,0};
    buf[5] = 0x02 + zoneNumber + command + data;
    write_and_receive(buf);
    log_zone_info(zoneNumber);
  }
  
  // For some reason MCA-66 is sending mutiple responses, so added this to clear the buffer
  void clear_buffer() {
    int availbleBytes = available();
    if (availbleBytes > 0) {
      //ESP_LOGD("MCA66", "Clearing buffer (%i bytes)", availbleBytes);
      uint8_t b[availbleBytes];
      read_array(b, availbleBytes);
      delay(100);
    }
  }

  // Set on/off state of zone
  void set_power(int zoneNumber, std::string powerStr) {
        uint8_t turn_on[COMMAND_SIZE]{0x02,0x00,zoneNumber,0x4,0x20,(0x26 + zoneNumber)};
        uint8_t turn_off[COMMAND_SIZE]{0x02,0x00,zoneNumber,0x4,0x21,(0x27 + zoneNumber)};
        std::string on ("on");
        std::string off ("off");
        if (!on.compare(powerStr)) { write_and_receive(turn_on); }
        if (!off.compare(powerStr)) { write_and_receive(turn_off); }
        log_zone_info(zoneNumber);
  }

  // Set volume of zone
  void set_volume(int zoneNumber, std::string volumeStr) {
    uint8_t turn_up[COMMAND_SIZE]{0x02,0x00,zoneNumber,0x4,0x9,(0xf + zoneNumber)};
    uint8_t turn_down[COMMAND_SIZE]{0x02,0x00,zoneNumber,0x4,0xa,(0x10 + zoneNumber)};

    if (!zones[zoneNumber].power1){
      ESP_LOGD("MCA66", "Can't adjust Zone%i volume when power is off", zoneNumber);
      return;
    }

    int targetVolume = atoi(volumeStr.c_str()); // volume should be a 0-61 value
    int currentVolume = (zones[zoneNumber].volume == 0 ? MAXIMUM_VOLUME : zones[zoneNumber].volume) - MINIMUM_VOLUME; // Scale value to 0-61
    int delta = targetVolume - currentVolume;
    int maxIterations = MAXIMUM_VOLUME - MINIMUM_VOLUME;
    while ((targetVolume != currentVolume) && ((maxIterations--) > 0)) {
      if (delta > 0) { write_and_receive(turn_up); }
      else { write_and_receive(turn_down); }
      delay(100);
      currentVolume = (zones[zoneNumber].volume == 0 ? MAXIMUM_VOLUME : zones[zoneNumber].volume) - MINIMUM_VOLUME;
    }
    log_zone_info(zoneNumber);
  }

  // Send command to controller and read response
  void write_and_receive(uint8_t *buffer) {
    uint8_t b[TX_RESPONSE_SIZE];
    bool success;
    clear_buffer();
    write_array(buffer, COMMAND_SIZE);
    flush();
    if (read_array(b, TX_RESPONSE_SIZE)) { // MCA-66 responds back with zone state
      // parse out zone info
      int zoneNumber = b[2];
      zones[zoneNumber].power1      = ((b[4] >> 7) & 1); // Data1 is byte 4 
      zones[zoneNumber].mute        = ((b[4] >> 6) & 1);
      zones[zoneNumber].mode        = ((b[4] >> 5) & 1);
      zones[zoneNumber].power2      = ((b[4] >> 4) & 1);
      zones[zoneNumber].party       = ((b[4] >> 3) & 1);
      zones[zoneNumber].party_input = (b[4] & 7); 
      zones[zoneNumber].volume      = b[9];  // Data6 is byte 9
      zones[zoneNumber].treble      = b[10]; // Data7 is byte 10
      zones[zoneNumber].bass        = b[11]; // Data8 is byte 11
      zones[zoneNumber].balance     = b[12]; // Data9 is byte 12
    }
    clear_buffer();
  }

  // Print current state of zone
  void log_zone_info(int zoneNumber){
    ESP_LOGD("MCA66", "Zone %i:\n\tpower1:%i\n\tmute:%i\n\tmode:%i\n\tpower2:%i\n\tparty:%i\n\tparty_input:%i\n\tvolume:%i\n\ttreble:%i\n\tbass:%i\n\tbalance:%i", 
      zoneNumber, zones[zoneNumber].power1, zones[zoneNumber].mute, zones[zoneNumber].mode, zones[zoneNumber].power2, zones[zoneNumber].party, zones[zoneNumber].party_input, zones[zoneNumber].volume - MINIMUM_VOLUME, zones[zoneNumber].treble, zones[zoneNumber].bass, zones[zoneNumber].balance);
  }
  
  void on_downstairs_music_power_changed(std::string powerStr) { set_power(1, powerStr); }  
  void on_downstairs_music_volume_changed(std::string volumeStr) { set_volume(1, volumeStr); }

  void on_upstairs_music_power_changed(std::string powerStr) { set_power(2, powerStr); }  
  void on_upstairs_music_volume_changed(std::string volumeStr) { set_volume(2, volumeStr); }

  void on_patio_music_power_changed(std::string powerStr) { set_power(3, powerStr); }  
  void on_patio_music_volume_changed(std::string volumeStr) { set_volume(3, volumeStr); }

  void on_garage_music_power_changed(std::string powerStr) { set_power(4, powerStr); }  
  void on_garage_music_volume_changed(std::string volumeStr) { set_volume(4, volumeStr); }

  void on_bathroom_music_power_changed(std::string powerStr) { set_power(5, powerStr); }
  void on_bathroom_music_volume_changed(std::string volumeStr) { set_volume(5, volumeStr); }

};
