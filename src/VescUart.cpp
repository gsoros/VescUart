#include <stdint.h>
#include "VescUart.h"

VescUart::VescUart(uint32_t timeout_ms) : _TIMEOUT(timeout_ms) {
    nunchuck.valueX = 127;
    nunchuck.valueY = 127;
    nunchuck.lowerButton = false;
    nunchuck.upperButton = false;
}

void VescUart::setSerialPort(Stream* port) {
    serialPort = port;
}

void VescUart::setDebugPort(Stream* port) {
    debugPort = port;
}

int VescUart::receiveUartMessage(uint8_t* payloadReceived) {
    if (debugPort != NULL) {
        // debugPort->printf("[VescUart::receiveUartMessage] Start\n");
    }
    // Messages <= 255 starts with "2", 2nd byte is length
    // Messages > 255 starts with "3" 2nd and 3rd byte is length combined with 1st >>8 and then &0xFF

    // Makes no sense to run this function if no serialPort is defined.
    if (serialPort == NULL)
        return -1;

    uint16_t counter = 0;
    uint16_t endMessage = 256;
    bool messageRead = false;
    uint8_t messageReceived[256];
    uint16_t lenPayload = 0;

    ulong start = millis();

    uint32_t timeout = millis() + _TIMEOUT;  // Defining the timestamp for timeout (1000ms timeout)

    while (millis() < timeout && messageRead == false) {
        while (serialPort->available()) {
            messageReceived[counter++] = serialPort->read();

            // if (debugPort != NULL) {
            //     debugPort->printf("[VescUart::receiveUartMessage] Read %d: %d\n", counter, messageReceived[counter]);
            // }

            if (counter == 2) {
                switch (messageReceived[0]) {
                    case 2:
                        endMessage = messageReceived[1] + 5;  // Payload size + 2 for size + 3 for SRC and End.
                        lenPayload = messageReceived[1];
                        break;

                    case 3:
                        // ToDo: Add Message Handling > 255 (starting with 3)
                        if (debugPort != NULL) {
                            debugPort->println("[VescUart::receiveUartMessage] Message is larger than 256 bytes - not supported");
                        }
                        break;

                    default:
                        if (debugPort != NULL) {
                            debugPort->println("[VescUart::receiveUartMessage] Invalid start bit");
                        }
                        break;
                }
            }

            if (counter >= sizeof(messageReceived)) {
                break;
            }

            if (counter == endMessage && messageReceived[endMessage - 1] == 3) {
                messageReceived[endMessage] = 0;
                if (debugPort != NULL) {
                    // debugPort->println("[VescUart::receiveUartMessage] End of message");
                }
                messageRead = true;
                break;  // Exit if end of message is reached, even if there is still more data in the buffer.
            }
        }
        // if (debugPort != NULL) {
        // debugPort->printf("[VescUart::receiveUartMessage] Waiting\n");
        // }
        // delay(100);
        yield();
    }
    if (messageRead == false && debugPort != NULL) {
        debugPort->printf("[VescUart::receiveUartMessage] Timeout after %dms\n", millis() - start);
    }

    bool unpacked = false;

    if (messageRead) {
        unpacked = unpackPayload(messageReceived, endMessage, payloadReceived);
    }

    if (unpacked) {
        // Message was read
        if (debugPort != NULL) {
            // debugPort->printf("[VescUart::receiveUartMessage] Received %dB payload\n", lenPayload);
        }
        return lenPayload;
    } else {
        // No message was read
        if (debugPort != NULL) {
            debugPort->printf("[VescUart::receiveUartMessage] Received nothing\n");
        }
        return 0;
    }
}

