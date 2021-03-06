/*
 * Copyright 2016-2020, Cypress Semiconductor Corporation or a subsidiary of
 * Cypress Semiconductor Corporation. All Rights Reserved.
 *
 * This software, including source code, documentation and related
 * materials ("Software"), is owned by Cypress Semiconductor Corporation
 * or one of its subsidiaries ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products. Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 */

/** @file
 *  thermistor_app.c
 *
 *  Version: 1.1
 *
 *  Related Document:
 *    CE226300 - BLE Environment Sensing Temperature with CYW20819.
 *
 *  @brief
 *  This file contains the starting point of thermistor application.
 *  The application_start registers for Bluetooth stack in this file.
 *  The Bluetooth Management callback manages the Bluetooth events and the
 *  application developer can customize the functionality and behavior based on
 *  the Bluetooth events. The Bluetooth Management event acts like a
 *  Finite State Machine (FSM) for the application.
 */

/*******************************************************************************
 *                               Includes
 ******************************************************************************/
#include "GeneratedSource/cycfg_gatt_db.h"
#include "thermistor_gatt_handler.h"
#include "thermistor_util_functions.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_cfg.h"
#include "wiced_bt_trace.h"
#include "wiced_bt_uuid.h"
#include "wiced_hal_adc.h"
#include "wiced_hal_gpio.h"
#include "wiced_timer.h"
#include "wiced_bt_stack.h"
#if ( defined(CYW20706A2) || defined(CYW20719B1) || defined(CYW20719B0) || \
      defined(CYW20721B1) || defined(CYW20735B0) || defined(CYW43012C0) )
#include "wiced_bt_app_common.h"
#endif
#include "wiced.h"
#include "wiced_bt_ota_firmware_upgrade.h"

/******************************************************************************
 *                                Constants
 ******************************************************************************/
/* This is the temperature measurement interval which is same as configured in
 * the BT Configurator - The variable represents interval in milliseconds.
 */
#define POLL_TIMER_IN_MS         (5000)

/* Absolute value of an integer. The absolute value is always positive. */
#ifndef ABS
#define ABS(N) ((N<0)?(-N):(N))
#endif

/*
 * The OTA library contains functionality to support both secure and non-secure
 * versions of the upgrade. In the non-secure version, a simple CRC32
 * Verification is performed to validate that all bytes that have been sent from
 * the device performing the upgrade are correctly saved in the device flash.
 * The secure version of the upgrade validates that the OTA image is correctly
 * signed and has correct production information in the header. This ensures
 * that unknown firmware is not uploaded to your device.
 */
#ifdef OTA_SECURE_FIRMWARE_UPGRADE
#include "bt_types.h"
#include "p_256_multprecision.h"
#include "p_256_ecc_pp.h"

/*
 * If secure version of the OTA firmware upgrade is used, the app should be
 * linked with the ecdsa256_pub.c which exports the public key.
 */
extern Point    ecdsa256_public_key;
#endif

/******************************************************************************
 *                                Variable/Structure/type Definitions
 ******************************************************************************/
/* Manages runtime configuration of Bluetooth stack */
extern const wiced_bt_cfg_settings_t wiced_app_cfg_settings;

/* Buffer for RF, HCI, ACL packets */
extern const wiced_bt_cfg_buf_pool_t wiced_app_cfg_buf_pools[];

/* This wiced_timer changes for every millisecond */
static wiced_timer_t            milli_seconds_timer;

/* Status variable for connection ID */
uint16_t                        thermistor_conn_id;

/*******************************************************************
 *                              Function Declarations/Prototypes
 ******************************************************************/
static wiced_bt_dev_status_t
thermistor_management_callback(wiced_bt_management_evt_t event,
                               wiced_bt_management_evt_data_t *p_event_data);

static wiced_result_t thermistor_set_advertisement_data(void);

static void seconds_timer_temperature_cb(uint32_t arg);

static void thermistor_app_init(void);

extern void thermistor_init(void);
extern int16_t thermistor_read(void);

