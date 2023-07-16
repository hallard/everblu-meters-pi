 /*  the radian_trx SW shall not be distributed  nor used for commercial product*/
 /*  it is exposed just to demonstrate CC1101 capability to reader water meter indexes */
 /*  there is no Warranty on radian_trx SW */

#include "time.h"
#include "stdio.h"
#include "stdarg.h"
#include "stdlib.h"
#include "pthread.h"
#include "stdint.h"
#include "string.h"
#include <time.h>
#include <unistd.h>    // pour sleep
#include <termios.h>

#include "wiringPi.h"

#define METER_YEAR      22
#define METER_SERIAL    828979

#define MQTT_HOST   "192.168.1.8"
#define MQTT_PORT   1883
#define MQTT_USER   ""
#define MQTT_PASS   ""
#define MQTT_TOPIC  "everblu/"
#define MQTT_KEEP_ALIVE     300
#define MQTT_MSG_MAX_SIZE   512

#define REG_DEFAULT 	0x10AF75 // CC1101 register values for 433.82MHz
#define REG_SCAN_LOOP	     128 // Allow up and dow 128 to REG_DEFAULT while scanning 

#define GDO0        6 // GPIO25
#define GDO2        3 // GPIO22
#define GDO1_MISO   13
#define MOSI        12
#define cc1101_CSn  10 // GPIO8 

// CC1101-PI E07 module
#define LED_RED 4   // GPIO23
#define LED_GRN 5   // GPIO24
#define LED_BLU 11  // GPIO7

// CC1101-PI Chinese 
#define LED_RED 4   // GPIO23
#define LED_GRN 5   // GPIO24


#include "utils.c"
#include "wiringPi.h"
#include "wiringPiSPI.h"



