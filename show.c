
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <termios.h>
#include <string.h>
#include <stdlib.h>
#include <mosquitto.h>
#include <assert.h>
#include <stdint.h>
#include <errno.h>

#define LCD_STR_SZ 255
#define SER_PATH_SZ 255
#define LINE_BUF_SZ 255
#define MQTT_ITEM_SZ 255
#define SUB_TOPIC_SZ 255
#define SUB_TOPICS_COUNT 63
#define DISPLAY_STR_SZ 255
#define CONFIG_PATH_SZ 255

#define HUMID_CLOUD_THRESHOLD 80

#define BUF_TYPE_STR 0
#define BUF_TYPE_INT 1

#define ERR_BITMAP 2

#define debug_printf_3( ... ) printf( __VA_ARGS__ )
#define debug_printf_2( ... ) printf( __VA_ARGS__ )
#if !defined( NDEBUG )
#  define debug_printf_1( ... ) printf( __VA_ARGS__ )
#else
#  warning "building debug version!"
#  define debug_printf_1( ... )
#endif
#define error_printf( ... ) fprintf( stderr, __VA_ARGS__ )

#define ff_num( buf, j ) while( '-' != buf[j] && '+' != buf[j] && '*' != buf[j] && '/' != buf[j] && '\0' != buf[j] ) { debug_printf_1( "macro iter: %c (%d)\n", buf[j], j ); j++; } j--;

int g_ser_fd = -1;
size_t g_sub_count = 0;
char g_sub_topics[SUB_TOPICS_COUNT][SUB_TOPIC_SZ + 1];
float g_sub_vals[SUB_TOPICS_COUNT];
int g_retval = 0;
int g_weather_sun_icon = 0;
int g_weather_moon_icon = 0;
int g_weather_cloud_icon = 0;
int g_weather_rain_icon = 0;

char display_str[DISPLAY_STR_SZ];
char g_cfg_path[CONFIG_PATH_SZ + 1] = "/etc/showorb.conf";

#define write_check( str, sz ) \
   if( sz != write( g_ser_fd, str, sz ) ) { \
      error_printf( "error writing to serial port!\n" ); \
      retval = 1; \
      goto cleanup; \
   }


#define checked_cfg_read( \
   cfg_path, sect, key, idx, buf_type, buf_out, buf_out_sz ) \
      if( \
         0 > \
         cfg_read( cfg_path, sect, key, idx, buf_type, buf_out, buf_out_sz ) \
      ) { \
         goto cleanup; \
      }

