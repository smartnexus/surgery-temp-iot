/**
 * \file
 *         A Contiki application using the buttons
 * \author
 *        frarosrap, 
*/

/*---TODO--------------------------------------------------------------------*/

/*
  Ahora mismo, el callback de recepcion de paquetes envia una notificacion al 
  proceso principal para que represente los leds con los datos actualizados.

  Una alternativa podría ser que la representacion la hiciera el callback mismo,
  pero la variable nodo tendria que ser global.
*/

/*
  En caso de que no funcione el callback, se podría poner en un proceso a parte
  que no se bloquee ¿?¿?. Primero hay que probarlo
*/

/*
  Otra cosa que esta en el aire es el parseo, no se si es necesario usar atoi o no.
*/

//CAMBIOS:

/*
  El efecto del boton largo se haga antes de que se libere
*/

/*
  El cambio de estado tiene que realizarse unicamente si se el mensaje
*/

/*---Etiquetas---------------------------------------------------------------*/

// Parametros de radio

#define IEEE802154_CONF_PANID 0xaabb
#define IEEE802154_CONF_DEFAULT_CHANNEL 26

// Uso de logs

#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

// Parametros del servidor UDP

#define UDP_CLIENT_PORT	6000
#define UDP_SERVER_PORT	5050

// Definicion de pulsacion larga

#define TIMEOUT_BOTON 2
#define OFF 1
#define ON 2

/*---Importaciones-----------------------------------------------------------*/

#include <stdio.h> // printf()
//#include <stdlib.h> // atoi()
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
PROCESS(main_process, "main_process #1");
AUTOSTART_PROCESSES(&main_process);

/*---Variables-Globales------------------------------------------------------*/

/**
 * \brief Estado de cada uno de los nodos
 *        [OFF : no activo]
 *        [ON : activo]
 */  
static uint8_t state_nodo[2] = {OFF,OFF};

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
  uint8_t nodo_recibido;
  uint8_t valor_recibido;

  LOG_INFO("Direccion = ");
  LOG_INFO_6ADDR(sender_addr);
  LOG_INFO_("\n");
  LOG_INFO("El mensaje recibido es: '%.*s' \n", datalen, (char *) data);

  //Parseo de los datos recibidos
  sprintf(str_rx,"%s",(char *) data);
  nodo_recibido = str_rx[0];
  valor_recibido = str_rx[2];
  LOG_INFO("Nodo: %d Valor: %d \n", nodo_recibido,valor_recibido);

  //Cambio del valor del flag recibido
  state_nodo[nodo_recibido] = valor_recibido;

  //Notificacion al proceso principal para que represente el estado si procede
  process_poll(&main_process);

#if LLSEC802154_CONF_ENABLED
  LOG_INFO_(" LLSEC LV:%d \n", uipbuf_get_attr(UIPBUF_ATTR_LLSEC_LEVEL));
#endif
}

static void representacion_leds(uint8_t nodo, uint8_t valor){

// LED del nodo - LED2 RGB
  if (nodo == 0){
    rgb_led_off();
    rgb_led_set(RGB_LED_BLUE);
  }

  if (nodo == 1){
    rgb_led_off();
    rgb_led_set(RGB_LED_MAGENTA);

  }

// LED del flag - LED1 VERDE
  if (valor == OFF){
    leds_single_off(LEDS_LED1); //Verde
  }

  if (valor == ON){
    leds_single_on(LEDS_LED1); //Verde
  }

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
   *        [0 : nodo 1]
   *        [1 : nodo 2]
   */
  static uint8_t nodo = 0;

  /**
   * \brief Buffer de transmision
   */
  static char str_tx[6];

  /**
   * \brief Control sobre si ha ocurrido o no la pulsacion larga
   *        [0 : la pulsacion larga no ha ocurrido]
   *        [1 : la pulsacion larga ha ocurrido]
   */
  static uint8_t pulsacion_larga = 0;

  /* Comienzo del proceso */
  PROCESS_BEGIN();

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
                      UDP_SERVER_PORT, udp_rx_callback);  


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
    if ( ev==button_hal_periodic_event && pulsacion_larga==0){
      printf("+1 segundo pulsado \n");
      contador = contador + 1;
      printf("   Contador = %d \n",contador);

      /* Pulsacion larga */
      if (contador>TIMEOUT_BOTON || contador==TIMEOUT_BOTON){
        printf("   Pulsacion larga \n");
        pulsacion_larga = 1;
        
        //Cambio del nodo a representar
        if (nodo == 0){
          nodo = 1;
        }else if (nodo==1){
          nodo = 0;
        }

        //Representacion del nodo seleccionado
        //Representacion del estado del nodo seleccionado
        representacion_leds(nodo,state_nodo[nodo]); //Led del nodo y flag

        //Logs
        printf("Nodo actual: %d \n",nodo);
        printf("Nodo %d : %d \n",0, state_nodo[0]);
        printf("Nodo %d : %d \n",1, state_nodo[1]);

      }      

    }

    /* Evento de release */
    if ( ev==button_hal_release_event){
      printf("Liberacion de boton \n");      

      /* Pulsacion corta */
      if (contador<TIMEOUT_BOTON){
        printf("   Pulsacion corta \n");

        //Envio del estado modificado
        if(NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
                  
          //Cambio del valor del flag
          if (state_nodo[nodo] == OFF){
            state_nodo[nodo] = ON;
          }else if (state_nodo[nodo]==ON){
            state_nodo[nodo] = OFF;
          }

          //Representacion del nuevo estado del nodo seleccionado
          representacion_leds(nodo,state_nodo[nodo]); //Led del flag

          //Logs
          printf("Nodo actual: %d \n",nodo);
          printf("Nodo %d : %d \n",0, state_nodo[0]);
          printf("Nodo %d : %d \n",1, state_nodo[1]);

          /* Send to DAG root */
          LOG_INFO("Enviando el nuevo valor del nodo %d \n",nodo);
          snprintf(str_tx, sizeof(str_tx), "3>%d:%d",nodo,state_nodo[nodo]);
          LOG_INFO("   Enviando la cadena '%s' \n",str_tx);
          simple_udp_sendto(&udp_conn, str_tx, strlen(str_tx), &dest_ipaddr);

        } else {
          LOG_INFO("Not reachable yet\n");
        }

      }else{
        // Fin de la punsacion larga
        pulsacion_larga = 0;
      }
      // Reseteo del contador
      contador = 0;
    }

  }
  
  /* Fin del proceso */
  PROCESS_END();
}
