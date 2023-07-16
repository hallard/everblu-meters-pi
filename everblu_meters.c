 /*  the radian_trx SW shall not be distributed  nor used for commercial product*/
 /*  it is exposed just to demonstrate CC1101 capability to reader water meter indexes */

#include <stdio.h>
#include <stdlib.h>
#include <mosquitto.h>
#include <string.h>
#include <signal.h>

#include "everblu_meters.h"
#include "cc1101.c"

struct tmeter_data meter_data;
struct mosquitto *mosq = NULL;
char meter_id[32];
char mqtt_topic[64];
float f_min, f_max ; // Scan results
uint32_t r_min, r_max ; // Scan results


void clean_exit(int code) 
{
    if (mosq) {
        mosquitto_destroy(mosq);
    }
	mosquitto_lib_cleanup();

    // Clear all LED
    #ifdef LED_RED
    digitalWrite(LED_RED, LOW);
    #endif
    #ifdef LED_GRN
    digitalWrite(LED_GRN, LOW);
    #endif
    #ifdef LED_BLU
    digitalWrite(LED_BLU, LOW);
    #endif

    exit(code);
}

void signal_handler (int signum)
{
  // Does we received CTRL-C ?
  if ( signum==SIGINT ) {
    // Indicate we want to quit
    fprintf(stderr, "\nReceived SIGINT\n");
    clean_exit(0);
  } else if ( signum==SIGTERM ) {
    // Indicate we want to quit
    fprintf(stderr, "\nReceived SIGTERM\n");
    clean_exit(0);
  } 
}

void my_message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{
	if ( message->payloadlen ) {
		printf("MQTT : Received %s %s", message->topic, (char *)message->payload);
	} else {
		//printf("%s (null)\n", message->topic);
	}
	fflush(stdout);
}

void my_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
    char full_topic[128] = MQTT_TOPIC;
    sprintf(full_topic, MQTT_TOPIC "/%s/WaterUsage", meter_id);

	if ( !result ) {
		/* Subscribe to broker information topics on successful connect. */
		mosquitto_subscribe(mosq, NULL, full_topic, 2);
	} else {
		fprintf(stderr, "MQTT : Subscribe to %s failed\n", full_topic);
	}
}

void my_subscribe_callback(struct mosquitto *mosq, void *userdata, int mid, int qos_count, const int *granted_qos)
{
	int i;
	printf("MQTT : Subscribed OK (mid: %d): %d", mid, granted_qos[0]);
	for ( i=1 ; i < qos_count ; i++ ) {
		printf(", %d", granted_qos[i]);
	}
	printf("\n");
}

void my_log_callback(struct mosquitto *mosq, void *userdata, int level, const char *str)
{
	/* Pring all log messages regardless of level. */
	printf("MQTT Log : %s\n", str);
}

void printMeterData(struct tmeter_data * data , char * json_data)
{
    time_t ts = time (NULL);
    printf("Consumption   : %d Liters\r\n", data->liters);
    printf("Battery left  : %d Months\r\n", data->battery_left);
    printf("Read counter  : %d times\r\n", data->reads_counter);
    printf("Working hours : from %02dH to %02dH\r\n", data->time_start, data->time_end);
    printf("Local Time    : %s\r\n", getDate());
    printf("RSSI  /  LQI  : %ddBm  /  %d\r\n", data->rssi, data->lqi);

    // Format this to JSON ?
    if (json_data) {
        sprintf(json_data, "{ \"liters\":%d, \"battery\":%d, \"read\":%d, \"hours\":\"%02d-%02d\", \"rssi\":%d, \"lqi\":%d, \"date\":\"%s\", \"ts\":%ld }", 
                data->liters, data->battery_left, data->reads_counter, 
                data->time_start, data->time_end, data->rssi, data->lqi, getDate(), ts );
    }
}

void IO_init(void)
{
	wiringPiSetup();
	pinMode (GDO2, INPUT);
	pinMode (GDO0, INPUT);
    #ifdef LED_RED
	pinMode (LED_RED, OUTPUT);
    digitalWrite(LED_RED, LOW);
    #endif
    #ifdef LED_GRN
	pinMode (LED_GRN, OUTPUT);
    digitalWrite(LED_GRN, LOW);
    #endif
    #ifdef LED_BLU
    // Light bkue LED on start
	pinMode (LED_BLU, OUTPUT);
    digitalWrite(LED_BLU, HIGH);
    #endif
}

