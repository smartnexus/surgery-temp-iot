/**
 * \file
 *         A Contiki application using the buttons
 * \author
 *        
*/

/*---TODO--------------------------------------------------------------------*/

/*---Etiquetas---------------------------------------------------------------*/

/*---Importaciones-----------------------------------------------------------*/

// Etiquetas
#include "protocol.h"
#include "sensors.h"

#include <stdio.h> // printf()
#include <stdlib.h> // atoi()
//#include <string.h>
#include "contiki.h"
#include "os/sys/process.h" // PROCESS_EVENT_POLL
#include "os/dev/button-hal.h" // button events
#include "os/dev/leds.h" // leds
#include "dev/etc/rgb-led/rgb-led.h" // led-rgb
#include "os/sys/log.h" //Log()

// Red
#include "os/net/netstack.h"
#include "os/net/routing/routing.h"
#include "os/net/ipv6/simple-udp.h"

/*---Procesos----------------------------------------------------------------*/
PROCESS(main_process, "main_process");
PROCESS(keepalive_process, "keepalive_process");
AUTOSTART_PROCESSES(&main_process,&keepalive_process);

/*---Variables-Globales------------------------------------------------------*/

/**
 * \brief Estado de cada uno de los nodos
 *        [FLAG_OFF : no activo]
 *        [FLAG_ON : activo]
 */  
static uint8_t state_nodo[2] = {FLAG_OFF,FLAG_OFF};

static struct simple_udp_connection udp_conn;

static uip_ipaddr_t dest_ipaddr;

/*---Funciones---------------------------------------------------------------*/
static void
udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  
  char str_rx[4]; //Buffer de recepcion
  uint8_t state_recibido[2];

  printf("\n");
  LOG_INFO("Direccion = ");
  LOG_INFO_6ADDR(sender_addr);
  LOG_INFO_("\n");
  LOG_INFO("El mensaje recibido es: '%.*s' \n", datalen, (char *) data);

  //Parseo de los datos recibidos
  sprintf(str_rx,"%s",(char *) data);
  state_recibido[SERVER_ID] = atoi(&str_rx[0]);
  state_recibido[SENSOSR_ID] = atoi(&str_rx[2]);
  LOG_INFO("Server: %d Sensor: %d \n\n", state_recibido[SERVER_ID],state_recibido[SENSOR_ID]);

  //Cambio del valor del flag recibido
  if (state_recibido[SERVER_ID]!=0){
    state_nodo[SERVER_ID] = state_recibido[SERVER_ID];
  }
  if (state_recibido[SENSOR_ID]!=0){
    state_nodo[SENSOR_ID] = state_recibido[SENSOR_ID];
  }

  //Notificacion al proceso principal para que represente el estado si procede
  process_poll(&main_process);

#if LLSEC802154_CONF_ENABLED
  LOG_INFO_(" LLSEC LV:%d \n", uipbuf_get_attr(UIPBUF_ATTR_LLSEC_LEVEL));
#endif
}

static void representacion_leds(uint8_t nodo, uint8_t valor){

// LED del nodo - LED2 RGB
  if (nodo == SERVER_ID){
    rgb_led_off();
    rgb_led_set(RGB_LED_BLUE);
  }

  if (nodo == SENSOR_ID){
    rgb_led_off();
    rgb_led_set(RGB_LED_MAGENTA);

  }

// LED del flag - LED1 VERDE
  if (valor == FLAG_OFF){
    leds_single_off(LEDS_LED1); //Verde
  }

  if (valor == FLAG_ON){
    leds_single_on(LEDS_LED1); //Verde
  }

}

/*---keepalive_process-------------------------------------------------------*/
PROCESS_THREAD(keepalive_process, ev, data)
{
  static struct etimer periodic_timer;
  static char str_tx[3];
  uip_ipaddr_t dest_ipaddr;

  PROCESS_BEGIN();

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, REMOTO_PORT, NULL,
                      SERVIDOR_PORT, udp_rx_callback);

  etimer_set(&periodic_timer, CLOCK_SECOND * 10);

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));

    if(NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {

      /* Send to DAG root */
      printf("\n");
      LOG_INFO("Enviando KeepAlive \n");
      snprintf(str_tx, sizeof(str_tx), "4>");
      LOG_INFO("   Enviando la cadena '%s' \n\n",str_tx);
      simple_udp_sendto(&udp_conn, str_tx, strlen(str_tx), &dest_ipaddr);

    } else {
      LOG_INFO("KeepAlive > Not reachable yet\n");
    }
    etimer_reset(&periodic_timer);
  }

  PROCESS_END();
}


