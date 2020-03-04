/* lpc.c
 *
 * Copyright (C) 2020 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include <stdint.h>
#include <target.h>
#include "image.h"
#include "fsl_common.h"
#include "fsl_flashiap.h"
#include "fsl_power.h"

static int flash_init = 0;
uint32_t SystemCoreClock;

#ifndef NVM_FLASH_WRITEONCE
#   error "wolfBoot LPC HAL: no WRITEONCE support detected. Please define NVM_FLASH_WRITEONCE"
#endif

#define BOARD_BOOTCLOCKPLL180M_CORE_CLOCK 180000000U
#ifdef __WOLFBOOT

/*******************************************************************************
 * Code for BOARD_BootClockPLL180M configuration (automatically generated by SDK)
 ******************************************************************************/
void BOARD_BootClockPLL180M(void)
{
    /*!< Set up the clock sources */
    /*!< Set up FRO */
    POWER_DisablePD(kPDRUNCFG_PD_FRO_EN);  /*!< Ensure FRO is on  */
    CLOCK_AttachClk(kFRO12M_to_MAIN_CLK);  /*!< Switch to FRO 12MHz first to ensure we can change voltage without
                                              accidentally  being below the voltage for current speed */
    POWER_DisablePD(kPDRUNCFG_PD_SYS_OSC); /*!< Enable System Oscillator Power */
    SYSCON->SYSOSCCTRL = ((SYSCON->SYSOSCCTRL & ~SYSCON_SYSOSCCTRL_FREQRANGE_MASK) |
                          SYSCON_SYSOSCCTRL_FREQRANGE(0U)); /*!< Set system oscillator range */
    POWER_SetVoltageForFreq(
        180000000U); /*!< Set voltage for the one of the fastest clock outputs: System clock output */
    CLOCK_SetFLASHAccessCyclesForFreq(180000000U); /*!< Set FLASH wait states for core */

    /*!< Set up SYS PLL */
    const pll_setup_t pllSetup = {
        .pllctrl = SYSCON_SYSPLLCTRL_SELI(32U) | SYSCON_SYSPLLCTRL_SELP(16U) | SYSCON_SYSPLLCTRL_SELR(0U),
        .pllmdec = (SYSCON_SYSPLLMDEC_MDEC(8191U)),
        .pllndec = (SYSCON_SYSPLLNDEC_NDEC(770U)),
        .pllpdec = (SYSCON_SYSPLLPDEC_PDEC(98U)),
        .pllRate = 180000000U,
        .flags   = PLL_SETUPFLAG_WAITLOCK | PLL_SETUPFLAG_POWERUP};
    CLOCK_AttachClk(kFRO12M_to_SYS_PLL); /*!< Set sys pll clock source*/
    CLOCK_SetPLLFreq(&pllSetup);         /*!< Configure PLL to the desired value */
    /*!< Need to make sure ROM and OTP has power(PDRUNCFG0[17,29]= 0U)
         before calling this API since this API is implemented in ROM code */
    CLOCK_SetupFROClocking(96000000U); /*!< Set up high frequency FRO output to selected frequency */

    /*!< Set up dividers */
    CLOCK_SetClkDiv(kCLOCK_DivAhbClk, 1U, false);  /*!< Reset divider counter and set divider to value 1 */
    CLOCK_SetClkDiv(kCLOCK_DivUsb0Clk, 0U, true);  /*!< Reset USB0CLKDIV divider counter and halt it */
    CLOCK_SetClkDiv(kCLOCK_DivUsb0Clk, 1U, false); /*!< Set USB0CLKDIV divider to value 1 */

    /*!< Set up clock selectors - Attach clocks to the peripheries */
    CLOCK_AttachClk(kSYS_PLL_to_MAIN_CLK); /*!< Switch MAIN_CLK to SYS_PLL */
    CLOCK_AttachClk(kFRO_HF_to_USB0_CLK);  /*!< Switch USB0_CLK to FRO_HF */
    SYSCON->MAINCLKSELA =
        ((SYSCON->MAINCLKSELA & ~SYSCON_MAINCLKSELA_SEL_MASK) |
         SYSCON_MAINCLKSELA_SEL(0U)); /*!< Switch MAINCLKSELA to FRO12M even it is not used for MAINCLKSELB */
    /* Set SystemCoreClock variable. */
    SystemCoreClock = BOARD_BOOTCLOCKPLL180M_CORE_CLOCK;
}

/* Assert hook needed by SDK */
void __assert_func(const char *a, int b, const char *c, const char *d)
{
    while(1)
        ;
}

void hal_init(void)
{
    BOARD_BootClockPLL180M();
}

void hal_prepare_boot(void)
{
}

#endif

#define FLASH_PAGE_SIZE 0x100 /* 256 bytes */

static uint8_t flash_page_cache[FLASH_PAGE_SIZE];

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int w = 0;
    int ret;
    int idx = 0;
    uint32_t page_address;
    uint32_t offset;
    int size;
    while (idx < len) {
        page_address = (address + idx) / FLASH_PAGE_SIZE;
        offset = address - page_address;
        size = FLASH_PAGE_SIZE - offset;
        if (size > (len - idx))
            size = len - idx;

        if (size > 0) {
            memcpy(flash_page_cache, (void *)page_address, FLASH_PAGE_SIZE);
            memcpy(flash_page_cache + offset, data + idx, size);
            FLASHIAP_PrepareSectorForWrite(page_address / WOLFBOOT_SECTOR_SIZE, page_address / WOLFBOOT_SECTOR_SIZE);
            FLASHIAP_CopyRamToFlash(page_address, (uint32_t *)flash_page_cache, FLASH_PAGE_SIZE, SystemCoreClock);
        }
        idx += size;
    }
    memset(flash_page_cache, 0xFF, FLASH_PAGE_SIZE);
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t start, end;
    start = address / WOLFBOOT_SECTOR_SIZE;
    end = (address + (len - 1)) / WOLFBOOT_SECTOR_SIZE;
    FLASHIAP_PrepareSectorForWrite(start, end);
    FLASHIAP_EraseSector(start, end, SystemCoreClock);
    return 0;
}



