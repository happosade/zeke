/**
 *******************************************************************************
 * @file    raspi_leds.c
 * @author  Olli Vanhoja
 * @brief   Raspberry Pi leds.
 * @section LICENSE
 * Copyright (c) 2014 Olli Vanhoja <olli.vanhoja@cs.helsinki.fi>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************
 */

#include <kinit.h>
#include <kerror.h>
#include "../bcm2835/bcm2835_mmio.h"
#include "../bcm2835/bcm2835_gpio.h"

#define RASPI_LED_POS   (1 << 16)

void raspi_led_invert(void);

void raspi_leds_init(void) __attribute__((constructor));
void raspi_leds_init(void) {
    SUBSYS_INIT();
    SUBSYS_DEP(bcm2835_mmio_init);

    unsigned int sel;
    istate_t s_entry;

    /* Each GPIO has 3 bits which determine its function
     * GPIO 14 and 16 are in GPFSEL1
     */

    mmio_start(&s_entry);

    /* Read current value of GPFSEL1 */
    sel = mmio_read(GPIO_GPFSEL1);

    /* GPIO 16 = 001 - output */
    sel &=~ (7<<18);
    sel |= 1 << 18;
    /* GPIO 14 = 000 - input */
    sel &=~ (7 << 12);

    /* Write back updated value */
    mmio_write(GPIO_GPFSEL1, sel);

    /* Set up pull-up on GPIO14 */
    /* Enable pull-up control, then wait for some cycles.
     */
    mmio_write(GPIO_GPPUD, 2);
    mmio_end(&s_entry);
    bcm2835_gpio_delay(150);

    /* Set the pull up/down clock for pin 14 */
    mmio_start(&s_entry);
    mmio_write(GPIO_PUDCLK0, 1 << 14);
    mmio_write(GPIO_PUDCLK1, 0);
    mmio_end(&s_entry);
    bcm2835_gpio_delay(150);

    /* Disable pull-up control and reset the clock registers. */
    mmio_start(&s_entry);
    mmio_write(GPIO_GPPUD, 0);
    mmio_write(GPIO_GPPUD, 0);
    mmio_write(GPIO_PUDCLK1, 0);
    mmio_end(&s_entry);

    for (int i = 0; i < 4; i++) {
        raspi_led_invert();
        bcm2835_gpio_delay(20000);
    }

    SUBSYS_INITFINI("raspi leds");
}

static unsigned int led_status;

void raspi_led_invert(void)
{
    istate_t s_entry;

    led_status = !led_status;
    mmio_start(&s_entry);

    if(led_status) {
        mmio_write(GPIO_GPCLR0, RASPI_LED_POS); /* on */
    } else {
        mmio_write(GPIO_GPSET0, RASPI_LED_POS); /* off */
    }

    mmio_end(&s_entry);
}
