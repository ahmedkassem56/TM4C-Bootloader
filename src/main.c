#include <stdint.h>
#include <stdbool.h>
#include "inc/hw_types.h"
#include "inc/hw_memmap.h"
#include "inc/hw_gpio.h"
#include "driverlib/gpio.h"
#include "driverlib/sysctl.h"
#include "driverlib/flash.h"

#include "crc.h"
#include "packet.h"
#include "utils.h"

#define LEDBASE GPIO_PORTF_BASE
#define LEDRED GPIO_PIN_1
#define LEDBLUE GPIO_PIN_2
#define LEDGREEN GPIO_PIN_3

#define SWBASE GPIO_PORTF_BASE
#define SW1 GPIO_PIN_4
#define SW2 GPIO_PIN_0

#define APP_START_ADDRESS ((uint32_t)0x00006000U)

strPacket_t rxBuffer;
strPacket_t txBuffer;

uint32_t flsDrvBuffer[64];
uint8_t flsDrvBufferInd = 0;

uint32_t payloadSize;

crc_t crc;

typedef enum {
    BL_STATE_IDLE,
    BL_STATE_ERASE_STARTED,
    BL_STATE_WRITE_STARTED,
    BL_STATE_WRITE_FINISHED,
} enuState_t;

static enuState_t BL_State = BL_STATE_IDLE;

static uint32_t flashIndex = APP_START_ADDRESS; // first app address

