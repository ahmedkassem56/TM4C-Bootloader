
## Introduction
A bootloader is a piece of software that runs before the user application. Its basic operation is to update the application on the MCU without the need of special hardware like debuggers and programmers.

We can use any communication protocol to communicate with the bootloader and upload the user application through. **UART** is used in this project because it is easy to develop a host desktop application as a flashing utility for this project. More on this later.

The target is TM4C123GH6PM microcontroller found in Tiva C LaunchPad Evaluation Kit by TI. 

## External libraries
- I used TI's TivaWare library which you can find [here](http://www.ti.com/tool/SW-TM4C) as an MCAL layer instead of writing my own MCAL for the target as I wanted to invest more time into the bootloader itself and not care much about the development and debugging of the MCAL. 
- For CRC32 calculations I used this [PyCRC](https://pycrc.org/index.html)'s generated C library. 

## Design
### Memory Map

    -----------------
    |               | 0x0000 0000H
    |               |   |
	|   Bootloader  |   |
	|               |   V
	|               | 0x0000 6000H
	-----------------
    |               | 0x0000 6000H
    |               |   |
	|  Application  |   |
	|               |   V
	|               | 0x0003 FFFFH
	-----------------

24KBs is reserved for bootloader despite the current version is less than 6KBs for further development. 

### Packet structure 
To ensure reliable communication on UART a structured packet is transmitted for each command/chunk of data. Each packet has 3 bytes overhead. Maximum useful data in a packet is **252** bytes (~1% overhead). Below is the packet structure: 

    [0]             -       Data length (n)
    [1]             -       Opcode
    [2 : (2+n-1)]   -       Data
    [2+n]           -       Terminator

	 

> Opcode: determines the type of request/response.



> Terminator: A static value representing the end of the packet. (0xA5)


Bootloader acknowledges successful reception of a packet with the below packet:

    0x01, 0xAA, [receivedOpcode], 0xA5
### Booting sequence
The bootloader's startup code begins by copying the code into RAM and executing from RAM as follows:

	void CopyCodeToRam(void) {
    uint32_t *pui32Src, *pui32Dest;

    // Copy the code from flash to SRAM.
    pui32Src = &__code_load__;
    for(pui32Dest = &__code_start__; pui32Dest < &__code_end__; )
    {
        *pui32Dest++ = *pui32Src++;
    }

    // Update vector table offset to RAM vector table
    __asm (
         "ldr     r0, =0x20000000\n"
         "ldr     r1, =0xe000ed08\n"
         "str     r0, [r1]\n"
          );

    // redirect return address to SRAM (add 0x20000000 to LR register) so we now return to
    // the ram copy of the program.
    __asm ("add     lr, lr, #0x20000000\n");
	}
	
The function is called from ResetISR which is the program entry point. After this function is executed the code will be executed directly from SRAM. 

After initialization is completed, SW1 push button on the lauchpad is used to determine wheter to jump directly into the application or to stay in the bootloader to update the application.
The bootloader will also check the first 8 bytes of the application area and if they are erased the bootloader will not jump into the application. The first 8 bytes cannot be 0xFFs as they are used to determine the stack pointer and the reset handler of the application. 

> To stay in the bootloader hold SW1 while resetting the MCU or erase application area

Blue LED will be turned on if bootloader is running.

The following code will jump into the application if the above conditions are not fulfilled:

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

 
### Flashing sequence
1. Erase (idle state only)
	- Erase request
		- Opcode: 0x01
		- Data: N/A
	- Erase response
		- Opcode: 0xA1
		- Data: 
			- 0x01 Erasing successful 
			- 0xF0 Erasing failed
2. Flashing request (idle state only)
	- Flashing request
		- Opcode: 0x02
		- Data: (4 bytes) Payload size (big-endian)
	- Flashing request response
		- Opcode: 0xA2
		- Data: 
			- 0x01 Flashing accepted
			- 0x00 Flashing refused
3. Flash data (only after accepted flashing request)
	- Flash data request
		-	Opcode: 0x03
		-	Data: (max 252) Application data
	- Flash data response
		- Opcode: 0xA3
		- Data:
			- 0x00 Writing flash memory failed
			- 0x01 Writing flash memory succeeded, send next chunk of data
			- 0x02 Writing flash memory succeeded, application is now completely transferred. 
4. Flash end
	- Flash end request: 
		- Opcode: 0x04
		- Data: (4 bytes) CRC32
	- Flash end response: 
		- Opcode: 0xA4
		- Data: 
			- 0x01 Received CRC matched calculated CRC. This means the application is correctly received and flashed. 
			- 0x00 CRC mismatch. Failed flashing operation.
5. Reset request: 
	The MCU will reset immediately. 

## Tive Flashing Utility
![Flashing utility](/utility/1.PNG)

This tool is coded in C#.Net. It communicates with the target using serial port and perform the flashing sequence. The executable is found here and the source code can be found in this [repo](https://github.com/ahmedkassem56/Tiva-Flashing-Utility).


## How to use
### Building the package
I use Code Composer Studio with GCC compiler. All needed files are included in src folder except for TivaWare. 
### Flash the bootloader itself
For the first time you need to flash the bootloader bin file using TI's [LM Flash Programmer](http://www.ti.com/tool/LMFLASHPROGRAMMER) on address 0 or using debugger
### Building application to be used with this bootloader
The only thing you need to change in your application is to change the application start address in your linker file to start from 0x00006000H , for example: 

	MEMORY
	{
	FLASH (RX) : ORIGIN = 0x00006000, LENGTH = 0x00040000 - 0x00006000
	SRAM (WX)  : ORIGIN = 0x20000000, LENGTH = 0x00008000
	}

### Generating .bin file using CCS
Navigate to: Project properties -> CCS build -> Steps -> Post-build steps 
Insert: 

    "${CG_TOOL_ROOT}/bin/arm-none-eabi-objcopy" -O ihex "${BuildArtifactFileName}" "${BuildArtifactFileBaseName}.hex"
	"${CG_TOOL_ROOT}/bin/arm-none-eabi-objcopy" -O binary "${BuildArtifactFileName}" "${BuildArtifactFileBaseName}.bin"

This will generate .bin and .hex files every time you compile the code.

## Performance
- sample_app_1: A sample application that just blinks the red LED (~3KBs). It is flashed in ~250ms
- sample_app_2: This is not a functioning application, it is just a sequence of bytes that fills the complete application area (~232KBs). It is flashed in ~25 seconds. 
## TODO
- Implement a feature to update the bootloader itself
- Support hex files in the flashing utility
- Fail safe mechanism 

### References
I used the concept of executing the code from ram with the help of this [repo](https://github.com/MarkDing/sim3u1xx_Bootloader).