bool VescUart::unpackPayload(uint8_t* message, int lenMes, uint8_t* payload) {
    uint16_t crcReceived = 0;
    uint16_t crcCalculated = 0;

    // Rebuild crc:
    crcReceived = message[lenMes - 3] << 8;
    crcReceived &= 0xFF00;
    crcReceived += message[lenMes - 2];

    if (debugPort != NULL) {
        // debugPort->print("[VescUart::unpackPayload] CRC received: ");
        // debugPort->println(crcReceived);
    }

    // Extract payload:
    memcpy(payload, &message[2], message[1]);

    crcCalculated = crc16(payload, message[1]);

    if (debugPort != NULL) {
        // debugPort->print("[VescUart::unpackPayload] CRC calculated: ");
        // debugPort->println(crcCalculated);
    }

    if (crcCalculated == crcReceived) {
        if (debugPort != NULL) {
            // debugPort->print("[VescUart::unpackPayload] Received: ");
            // serialPrint(message, lenMes);
            //  debugPort->println();

            // debugPort->print("[VescUart::unpackPayload] Payload:       ");
            // serialPrint(payload, message[1] - 1);
            // debugPort->println();
        }
        return true;
    }
    return false;
}

int VescUart::packSendPayload(uint8_t* payload, int lenPay) {
    uint16_t crcPayload = crc16(payload, lenPay);
    int count = 0;
    uint8_t package[lenPay + 6];

    if (lenPay <= 256) {
        package[count++] = 2;
        package[count++] = lenPay;
    } else {
        package[count++] = 3;
        package[count++] = (uint8_t)(lenPay >> 8);
        package[count++] = (uint8_t)(lenPay & 0xFF);
    }

    memcpy(package + count, payload, lenPay);
    count += lenPay;

    package[count++] = (uint8_t)(crcPayload >> 8);
    package[count++] = (uint8_t)(crcPayload & 0xFF);
    package[count++] = 3;
    // messageSend[count] = NULL;

    if (debugPort != NULL) {
        // debugPort->print("[VescUart::packSendPayload] Package to send: ");
        // serialPrint(package, count);
    }

    // Send package
    if (serialPort != NULL) {
        serialPort->write(package, count);
    }

    // Return number of bytes sent
    return count;
}

bool VescUart::processReadPacket(uint8_t* message) {
    COMM_PACKET_ID packetId;
    int32_t index = 0;

    packetId = (COMM_PACKET_ID)message[0];
    message++;  // Removes the packetId from the actual message (payload)

    switch (packetId) {
        case COMM_FW_VERSION:  // Structure defined here: https://github.com/vedderb/bldc/blob/43c3bbaf91f5052a35b75c2ff17b5fe99fad94d1/commands.c#L164

            fw_version.major = message[index++];
            fw_version.minor = message[index++];
            return true;
        case COMM_GET_VALUES:  // Structure defined here: https://github.com/vedderb/bldc/blob/43c3bbaf91f5052a35b75c2ff17b5fe99fad94d1/commands.c#L164

            data.tempMosfet = buffer_get_float16(message, 10.0, &index);           // 2 bytes - mc_interface_temp_fet_filtered()
            data.tempMotor = buffer_get_float16(message, 10.0, &index);            // 2 bytes - mc_interface_temp_motor_filtered()
            data.avgMotorCurrent = buffer_get_float32(message, 100.0, &index);     // 4 bytes - mc_interface_read_reset_avg_motor_current()
            data.avgInputCurrent = buffer_get_float32(message, 100.0, &index);     // 4 bytes - mc_interface_read_reset_avg_input_current()
            index += 4;                                                            // Skip 4 bytes - mc_interface_read_reset_avg_id()
            index += 4;                                                            // Skip 4 bytes - mc_interface_read_reset_avg_iq()
            data.dutyCycleNow = buffer_get_float16(message, 1000.0, &index);       // 2 bytes - mc_interface_get_duty_cycle_now()
            data.rpm = buffer_get_float32(message, 1.0, &index);                   // 4 bytes - mc_interface_get_rpm()
            data.inpVoltage = buffer_get_float16(message, 10.0, &index);           // 2 bytes - GET_INPUT_VOLTAGE()
            data.ampHours = buffer_get_float32(message, 10000.0, &index);          // 4 bytes - mc_interface_get_amp_hours(false)
            data.ampHoursCharged = buffer_get_float32(message, 10000.0, &index);   // 4 bytes - mc_interface_get_amp_hours_charged(false)
            data.wattHours = buffer_get_float32(message, 10000.0, &index);         // 4 bytes - mc_interface_get_watt_hours(false)
            data.wattHoursCharged = buffer_get_float32(message, 10000.0, &index);  // 4 bytes - mc_interface_get_watt_hours_charged(false)
            data.tachometer = buffer_get_int32(message, &index);                   // 4 bytes - mc_interface_get_tachometer_value(false)
            data.tachometerAbs = buffer_get_int32(message, &index);                // 4 bytes - mc_interface_get_tachometer_abs_value(false)
            data.error = (mc_fault_code)message[index++];                          // 1 byte  - mc_interface_get_fault()
            data.pidPos = buffer_get_float32(message, 1000000.0, &index);          // 4 bytes - mc_interface_get_pid_pos_now()
            data.id = message[index++];                                            // 1 byte  - app_get_configuration()->controller_id

            return true;

            break;

            /* case COMM_GET_VALUES_SELECTIVE:

                    uint32_t mask = 0xFFFFFFFF; */

        default:
            return false;
            break;
    }
}

