#include "ets_sys.h"
#include "driver/uart.h"
#include "osapi.h"
#include "mqtt.h"
#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "driver/dht22.h"

#define user_procTaskPrio        0
#define user_procTaskQueueLen    1
os_event_t    user_procTaskQueue[user_procTaskQueueLen];
static void user_procTask(os_event_t *events);
static volatile os_timer_t some_timer;
LOCAL int  timer_count = 0;
MQTT_Client mqttClient;

void wifiConnectCb(uint8_t status)
{
	if(status == STATION_GOT_IP){
		MQTT_Connect(&mqttClient);
	} else {
		MQTT_Disconnect(&mqttClient);
	}
}

void mqttConnectedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Connected\r\n");
	MQTT_Subscribe(client, "/dht/cmd", 0);
}

void mqttDisconnectedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Disconnected\r\n");
}

void mqttPublishedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Published\r\n");
}

void mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
	char *topicBuf = (char*)os_zalloc(topic_len+1),
			*dataBuf = (char*)os_zalloc(data_len+1);

	MQTT_Client* client = (MQTT_Client*)args;

	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;

	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;

	INFO("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);
	os_free(topicBuf);
	os_free(dataBuf);

	struct dht_sensor_data* r = DHTRead();
	float lastTemp = r->temperature;
	float lastHum = r->humidity;
	char str_temp[64], str_hum[64];
	char str_url[256];

	if (r->success) {
		// Send temperature and humidity data to MQTT broker
		//INFO("Temperature: %d.%d *C, Humidity: %d.%d %%\r\n", (int)(lastTemp),(int)((lastTemp - (int)lastTemp)*100), (int)(lastHum),(int)((lastHum - (int)lastHum)*100));
		os_sprintf(str_temp, "%d.%d", (int) (lastTemp), (int) ((lastTemp - (int) lastTemp) * 100));
		MQTT_Publish(&mqttClient, "/dht/temperature", str_temp, os_strlen(str_temp), 0, 0);

		os_sprintf(str_hum, "%d.%d", (int) (lastHum),(int) ((lastHum - (int) lastHum) * 100));
		MQTT_Publish(&mqttClient, "/dht/humidity", str_hum, os_strlen(str_hum), 0, 0);

		// Send temperature and humidity data to Thingspeak.com
		//os_sprintf(str_url,"http://api.thingspeak.com/update?key=%s&field2=%s&field3=%s",YOUR_THINGSPEAK_API_KEY,str_temp,str_hum);
		//http_post(str_url,"",http_post_callback);
	} else {
		os_sprintf("Error reading temperature and humidity\r\n");
	}


	//MQTT_Publish(client, "/dht/cmd/resp", "test", 4, 0, 0);
}


void ICACHE_FLASH_ATTR some_timerfunc(void *arg)
{
	struct dht_sensor_data* r = DHTRead();
	float lastTemp = r->temperature;
	float lastHum = r->humidity;
	char str_temp[64],str_hum[64];
	char str_url[256];
	if (timer_count==10)
		{
			if(r->success)
			{
				// Send temperature and humidity data to MQTT broker
				//INFO("Temperature: %d.%d *C, Humidity: %d.%d %%\r\n", (int)(lastTemp),(int)((lastTemp - (int)lastTemp)*100), (int)(lastHum),(int)((lastHum - (int)lastHum)*100));
				os_sprintf(str_temp, "%d.%d", (int) (lastTemp), (int) ((lastTemp - (int) lastTemp) * 100));
				MQTT_Publish(&mqttClient, "/dht/temperature", str_temp, os_strlen(str_temp), 0, 0);

				os_sprintf(str_hum, "%d.%d", (int) (lastHum),(int) ((lastHum - (int) lastHum) * 100));
				MQTT_Publish(&mqttClient, "/dht/humidity", str_hum, os_strlen(str_hum), 0, 0);

				// Send temperature and humidity data to Thingspeak.com
				//os_sprintf(str_url,"http://api.thingspeak.com/update?key=%s&field2=%s&field3=%s",YOUR_THINGSPEAK_API_KEY,str_temp,str_hum);
				//http_post(str_url,"",http_post_callback);
			}
			else
			{
				os_sprintf("Error reading temperature and humidity\r\n");
			}
			timer_count = 1;
		}

	timer_count++;
}



//Init function 
void ICACHE_FLASH_ATTR
user_init()
{
	uart_div_modify( 0, UART_CLK_FREQ / ( 115200 ) );
	os_delay_us(1000000);
    // Initialize the GPIO subsystem.
    gpio_init();
    CFG_Load();

    MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	//MQTT_InitConnection(&mqttClient, "192.168.11.122", 1880, 0);

	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, sysCfg.mqtt_keepalive, 1);
	//MQTT_InitClient(&mqttClient, "client_id", "user", "pass", 120, 1);

	MQTT_InitLWT(&mqttClient, "/lwt", "offline", 0, 0);
	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnPublished(&mqttClient, mqttPublishedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);



    DHTInit(DHT11, 2000);

    //Disarm timer
    os_timer_disarm(&some_timer);
    //Setup timer
    os_timer_setfn(&some_timer, (os_timer_func_t *)some_timerfunc, NULL);
    //Arm the timer
    //&some_timer is the pointer
    //1000 is the fire time in ms
    //0 for once and 1 for repeating
    os_timer_arm(&some_timer, 1000, 1);
    
}
