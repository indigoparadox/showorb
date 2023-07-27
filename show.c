
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <termios.h>
#include <string.h>
#include <stdlib.h>
#include <mosquitto.h>

#define LCD_STR_SZ 255

int ser_fd = -1;

char sub_topics[][255] = {
   "rtl_433/weathersdr/devices/Fineoffset-WH24/42/temperature_C",
   "rtl_433/weathersdr/devices/Fineoffset-WH24/42/humidity",
   "home_assistant/sensors/rain_in_per_hour",
   "home_assistant/sensors/south_albany_pm25_ug_m3",
   ""
};

float sub_vals[] = {
   0,
   0,
   0,
   0
};

#define write_check( str, sz ) \
   if( sz != write( ser_fd, str, sz ) ) { \
      fprintf( stderr, "error writing to serial port!\n" ); \
      retval = 1; \
      goto cleanup; \
   }

int update_lcd() {
   char lcd_str[LCD_STR_SZ + 1];
   int retval = 0;
   time_t now = 0;
   struct tm* now_info = NULL;

   time( &now );
   now_info = localtime( &now );

   /* Disable autoscroll. */
   write_check( "\xfe\x52", 2 );

   /* Disable line wrap. */
   write_check( "\xfe\x44", 2 );

   /* Clear screen and reset cursor. */
   write_check( "\xfe\x58", 2 );

   memset( lcd_str, '\0', LCD_STR_SZ + 1 );
   snprintf( lcd_str, LCD_STR_SZ,
      "%d:%d %.2fF %d%% RH\nRain: %.2f in/hr\nPM2.5: %.2f ug/m3",
      now_info->tm_hour, now_info->tm_min,
      sub_vals[0], (int)(sub_vals[1]), sub_vals[2], sub_vals[3] );
   write_check( lcd_str, strlen( lcd_str ) );

cleanup:

   return retval;
}

void on_connect( struct mosquitto* mqtt, void* obj, int reason_code ) {
   int rc = 0;
   int i = 0;
   printf( "MQTT connected\n" );

   while( '\0' != sub_topics[i][0] ) {
      rc = mosquitto_subscribe( mqtt, NULL, sub_topics[i], 1 );
      if( MOSQ_ERR_SUCCESS == rc ) {
         printf( "subscribed\n" );
      } else {
         fprintf( stderr, "error subscribing\n" );
         mosquitto_disconnect( mqtt );
      }
      i++;
   }
}

void on_subscribe(
   struct mosquitto* mqtt, void* obj, int mid, int qos_count,
   const int* granted_qos
) {
}

void on_message(
   struct mosquitto* mqtt, void *obj, const struct mosquitto_message *msg
) {
   int i = 0;
   char* payload = msg->payload;
   char* topic = msg->topic;

   if( 0 == strcmp(
      "rtl_433/weathersdr/devices/Fineoffset-WH24/42/temperature_C",
      topic
   ) ) {
      /* Draw temperature. */
      sub_vals[0] = ((atof( payload ) * 9.0f) / 5.0f) + 32.0f;

   } else if( 0 == strcmp(
      "rtl_433/weathersdr/devices/Fineoffset-WH24/42/humidity",
      topic
   ) ) {
      sub_vals[1] = atof( payload );

   } else if( 0 == strcmp( sub_topics[2], topic ) ) {
      sub_vals[2] = atof( payload );

   } else if( 0 == strcmp( sub_topics[3], topic ) ) {
      sub_vals[3] = atof( payload );

   }

   if( update_lcd() ) {
      mosquitto_disconnect( mqtt );
      mosquitto_loop_stop( mqtt, 0 );
   }

#if 0
   /* Place large 5 */
   write( ser_fd, "\xfe\x6e", 2 );
   for( i = 0 ; strlen( payload ) > i ; i++ ) {
      printf( "%c vs %c\n", '0', payload[i] );
      if( '0' >= payload[i] || '9' <= payload[i] || 30 <= i ) {
         break;
      }
      printf( "c: (%d)\n", payload[i] - '0' );
      memset( lcd_str, '\0', 31 );
      snprintf( lcd_str, 30, "\xfe\x23%c%c", i * 5, payload[i] - '0' );
      write( ser_fd, lcd_str, 4 );
   }
#endif
}

int main( int argc, char* argv[] ) {
   struct termios serset;
   struct mosquitto* mqtt = NULL;
   int rc = 0;

   mosquitto_lib_init();

   mqtt = mosquitto_new( NULL, 1, NULL );
   if( NULL == mqtt ) {
      fprintf( stderr, "mosuitto init failure\n" );
      goto cleanup;
   }

   mosquitto_connect_callback_set( mqtt, on_connect );
   mosquitto_subscribe_callback_set( mqtt, on_subscribe );
   mosquitto_message_callback_set( mqtt, on_message );

   memset( &serset, '\0', sizeof( struct termios ) );
   ser_fd = open( "/dev/serial/by-id/usb-Palm__Inc._Palm_Handheld_00R03BF1628Y-if00-port1", O_RDWR | O_NOCTTY );
   if( 0 >= ser_fd ) {
      printf( "could not open serial!\n" );
      goto cleanup;
   }
   cfsetospeed( &serset, 115200 );
   printf( "serial opened\n" );

   mosquitto_username_pw_set( mqtt, "orbitlcd", "" );

   rc = mosquitto_connect( mqtt, "mqtt.interfinitydynamics.info", 1883, 60 );
   if( MOSQ_ERR_SUCCESS != rc ) {
      mosquitto_destroy( mqtt );
      fprintf( stderr, "MQTT error: %s\n", mosquitto_strerror( rc ) );
      goto cleanup;
   }

   mosquitto_loop_forever( mqtt, -1, 1 );

cleanup:

   close( ser_fd );

   mosquitto_lib_cleanup();

   return 1;
}