/*******************************************************************
 *                              Function Definitions
 ******************************************************************/


/*
 Function name:
 application_start

 Function Description:
 @brief    Entry point to the application. Set device configuration and start
           BT stack initialization. The actual application initialization will
           happen when stack reports that BT device is ready.

 @param void

 @return void
 */
void application_start(void)
{

    /*WICED_BT_TRACE_ENABLE*/
#if defined WICED_BT_TRACE_ENABLE || defined TRACE_TO_WICED_HCI
    wiced_set_debug_uart(WICED_ROUTE_DEBUG_TO_PUART);
#endif

    /* Initialize Stack and Register Management Callback */
    wiced_bt_stack_init(thermistor_management_callback,
                        &wiced_app_cfg_settings,
                        wiced_app_cfg_buf_pools);
}

/*
 Function Name:
 thermistor_management_callback

 Function Description:
 @brief  Bluetooth Management Event callback function registered by
         application_start() This function acts like a Finite State Machine(FSM)
         for the application.

 @param  event           Bluetooth management event type
 @param  p_event_data    Pointer to the bluetooth management event data

 @return  wiced_bt_dev_status_t BT status of the callback function
 */
static wiced_bt_dev_status_t thermistor_management_callback(
                                wiced_bt_management_evt_t event,
                                wiced_bt_management_evt_data_t *p_event_data)
{

    wiced_bt_dev_status_t       status              = WICED_SUCCESS;
    wiced_bt_ble_advert_mode_t *p_adv_mode          = NULL;
    wiced_bt_device_address_t  local_device_bd_addr;

    switch (event)
    {

    case BTM_ENABLED_EVT:
        WICED_BT_TRACE( "\n\r--------------------------------------------------------- \r\n\n"
                        " CE226300 BLE Environmental Sensing Service Application \n\r\n\r"
                        "---------------------------------------------------------\n\r"
                        "This application measures voltage on the selected DC channel\r\n"
                        "every %d milliseconds (configurable) and displays\r\n"
                        "the measured temperature via PUART. \n\r"
                        "---------------------------------------------------------\n\r"
                        , POLL_TIMER_IN_MS);

        WICED_BT_TRACE("\r\nDiscover this device with the name: \"%s\"\r\n",
                      app_gap_device_name);

        wiced_bt_dev_read_local_addr(local_device_bd_addr);

        WICED_BT_TRACE("\r\nBluetooth Device Address: %B \r\n",
                        local_device_bd_addr);

        WICED_BT_TRACE("\r\n");
        WICED_BT_TRACE("Bluetooth Management Event: \t");
        WICED_BT_TRACE(btm_event_name(event));
        WICED_BT_TRACE("\r\n");

        /* Perform application-specific initialization */
        thermistor_app_init();
        break;

    case BTM_DISABLED_EVT:
        /* Bluetooth Controller and Host Stack Disabled */
        WICED_BT_TRACE("\r\n");
        WICED_BT_TRACE("Bluetooth Management Event: \t");
        WICED_BT_TRACE(btm_event_name(event));
        WICED_BT_TRACE("\r\n");
        WICED_BT_TRACE("Bluetooth Disabled\r\n");
        break;

    case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
        /* Advertisement State Changed */
        WICED_BT_TRACE("\r\n");
        WICED_BT_TRACE("Bluetooth Management Event: \t");
        WICED_BT_TRACE(btm_event_name(event));
        WICED_BT_TRACE("\r\n");
        p_adv_mode = &p_event_data->ble_advert_state_changed;
        WICED_BT_TRACE("\r\n");
        WICED_BT_TRACE("Advertisement state changed to ");
        WICED_BT_TRACE(btm_advert_mode_name(*p_adv_mode));
        WICED_BT_TRACE("\r\n");
        break;

    case BTM_BLE_PHY_UPDATE_EVT:
        /* BLE PHY Update to 1M or 2M */
        WICED_BT_TRACE("\r\n");
        WICED_BT_TRACE("Bluetooth Management Event: \t");
        WICED_BT_TRACE(btm_event_name(event));
        WICED_BT_TRACE("\r\n");
        WICED_BT_TRACE("\rPHY config is updated as TX_PHY : %dM, RX_PHY : %dM \n",
                p_event_data->ble_phy_update_event.tx_phy,
                p_event_data->ble_phy_update_event.rx_phy);
        break;

    default:
        break;
    }

    return status;
}

