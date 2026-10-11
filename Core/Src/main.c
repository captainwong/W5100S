/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "fsmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "wizchip_conf.h"
#include "socket.h"
#include "loopback.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Default network configuration: 192.168.<NET_SUBNET>.<host> */
#define NET_SUBNET       50      /* third octet, change this to re-segment   */
#define NET_IP_HOST      254     /* board IP : 192.168.NET_SUBNET.NET_IP_HOST */
#define NET_GW_HOST      1       /* gateway  : 192.168.NET_SUBNET.NET_GW_HOST */
#define NET_DNS_HOST     1       /* dns      : 192.168.NET_SUBNET.NET_DNS_HOST */
#define NET_SUBNET_MASK  {255, 255, 255, 0}
#define NET_MAC_ADDR     {0x00, 0x08, 0xDC, 0x78, 0x91, 0x71}

/* TCP echo server */
#define TCP_ECHO_PORT    50000
#define ETH_MAX_BUF_SIZE 2048
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static wiz_NetInfo g_net_info = {
    .mac  = NET_MAC_ADDR,
    .ip   = {192, 168, NET_SUBNET, NET_IP_HOST},
    .sn   = NET_SUBNET_MASK,
    .gw   = {192, 168, NET_SUBNET, NET_GW_HOST},
    .dns  = {192, 168, NET_SUBNET, NET_DNS_HOST},
    .dhcp = NETINFO_STATIC
};
static uint8_t g_eth_buf[ETH_MAX_BUF_SIZE];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static iodata_t bus_read_byte(uint32_t addr);
static void bus_write_byte(uint32_t addr, iodata_t data);
static void w5100s_hw_reset(void);
static int  w5100s_init(void);
static void print_netinfo(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
  * @brief printf retarget to USART1 (consumed by syscalls.c _write)
  */
int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;
  HAL_UART_Transmit(&huart1, &c, 1, HAL_MAX_DELAY);
  return ch;
}

/**
  * @brief W5100S bus access callbacks.
  * @note  ADDR[1:0] are wired to FSMC_A0/A1, so accessing base+offset makes
  *        the FSMC output the phase (00=MR, 01=IDM_AR0, 10=IDM_AR1, 11=IDM_DR)
  *        in hardware - plain dereferences are all that is needed.
  */
static iodata_t bus_read_byte(uint32_t addr)
{
  return *(volatile iodata_t *)addr;
}

static void bus_write_byte(uint32_t addr, iodata_t data)
{
  *(volatile iodata_t *)addr = data;
}

/**
  * @brief Hardware reset of W5100S (RST pin, active low).
  */
static void w5100s_hw_reset(void)
{
  HAL_GPIO_WritePin(W5100S_RST_GPIO_Port, W5100S_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(5);
  HAL_GPIO_WritePin(W5100S_RST_GPIO_Port, W5100S_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100);
}

/**
  * @brief Register bus callbacks, reset the chip, configure sockets and network.
  * @retval 0 on success, -1 on failure
  */
static int w5100s_init(void)
{
  uint8_t memsize[2][4] = {{2, 2, 2, 2}, {2, 2, 2, 2}};
  intr_kind imr = IK_DEST_UNREACH;
  intr_kind link;

  reg_wizchip_bus_cbfunc(bus_read_byte, bus_write_byte);

  /* This ioLibrary version uses _read_data_buf/_write_data_buf in bus
     indirect mode; they are NULL unless the defaults are registered. */
  reg_wizchip_busbuf_cbfunc(NULL, NULL);

  w5100s_hw_reset();

  if (ctlwizchip(CW_INIT_WIZCHIP, (void *)memsize) == -1)
  {
    printf("W5100S init failed\r\n");
    return -1;
  }
  if (ctlwizchip(CW_SET_INTRMASK, &imr) == -1)
  {
    printf("W5100S set interrupt mask failed\r\n");
    return -1;
  }

  ctlnetwork(CN_SET_NETINFO, (void *)&g_net_info);

  do
  { /* wait for PHY link up */
    if (ctlwizchip(CW_GET_PHYLINK, (void *)&link) == -1)
    {
      printf("PHY link status unknown\r\n");
      return -1;
    }
  } while (link == PHY_LINK_OFF);

  return 0;
}

/**
  * @brief Print the current network configuration over USART1.
  */
static void print_netinfo(void)
{
  wiz_NetInfo info;

  ctlnetwork(CN_GET_NETINFO, (void *)&info);
  printf("MAC address: %02x:%02x:%02x:%02x:%02x:%02x\r\n",
         info.mac[0], info.mac[1], info.mac[2], info.mac[3], info.mac[4], info.mac[5]);
  printf("IP address : %d.%d.%d.%d\r\n", info.ip[0], info.ip[1], info.ip[2], info.ip[3]);
  printf("Subnet mask: %d.%d.%d.%d\r\n", info.sn[0], info.sn[1], info.sn[2], info.sn[3]);
  printf("Gateway    : %d.%d.%d.%d\r\n", info.gw[0], info.gw[1], info.gw[2], info.gw[3]);
  printf("DNS server : %d.%d.%d.%d\r\n", info.dns[0], info.dns[1], info.dns[2], info.dns[3]);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FSMC_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  if (w5100s_init() != 0)
  {
    Error_Handler();
  }
  print_netinfo();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    loopback_tcps(0, g_eth_buf, TCP_ECHO_PORT);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
