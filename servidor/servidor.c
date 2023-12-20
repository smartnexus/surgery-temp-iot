#define IEEE802154_CONF_PANID 0xaabb
#define IEEE802154_CONF_DEFAULT_CHANNEL 26

#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "dev/leds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#include "com.h"

void push_mqtt(char* data, uint8_t datatype, uint8_t nodeid);

static struct simple_udp_connection connections[2];
int ports[2] = {SENSOR_PORT, REMOTO_PORT};

int8_t pending_update = 0;
int flags[2] = {0,0};

/*---------------------------------------------------------------------------*/
PROCESS(udp_server_process, "UDP server");
AUTOSTART_PROCESSES(&udp_server_process);
/*---------------------------------------------------------------------------*/
void push_mqtt(char* data, uint8_t datatype, uint8_t nodeid) {
   LOG_INFO("   --> Mandando datos por puerto serie para lectura MQTT.\n");
   LOG_INFO("   --> %s - %d - %d <\n", data, datatype, nodeid);
}
/*---------------------------------------------------------------------------*/
static void udp_rx_callback(struct simple_udp_connection *c, const uip_ipaddr_t *sender_addr, uint16_t sender_port, const uip_ipaddr_t *receiver_addr, uint16_t receiver_port, const uint8_t *data, uint16_t datalen) {
   leds_single_on(LEDS_LED2);
   printf("\n");
   LOG_INFO("[*] Mensaje recibido: ('%.*s')\n", datalen, (char *) data);
   LOG_INFO("[*] Direccion origen (");
   LOG_INFO_6ADDR(sender_addr);
   LOG_INFO_(")\n");

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
         static char info[2];
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
      flags[SENSOR_ID] = atoi(content);
      LOG_INFO("   --> programando actualizacion del flag del REMOTO: %d:%d\n", flags[SERVER_ID], flags[SENSOR_ID]);
      break;
   case 3:
      LOG_INFO("   --> <msg=3> recibido conmutacion estado de flag de REMOTO: %s\n", content);
      // Push new value from REMOTE to MQTT server
      char* body = content;
      push_mqtt(&body[2], FLAG, atoi(&body[0]));
      // Send to SENSOR new flag value
      char* toSend = body+2;
      pending_update = atoi(toSend);
      LOG_INFO("   --> programando actualizacion del flag del SENSOR: %s\n", toSend);
      break;
   case 4:
      LOG_INFO("   --> <msg=4> recibido peticion de estado de alarmas de REMOTO: %s\n", content);
      static char info[4];
      snprintf(info, sizeof(info), "%d:%d", flags[SERVER_ID], flags[SENSOR_ID]);
      simple_udp_sendto(&connections[1], info, strlen(info), sender_addr);
      LOG_INFO("   --> Enviando actualizacion pendiente de flag al REMOTO: %s\n", info);
      for (int i = 0; i < 2; i++) {
         flags[i] = 0;
      }
      
      break;
   default:
      LOG_INFO("   --> No se ha reconocido el codigo del mensaje recibido.\n");
      break;
   }
   leds_single_off(LEDS_LED2);
}
/*---------------------------------------------------------------------------*/
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