int main(void)
{
    // Enable GPIOF clock
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF));

    // Unlock PF0
    HWREG(GPIO_PORTF_BASE+GPIO_O_LOCK) = GPIO_LOCK_KEY;
    HWREG(GPIO_PORTF_BASE+GPIO_O_CR) |= GPIO_PIN_0;

    // Set push buttons pins as input (PF0,PF4)
    GPIOPinTypeGPIOInput(SWBASE, SW1 | SW2);
    // Enable pullup resistors on PF0,PF4
    GPIOPadConfigSet(SWBASE,SW1 | SW2,GPIO_STRENGTH_2MA,GPIO_PIN_TYPE_STD_WPU);

    // Erased application check
    uint64_t *pAppArea = (uint64_t *) 0x00006000U;
    uint8_t erasedAppFlag = *pAppArea == 0xFFFFFFFFFFFFFFFFU ? 1 : 0;

    // If pushbuttons were not pressed, jump to application.
    if (GPIOPinRead(SWBASE,/*SW2 | */SW1)
            && erasedAppFlag == 0x00) {
        // Reinit used peripherals
        SysCtlPeripheralReset(SYSCTL_PERIPH_GPIOF);
        __asm (
            // Update vector table offset to application vector table
            "ldr     r0, = 0x00006000\n"
            "ldr     r1, = 0xe000ed08\n"
            "str     r0, [r1]\n"

            // Update stack pointer from application vector table. First entry of vector table is SP
            "ldr     r1, [r0]\n"
            "mov     sp, r1\n"

            // Load application reset handler and jump to the user code
            "ldr     r0, [r0, #4]\n"
            "bx      r0\n");
    }

    // Enable GPIOA clock for UART
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA));

    // Enable UART0 clock
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_UART0));

    // Enable UART on PA0/PA1
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    // UART Init
    UARTConfigSetExpClk(CFG_UART_BASE, 16000000, 115200,
                        (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                                UART_CONFIG_PAR_NONE));

    // Set LEDs pins type as output
    GPIOPinTypeGPIOOutput(LEDBASE, LEDRED | LEDBLUE | LEDGREEN);

    // Initialize CRC lib
    crc = crc_init();

    GPIOPinWrite(LEDBASE, LEDBLUE, LEDBLUE);

    while (1) {

        rcvPacket(&rxBuffer);

        if (rxBuffer.packetValid) {

            // Erase request
            if (rxBuffer.packetOpcode == 1 && BL_State == BL_STATE_IDLE) {

                BL_State = BL_STATE_ERASE_STARTED;

                for(uint32_t appArea = APP_START_ADDRESS; appArea < 0x00040000; appArea+=0x400) {
                    GPIOPinWrite(LEDBASE, LEDRED, LEDRED);
                    if (FlashErase(appArea) != 0) {
                        // Erasing flash failed. Send error and break.
                        txBuffer.packetOpcode = 0xA1;
                        txBuffer.dataLen = 1;
                        // erase failed
                        txBuffer.packetData[0] = 0xF0;
                        sendPacket(&txBuffer);
                        break;

                    } else {
                        GPIOPinWrite(LEDBASE, LEDRED, 0);
                    }
                }

                // Send erase end indication
                txBuffer.packetOpcode = 0xA1;
                txBuffer.dataLen = 1;
                // erase finished
                txBuffer.packetData[0] = 1;
                sendPacket(&txBuffer);

                BL_State = BL_STATE_IDLE;

            // Flash request
            } else if (rxBuffer.packetOpcode == 0x02 && BL_State == BL_STATE_IDLE) {
                payloadSize = bytesToU32(rxBuffer.packetData, 0);

                txBuffer.packetOpcode = 0xA2;
                txBuffer.dataLen = 0x01;

                if (payloadSize <= (0x00040000 - APP_START_ADDRESS)) {
                    flashIndex = APP_START_ADDRESS;
                    BL_State = BL_STATE_WRITE_STARTED;
                    crc = crc_init();
                    txBuffer.packetData[0] = 0x01; // flashing accepted
                } else {
                    txBuffer.packetData[0] = 0x00; // flashing refused
                }

                sendPacket(&txBuffer);

            // Flash data
            } else if (rxBuffer.packetOpcode == 0x03 && BL_State == BL_STATE_WRITE_STARTED) {
                if (((rxBuffer.dataLen % 4) == 0)
                        && ((flashIndex + rxBuffer.dataLen) <= 0x00040000))  {

                    // Concatenate every 4 bytes into an UInt32 because flash driver expects an array of UInt32s
                    // TM4C123 is little-endian.
                    flsDrvBufferInd = 0;
                    for (int i = 0; i < rxBuffer.dataLen;) {
                        flsDrvBuffer[flsDrvBufferInd] = rxBuffer.packetData[i++];
                        flsDrvBuffer[flsDrvBufferInd] |= rxBuffer.packetData[i++] << 8;
                        flsDrvBuffer[flsDrvBufferInd] |= rxBuffer.packetData[i++] << 16;
                        flsDrvBuffer[flsDrvBufferInd] |= rxBuffer.packetData[i++] << 24;
                        flsDrvBufferInd++;
                    }

                    int32_t writeRes = FlashProgram(flsDrvBuffer,flashIndex,rxBuffer.dataLen);

                    // prepare response
                    txBuffer.packetOpcode = 0xA3;
                    txBuffer.dataLen = 0x01;

                    if (writeRes == 0) {
                        // success

                        flashIndex += rxBuffer.dataLen;

                        // update CRC
                        crc = crc_update(crc, rxBuffer.packetData, rxBuffer.dataLen);

                        if ((flashIndex - APP_START_ADDRESS) < payloadSize) {
                            txBuffer.packetData[0] = 0x01; // send next chunk of data
                        } else {
                            txBuffer.packetData[0] = 0x02; // application is completely transferred.
                            BL_State = BL_STATE_WRITE_FINISHED;
                        }

                    } else {
                        txBuffer.packetData[0] = 0x00; // failed
                    }
                    sendPacket(&txBuffer);

                } else {
                    txBuffer.packetOpcode = 0xA3;
                    txBuffer.dataLen = 0x01;
                    txBuffer.packetData[0] = 0xFF; // overflow
                    sendPacket(&txBuffer);
                }

            // End flash
            } else if (rxBuffer.packetOpcode == 0x04 && BL_State == BL_STATE_WRITE_FINISHED) {
                crc = crc_finalize(crc);

                txBuffer.packetOpcode = 0xA4;
                txBuffer.dataLen = 1;

                uint32_t rcvdCrc = bytesToU32(rxBuffer.packetData,0);

                if (rcvdCrc != crc) {
                    // mismatch
                    txBuffer.packetData[0] = 0;
                } else {
                    // correct
                    txBuffer.packetData[0] = 1;
                }

                sendPacket(&txBuffer);

                BL_State = BL_STATE_IDLE;
            // Restart request
            } else if (rxBuffer.packetOpcode == 0x05) {
                SysCtlReset();
            // Get PC (Just for testing code execution from RAM)
            } else if (rxBuffer.packetOpcode == 0x06) {
                // send PC
                uint32_t pc = _get_PC();
                txBuffer.packetOpcode = 0xA6;
                txBuffer.dataLen = 4;
                txBuffer.packetData[0] = (pc & 0xFF000000) >> 24;
                txBuffer.packetData[1] = (pc & 0x00FF0000) >> 16;
                txBuffer.packetData[2] = (pc & 0x0000FF00) >> 8;
                txBuffer.packetData[3] = pc & 0x000000FF;
                sendPacket(&txBuffer);
            }
        }
    }
	return 0;
}