/*---main_process------------------------------------------------------------*/
PROCESS_THREAD(main_process, ev, data){ 

  /* Definicion de variables */

  /**
   * \brief Numero de segundos que el boton esta pulsado
   */
  static uint8_t contador = 0;

  /**
   * \brief Nodo a representar
   *        [SERVER_ID : nodo 1]
   *        [SENSOR_ID : nodo 2]
   */
  static uint8_t nodo = SERVER_ID;

  /**
   * \brief Buffer de transmision
   */
  static char str_tx[6];

  /**
   * \brief Control sobre si ha ocurrido o no la pulsacion larga
   *        [FALSE : la pulsacion larga no ha ocurrido]
   *        [TRUE : la pulsacion larga ha ocurrido]
   */
  static uint8_t pulsacion_larga = FALSE;

  /* Comienzo del proceso */
  PROCESS_BEGIN();

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, REMOTO_PORT, NULL,
                      SERVIDOR_PORT, udp_rx_callback);  


  // Representacion inicial
  representacion_leds(nodo,state_nodo[nodo]);

  while(1){

    /* Espera de un evento */
    /* Los eventos que se esperan son:
        - button_hal_press_event: Upon press of the button
        - button_hal_release_event: Upon release of the button
        - button_hal_periodic_event: Generated every second that 
              the user button is kept pressed.
    */    
    PROCESS_WAIT_EVENT_UNTIL( ev==button_hal_press_event || 
                              ev==button_hal_periodic_event || 
                              ev==button_hal_release_event ||
                              ev==PROCESS_EVENT_POLL);

    /* Evento de notificacion*/
    if( ev==PROCESS_EVENT_POLL ){
      //Representacion del nuevo estado del nodo seleccionado
      representacion_leds(nodo,state_nodo[nodo]); //Led del flag
    }

    /* Evento de pulsacion */
    if ( ev==button_hal_press_event){
      printf("Pulsación de boton \n");
    }

    /* Evento de boton mantenido */
    // Si el tiempo para la pulsacion larga se ha alcanzado, ya no es 
    // necesario ejecutar de nuevo este bloque
    if ( ev==button_hal_periodic_event && pulsacion_larga==FALSE){
      printf("   +1 segundo pulsado \n");
      contador = contador + 1;
      printf("   Contador = %d \n",contador);

      /* Pulsacion larga */
      if (contador>TIMEOUT_BOTON || contador==TIMEOUT_BOTON){
        printf("   Pulsacion larga \n");
        pulsacion_larga = TRUE;
        
        //Cambio del nodo a representar
        if (nodo == SERVER_ID){
          nodo = SENSOR_ID;
        }else if (nodo==SENSOR_ID){
          nodo = SERVER_ID;
        }

        //Representacion del nodo seleccionado
        //Representacion del estado del nodo seleccionado
        representacion_leds(nodo,state_nodo[nodo]); //Led del nodo y flag

        //Logs
        printf("------ \n");
        printf("Nodo visible: %d \n",nodo);
        printf("Nodo %d : %d \n",SERVER_ID, state_nodo[SERVER_ID]);
        printf("Nodo %d : %d \n",SENSOR_ID, state_nodo[SENSOR_ID]);
        printf("------ \n");

      }      
      printf("\n");
    }

    /* Evento de release */
    if ( ev==button_hal_release_event){
      printf("\n");
      printf("Liberacion de boton \n");      

      /* Pulsacion corta */
      if (contador<TIMEOUT_BOTON){
        printf("   Pulsacion corta \n");

        //Envio del estado modificado
        if(NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
                  
          //Cambio del valor del flag
          if (state_nodo[nodo] == FLAG_OFF){
            state_nodo[nodo] = FLAG_ON;
          }else if (state_nodo[nodo]==FLAG_ON){
            state_nodo[nodo] = FLAG_OFF;
          }

          //Representacion del nuevo estado del nodo seleccionado
          representacion_leds(nodo,state_nodo[nodo]); //Led del flag

          //Logs
          printf("------ \n");
          printf("Nodo visible: %d \n",nodo);
          printf("Nodo %d : %d \n",SERVER_ID, state_nodo[SERVER_ID]);
          printf("Nodo %d : %d \n",SENSOR_ID, state_nodo[SENSOR_ID]);
          printf("------ \n");

          /* Send to DAG root */
          printf("\n");
          LOG_INFO("Enviando el nuevo valor del nodo %d \n",nodo);
          snprintf(str_tx, sizeof(str_tx), "3>%d:%d",nodo,state_nodo[nodo]);
          LOG_INFO("   Enviando la cadena '%s' \n\n",str_tx);
          simple_udp_sendto(&udp_conn, str_tx, strlen(str_tx), &dest_ipaddr);

        } else {
          LOG_INFO("StateChange> Not reachable yet\n");
        }

      }else{
        // Fin de la punsacion larga
        pulsacion_larga = FALSE;
      }
      // Reseteo del contador
      contador = 0;
    }

  }
  
  /* Fin del proceso */
  PROCESS_END();
}