ssize_t cfg_read(
   const char* cfg_path, const char* sect, const char* key, int idx,
   int buf_type, void* buf_out, size_t buf_out_sz
) {
   char c,
      key_buf[LINE_BUF_SZ + 1],
      val_buf[LINE_BUF_SZ + 1],
      sect_buf[LINE_BUF_SZ + 1];
   int cfg = 0,
      in_val = 1,
      in_sect = 0,
      * int_out = NULL;
   size_t read_sz = 0,
      line_total = 0,
      idx_iter = 0;
   ssize_t retval = 0;

   memset( buf_out, '\0', buf_out_sz );

   cfg = open( cfg_path, O_RDONLY );
   if( 0 > cfg ) {
      error_printf( "could not open config: %d\n", errno );
      retval = -1;
      goto cleanup;
   }

   do {
      line_total = 0;
      in_val = 0;
      memset( key_buf, '\0', LINE_BUF_SZ + 1 );
      memset( val_buf, '\0', LINE_BUF_SZ + 1 );

      /* Read characters into line buffer. */
      do {
         read_sz = read( cfg, &c, 1 );
         if( 1 > read_sz ) {
            /* Ran out of config to read! */
            goto cleanup;

         } else if( '[' == c ) {
            line_total = 0;
            in_sect = 1;
            in_val = 0;
            idx_iter = 0; /* Index is only per-section. */

         } else if( ']' == c ) {
            sect_buf[line_total] = 0;
            line_total = 0;
            in_sect = 0;

         } else if( '=' == c ) {
            /* We're done reading the key and moving to the value. */
            line_total = 0;
            in_val = 1;

         } else if( '\n' == c ) {
            /* We're reached the end of the line. */
            break;

         } else if( in_sect ) {
            /* We're reading the section. */
            sect_buf[line_total++] = c;

         } else if( in_val ) {
            /* We're reading the value. */
            val_buf[line_total++] = c;

         } else {
            /* We're still reading the key. */
            key_buf[line_total++] = c;
         }
      } while( read_sz == 1 );

      if(
         0 == strncmp( sect, sect_buf, strlen( sect ) ) &&
         0 == strncmp( key, key_buf, strlen( key ) ) &&
         idx_iter == idx
      ) {
         switch( buf_type ) {
         case BUF_TYPE_STR:
            strncpy( buf_out, val_buf, buf_out_sz );
            retval = strlen( val_buf );
            goto cleanup;

         case BUF_TYPE_INT:
            assert( sizeof( int ) == buf_out_sz );
            int_out = (int*)buf_out;
            *int_out = atoi( val_buf );
            retval = sizeof( int );
            goto cleanup;

         default:
            error_printf( "invalid type: %d\n", buf_type );
            retval = -1;
            goto cleanup;
         }
      } else if( 0 == strncmp( key, key_buf, strlen( key ) ) ) {
         /* Found a match, but not the index requested! */
         idx_iter++;
      }
   } while( 1 );

cleanup:

   if( 0 <= cfg ) {
      close( cfg );
   }

   return retval;
}

int bmp_read( const char* bmp_path, uint8_t char_bits[8] ) {
   int bmp_file = 0,
      r = 0,
      c = 0,
      retval = 0;
   uint32_t bmp_offset = 0,
      bmp_sz = 0;
   uint8_t px = 0;
   char bmp_check[3] = { 0, 0, 0 };
   ssize_t read_sz = 0;

   memset( char_bits, '\0', 8 );

   debug_printf_1( "loading bitmap: %s\n", bmp_path );

   bmp_file = open( bmp_path, O_RDONLY );
   if( 0 >= bmp_file ) {
      error_printf( "could not load char bitmap: %s\n", bmp_path );
      retval = ERR_BITMAP;
      goto cleanup;
   }

   /* Check valid bitmap file. */
   read_sz = read( bmp_file, &bmp_check, 2 );
   if( 2 != read_sz ) {
      error_printf( "error reading bitmap: %s", bmp_path );
      retval = ERR_BITMAP;
      goto cleanup;
   }
   if( 0 != strncmp( "BM", bmp_check, 2 ) ) {
      error_printf( "invalid bitmap file!\n" );
      retval = ERR_BITMAP;
      goto cleanup;
   }

   lseek( bmp_file, 10, SEEK_SET );
   if(
      sizeof( uint32_t ) > read( bmp_file, &bmp_offset, sizeof( uint32_t ) )
   ) {
      error_printf( "could not read bitmap offset!\n" );
      retval = ERR_BITMAP;
      goto cleanup;
   }

   /* TODO: Check bitmap size greater than offset. */

   lseek( bmp_file, 14, SEEK_SET );
   if(
      sizeof( uint32_t ) > read( bmp_file, &bmp_sz, sizeof( uint32_t ) ) ||
      40 != bmp_sz
   ) {
      error_printf( "invalid bitmap header!\n" );
      retval = ERR_BITMAP;
      goto cleanup;
   }

   debug_printf_1( "reading bitmap bits...\n" );

   /* Seek to bitmap offset and start parsing bits into bits array. */
   lseek( bmp_file, bmp_offset, SEEK_SET );
   for( r = 0 ; 8 > r ; r++ ) {
      for( c = 0 ; 5 > c ; c++ ) {
         /* Start from bottom, since the rows are reversed in bitmap format. */
         char_bits[7 - r] <<= 1;

         /* Read next bit into row. */
         read_sz = read( bmp_file, &px, 1 );
         if( 1 != read_sz ) {
            error_printf( "error reading bitmap!" );
            retval = ERR_BITMAP;
            goto cleanup;
         }
         if( 0 == px ) {
            char_bits[7 - r] |= 0x01;
         }
      }

      /* Read out padding for row pixels divisble by 4. */
      while( 8 > c ) {
         read_sz = read( bmp_file, &px, 1 );
         if( 1 != read_sz ) {
            error_printf( "error reading bitmap!" );
            retval = ERR_BITMAP;
            goto cleanup;
         }
         c++;
      }
   }

   debug_printf_1( "bitmap %s read successfully!\n", bmp_path );

cleanup:
   if( 0 < bmp_file ) {
      close( bmp_file );
   }

   return retval;
}

