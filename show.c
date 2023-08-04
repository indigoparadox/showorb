
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
#define DISPLAY_STR_SZ 255
#define CONFIG_PATH_SZ 255

#define BUF_TYPE_STR 0
#define BUF_TYPE_INT 1

#define debug_printf_3( ... ) printf( __VA_ARGS__ )
#define debug_printf_2( ... ) printf( __VA_ARGS__ )
#if defined( DEBUG )
#  define debug_printf_1( ... ) printf( __VA_ARGS__ )
#else
#  define debug_printf_1( ... )
#endif
#define error_printf( ... ) fprintf( stderr, __VA_ARGS__ )

#define ff_num( buf, j ) while( '-' != buf[j] && '+' != buf[j] && '*' != buf[j] && '/' != buf[j] && '\0' != buf[j] ) { debug_printf_1( "macro iter: %c (%d)\n", buf[j], j ); j++; } j--;

int g_ser_fd = -1;
size_t g_sub_count = 0;
char g_sub_topics[SUB_TOPICS_COUNT][SUB_TOPIC_SZ + 1];
float g_sub_vals[SUB_TOPICS_COUNT];
int g_retval = 0;

char display_str[DISPLAY_STR_SZ];
char g_cfg_path[CONFIG_PATH_SZ] = "show.conf";

#define write_check( str, sz ) \
   if( sz != write( g_ser_fd, str, sz ) ) { \
      error_printf( "error writing to serial port!\n" ); \
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
   int retval = 0,
      in_token = 0;
   time_t now = 0;
   struct tm* now_info = NULL;
   size_t i = 0,
      j = 0,
      k = 0;

   time( &now );
   now_info = localtime( &now );

   /* Disable autoscroll. */
   write_check( "\xfe\x52", 2 );

   /* Disable line wrap. */
   write_check( "\xfe\x44", 2 );

   /* Clear screen and reset cursor. */
   write_check( "\xfe\x58", 2 );

   memset( lcd_str, '\0', LCD_STR_SZ + 1 );
   for( i = 0 ; strlen( display_str ) > i ; i++ ) {
      if( '$' == display_str[i] ) {
         in_token = 1;

      } else if( in_token ) {
         /* Substitute tokens. */
         switch( display_str[i] ) {
         case 'T':
            snprintf( &(lcd_str[j]), LCD_STR_SZ - j, "%d:%02d",
               now_info->tm_hour, now_info->tm_min );
            j = strlen( lcd_str );
            break;

         case 'F':
            snprintf( &(lcd_str[j]), LCD_STR_SZ - j, "%0.2f", g_sub_vals[k] );
            j = strlen( lcd_str );
            k++; /* Use next value for next token. */
            break;

         case 'D':
            snprintf(
               &(lcd_str[j]), LCD_STR_SZ - j, "%d", (int)(g_sub_vals[k]) );
            j = strlen( lcd_str );
            k++; /* Use next value for next token. */
            break;

         case 'N':
            lcd_str[j++] = '\n';
            break;
         }
         in_token = 0;

      } else {
         /* Just copy over chars. */
         lcd_str[j] = display_str[i];
         j++;
      }
   }

   write_check( lcd_str, strlen( lcd_str ) );

cleanup:

   return retval;
}

void on_connect( struct mosquitto* mqtt, void* obj, int reason_code ) {
   int rc = 0;
   size_t read_sz = 0;
   int i = 0;

   debug_printf_3( "MQTT connected\n" );

   /* Load topic string. */
   read_sz = cfg_read(
      g_cfg_path, "display", 0,
      BUF_TYPE_STR, display_str, DISPLAY_STR_SZ );
   
   g_sub_count = 0;
   do {
      /* Read the topic into the buffer. */
      read_sz = cfg_read(
         g_cfg_path, "topic", g_sub_count,
         BUF_TYPE_STR, g_sub_topics[g_sub_count], SUB_TOPIC_SZ );

      if( 0 == read_sz ) {
         break;
      }

      /* Assemble processing macro. */
      for( i = 0 ; strlen( g_sub_topics[g_sub_count] ) > i ; i++ ) {
         if( '|' == g_sub_topics[g_sub_count][i] ) {
            g_sub_topics[g_sub_count][i] = '\0';
            debug_printf_1( "macro: %s\n", &(g_sub_topics[g_sub_count][i + 1]) );
         }
      }

      debug_printf_2( "subscribing to topic %d: %s\n",
         g_sub_count, g_sub_topics[g_sub_count] );

      /* Subscribe to the read topic. */
      rc = mosquitto_subscribe( mqtt, NULL, g_sub_topics[g_sub_count], 1 );
      if( MOSQ_ERR_SUCCESS == rc ) {
         debug_printf_2( "subscribed\n" );
      } else {
         error_printf( "error subscribing\n" );
         mosquitto_disconnect( mqtt );
      }

      g_sub_count++;
   } while( read_sz > 0 );

   debug_printf_2( "%d subs!\n", g_sub_count );
}

void on_subscribe(
   struct mosquitto* mqtt, void* obj, int mid, int qos_count,
   const int* granted_qos
) {
}