/*
 Function name:
 seconds_timer_temperature_cb

 Function Description:
 @brief  This callback function is invoked on timeout of seconds timer.

 @param  arg

 @return void
 */
static void seconds_timer_temperature_cb(uint32_t arg)
{
    volatile int16_t    temperature             = 0;

    /*
     * Temperature values might vary upto +/-2 degree Celsius
     */
    temperature = thermistor_read();
    WICED_BT_TRACE("\r\nTemperature (in degree Celsius) \t\t%d.%02d\r\n",
                    (temperature / 100),
                    ABS(temperature % 100));

    /*
     * app_ess_temperature value is set both for read operation and
     * notify operation.
     */
    app_ess_temperature[0] = (uint8_t) (temperature & 0xff);
    app_ess_temperature[1] = (uint8_t) ((temperature >> 8) & 0xff);

    /* To check that connection is up and
     * client is registered to receive notifications
     * to send temperature data in Little Endian Format
     * as per BT SIG's ESS Specification
     */
    if (0 != thermistor_conn_id)
    {
        if (0 != (app_ess_temperature_client_char_config[0] &
                 GATT_CLIENT_CONFIG_NOTIFICATION))
        {
            WICED_BT_TRACE("This device is connected to a central device and\r\n"
                           "GATT client notifications are enabled\r\n");

            wiced_bt_gatt_send_notification(thermistor_conn_id,
                                            HDLC_ESS_TEMPERATURE_VALUE,
                                            app_ess_temperature_len,
                                            app_ess_temperature);
        }
        else
        {
            WICED_BT_TRACE("This device is connected to a central device and\r\n"
                           "GATT client notifications are not enabled\r\n");
        }
    }
    else
    {
        WICED_BT_TRACE("This device is not connected to any BLE central device\r\n");
    }

}

/*
 Function name:
 thermistor_app_init

 Function Description:
 @brief    This function is executed if BTM_ENABLED_EVT event occurs in
           thermistor management callback.

 @param    void

 @return    void
 */
static void thermistor_app_init(void)
{

    wiced_bt_gatt_status_t status;

    /* Register with stack to receive GATT callback */
    status = wiced_bt_gatt_register(thermistor_event_handler);
    WICED_BT_TRACE("\r\nGATT status:\t");
    WICED_BT_TRACE(gatt_status_name(status));
    WICED_BT_TRACE("\r\n");

    /* Initialize GATT Database */
    if(WICED_BT_GATT_SUCCESS != wiced_bt_gatt_db_init(gatt_database,
                                                      gatt_database_len))
    {
        WICED_BT_TRACE("\r\n GATT DB Initialization not successful\r\n");
    }

    /*
     * The thermistor_init() automatically powers up ADC block before reading
     * ADC registers and powers down after reading which reduces power
     * consumption. By multiple single shot sampling, it also improves the
     * accuracy of the reading.
     */
    thermistor_init();           /*ADC Initialization*/

    /* Starting the MILLI_SECONDS timer for every POLL_TIMER_IN_MS*/
    if ( WICED_SUCCESS == wiced_init_timer( &milli_seconds_timer,
                                            &seconds_timer_temperature_cb,
                                            0,
                                            WICED_MILLI_SECONDS_PERIODIC_TIMER))
    {
        if ( WICED_SUCCESS != wiced_start_timer(&milli_seconds_timer,
                                                POLL_TIMER_IN_MS))
        {
            WICED_BT_TRACE("Seconds Timer Error\r\n");
        }
    }

    /* Set Advertisement Data */
    if(WICED_SUCCESS != thermistor_set_advertisement_data())
    {
        WICED_BT_TRACE("Raw advertisement failed\r\n");
    }

    /* Do not allow peer to pair */
    wiced_bt_set_pairable_mode(WICED_FALSE, TRUE);

    /* OTA Firmware upgrade Initialization */
#ifdef OTA_SECURE_FIRMWARE_UPGRADE
    if (!wiced_ota_fw_upgrade_init(&ecdsa256_public_key, NULL, NULL))
#else
    if (!wiced_ota_fw_upgrade_init(NULL, NULL, NULL))
#endif
    {
          WICED_BT_TRACE("OTA upgrade Init failure !!! \n");
    }

    /* Start Undirected LE Advertisements on device startup. */
    if(WICED_SUCCESS ==
            wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH,
                                          BLE_ADDR_PUBLIC,
                                          NULL))
    {
        WICED_BT_TRACE("Starting undirected BLE advertisements successful\r\n");
    }

}

