/* generated configuration header file - do not edit */
#ifndef BSP_CLOCK_CFG_H_
#define BSP_CLOCK_CFG_H_
#define BSP_CFG_CLOCKS_SECURE (0)
#define BSP_CFG_CLOCKS_OVERRIDE (0)
#define BSP_CFG_XTAL_HZ (12000000) /* XTAL 12000000Hz */
#define BSP_CFG_PLL_SOURCE (BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC) /* PLL Src: XTAL */
#define BSP_CFG_HOCO_FREQUENCY (2) /* HOCO 20MHz */
#define BSP_CFG_PLL_DIV (BSP_CLOCKS_PLL_DIV_1) /* PLL Div /1 */
#define BSP_CFG_PLL_MUL BSP_CLOCKS_PLL_MUL(20U, 0U) /* PLL Mul x20.0 */
#define BSP_CFG_CLOCK_SOURCE (BSP_CLOCKS_SOURCE_CLOCK_PLL) /* Clock Src: PLL */
#define BSP_CFG_ICLK_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_2) /* ICLK Div /2 */
#define BSP_CFG_PCLKA_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_2) /* PCLKA Div /2 */
#define BSP_CFG_PCLKB_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_4) /* PCLKB Div /4 */
#define BSP_CFG_PCLKC_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_4) /* PCLKC Div /4 */
#define BSP_CFG_PCLKD_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_2) /* PCLKD Div /2 */
#define BSP_CFG_SDCLK_OUTPUT (1) /* SDCLK Enabled */
#define BSP_CFG_BCLK_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_2) /* BCLK Div /2 */
#define BSP_CFG_BCLK_OUTPUT (2) /* EBCLK Div /2 */
#define BSP_CFG_UCK_DIV (BSP_CLOCKS_USB_CLOCK_DIV_5) /* UCLK Div /5 */
#define BSP_CFG_FCLK_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_4) /* FCLK Div /4 */
#define BSP_CFG_CLKOUT_SOURCE (BSP_CLOCKS_CLOCK_DISABLED) /* CLKOUT Disabled */
#define BSP_CFG_CLKOUT_DIV (BSP_CLOCKS_SYS_CLOCK_DIV_1) /* CLKOUT Div /1 */
#endif /* BSP_CLOCK_CFG_H_ */