bool VescUart::getFWversion(void) {
    return getFWversion(0);
}

bool VescUart::getFWversion(uint8_t canId) {
    int32_t index = 0;
    int payloadSize = (canId == 0 ? 1 : 3);
    uint8_t payload[payloadSize];

    if (canId != 0) {
        payload[index++] = {COMM_FORWARD_CAN};
        payload[index++] = canId;
    }
    payload[index++] = {COMM_FW_VERSION};

    packSendPayload(payload, payloadSize);

    uint8_t message[256];
    int messageLength = receiveUartMessage(message);
    if (messageLength > 0) {
        return processReadPacket(message);
    }
    return false;
}

bool VescUart::getVescValues(void) {
    return getVescValues(0);
}

bool VescUart::getVescValues(uint8_t canId) {
    // if (debugPort != NULL) {
    //     debugPort->println("Command: COMM_GET_VALUES " + String(canId));
    // }

    int32_t index = 0;
    int payloadSize = (canId == 0 ? 1 : 3);
    uint8_t payload[payloadSize];
    if (canId != 0) {
        payload[index++] = {COMM_FORWARD_CAN};
        payload[index++] = canId;
    }
    payload[index++] = {COMM_GET_VALUES};

    packSendPayload(payload, payloadSize);

    uint8_t message[256];
    int messageLength = receiveUartMessage(message);

    if (messageLength > 55) {
        return processReadPacket(message);
    }
    return false;
}
void VescUart::setNunchuckValues() {
    return setNunchuckValues(0);
}

void VescUart::setNunchuckValues(uint8_t canId) {
    if (debugPort != NULL) {
        debugPort->println("Command: COMM_SET_CHUCK_DATA " + String(canId));
    }
    int32_t index = 0;
    int payloadSize = (canId == 0 ? 11 : 13);
    uint8_t payload[payloadSize];

    if (canId != 0) {
        payload[index++] = {COMM_FORWARD_CAN};
        payload[index++] = canId;
    }
    payload[index++] = {COMM_SET_CHUCK_DATA};
    payload[index++] = nunchuck.valueX;
    payload[index++] = nunchuck.valueY;
    buffer_append_bool(payload, nunchuck.lowerButton, &index);
    buffer_append_bool(payload, nunchuck.upperButton, &index);

    // Acceleration Data. Not used, Int16 (2 byte)
    payload[index++] = 0;
    payload[index++] = 0;
    payload[index++] = 0;
    payload[index++] = 0;
    payload[index++] = 0;
    payload[index++] = 0;

    if (debugPort != NULL) {
        debugPort->println("Nunchuck Values:");
        debugPort->print("x=");
        debugPort->print(nunchuck.valueX);
        debugPort->print(" y=");
        debugPort->print(nunchuck.valueY);
        debugPort->print(" LBTN=");
        debugPort->print(nunchuck.lowerButton);
        debugPort->print(" UBTN=");
        debugPort->println(nunchuck.upperButton);
    }

    packSendPayload(payload, payloadSize);
}

void VescUart::setCurrent(float current) {
    return setCurrent(current, 0);
}

void VescUart::setCurrent(float current, uint8_t canId) {
    int32_t index = 0;
    int payloadSize = (canId == 0 ? 5 : 7);
    uint8_t payload[payloadSize];
    if (canId != 0) {
        payload[index++] = {COMM_FORWARD_CAN};
        payload[index++] = canId;
    }
    payload[index++] = {COMM_SET_CURRENT};
    buffer_append_int32(payload, (int32_t)(current * 1000), &index);
    packSendPayload(payload, payloadSize);
}

