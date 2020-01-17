/*
 * packet.c
 *
 *  Created on: Dec 28, 2019
 *      Author: Ahmed Kassem
 */

#include "packet.h"

/* *************************************************************************** */

static void sendAck(strPacket_t *packet);
static void UARTWriteBytes(uint32_t uart_base, uint8_t *buff, int len);
static void UARTWriteByte(uint32_t uart_base, char byte);

/* *************************************************************************** */

void rcvPacket(strPacket_t *packet) {
    int index;
    packet->dataLen = UARTCharGet(CFG_UART_BASE);

    packet->packetOpcode = UARTCharGet(CFG_UART_BASE);

    for (index = 0;index < packet->dataLen; index++) {
        packet->packetData[index] = UARTCharGet(CFG_UART_BASE);
    }

    // Terminator
    if (UARTCharGet(CFG_UART_BASE) == 0xA5) {
        packet->packetValid = 1;
        sendAck(packet);
    } else {
        packet->packetValid = 0;
    }
}

void sendPacket(strPacket_t *p) {
    UARTWriteByte(CFG_UART_BASE, p->dataLen);
    UARTWriteByte(CFG_UART_BASE, p->packetOpcode);

    UARTWriteBytes(CFG_UART_BASE, p->packetData, p->dataLen);

    UARTWriteByte(CFG_UART_BASE, 0xA5);
}

void sendAck(strPacket_t *packet) {
    uint8_t buffer[] = {0x01, 0xAA, packet->packetOpcode, 0xA5};
    UARTWriteBytes(CFG_UART_BASE, buffer, 4);
}

void UARTWriteBytes(uint32_t uart_base, uint8_t *buff, int len) {
    for (int i = 0; i < len; i++) {
        UARTWriteByte(uart_base, buff[i]);
    }
}

void UARTWriteByte(uint32_t uart_base, char byte) {
    UARTCharPut(uart_base, byte);
    while(UARTBusy(uart_base));
}

