
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <termios.h>
#include <string.h>
#include <stdlib.h>
#include <mosquitto.h>
#include <assert.h>

#define LCD_STR_SZ 255
#define SER_PATH_SZ 255
#define LINE_BUF_SZ 255
#define MQTT_ITEM_SZ 255
#define SUB_TOPIC_SZ 255
#define SUB_TOPICS_COUNT 63

#define BUF_TYPE_STR 0
#define BUF_TYPE_INT 1

int ser_fd = -1;
size_t sub_count = 0;

char sub_topics[SUB_TOPICS_COUNT][SUB_TOPIC_SZ + 1];
float sub_vals[SUB_TOPICS_COUNT];

#define write_check( str, sz ) \
   if( sz != write( ser_fd, str, sz ) ) { \
      fprintf( stderr, "error writing to serial port!\n" ); \
      retval = 1; \
      goto cleanup; \
   }

size_t cfg_read(
   const char* cfg_path, const char* key, int idx,
   int buf_type, void* buf_out, size_t buf_out_sz
) {
   char c,
      key_buf[LINE_BUF_SZ + 1],
      val_buf[LINE_BUF_SZ + 1];
   int cfg = 0,
      in_val = 1,
      * int_out = NULL;
   size_t read_sz = 0,
      line_total = 0,
      idx_iter = 0;

   memset( buf_out, '\0', buf_out_sz );

   cfg = open( cfg_path, O_RDONLY );

   do {
      line_total = 0;
      in_val = 0;
      memset( key_buf, '\0', LINE_BUF_SZ + 1 );
      memset( val_buf, '\0', LINE_BUF_SZ + 1 );

      /* Read characters into line buffer. */
      do {
         read_sz = read( cfg, &c, 1 );
         if( 1 > read_sz ) {
            goto cleanup;

         } else if( '=' == c ) {
            /* We're done reading the key and moving to the value. */
            line_total = 0;
            in_val = 1;

         } else if( '\n' == c ) {
            /* We're reached the end of the line. */
            break;

         } else if( in_val ) {
            /* We're reading the value. */
            val_buf[line_total++] = c;

         } else {
            /* We're still reading the key. */
            key_buf[line_total++] = c;
         }
      } while( read_sz == 1 );

      if(
         0 == strncmp( key, key_buf, strlen( key ) ) &&
         idx_iter == idx
      ) {
         switch( buf_type ) {
         case BUF_TYPE_STR:
            strncpy( buf_out, val_buf, buf_out_sz );
            return strlen( val_buf );

         case BUF_TYPE_INT:
            assert( sizeof( int ) == buf_out_sz );
            int_out = (int*)buf_out;
            *int_out = atoi( val_buf );
            return sizeof( int );

         default:
            return 0;
         }
      } else if( 0 == strncmp( key, key_buf, strlen( key ) ) ) {
         /* Found a match, but not the index requested! */
         idx_iter++;
      }
   } while( 1 );

cleanup:
   return 0;
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
   size_t read_sz = 0;
   int i = 0;

   printf( "MQTT connected\n" );

   sub_count = 0;
   
   do {
      /* Read the topic into the buffer. */
      read_sz = cfg_read(
         "show.conf", "topic", sub_count,
         BUF_TYPE_STR, sub_topics[sub_count], SUB_TOPIC_SZ );

      if( 0 == read_sz ) {
         break;
      }

      /* Assemble processing macro. */
      for( i = 0 ; strlen( sub_topics[sub_count] ) > i ; i++ ) {
         if( '|' == sub_topics[sub_count][i] ) {
            sub_topics[sub_count][i] = '\0';
            printf( "macro: %s\n", &(sub_topics[sub_count][i + 1]) );
         }
      }

      printf( "subscribing to topic %d: %s\n",
         sub_count, sub_topics[sub_count] );

      /* Subscribe to the read topic. */
      rc = mosquitto_subscribe( mqtt, NULL, sub_topics[sub_count], 1 );
      if( MOSQ_ERR_SUCCESS == rc ) {
         printf( "subscribed\n" );
      } else {
         fprintf( stderr, "error subscribing\n" );
         mosquitto_disconnect( mqtt );
      }

      sub_count++;
   } while( read_sz > 0 );

   printf( "%d subs!\n", sub_count );
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
}

int main( int argc, char* argv[] ) {
   struct termios serset;
   struct mosquitto* mqtt = NULL;
   int rc = 0,
      ser_baud = 0,
      mqtt_port = 0;
   char ser_path[SER_PATH_SZ + 1];
   char mqtt_host[MQTT_ITEM_SZ + 1];
   char mqtt_user[MQTT_ITEM_SZ + 1];
   char mqtt_pass[MQTT_ITEM_SZ + 1];
   int retval = 0;

   mosquitto_lib_init();

   mqtt = mosquitto_new( NULL, 1, NULL );
   if( NULL == mqtt ) {
      fprintf( stderr, "mosuitto init failure\n" );
      goto cleanup;
   }

   mosquitto_connect_callback_set( mqtt, on_connect );
   mosquitto_subscribe_callback_set( mqtt, on_subscribe );
   mosquitto_message_callback_set( mqtt, on_message );

   /* Open the config. */
   cfg_read( "show.conf", "port", 0, BUF_TYPE_STR, ser_path, SER_PATH_SZ );
   cfg_read( "show.conf", "baud", 0, BUF_TYPE_INT, &ser_baud, sizeof( int ) );

   /* Open the serial port to the display. */
   memset( &serset, '\0', sizeof( struct termios ) );
   printf( "opening %s at %d bps...\n", ser_path, ser_baud );
   ser_fd = open( ser_path, O_RDWR | O_NOCTTY );
   if( 0 >= ser_fd || 0 >= ser_baud ) {
      retval = 1;
      printf( "could not open serial!\n" );
      goto cleanup;
   }
   cfsetospeed( &serset, ser_baud );
   printf( "serial opened\n" );

   /* Setup MQTT connection. */
   cfg_read(
      "show.conf", "mqtt_host", 0, BUF_TYPE_STR, mqtt_host, MQTT_ITEM_SZ );
   cfg_read(
      "show.conf", "mqtt_port", 0, BUF_TYPE_INT, &mqtt_port, sizeof( int ) );
   cfg_read(
      "show.conf", "mqtt_user", 0, BUF_TYPE_STR, mqtt_user, MQTT_ITEM_SZ );
   cfg_read(
      "show.conf", "mqtt_pass", 0, BUF_TYPE_STR, mqtt_pass, MQTT_ITEM_SZ );

   printf( "connecting to %s:%d as %s...\n", mqtt_host, mqtt_port, mqtt_user );

   mosquitto_username_pw_set( mqtt, mqtt_user, mqtt_pass );

   rc = mosquitto_connect( mqtt, mqtt_host, mqtt_port, 60 );
   if( MOSQ_ERR_SUCCESS != rc ) {
      retval = 1;
      mosquitto_destroy( mqtt );
      fprintf( stderr, "MQTT error: %s\n", mosquitto_strerror( rc ) );
      goto cleanup;
   }

   mosquitto_loop_forever( mqtt, -1, 1 );

cleanup:

   close( ser_fd );

   mosquitto_lib_cleanup();

   return retval; 
}

