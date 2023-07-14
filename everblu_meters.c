 /*  the radian_trx SW shall not be distributed  nor used for commercial product*/
 /*  it is exposed just to demonstrate CC1101 capability to reader water meter indexes */

#define METER_YEAR      22
#define METER_SERIAL    828979

#define MQTT_HOST   "192.168.1.8"
#define MQTT_PORT   1883
#define MQTT_USER   ""
#define MQTT_PASS   ""
#define MQTT_TOPIC  "everblu/"
#define MQTT_KEEP_ALIVE     300
#define MQTT_MSG_MAX_SIZE   512


#include <stdio.h>
#include <stdlib.h>
#include <mosquitto.h>
#include <string.h>

#include "everblu_meters.h"
#include "cc1101.c"

struct tmeter_data meter_data;

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

void printMeterData(struct tmeter_data * data , char * json_data)
{
    time_t rawtime;
    struct tm * timeinfo;
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );

    printf("Consumption   : %d Liters\r\n", data->liters);
    printf("Battery left  : %d Months\r\n", data->battery_left);
    printf("Read counter  : %d times\r\n", data->reads_counter);
    printf("Working hours : from %02dH to %02dH\r\n", data->time_start, data->time_end);
    printf("Local Time    : %s\r\n", asctime(timeinfo));

    if (json_data) {
        sprintf(json_data, "{ \"liters\":%d, \"battery\":%d, \"read\":%d, \"hours\":\"%02d-%02d\", \"time\":\"%s\" }", 
                data->liters, data->battery_left, data->reads_counter, 
                data->time_start, data->time_end, asctime (timeinfo) );
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
    // Light green LED
	pinMode (LED_GRN, OUTPUT);
    digitalWrite(LED_GRN, HIGH);
    #endif
    #ifdef LED_BLU
	pinMode (LED_BLU, OUTPUT);
    digitalWrite(LED_BLU, LOW);
    #endif
}

bool scan_frequency(float _frequency) 
{
    bool ret = false;
    printf("Test frequency : %.4fMHz => ", _frequency);
    fflush(stdout);
    cc1101_init(_frequency, false);
    meter_data = get_meter_data();
    // Got datas ?
    if (meter_data.reads_counter != 0 || meter_data.liters != 0) {
        ret = true;
        printf("** OK **\r\n");
        //printMeterData(&meter_data, NULL);
    } else {
        printf("No answer\r\n");
    }
    return ret;
}

int main(int argc, char *argv[])
{
	struct mosquitto *mosq = NULL;
	char buff[MQTT_MSG_MAX_SIZE];
	char meter_id[32];
	char mqtt_topic[64];
	float frequency ;

    if ( argc != 2 ) {
        printf("usage: ./everblu_meters [frequency]\n");
        printf("./everblu_meters 433.82\n");
        printf("use 0 to start a frequency scan\n");
        printf("./everblu_meters 0\n");
        exit(1);
    }

    frequency = atof(argv[1]);
    if ( frequency !=0 && (frequency < 432.0f || frequency > 435.0f) ) {
        printf("wrong frequency %.4f\n", frequency);
        exit(1);
    }

	sprintf(meter_id, "cyble-%02d-%07d-pi", METER_YEAR, METER_SERIAL);
	IO_init();

	if (!cc1101_init(433.8200f, true)) {
		printf("CC1101 Not found, check wiring!\n");
        exit(1);
	}

    printf("CC1101 found OK!\n");

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

    printf("\r\n");
    if (frequency == 0.0f) {
        // Scan al frequencies and blink Blue Led
        frequency = 433.8200f;
        float fdev = 0.0000f;
        while (frequency >= 433.7600f && frequency <= 433.8900f ) {
            // Blink blue when scanning
            #ifdef LED_BLU
            digitalWrite(LED_BLU, HIGH);
            delay(250);
            digitalWrite(LED_BLU, LOW);
            #endif
            scan_frequency(433.8200f + fdev);
            scan_frequency(433.8200f - fdev);
            fdev = fdev + 0.0005f;
        }

    } else {

        #ifdef LED_RED
        digitalWrite(LED_RED, HIGH);
        #endif
        cc1101_init(frequency, false);
        meter_data = get_meter_data();
        #ifdef LED_RED
        digitalWrite(LED_RED, LOW);
        #endif
    
        // Got datas ?
        if (meter_data.reads_counter != 0 || meter_data.liters != 0) {
            printMeterData(&meter_data, buff);
            sprintf(mqtt_topic, MQTT_TOPIC "%s/json", meter_id);
            mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

            sprintf(mqtt_topic, MQTT_TOPIC "%s/liters", meter_id);
            sprintf(buff, "%d", meter_data.liters );
            mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

            sprintf(mqtt_topic, MQTT_TOPIC "%s/battery", meter_id);
            sprintf(buff, "%d", meter_data.battery_left );
            mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

            sprintf(mqtt_topic, MQTT_TOPIC "%s/counter", meter_id);
            sprintf(buff, "%d", meter_data.reads_counter );
            mosquitto_publish(mosq, NULL, mqtt_topic, strlen(buff),buff,0,true);

            delay(1000);
        }
    }

	mosquitto_destroy(mosq);
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

	return 0;
}
