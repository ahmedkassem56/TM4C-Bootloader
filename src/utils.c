/*
 * utils.c
 *
 *  Created on: Dec 28, 2019
 *      Author: Ahmed Kassem
 */


#include "utils.h"

uint32_t bytesToU32(uint8_t *buff, int offset) {
    uint32_t res = 0;

    res |= (buff[offset+0] << 24);
    res |= (buff[offset+1] << 16);
    res |= (buff[offset+2] << 8);
    res |= buff[offset+3];

    return res;
}

uint32_t _get_PC(void) {

  register uint32_t result;

  __asm volatile ("push    {lr}");
  __asm volatile ("pop    {%0}"  : "=r" (result) );

  return(result);
}
