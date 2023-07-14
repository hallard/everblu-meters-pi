 /*  the radian_trx SW shall not be distributed  nor used for commercial product*/
 /*  it is exposed just to demonstrate CC1101 capability to reader water meter indexes */

#define METER_YEAR              22
#define METER_SERIAL            828979

#define MQTT_HOST "192.168.1.8"
#define MQTT_PORT  1883
#define MQTT_USER ""
#define MQTT_PASS ""


#define MQTT_KEEP_ALIVE 300
#define MQTT_MSG_MAX_SIZE  512


#include <stdio.h>
#include <stdlib.h>
#include <mosquitto.h>
#include <string.h>

#include "everblu_meters.h"
#include "cc1101.c"

void my_message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{

	if(message->payloadlen){
		printf("%s %s", message->topic, (char *)message->payload);
	}else{
		//printf("%s (null)\n", message->topic);
	}
	fflush(stdout);
}

void my_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
	if(!result){
		/* Subscribe to broker information topics on successful connect. */
		mosquitto_subscribe(mosq, NULL, "WaterUsage ", 2);
	}else{
		fprintf(stderr, "Connect failed\n");
	}
}

void my_subscribe_callback(struct mosquitto *mosq, void *userdata, int mid, int qos_count, const int *granted_qos)
{
	int i;
	printf("Subscribed (mid: %d): %d", mid, granted_qos[0]);
	for(i=1; i<qos_count; i++){
		printf(", %d", granted_qos[i]);
	}
	printf("\n");
}

void my_log_callback(struct mosquitto *mosq, void *userdata, int level, const char *str)
{
	/* Pring all log messages regardless of level. */
	printf("%s\n", str);

}

void printMeterData(struct tmeter_data * data)
{
    printf("Liters  : %d\r\n", data->liters);
    printf("Battery : %d months\r\n ", data->battery_left);
    printf("Counter : %d\r\n", data->reads_counter);
    printf("Rzding  : from %d to %d\r\n ", data->time_start, data->time_end);
}


void IO_init(void)
{
	wiringPiSetup();
	pinMode (GDO2, INPUT);
	pinMode (GDO0, INPUT);           
}


int main(int argc, char *argv[])
{
	struct tmeter_data meter_data;
	struct mosquitto *mosq = NULL;
	char buff[MQTT_MSG_MAX_SIZE];
	char meter_id[12];
	char mqtt_topic[64];
	//float frequency = 0.0f; // Set to 0 to scan
	float frequency = 433.8200f; // Set your frequency
	float f_start = 0.0f;
    float f_end = 0.0f;

	sprintf(meter_id, "%i_%i", METER_YEAR, METER_SERIAL);

	mosquitto_lib_init();
	mosq = mosquitto_new(NULL, true, NULL);
	if(!mosq){
		fprintf(stderr, "ERROR: Create MQTT client failed..\n");
		mosquitto_lib_cleanup();
		return 1;
	}
	
	//Set callback functions
	mosquitto_log_callback_set(mosq, my_log_callback);
	mosquitto_connect_callback_set(mosq, my_connect_callback);
	mosquitto_message_callback_set(mosq, my_message_callback);
	mosquitto_subscribe_callback_set(mosq, my_subscribe_callback);

	mosquitto_username_pw_set(mosq, MQTT_USER, MQTT_PASS);

	//Connect to MQTT server
	if(mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, MQTT_KEEP_ALIVE)){
		fprintf(stderr, "ERROR: Unable to connect to MQTT broker.\n");
		return 1;
	}
	//Start a thread, and call mosquitto? Loop() continuously in the thread to process network information
	int loop = mosquitto_loop_start(mosq);
	if(loop != MOSQ_ERR_SUCCESS)
	{
		fprintf(stderr, "ERROR: failed to create mosquitto loop");
		return 1;
	}

	IO_init();

	if (!cc1101_init(433.8200f, true)) {
		printf("\r\nCC1101 Not found, check wiring!\r\n");
	} else {
		printf("\r\n");
		if (frequency == 0.0f) {
			// Scan al frequencies and blink Blue Led
			frequency = 433.8200f;
			float fdev = 0.0000f;
			while (frequency >= 433.7600f && frequency <= 433.8900f ) {
				frequency = 433.8200f + fdev;
				printf("Test frequency : %.4fMHz => ", frequency);
				fflush(stdout);
				cc1101_init(frequency, false);
				meter_data = get_meter_data();
				// Got datas ?
				if (meter_data.reads_counter != 0 || meter_data.liters != 0) {
					printMeterData(&meter_data);
				} else {
					printf("No answer\r\n");
				}

				frequency = 433.8200f - fdev;
				printf("Test frequency : %.4fMHz => ", frequency);
				fflush(stdout);
				cc1101_init(frequency, false);
				meter_data = get_meter_data();
				// Got datas ?
				if (meter_data.reads_counter != 0 || meter_data.liters != 0) {
					printMeterData(&meter_data);
				} else {
					printf("No answer\r\n");
				}
				fdev = fdev + 0.0005f;
			}

			// Found frequency Range, setup in the middle
			if (f_start || f_end) {
				printf( "Working from %.4fMHz to %.4fMhz\r\n", f_start, f_end);
				frequency = (f_end - f_start) / 2;
				frequency += f_start ;
			} else {
				printf("Not found a working Frequency!\r\n");
				frequency = 0.0f;
			}
		}

		if (frequency != 0.0f) {
			cc1101_init(frequency, true);
			meter_data = get_meter_data();
		
			// Got datas ?
			if (meter_data.reads_counter != 0 || meter_data.liters != 0) {
				printMeterData(&meter_data);
				sprintf(buff, "%d", meter_data.liters);
				sprintf(mqtt_topic, "homeassistant/sensor/cyblemeter_%s/state", meter_id);
				mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,false);
			}
		}
	}

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

	return 0;
}