// test a read on specified frequency using CC1101 register settings
bool test_frequency_register(uint32_t reg)  
{
    char buff[256];

    // keep only 3 bytes
    reg &= 0xFFFFFF;
    float _frequency = (26.0f/65536.0f) * (float) reg ;

    #ifdef LED_GRN
    digitalWrite(LED_GRN, HIGH);
    delay(250);
    digitalWrite(LED_GRN, LOW);
    #endif
    printf("Test register : 0x%06X (%.4fMHz) => ", reg, _frequency);
    fflush(stdout);
    cc1101_init(0.0f, reg, false);
    // Used for testing my code only by simulation
    //if (_frequency>433.83f && _frequency<433.84f) {
    //    meter_data.ok = true;
    //} else {
    //    meter_data.ok = false;
    //}
    meter_data = get_meter_data();
    // Got datas ?
    if (meter_data.ok) {
        printf("** OK! **");
        // check and adjust working boundaries
        if (_frequency > f_max) {
            f_max = _frequency;
            r_max = reg;
        }
        if (_frequency < f_min) {
            f_min = _frequency;
            r_min = reg;
        }
    } else {
        printf("No answer");
        #ifdef LED_RED
        digitalWrite(LED_RED, HIGH);
        delay(250);
        digitalWrite(LED_RED, LOW);
        #endif
    }

    // Found working boudaries?
    if (r_min<(REG_DEFAULT+REG_SCAN_LOOP) && r_max>(REG_DEFAULT-REG_SCAN_LOOP)) {
        printf("    %.4f < Works < %.4f ", f_min, f_max);
        printf("    RSSI:%ddBm LQI:%d ", meter_data.rssi, meter_data.lqi);
    }
    printf("\n");

    sprintf(mqtt_topic, MQTT_TOPIC "%s/scanning", meter_id);
    sprintf(buff, "{ \"date\":\"%s\", \"frequency\":\"%.4f\", \"register\":\"0x%06X\", \"rssi\":%d, \"lqi\":%d, \"result\":%d }", 
                        getDate(), _frequency, reg, meter_data.rssi, meter_data.lqi, meter_data.reads_counter );
    mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

    return meter_data.ok;
}

void usage() {
    printf("usage: ./everblu_meters [frequency]\n");
    printf("set frequency between 432 and 435\n");
    printf("set frequency to 0 to start a scan\n");
    printf("examples:\n");
    printf("  everblu_meters 433.82\n");
    printf("  everblu_meters 0\n");
    clean_exit(1);
}

