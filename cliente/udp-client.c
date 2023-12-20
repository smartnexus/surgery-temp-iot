#define IEEE802154_CONF_PANID 0xaabb
#define IEEE802154_CONF_DEFAULT_CHANNEL 26

#include "contiki.h"
#include "net/routing/routing.h"
#include "random.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "dev/button-hal.h"
#include "os/sys/process.h"

#include "lib/sensors.h"
#include "common/temperature-sensor.h"

#include "dev/leds.h"
#include "dev/etc/rgb-led/rgb-led.h"

#include <stdlib.h>
#include <stdio.h>

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define WITH_SERVER_REPLY  1
#define UDP_CLIENT_PORT	5000
#define UDP_SERVER_PORT	5050

#define UMBRAL_TOP 24
#define UMBRAL_BOTTOM 20

#define FLAG_ON 2
#define FLAG_OFF 1

static struct simple_udp_connection udp_conn;

/*---------------------------------------------------------------------------*/
static int flag = FLAG_OFF;
static int estado_boton = 0;

PROCESS(temp_alarm, "Lectura del sensor de temperatura -> envio al servidor + threshold -> alarma");
PROCESS(led_blink, "Parpadeo del LED y apagar alarma");
PROCESS(button_press, "Boton");
AUTOSTART_PROCESSES(&temp_alarm, &led_blink, &button_press);
/*---------------------------------------------------------------------------*/
static void
udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  
  LOG_INFO("Flag RX por el servidor -> '%.*s'.", datalen, (char *) data);
  LOG_INFO_("\n");

  char * msj_rx = (char *) data;
  flag = atoi(msj_rx);
  LOG_INFO("Almacenado el flag -> '%d'.", flag);
  LOG_INFO_("\n");

}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(temp_alarm, ev, data)
{
  static struct etimer periodic_timer;
  static int16_t raw_tmp = 0;
  static int16_t int_tmp_c = 0;
  static int16_t frac_tmp_c = 0;

  static char str[32];
  uip_ipaddr_t dest_ipaddr;

  PROCESS_BEGIN();

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
                      UDP_SERVER_PORT, udp_rx_callback);

  etimer_set(&periodic_timer, CLOCK_SECOND * 10);
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    SENSORS_ACTIVATE(temperature_sensor);

    raw_tmp = (int16_t)temperature_sensor.value(0);
    int_tmp_c = raw_tmp >> 2;                  // realiza un desplazamiento de 2 bits a la derecha
    frac_tmp_c = (raw_tmp & 0x3)*25;           // se calcula tomando los 2 bits menos significativos

    if(NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
      /* Send to DAG root */
      LOG_INFO("Enviando temperatura al servidor = %d.%d", int_tmp_c, frac_tmp_c);
      LOG_INFO_("\n");
      snprintf(str, sizeof(str), "1>%d.%d", int_tmp_c, frac_tmp_c);
      simple_udp_sendto(&udp_conn, str, strlen(str), &dest_ipaddr);
    } else {
      LOG_INFO("Not reachable yet\n");
    }

    LOG_INFO("Se comprueba el flag -> '%d'.", flag);
    LOG_INFO_("\n");
    if (flag == FLAG_ON && (int_tmp_c < UMBRAL_BOTTOM || int_tmp_c > UMBRAL_TOP)){
      flag = FLAG_OFF;
      process_poll(&led_blink);
      
    }

    SENSORS_DEACTIVATE(temperature_sensor);
    etimer_reset(&periodic_timer);
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(led_blink, ev, data)
{
  static struct etimer led_timer;
  static struct etimer alarm_timer;

  static char str[32];
  uip_ipaddr_t dest_ipaddr;

  uint16_t estado_alarma_tx = 0;

  PROCESS_BEGIN();
  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
                      UDP_SERVER_PORT, udp_rx_callback);

  etimer_set(&led_timer, CLOCK_SECOND * 1);
  etimer_set(&alarm_timer, CLOCK_SECOND * 3);

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

    while(estado_alarma_tx == 0) {
      // Enviar al servidor el nuevo estado
      if(NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
        /* Send to DAG root */
        LOG_INFO("Avisando al servidor que se ha apagado la alarma");
        LOG_INFO_("\n");
        snprintf(str, sizeof(str), "2>%d", flag);
        simple_udp_sendto(&udp_conn, str, strlen(str), &dest_ipaddr);
        estado_alarma_tx = 1;
      } else {
        LOG_INFO("Not reachable yet\n");
        // Para asegurar que se envia el flag actualizado al servidor
        etimer_reset(&alarm_timer);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&alarm_timer));
      }
    }

  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(button_press, ev, data)
{

  PROCESS_BEGIN();

  while (1)
  {
    PROCESS_WAIT_EVENT_UNTIL(ev == button_hal_press_event);

    estado_boton = 1;

    LOG_INFO("Se ha apagado la alarma (boton)\r\n");
  }

  PROCESS_END();
}