void VescUart::setBrakeCurrent(float brakeCurrent) {
    return setBrakeCurrent(brakeCurrent, 0);
}

void VescUart::setBrakeCurrent(float brakeCurrent, uint8_t canId) {
    int32_t index = 0;
    int payloadSize = (canId == 0 ? 5 : 7);
    uint8_t payload[payloadSize];
    if (canId != 0) {
        payload[index++] = {COMM_FORWARD_CAN};
        payload[index++] = canId;
    }

    payload[index++] = {COMM_SET_CURRENT_BRAKE};
    buffer_append_int32(payload, (int32_t)(brakeCurrent * 1000), &index);

    packSendPayload(payload, payloadSize);
}

void VescUart::setRPM(float rpm) {
    return setRPM(rpm, 0);
}

void VescUart::setRPM(float rpm, uint8_t canId) {
    int32_t index = 0;
    int payloadSize = (canId == 0 ? 5 : 7);
    uint8_t payload[payloadSize];
    if (canId != 0) {
        payload[index++] = {COMM_FORWARD_CAN};
        payload[index++] = canId;
    }
    payload[index++] = {COMM_SET_RPM};
    buffer_append_int32(payload, (int32_t)(rpm), &index);
    packSendPayload(payload, payloadSize);
}

void VescUart::setDuty(float duty) {
    return setDuty(duty, 0);
}

void VescUart::setDuty(float duty, uint8_t canId) {
    int32_t index = 0;
    int payloadSize = (canId == 0 ? 5 : 7);
    uint8_t payload[payloadSize];
    if (canId != 0) {
        payload[index++] = {COMM_FORWARD_CAN};
        payload[index++] = canId;
    }
    payload[index++] = {COMM_SET_DUTY};
    buffer_append_int32(payload, (int32_t)(duty * 100000), &index);

    packSendPayload(payload, payloadSize);
}

void VescUart::sendKeepalive(void) {
    return sendKeepalive(0);
}

void VescUart::sendKeepalive(uint8_t canId) {
    int32_t index = 0;
    int payloadSize = (canId == 0 ? 1 : 3);
    uint8_t payload[payloadSize];
    if (canId != 0) {
        payload[index++] = {COMM_FORWARD_CAN};
        payload[index++] = canId;
    }
    payload[index++] = {COMM_ALIVE};
    packSendPayload(payload, payloadSize);
}

void VescUart::serialPrint(uint8_t* data, int len) {
    if (debugPort != NULL) {
        for (int i = 0; i < len; i++) {
            debugPort->print(data[i]);
            debugPort->print(" ");
        }
        debugPort->println("");
    }
}

void VescUart::printVescValues() {
    if (debugPort != NULL) {
        debugPort->print("avgMotorCurrent: ");
        debugPort->println(data.avgMotorCurrent);
        debugPort->print("avgInputCurrent: ");
        debugPort->println(data.avgInputCurrent);
        debugPort->print("dutyCycleNow: ");
        debugPort->println(data.dutyCycleNow);
        debugPort->print("rpm: ");
        debugPort->println(data.rpm);
        debugPort->print("inputVoltage: ");
        debugPort->println(data.inpVoltage);
        debugPort->print("ampHours: ");
        debugPort->println(data.ampHours);
        debugPort->print("ampHoursCharged: ");
        debugPort->println(data.ampHoursCharged);
        debugPort->print("wattHours: ");
        debugPort->println(data.wattHours);
        debugPort->print("wattHoursCharged: ");
        debugPort->println(data.wattHoursCharged);
        debugPort->print("tachometer: ");
        debugPort->println(data.tachometer);
        debugPort->print("tachometerAbs: ");
        debugPort->println(data.tachometerAbs);
        debugPort->print("tempMosfet: ");
        debugPort->println(data.tempMosfet);
        debugPort->print("tempMotor: ");
        debugPort->println(data.tempMotor);
        debugPort->print("error: ");
        debugPort->println(data.error);
    }
}