int main(int argc, char *argv[])
{
	char buff[MQTT_MSG_MAX_SIZE];
    float frequency;
    struct sigaction sa;

    if ( argc != 2 ) {
        usage();
    }
    frequency = atof(argv[1]);
    if ( frequency !=0 && (frequency < 432.0f || frequency > 435.0f) ) {
        printf("wrong frequency %.4f\n\n", frequency);
        usage();
    }
	sprintf(meter_id, "cyble-%02d-%07d-pi", METER_YEAR, METER_SERIAL);

    // SPI and LED init
	IO_init();

    // Check CC1101 with default frequency
	if ( !cc1101_init(0.0f, REG_DEFAULT, true) ) {
		printf("CC1101 Not found, check wiring!\n");
        clean_exit(1);
	}

    printf("CC1101 found OK!\n");
    printf("Base MQTT topic is now " MQTT_TOPIC "%s\n", meter_id);

    // Set up the structure to specify the exit action.
    sa.sa_handler = signal_handler;
    sa.sa_flags = SA_RESTART;
    sigfillset(&sa.sa_mask);
    sigaction (SIGTERM, &sa, NULL);
    sigaction (SIGINT, &sa, NULL); 

    // Init MQTT
	mosquitto_lib_init();
	mosq = mosquitto_new(NULL, true, NULL);
	if ( !mosq ) {
		fprintf(stderr, "ERROR: Create MQTT client failed..\n");
		mosquitto_lib_cleanup();
        clean_exit(1);
	}
	
	// Set callback functions
    // Enable this one to get MQTT logs
	//mosquitto_log_callback_set(mosq, my_log_callback);
	mosquitto_connect_callback_set(mosq, my_connect_callback);
	mosquitto_message_callback_set(mosq, my_message_callback);
	mosquitto_subscribe_callback_set(mosq, my_subscribe_callback);

	mosquitto_username_pw_set(mosq, MQTT_USER, MQTT_PASS);

	// Connect to MQTT server
	if ( mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, MQTT_KEEP_ALIVE) ) {
		fprintf(stderr, "MQTT : Unable to connect to MQTT broker.\n");
		clean_exit(1);
	}
	// Start a thread, and call mosquitto? Loop() continuously in the thread to process network information
	if ( mosquitto_loop_start(mosq) != MOSQ_ERR_SUCCESS ) {
		fprintf(stderr, "MQTT : Failed to create mosquitto loop\n");
		clean_exit(1);
	}

    printf("Connected to MQTT broker (almost)\n");

    // Scan all frequencies
    if ( frequency == 0.0f ) {
        // value for 433.82MHz is REG_DEFAULT => 0x10AF75
        uint32_t reg = REG_DEFAULT;
        uint32_t scanned = reg;
        uint32_t index = 0;
        r_min = reg + REG_SCAN_LOOP;
        r_max = reg - REG_SCAN_LOOP;
        f_min = 450; // Just to be sure out of bounds
        f_max = 400; // Just to be sure out of bounds

        // Step is 26000000 / 2^16 => 0,0004Mhz
        // so REG_SCAN_LOOP=128 => 0.05MHz step
        // so scan is from 433.77 to 433.87
        while ( index <= REG_SCAN_LOOP ) {
            scanned = REG_DEFAULT - index;
            test_frequency_register(scanned);
            // Avoid duplicate on 1st loop
            if ( index > 0 ) {
                scanned = REG_DEFAULT + index;
                test_frequency_register(scanned);
            }
            index++;
        }

        // Scan results
        sprintf(mqtt_topic, MQTT_TOPIC "%s/scan", meter_id);

        // Found frequency Range, setup in the middle
        if (r_min<(REG_DEFAULT+REG_SCAN_LOOP) && r_max>(REG_DEFAULT-REG_SCAN_LOOP)) {
            printf( "Working from %06X to %06X => ", r_min, r_max);
            reg = r_min + ((r_max - r_min) / 2);
            frequency = (26.0f/65536.0f) * (float) reg;
            f_min = (26.0f/65536.0f) * (float) r_min;
            f_max = (26.0f/65536.0f) * (float) r_max;
            printf( "%.4f to %.4f\n", f_min, f_max);
            printf( "Please use %.4f as frequency\n", frequency);
            sprintf(buff, "{ \"date\":\"%s\", \"frequency\":\"%.4f\", \"min\":\"%.4f\", \"max\":\"%.4f\" } ",  getDate(), frequency,f_min, f_max );
        } else {
            printf( "No working frequency found!\n");
            frequency = 0.0f;
            sprintf(buff, "{ \"date\":\"%s\", \"frequency\":\"%.1f\" } ",  getDate(), frequency );
        }
        mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

    } else {
        //time_t timestamp = time( NULL );
        char ts[64];
        sprintf(ts, "%s", getDate() );
        #ifdef LED_RED
        digitalWrite(LED_RED, HIGH);
        #endif
        printf( "Trying to query Cyble at %.4fMHz\n", frequency);
        cc1101_init(frequency, 0, false);
        meter_data = get_meter_data();
        #ifdef LED_RED
        digitalWrite(LED_RED, LOW);
        #endif

        // always publish date and timestmap
        sprintf(mqtt_topic, MQTT_TOPIC "%s/ts", meter_id);
        sprintf(buff, "%ld", time(NULL) );
        mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

        sprintf(mqtt_topic, MQTT_TOPIC "%s/date", meter_id);
        sprintf(buff, "%s",  getDate() );
        mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

        // Got datas ?
        if ( meter_data.ok ) {
            printMeterData(&meter_data, buff);
            sprintf(mqtt_topic, MQTT_TOPIC "%s/json", meter_id);
            mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

            sprintf(mqtt_topic, MQTT_TOPIC "%s/liters", meter_id);
            sprintf(buff, "%d", meter_data.liters );
            mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

            sprintf(mqtt_topic, MQTT_TOPIC "%s/battery", meter_id);
            sprintf(buff, "%d", meter_data.battery_left );
            mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

            sprintf(mqtt_topic, MQTT_TOPIC "%s/read", meter_id);
            sprintf(buff, "%d", meter_data.reads_counter );
            mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

            sprintf(mqtt_topic, MQTT_TOPIC "%s/rssi", meter_id);
            sprintf(buff, "%d", meter_data.rssi );
            mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

            sprintf(mqtt_topic, MQTT_TOPIC "%s/lqi", meter_id);
            sprintf(buff, "%d", meter_data.lqi );
            mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

        } else {

            printf("No data, are you in business hours?\n");
            sprintf(mqtt_topic, MQTT_TOPIC "%s/error", meter_id);
            sprintf(buff, "{ \"date\":\"%s\", \"type\":\"No Data\" }",  getDate() );
            mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);
        }
        // Let MQTT finish
        sleep(1);
    }

    clean_exit(0);
	return 0;
}
