#include "../common/protocol.h"

#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"

#include "dev/leds.h"
#include "dev/etc/rgb-led/rgb-led.h"

#include "dev/button-hal.h"

#include "lib/sensors.h"
#include "common/temperature-sensor.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#include "../common/sensors.h"
/*-----------------------------SERVIDOR------------------------------------*/
void push_mqtt(char* data, uint8_t datatype, uint8_t nodeid);
void convert_f(char* value, char* dest);

static struct simple_udp_connection connections[2];
int ports[2] = {SENSOR_PORT, REMOTO_PORT};

int8_t pending_update = 0;
uint8_t flags[2] = {FLAG_DEFAULT,FLAG_DEFAULT};

static char field[4];
/*------------------------------SENSOR------------------------------------*/
static int flag = FLAG_OFF;
static int estado_boton = 0;
static int16_t int_temp_c;
static int16_t frac_temp_c;
static int16_t int_temp_f;
static int16_t frac_temp_f;
static float aux1;
/*---------------------------------------------------------------------------*/
PROCESS(udp_server_process, "UDP server");
PROCESS(temp_alarm, "Lectura del sensor de temperatura -> envio al servidor + threshold -> alarma");
PROCESS(led_blink, "Parpadeo del LED y apagar alarma");
PROCESS(button_press, "Boton");
AUTOSTART_PROCESSES(&udp_server_process, &temp_alarm, &led_blink, &button_press);
/*---------------------------------------------------------------------------*/
void convert_f(char* value, char* dest) {
   char* beg = value;
   beg[2] = '\0';
   int_temp_c = atoi(beg);
   frac_temp_c = atoi(beg+3);

   int_temp_f = 0;
   frac_temp_f = 0;

   /* Conversion de la temperatura celsius a fahrenheit */
   aux1 = (float)frac_temp_c / (float)50;
   //printf("%d",(uint16_t)aux1);
   int_temp_f = 2*int_temp_c + 32 + (uint16_t)aux1;
   frac_temp_f =  (uint16_t) 100*(aux1 - (float)(uint16_t)aux1);

   sprintf(dest, "%d.%d", int_temp_f, frac_temp_f);
}
void push_mqtt(char* data, uint8_t datatype, uint8_t nodeid) {
   LOG_INFO("[*] Mensaje enviado para MQTT EXPORTER\n");
   LOG_INFO("   --> %s - %d - %d <\n", data, datatype, nodeid);
   // Using character "#" to differ between logs and communication protocol
   switch(nodeid) {
   case SENSOR_ID:
      if (datatype == FLAG) {
         printf("[#] %s;%d\n", data, FLAG_SENSOR);
      } else if(datatype == TEMPERATURE) {
         printf("[#] %s;%d\n", data, TEMPERATURE_C_SENSOR);
         convert_f(data, field);
         printf("[#] %s;%d\n", field, TEMPERATURE_F_SENSOR);
      }
      break;
   case SERVER_ID:
      if (datatype == FLAG) {
         printf("[#] %s;%d\n", data, FLAG_SERVER);
      } else if(datatype == TEMPERATURE) {
         printf("[#] %s;%d\n", data, TEMPERATURE_C_SERVER);
         convert_f(data, field);
         printf("[#] %s;%d\n", field, TEMPERATURE_F_SERVER);
      }
      break;
   default:
      printf("[#] %s;%d\n", data, -1);
      break;
   }
}
static void udp_rx_callback(struct simple_udp_connection *c, const uip_ipaddr_t *sender_addr, uint16_t sender_port, const uip_ipaddr_t *receiver_addr, uint16_t receiver_port, const uint8_t *data, uint16_t datalen) {
   LOG_INFO("\n");
   LOG_INFO("[*] Mensaje recibido: ('%.*s')\n", datalen, (char *) data);

   char* mensaje = (char *) data;
   snprintf(mensaje, datalen+1, "%s", (char *) data);
   int message_code = atoi(&mensaje[0]);
   char* content = mensaje+2;

   switch (message_code) {
   case 1:
      LOG_INFO("   --> <msg=1> recibida medida de temperatura: %s\n", content);
      push_mqtt(content, TEMPERATURE, SENSOR_ID);
      // Send pending flag updates to SENSOR
      if(pending_update != 0) {
         char info[2];
         snprintf(info, sizeof(info), "%d", pending_update);
         simple_udp_sendto(&connections[0], info, strlen(info), sender_addr);
         LOG_INFO("   --> Valor recuperado de la variable global: %d\n", pending_update);
         pending_update = 0;
         LOG_INFO("   --> Enviando actualizacion pendiente de flag al SENSOR: %s\n", info);
      } else {
         LOG_INFO("   --> Saltando envio de actualizacion pendiente al SENSOR \n");
      }
      break;
   case 2:
      LOG_INFO("   --> <msg=2> recibido conmutacion estado de flag de SENSOR: %s\n", content);
      // Push new value from REMOTE to MQTT server
      push_mqtt(content, FLAG, SENSOR_ID);
      // Send to REMOTO new flag value
      flags[SENSOR_ID] = (uint8_t) atoi(content);
      LOG_INFO("   --> programando actualizacion del flag del REMOTO: %d:%d\n", flags[SERVER_ID], flags[SENSOR_ID]);
      break;
   case 3: ;
      LOG_INFO("   --> <msg=3> recibido conmutacion estado de flag de REMOTO: %s\n", content);
      // Push new value from REMOTE to MQTT server
      char* cuerpo = content;
      uint8_t nodeID = atoi(&content[0]);
      // Send to SENSOR new flag value
      char* toSend = cuerpo+2;
      push_mqtt(&content[2], FLAG, nodeID);
      switch (nodeID) {
      case SERVER_ID:
         flag = atoi(toSend);
         LOG_INFO("   --> programando actualizacion del flag del SERVIDOR: %s\n", toSend);
         break;
      case SENSOR_ID:
         pending_update = atoi(toSend);
         LOG_INFO("   --> programando actualizacion del flag del SENSOR: %s\n", toSend);
         break;
      default:
         break;
      }
      break;
   case 4: ;
      LOG_INFO("   --> <msg=4> recibido peticion de estado de alarmas de REMOTO: %s\n", content);
      char info[4];
      snprintf(info, sizeof(info), "%d:%d", flags[SERVER_ID], flags[SENSOR_ID]);
      simple_udp_sendto(&connections[1], info, strlen(info), sender_addr);
      LOG_INFO("   --> Enviando actualizacion pendiente de flag al REMOTO: %s\n", info);
      for (int i = 0; i < 2; i++) {
         flags[i] = FLAG_DEFAULT;
      }
      break;
   default:
      LOG_INFO("   --> No se ha reconocido el codigo del mensaje recibido.\n");
      break;
   }
}
PROCESS_THREAD(udp_server_process, ev, data) {
   static struct etimer periodic_timer;
   PROCESS_BEGIN();

   /* Timeout de arranque inicial */
   etimer_set(&periodic_timer, 10 * CLOCK_SECOND);
   PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
   LOG_INFO("[*] Iniciando el programa...\n");

   /* Initialize DAG root */
   NETSTACK_ROUTING.root_start();

   /* Initialize UDP connections */
   for (uint8_t i = 0; i < 2; i++) {
      simple_udp_register(&connections[i], SERVIDOR_PORT, NULL, ports[i], udp_rx_callback);
   }
   LOG_INFO("[*] Conexiones UDP registradas con sensor y remoto.\n");

   etimer_set(&periodic_timer, 0.5 * CLOCK_SECOND);
   while (true) {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
      leds_single_toggle(LEDS_LED1);
      etimer_reset(&periodic_timer);
   }
   
   PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(temp_alarm, ev, data) {
   static struct etimer periodic_timer;
   static int16_t raw_tmp = 0;
   static int16_t int_tmp_c = 0;
   static int16_t frac_tmp_c = 0;

   static char str[32];

   PROCESS_BEGIN();

   etimer_set(&periodic_timer, CLOCK_SECOND * 10);
   while(1) {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
      SENSORS_ACTIVATE(temperature_sensor);

      raw_tmp = (int16_t) temperature_sensor.value(0);
      int_tmp_c = raw_tmp >> 2;                  // realiza un desplazamiento de 2 bits a la derecha
      frac_tmp_c = (raw_tmp & 0x3)*25;           // se calcula tomando los 2 bits menos significativos

      snprintf(str, sizeof(str), "%d.%d", int_tmp_c, frac_tmp_c);
      push_mqtt(str, TEMPERATURE, SERVER_ID);
    
      LOG_INFO("Se comprueba el flag -> '%d'.\n", flag);
      if (flag == FLAG_ON && (int_tmp_c < UMBRAL_BOTTOM || int_tmp_c > UMBRAL_TOP)){
         flag = FLAG_OFF;
         process_poll(&led_blink);     
      }
      
      
      SENSORS_DEACTIVATE(temperature_sensor);
      etimer_reset(&periodic_timer);
   }

  PROCESS_END();
}
PROCESS_THREAD(led_blink, ev, data) {
   static struct etimer led_timer;
   static char str_button[32];

   PROCESS_BEGIN();

   etimer_set(&led_timer, CLOCK_SECOND * 1);

   while(1) {
      PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
      LOG_INFO("Ha saltado la alarma\n");
  

      while (estado_boton == 0){
         rgb_led_set(RGB_LED_RED);
      
         etimer_reset(&led_timer);
         PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&led_timer));

         rgb_led_off();

         etimer_reset(&led_timer);
         PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&led_timer));
      }

      // Nos aseguramos de apagar el LED
      LOG_INFO("LED1 off\n");
      rgb_led_off();
      estado_boton = 0;

      flags[SERVER_ID] = (uint8_t) flag;
      printf("Flag servidor actualizada 'desarmada': %d \n",flags[SERVER_ID]);
      
      snprintf(str_button, sizeof(str_button), "%d", flags[SERVER_ID]);
      push_mqtt(str_button, FLAG, SERVER_ID);
   }   
   PROCESS_END();
}
PROCESS_THREAD(button_press, ev, data) {
   PROCESS_BEGIN();
   while (1) {
      PROCESS_WAIT_EVENT_UNTIL(ev == button_hal_press_event);
      estado_boton = 1;
      LOG_INFO("Se ha apagado la alarma (boton)\r\n");
   }
   PROCESS_END();
}