/*
 Function Name:
 thermistor_set_advertisement_data

 Function Description:
 @brief  Set Advertisement Data

 @param void

 @return wiced_result_t WICED_SUCCESS or WICED_failure
 */
static wiced_result_t thermistor_set_advertisement_data(void)
{

    wiced_bt_ble_advert_elem_t adv_elem[3];
    wiced_result_t result;
    uint8_t num_elem        = 0;
    uint8_t flag            = BTM_BLE_GENERAL_DISCOVERABLE_FLAG |
                              BTM_BLE_BREDR_NOT_SUPPORTED;
    uint8_t appearance_data = FROM_BIT16_TO_8(APPEARANCE_GENERIC_THERMOMETER);
    uint8_t uuid_data       = FROM_BIT16_TO_8(UUID_SERVICE_ENVIRONMENTAL_SENSING);

    adv_elem[num_elem].advert_type = BTM_BLE_ADVERT_TYPE_FLAG;
    adv_elem[num_elem].len         = sizeof(uint8_t);
    adv_elem[num_elem].p_data      = &flag;
    num_elem++;

    adv_elem[num_elem].advert_type = BTM_BLE_ADVERT_TYPE_NAME_COMPLETE;
    adv_elem[num_elem].len         = app_gap_device_name_len;
    adv_elem[num_elem].p_data      = app_gap_device_name;
    num_elem++;

    adv_elem[num_elem].advert_type = BTM_BLE_ADVERT_TYPE_FLAG;
    adv_elem[num_elem].len         = sizeof(FROM_BIT16_TO_8(UUID_SERVICE_ENVIRONMENTAL_SENSING));
    adv_elem[num_elem].p_data      = &uuid_data;
    num_elem++;

    result = wiced_bt_ble_set_raw_advertisement_data(num_elem, adv_elem);

    return result;
}

/* Note for OTA support - The handles for OTA services should be defined as
   below in cycfg_gatt_db.h. If the application is updated with Bluetooth
   Configurator, ensure the handles are set as below. Also the OTA service
   should be last service in the GATT database.

#define HDLS_FWUPGRADESERVICE                                            HANDLE_OTA_FW_UPGRADE_SERVICE
#define HDLC_FWUPGRADESERVICE_FWUPGRADECONTOLPOINT                       HANDLE_OTA_FW_UPGRADE_CHARACTERISTIC_CONTROL_POINT
#define HDLC_FWUPGRADESERVICE_FWUPGRADECONTOLPOINT_VALUE                 HANDLE_OTA_FW_UPGRADE_CONTROL_POINT
#define HDLD_FWUPGRADESERVICE_FWUPGRADECONTOLPOINT_CLIENT_CHAR_CONFIG    HANDLE_OTA_FW_UPGRADE_CLIENT_CONFIGURATION_DESCRIPTOR
#define HDLC_FWUPGRADESERVICE_FWUPGRADEDATA                              HANDLE_OTA_FW_UPGRADE_CHARACTERISTIC_DATA
#define HDLC_FWUPGRADESERVICE_FWUPGRADEDATA_VALUE                        HANDLE_OTA_FW_UPGRADE_DATA

*/