void on_message(
   struct mosquitto* mqtt, void *obj, const struct mosquitto_message *msg
) {
   int i = 0,
      j = 0;
   char next_op = 0;
   float f_buf = 0;
   char* payload = msg->payload;
   char* topic = msg->topic;

   for( i = 0 ; g_sub_count > i ; i++ ) {
      if( 0 == strcmp( g_sub_topics[i], topic ) ) {
         /* Draw temperature. */
         f_buf = atof( payload );

         j = strlen( g_sub_topics[i] ) + 1;
         while( SUB_TOPIC_SZ > j && '\0' != g_sub_topics[i][j] ) {
            /* Grab the next math op in the macro. */
            switch( g_sub_topics[i][j] ) {
            case '-':
            case '+':
            case '/':
            case '*':
               debug_printf_1( "next op: %c\n", g_sub_topics[i][j] );
               next_op = g_sub_topics[i][j];
               break;
            default:
               switch( next_op ) {
               case '*':
                  debug_printf_1( "multiplying %f by %f\n",
                     f_buf, atof( &(g_sub_topics[i][j]) ) );
                  f_buf *= atof( &(g_sub_topics[i][j]) );
                  ff_num( g_sub_topics[i], j );
                  break;

               case '/':
                  debug_printf_1( "dividing %f by %f\n",
                     f_buf, atof( &(g_sub_topics[i][j]) ) );
                  f_buf /= atof( &(g_sub_topics[i][j]) );
                  ff_num( g_sub_topics[i], j );
                  break;

               case '+':
                  debug_printf_1( "adding %f to %f\n",
                     atof( &(g_sub_topics[i][j]) ), f_buf );
                  f_buf += atof( &(g_sub_topics[i][j]) );
                  ff_num( g_sub_topics[i], j );
                  break;

               case '-':
                  debug_printf_1( "subtracting %f from %f\n",
                     atof( &(g_sub_topics[i][j]) ), f_buf );
                  f_buf -= atof( &(g_sub_topics[i][j]) );
                  ff_num( g_sub_topics[i], j );
                  break;
               }
               next_op = '\0';
               break;  
            }
            j++;
         }
         g_sub_vals[i] = f_buf;
         debug_printf_3( "%s: %f\n", g_sub_topics[i], g_sub_vals[i] );
         break;
      }
   }

   if( update_lcd() ) {
      g_retval = 1;
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

   mosquitto_lib_init();

   mqtt = mosquitto_new( NULL, 1, NULL );
   if( NULL == mqtt ) {
      error_printf( "mosquitto init failure\n" );
      g_retval = 1; \
      goto cleanup;
   }

   if( 1 < argc ) {
      memset( g_cfg_path, '\0', CONFIG_PATH_SZ );
      strncpy( g_cfg_path, argv[1], strlen( argv[1] ) );
      debug_printf_3( "using config: %s\n", g_cfg_path );
   }

   mosquitto_connect_callback_set( mqtt, on_connect );
   mosquitto_subscribe_callback_set( mqtt, on_subscribe );
   mosquitto_message_callback_set( mqtt, on_message );

   /* Open the config. */
   cfg_read( g_cfg_path, "port", 0, BUF_TYPE_STR, ser_path, SER_PATH_SZ );
   cfg_read( g_cfg_path, "baud", 0, BUF_TYPE_INT, &ser_baud, sizeof( int ) );

   /* Open the serial port to the display. */
   memset( &serset, '\0', sizeof( struct termios ) );
   debug_printf_3( "opening %s at %d bps...\n", ser_path, ser_baud );
   g_ser_fd = open( ser_path, O_RDWR | O_NOCTTY );
   if( 0 >= g_ser_fd || 0 >= ser_baud ) {
      g_retval = 1;
      error_printf( "could not open serial!\n" );
      goto cleanup;
   }
   cfsetospeed( &serset, ser_baud );
   debug_printf_3( "serial opened\n" );

   /* Setup MQTT connection. */
   cfg_read(
      g_cfg_path, "mqtt_host", 0, BUF_TYPE_STR, mqtt_host, MQTT_ITEM_SZ );
   cfg_read(
      g_cfg_path, "mqtt_port", 0, BUF_TYPE_INT, &mqtt_port, sizeof( int ) );
   cfg_read(
      g_cfg_path, "mqtt_user", 0, BUF_TYPE_STR, mqtt_user, MQTT_ITEM_SZ );
   cfg_read(
      g_cfg_path, "mqtt_pass", 0, BUF_TYPE_STR, mqtt_pass, MQTT_ITEM_SZ );

   debug_printf_3(
      "connecting to %s:%d as %s...\n", mqtt_host, mqtt_port, mqtt_user );

   mosquitto_username_pw_set( mqtt, mqtt_user, mqtt_pass );

   rc = mosquitto_connect( mqtt, mqtt_host, mqtt_port, 60 );
   if( MOSQ_ERR_SUCCESS != rc ) {
      g_retval = 1;
      mosquitto_destroy( mqtt );
      error_printf( "MQTT error: %s\n", mosquitto_strerror( rc ) );
      goto cleanup;
   }

   mosquitto_loop_forever( mqtt, -1, 1 );

cleanup:

   close( g_ser_fd );

   mosquitto_lib_cleanup();

   return g_retval; 
}