int update_chars() {
   int i = 0,
      retval = 0;
   char char_bits[9];
   char icon_path[CONFIG_PATH_SZ + 1];

   debug_printf_1( "sending custom characters...\n" );

   /* Send custom chars. */
   /* Start at 1 because sending a 0 in a string is... tricky. */
   for( i = 1 ; 8 > i ; i++ ) {
      memset( icon_path, '\0', CONFIG_PATH_SZ + 1 );
      debug_printf_1( "loading char: %s\n", icon_path );
      cfg_read(
         g_cfg_path, "font", "icon", i - 1,
         BUF_TYPE_STR, icon_path, CONFIG_PATH_SZ );
      debug_printf_1( "loaded char: %s\n", icon_path );

      if( 0 >= strlen( icon_path ) ) {
         debug_printf_1( "done loading chars\n" );
         break;
      }

      char_bits[0] = i;
      if( bmp_read( icon_path, &(char_bits[1]) ) ) {
         retval = 1;
         goto cleanup;
      }
      write_check( "\xfe\x4e", 2 );
      write_check( char_bits, 9 ); /* Char selector prepended. */
   }

cleanup:

   return retval;
}

int update_lcd() {
   char lcd_str[LCD_STR_SZ + 1];
   int retval = 0,
      in_token = 0;
   time_t now = 0;
   struct tm* now_info = NULL;
   size_t i = 0,
      j = 0,
      k = 0; /* Index of current topic token in list of topics. */

   time( &now );
   now_info = localtime( &now );

   debug_printf_1( "setting up display...\n" );

   /* Disable autoscroll. */
   write_check( "\xfe\x52", 2 );

   /* Disable line wrap. */
   write_check( "\xfe\x44", 2 );

   /* Clear screen and reset cursor. */
   write_check( "\xfe\x58", 2 );

   memset( lcd_str, '\0', LCD_STR_SZ + 1 );
   debug_printf_1( "formatting display string...\n" );
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

         case 'W':
            if( 0 == g_sub_vals[k] ) {
               lcd_str[j++] = g_weather_rain_icon;
	    } else if( 1 == g_sub_vals[k] ) {
               lcd_str[j++] = g_weather_cloud_icon; 
	    } else if( 2 == g_sub_vals[k] ) {
               lcd_str[j++] = g_weather_moon_icon;
	    } else if( 3 == g_sub_vals[k] ) {
               lcd_str[j++] = g_weather_sun_icon; 
            } else {
               lcd_str[j++] = '?';
	    }
            k++; /* Use next value for next token. */
            break;
         }
         in_token = 0;

      } else {
         /* Just copy over chars. */
         lcd_str[j] = display_str[i];
         j++;
      }
   }

   debug_printf_1( "writing to display...\n" );

   write_check( lcd_str, strlen( lcd_str ) );

   debug_printf_1( "update complete!\n" );

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
      g_cfg_path, "lcd", "display", 0,
      BUF_TYPE_STR, display_str, DISPLAY_STR_SZ );
   
   g_sub_count = 0;
   do {
      /* Read the topic into the buffer. */
      read_sz = cfg_read(
         g_cfg_path, "mqtt", "topic", g_sub_count,
         BUF_TYPE_STR, g_sub_topics[g_sub_count], SUB_TOPIC_SZ );

      if( 0 == read_sz ) {
         break;
      }

      /* Assemble processing macro. */
      for( i = 0 ; strlen( g_sub_topics[g_sub_count] ) > i ; i++ ) {
         if( '|' == g_sub_topics[g_sub_count][i] ) {
            g_sub_topics[g_sub_count][i] = '\0';
            debug_printf_1(
               "macro: %s\n", &(g_sub_topics[g_sub_count][i + 1]) );
         }
      }

      debug_printf_2( "subscribing to topic %lu: %s\n",
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

   debug_printf_2( "%ld subs!\n", g_sub_count );
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

   debug_printf_1( "received message at %s: %s\n", topic, payload );

   for( i = 0 ; g_sub_count > i ; i++ ) {
      if( 0 == strcmp( g_sub_topics[i], topic ) ) {
         /* Draw variable. */
         f_buf = atof( payload );
         j = strlen( g_sub_topics[i] ) + 1;

         debug_printf_1( "applying message transformation: %s\n",
            &(g_sub_topics[i][j]) );

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
         debug_printf_1( "%s: %f\n", g_sub_topics[i], g_sub_vals[i] );

         break;
      }
   }

   if( update_lcd() ) {
      error_printf( "stopping MQTT loop!\n" );
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
      memset( g_cfg_path, '\0', CONFIG_PATH_SZ + 1 );
      strncpy( g_cfg_path, argv[1], CONFIG_PATH_SZ );
      debug_printf_3( "using config: %s\n", g_cfg_path );
   }

   mosquitto_connect_callback_set( mqtt, on_connect );
   mosquitto_subscribe_callback_set( mqtt, on_subscribe );
   mosquitto_message_callback_set( mqtt, on_message );

   /* Open the config. */
   checked_cfg_read(
      g_cfg_path, "lcd", "port", 0, BUF_TYPE_STR, ser_path, SER_PATH_SZ );
   checked_cfg_read(
      g_cfg_path, "lcd", "baud", 0, BUF_TYPE_INT, &ser_baud, sizeof( int ) );

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

   /* Load weather icon config. */
   checked_cfg_read(
      g_cfg_path, "weather", "sun_icon", 0,
      BUF_TYPE_INT, &g_weather_sun_icon, sizeof( int ) );
   checked_cfg_read(
      g_cfg_path, "weather", "moon_icon", 0,
      BUF_TYPE_INT, &g_weather_moon_icon, sizeof( int ) );
   checked_cfg_read(
      g_cfg_path, "weather", "cloud_icon", 0,
      BUF_TYPE_INT, &g_weather_cloud_icon, sizeof( int ) );
   checked_cfg_read(
      g_cfg_path, "weather", "rain_icon", 0,
      BUF_TYPE_INT, &g_weather_rain_icon, sizeof( int ) );

   if( update_chars() ) {
      error_printf( "failed to update custom characters!\n" );
      g_retval = 1;
      goto cleanup;
   }

   /* Setup MQTT connection. */
   checked_cfg_read(
      g_cfg_path, "mqtt", "host", 0, BUF_TYPE_STR, mqtt_host, MQTT_ITEM_SZ );
   checked_cfg_read(
      g_cfg_path, "mqtt", "port", 0, BUF_TYPE_INT, &mqtt_port, sizeof( int ) );
   checked_cfg_read(
      g_cfg_path, "mqtt", "user", 0, BUF_TYPE_STR, mqtt_user, MQTT_ITEM_SZ );
   checked_cfg_read(
      g_cfg_path, "mqtt", "pass", 0, BUF_TYPE_STR, mqtt_pass, MQTT_ITEM_SZ );

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

