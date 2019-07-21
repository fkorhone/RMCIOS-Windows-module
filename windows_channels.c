/* 
RMCIOS - Reactive Multipurpose Control Input Output System
Copyright (c) 2018 Frans Korhonen

RMCIOS was originally developed at Institute for Atmospheric 
and Earth System Research / Physics, Faculty of Science, 
University of Helsinki, Finland

Assistance, experience and feedback from following persons have been 
critical for development of RMCIOS: Erkki Siivola, Juha Kangasluoma, 
Lauri Ahonen, Ella Häkkinen, Pasi Aalto, Joonas Enroth, Runlong Cai, 
Markku Kulmala and Tuukka Petäjä.

This file is part of RMCIOS. This notice was encoded using utf-8.

RMCIOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RMCIOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public Licenses
along with RMCIOS.  If not, see <http://www.gnu.org/licenses/>.
*/

/* 
 * Windows API channels module
 *
 * Changelog: (date,who,description)
 */
#define DLL

#define _WIN32_WINNT 0x0500a

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "RMCIOS-functions.h"
#include <sys/time.h>

const struct context_rmcios *module_context;

struct timer_data
{
   int linked_channel;
   float period;
   unsigned int loops;
   HANDLE hTimerQueue;
   HANDLE timer;
   int index;
   int completion_channel;
};

// Ticker that will handle all rtc scheduled tasks
VOID CALLBACK timer_ticker (struct timer_data *this, BOOLEAN TimerOrWaitFired)
{
   if (this->linked_channel != 0)
      module_context->run_channel (module_context, this->linked_channel,
                                   write_rmcios, int_rmcios,
                                   0,
                                   0, (const union param_rmcios) 0);
   if (this->loops > 0)
   {
      this->index++;
      if (this->index >= this->loops && this->timer != NULL)    
      // Stop timer
      {
         if (this->completion_channel != 0)
            module_context->run_channel (module_context,
                                         this->completion_channel,
                                         write_rmcios, int_rmcios,
                                         0, 0,
                                         (const union param_rmcios) 0);
         DeleteTimerQueueTimer (this->hTimerQueue, this->timer, NULL);
         this->timer = NULL;
         return;        
      }
   }

   if (this->timer != NULL)
      DeleteTimerQueueTimer (this->hTimerQueue, this->timer, NULL);
   // Create new timer
   CreateTimerQueueTimer (&this->timer, //_Out_ PHANDLE phNewTimer,
                          this->hTimerQueue,//_In_opt_ HANDLE TimerQueue,
                          //_In_ WAITORTIMERCALLBACK Callback:
                          (WAITORTIMERCALLBACK) timer_ticker,
                          this, //_In_opt_ PVOID Parameter,
                          this->period * 1000,  // _In_ DWORD DueTime,
                          0,    //_In_ DWORD Period,
                          WT_EXECUTEONLYONCE //_In_ ULONG  Flags
      );
}

void timer_class_func (struct timer_data *this,
                       const struct context_rmcios *context, int id,
                       enum function_rmcios function,
                       enum type_rmcios paramtype,
                       struct combo_rmcios *returnv,
                       int num_params, const union param_rmcios param)
{
   int plen;
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv, 
                     "timer channel help:\r\n"
                     " create timer newname \r\n"
                     " setup newname period | loops(0) | completion_channel \r\n"
                     "  #-Sets the timer period and number loops to trigger.\r\n"
                     "  # Setting loops to 0 will start timer immediately\r\n"
                     "  # and run timer continuosly.\r\n"
                     "  # n loops specified timer starts on next setup/write\r\n"
                     "  # command (without the loops parameter).\r\n"
                     " write newname "
                     "  # -start/reset timer.\r\n"
                     " write newname period \r\n"
                     "  # -set/reset timer time and run timer.\r\n"
                     " read newname\r\n"
                     "  # -Get remaining time\r\n"
                     " link timer link_channel \r\n"
                     "  # link to channel called on match.\r\n"
                     );
      break;

   case create_rmcios:
      if (num_params < 1)
         break;
      int id;
      // allocate new data  
      this = (struct timer_data *) malloc (sizeof (struct timer_data)); 
      
      // create channel
      id = create_channel_param (context, paramtype, param, 0, 
                                 (class_rmcios) timer_class_func, this);  

      //default values :
      this->linked_channel = linked_channels (context, id);
      this->loops = 0;
      this->period = 1;
      this->completion_channel = 0;

      this->hTimerQueue = NULL;
      this->timer = NULL;

      // Create the timer queue for timer.
      this->hTimerQueue = CreateTimerQueue ();
      break;

   case setup_rmcios:
   case write_rmcios:
      if (this == NULL)
         break;
      else
      {
         if (num_params > 0)
            this->period = param_to_float (context, paramtype, param, 0);
         if (num_params > 1)
            this->loops = param_to_int (context, paramtype, param, 1);
         if (num_params > 2)
            this->completion_channel =
               param_to_int (context, paramtype, param, 2);


         // Remove old timer
         if (this->timer != NULL)       
         {
            DeleteTimerQueueTimer (this->hTimerQueue, this->timer, NULL);
            this->timer = NULL;
         }

         this->index = 0;

         if (this->loops == 0 || num_params < 2)
         {
            // Create new timer
            CreateTimerQueueTimer (&this->timer,//_Out_ PHANDLE phNewTimer,
                                   //_In_opt_ HANDLE TimerQueue:
                                   this->hTimerQueue, 
                                   //_In_ WAITORTIMERCALLBACK Callback:
                                   (WAITORTIMERCALLBACK) timer_ticker,
                                   this, //_In_opt_ PVOID Parameter,
                                   this->period * 1000, //_In_ DWORD DueTime,
                                   0,   //_In_ DWORD Period,
                                   WT_EXECUTEONLYONCE //_In_ULONG Flags
                                  );
         }

      }
      break;
   default:
      break;
   }
}

/////////////////////////////////////////////////////////////
// RTC - Real time clock                                   //
/////////////////////////////////////////////////////////////
FILE *rtc_dbg = NULL;           // Debug stream
int timezone_offset = 0;        // Timezone offset(seconds) from UTC
char use_localtime = 1;         // Flag to use local time

void rtc_class_func (void *data, const struct context_rmcios *context, int id,
                     enum function_rmcios function,
                     enum type_rmcios paramtype, 
                     struct combo_rmcios *returnv,
                     int num_params, 
                     const union param_rmcios param)
{
   time_t rawtime, seconds;
   time (&seconds);

   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv,
                     "rtc (realtime clock) channel help\r\n" 
                     " setup rtc timezone_offset(hours)\r\n"
                     " read rtc \r\n"
                     "   #read time in unix time\r\n"
                     " write rtc unixtime "
                     "   #set UTC time in unix time\r\n"
                     );
      break;
   
   case setup_rmcios:
      if (num_params < 1)
         break;
      timezone_offset = param_to_int (context, paramtype, param, 0) * 60 * 60;
      use_localtime = 0;
      break;

   case read_rmcios:
      return_int (context, returnv, seconds);
      break;
   
   case write_rmcios:
      if (num_params < 1)
         break;
      break;
   }
}

// Utility function determine local time offset from gmt
long tz_offset_second (time_t t)
{
   struct tm local = *localtime (&t);
   struct tm utc = *gmtime (&t);
   long diff =
      ((local.tm_hour - utc.tm_hour) * 60 +
       (local.tm_min - utc.tm_min)) * 60L + (local.tm_sec - utc.tm_sec);
   int delta_day = local.tm_mday - utc.tm_mday;
   if ((delta_day == 1) || (delta_day < -1))
   {
      diff += 24L * 60 * 60;
   }
   else if ((delta_day == -1) || (delta_day > 1))
   {
      diff -= 24L * 60 * 60;
   }
   return diff;
}

// Utility function for printing current time with second decimals.
// %z to insert ISO8601 timezone offset in windows.
void print_current_time (char *buffer, int buffer_length, const char *format,
                         int second_decimals, int timezone_offset)
{
   int i = 0;
   struct timeval curTime;
   gettimeofday (&curTime, NULL);

   struct tm *timeinfo;

   int seconds;
   seconds = curTime.tv_sec % 60;

   int parts;
   int parts_precision = 1;
   for (i = 0; i < second_decimals; i++)
      parts_precision *= 10;
   parts = curTime.tv_usec / (1E6 / (parts_precision));

   time_t rawtime;
   time (&rawtime);
   if (use_localtime == 1)
      timezone_offset = tz_offset_second (rawtime);
   rawtime += timezone_offset;
   timeinfo = gmtime (&rawtime);

   char format_buffer[buffer_length];
   const char *c = format;

   for (i = 0; i < (buffer_length - 1) && *c != 0; i++)
   {

      if (*c == '%')
      // Specifier
      {
         c++;
         if (*c == 0)
            break;
         if (*c == 'S' && second_decimals > 0)  
         // Insert precision seconds
         {
            char sseconds[128];
            char sformat[128];
            snprintf (sformat, sizeof (sformat), "%%02d.%%0%dd",
                      second_decimals);
            snprintf (sseconds, sizeof (sseconds), sformat, seconds, parts);
            strncpy (format_buffer + i, sseconds,
                     sizeof (format_buffer) - i - 1);
            i += strlen (sseconds) - 1;
         }
         else if (*c == 'z')    // Insert ISO 8601 timezone offset
         {
            char sign = '+';
            if (timezone_offset < 0)
            {
               timezone_offset = -1 * timezone_offset;
               sign = '-';
            }
            i += snprintf (format_buffer + i,
                           sizeof (format_buffer) - i - 1, "%c%02d",
                           sign, timezone_offset / 60 / 60);
            int minutes = timezone_offset / 60 % 60;
            if (minutes > 0)
            {
               i += snprintf (format_buffer + i,
                              sizeof (format_buffer) - i - 1,
                              ":%02d", timezone_offset / 60 % 60);

            }
            i--;
         }
         else   
         // Continue normally
         {
            c--;
            format_buffer[i] = *c;
         }
      }
      else
      {
         format_buffer[i] = *c;
      }
      c++;
   }
   format_buffer[i] = 0;
   strftime (buffer, buffer_length, format_buffer, timeinfo);
}

struct rtc_str_data
{
   char rtc_str_format[256];
   int second_decimals;
} default_rtc_str_data =
{
"%Y-%m-%dT%H:%M:%S%z", 0};

void rtc_str_class_func (struct rtc_str_data *this,
                         const struct context_rmcios *context, int id,
                         enum function_rmcios function,
                         enum type_rmcios paramtype,
                         struct combo_rmcios *returnv,
                         int num_params,
                         const union param_rmcios param)
{
   int i;
   int writelen;
   char buffer[256];
   time_t seconds = time (NULL) + timezone_offset;
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv,
                     "rtc string representation subchannel help\r\n"
                     " create rtc_str newname\r\n"
                     " setup rtc_str formatstring | second_decimals(0) \r\n"
                     "  Configure time string (C strftime format) \r\n"
                     "  Second decimal precision can be specified optionally\r\n"
                     " read rtc_str \r\n"
                     "  # Read formatted string \r\n"
                     " write rtc_str \r\n"
                     "  # Send formatted string to linked channel.\r\n"
                     "  # Also returns the formatted string. \r\n"
                     " link rtc_str linked_channel \r\n"
                     "  Common format specifiers:\r\n"
                     "   %Y Year \n"
                     "   %m Month as number \n"
                     "   %d Day of the month \n"
                     "   %H Hour in 24h format \n"
                     "   %M Minute \n"
                     "   %S Second \n"
                     "   %z Timezone offset\n"
                     );
      break;

   case create_rmcios:
      if (num_params < 1)
         break;
      struct rtc_str_data *old = this;
      // allocate new data      
      this = (struct rtc_str_data *) malloc (sizeof (struct rtc_str_data));     
      
      if (this == NULL)
         printf ("Could not allocate memory for new rtc_str channel\r\n");
      // create channel
      create_channel_param (context, paramtype, param, 0, 
                            (class_rmcios) rtc_str_class_func, this);     

      //default values :
      strcpy (this->rtc_str_format, "%Y-%m-%dT%H:%M:%S%z");
      this->second_decimals = 0;
      break;

   case setup_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
         break;
      param_to_string (context, paramtype, param, 0,
                       sizeof (this->rtc_str_format), this->rtc_str_format);
      if (num_params < 2)
         break;
      this->second_decimals = param_to_int (context, paramtype, param, 1);
      break;

   case write_rmcios:
      {
         if (this == NULL)
            break;

         print_current_time (buffer, sizeof (buffer),
                             this->rtc_str_format, this->second_decimals,
                             timezone_offset);
         write_str (context, linked_channels (context, id), buffer, 0);
         return_string (context, returnv, buffer);
         break;
      }
   case read_rmcios:
      {
         if (this == NULL)
            break;
         print_current_time (buffer, sizeof (buffer),
                             this->rtc_str_format, this->second_decimals,
                             timezone_offset);
         return_string (context, returnv, buffer);
         break;
      }
   }
}

///////////////////////////////////////////////////////
// Realtime clock timer
///////////////////////////////////////////////////////
struct rtc_timer_data
{
   int offset;
   int period;
   time_t prevtime;
   int id;
   struct rtc_timer_data *nextimer; // linked list of sheduled times
   int wait;
};

struct rtc_timer_data *first_timer = NULL;

// Ticker that will handle all rtc scheduled tasks
VOID CALLBACK rtc_ticker (void *data, BOOLEAN TimerOrWaitFired)
{
   time_t seconds;
   // time now
   seconds = time (NULL);
   seconds += timezone_offset;
   struct rtc_timer_data *t = first_timer;
   while (t != NULL)
   {
      if (t->period != 0)       
      // timer is active
      {
         // Time has changed -> change prevtime to lastest possible:
         if (t->prevtime > seconds || (seconds - t->prevtime) >= t->period * 2)
         {
            t->prevtime =
               seconds + (t->offset % t->period) - (seconds % t->period);
         }
         if (seconds >= (t->prevtime + t->period))
         // Trigger
         {      
            t->wait = 0;
            // Execute linked channels
            write_fv (module_context, 
                      linked_channels (module_context, t->id), 0, 0);   
            
            // Update calculated current trigger time :
            t->prevtime = seconds + (t->offset % t->period) 
                          - (seconds % t->period);
         }
      }
      t = t->nextimer;
   }
}

void rtc_timer_class_func (struct rtc_timer_data *t,
                           const struct context_rmcios *context, int id,
                           enum function_rmcios function,
                           enum type_rmcios paramtype,
                           struct combo_rmcios *returnv,
                           int num_params,
                           const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv, 
                     "rtc timer channel help\r\n"
                     "periodic realtime clock timer\r\n"
                     " create rtc_timer newname\r\n"
                     " setup newname period | offset_s(0) "
                     "               | min(0) | h(0) | day(0=Thursday)) \r\n"
                     "               | month(1) | year(1970) \r\n"
                     " read newname\r\n"
                     " link newname execute_channel\r\n"
                     );
      break;

   case create_rmcios:
      if (num_params < 1)
         break;

      // allocate new data
      t = (struct rtc_timer_data *) malloc (sizeof (struct rtc_timer_data)); 
      if (t == NULL) break;

      // Default values:
      t->offset = timezone_offset;
      t->period = 0;
      t->prevtime = 0;
      
      // linked list of sheduled times
      t->nextimer = NULL;       

      // create channel
      t->id = create_channel_param (context, paramtype, param, 0, 
                                    (class_rmcios) rtc_timer_class_func, t);  

      // Attach timer to executing timers:
      if (first_timer == NULL)
         first_timer = t;
      else
      {
         struct rtc_timer_data *p_iter = first_timer;
         while (p_iter->nextimer != NULL)
         {
            p_iter = p_iter->nextimer;
         }
         // Add to be executed.
         p_iter->nextimer = t;  
      }
      break;

   case setup_rmcios:
      if (num_params < 1)
         // only perioid as parameter
         break; 
      if (t == NULL)
         break;
      {
         time_t seconds;
         seconds = time (NULL); // time now
         seconds += timezone_offset;

         struct tm newtime;
         newtime.tm_year = 70;  // years since 1900
         newtime.tm_mon = 0;    // months since January
         newtime.tm_mday = 1;   // day of the month
         newtime.tm_hour = 0;
         newtime.tm_min = 0;
         newtime.tm_sec = 0;
         newtime.tm_isdst = 0;

         t->period = param_to_int (context, paramtype, param, 0);
         if (num_params >= 2)
            newtime.tm_sec = param_to_int (context, paramtype, param, 1);
         if (num_params >= 3)
            newtime.tm_min = param_to_int (context, paramtype, param, 2);
         if (num_params >= 4)
            newtime.tm_hour = param_to_int (context, paramtype, param, 3);
         if (num_params >= 5)
            // day of the month
            newtime.tm_mday = param_to_int (context, paramtype, param, 4);      
         if (num_params >= 6)
            // months since January
            newtime.tm_mon = param_to_int (context, paramtype, param, 5);       
         if (num_params >= 7)
            // years since 1900
            newtime.tm_year = param_to_int (context, paramtype, param, 6) - 1900;       
         time_t sync_time;
         sync_time = mktime (&newtime);
         if (sync_time < 0)
            sync_time = 0;

         t->offset = sync_time % t->period;
         t->prevtime =
            seconds + (t->offset % t->period) - (seconds % t->period);
      }
      break;

   case read_rmcios:
      if (t == NULL)
         break;
      {
         time_t seconds;
         // time now
         seconds = time (NULL); 
         seconds += timezone_offset;
         int tleft = t->prevtime + t->period - seconds;
         return_int(context, returnv, tleft);
      }
      break;
   }
}



///////////////////////////////////////////////////////
// Standard C file flass
///////////////////////////////////////////////////////
struct file_data
{
   FILE *f;
   unsigned int id;
   int keep_open;
   char *filename;
   char mode[5];
};

DWORD WINAPI file_rx_thread (LPVOID data)
{
   struct file_data *this = (struct file_data *) data;
   char s[2];
   s[1] = 0;
   while (1)
   {
      s[0] = fgetc (this->f);
      write_str (module_context,
                 linked_channels (module_context, this->id), s, this->id);
   }
   return 0;
}

void file_class_func (struct file_data *this,
                      const struct context_rmcios *context, int id,
                      enum function_rmcios function,
                      enum type_rmcios paramtype, 
                      struct combo_rmcios *returnv,
                      int num_params, const union param_rmcios param)
{
   const char *s;
   int plen;

   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv, 
                     "file channel help\r\n"
                     " create file ch_name\r\n"
                     " setup ch_name filename | mode=a | keep_open=1 \r\n"
                     "   #open and read data from file to linked channel. \r\n"
                     " setup ch_name \r\n #Close file \r\n"
                     " write ch_name file data # write data to file\r\n"
                     " link ch_name filename\r\n"
                     " read file filename\r\n"
                     );
      break;

   case create_rmcios:
      if (num_params < 1)
         break;
      // Allocate memory
      // allocate new data
      this = (struct file_data *) malloc (sizeof (struct file_data));   

      // Default values :
      this->f = NULL;
      this->keep_open = 1;
      this->filename = NULL;
      this->mode[0] = 'a';
      this->mode[1] = 0;

      // Create the channel
      this->id = create_channel_param (context, paramtype, param, 0, 
                                (class_rmcios) file_class_func, this);     
      
      break;
      
   case setup_rmcios:
      if (this == NULL)
         break;
      if (this->f != NULL)      
      // Close the file if it exist
      {
         fclose (this->f);
         this->f = NULL;
      }

      if (num_params > 0)
      {
         int namelen = param_string_length (context, paramtype, param, 0); 
         // Create directory if it dosent exist
         {
            char dirname[namelen + 1];
            int i;
            param_to_string (context, paramtype, param, 0,
                             namelen + 1, dirname);
            for (i = 0; i < namelen; i++)
            {
               if (dirname[i] == '\\' || dirname[i] == '/')
               {
                  char c = dirname[i];
                  dirname[i] = 0;
                  CreateDirectory (dirname, NULL);
                  dirname[i] = c;
               }
            }
         }

         // Get the filename
         if (this->filename != NULL)
            free (this->filename);
         this->filename = malloc (namelen + 1);
         this->filename[0] = 0;
         if (this->filename == NULL)
            printf ("ERROR! could not allocate mem!\n");
         const char *s = param_to_string (context, paramtype, param, 0,
                                          namelen + 1, this->filename);

         if (num_params > 1)
         {
            param_to_string (context, paramtype, param, 1,
                             sizeof (this->mode), this->mode);
         }

         if (num_params > 2)
         {
            this->keep_open = param_to_int (context, paramtype, param, 2);
         }
         this->f = fopen (this->filename, this->mode);
         if (this->keep_open == 0 && this->f != NULL)
         {
            fclose (this->f);
            this->f = NULL;
         }
         else if (this->f == NULL)
            printf ("Could not open file %s\r\n", this->filename);
      }

      break;
   case write_rmcios:
      if (this == NULL)
         break;
      if (this->f == NULL)
      {
         this->f = fopen (this->filename, this->mode);
         if (this->f == NULL)
         {
            if (this->f == NULL)
               printf ("Could not open file %s\r\n", this->filename);
            break;
         }
      }

      if (num_params < 1)
      {
         fflush (this->f);
      }
      else
      {
         // Determine the needed buffer size
         plen = param_string_alloc_size (context, paramtype, param, 0);
         {
            char buffer[plen];  // allocate buffer
            s = param_to_string (context, paramtype, param, 0, plen, buffer);
            fprintf (this->f, "%s", s);
            fflush (this->f);
         }
      }
      if (this->keep_open == 0 && this->f != NULL)
      {
         fclose (this->f);
         this->f = NULL;
      }

      break;
   case read_rmcios:
      if (this == NULL)
      {
         int namelen;
         namelen = param_string_alloc_size (context, paramtype, param, 0);
         {
            char namebuffer[namelen];
            s = param_to_string (context, paramtype, param, 0,
                                 namelen, namebuffer);
            FILE *f;
            int fsize;
            
            f = fopen (s, "rb");
            if (f != 0)
            { 
               // seek to end of file
               fseek (f, 0, SEEK_END);     
               // get current file pointer
               fsize = ftell (f);  
               // seek back to beginning of file
               fseek (f, 0, SEEK_SET);     

               if (fsize != -1)
               {
                  // Allocate memory for the file:
                  char fbuffer[fsize];
                  fread (fbuffer, 1, fsize, f);
                  // Return the contents of the file in one call.
                  return_buffer (context, returnv, fbuffer, fsize);
               }
               fclose (f);
            }
         }
      }
      break;
   }
}


struct console_data
{
   FILE *fIN;
   FILE *fOUT;
   unsigned int id;
} console =
{
NULL, NULL, 0};

DWORD WINAPI console_rx_thread (LPVOID data)
{
   struct console_data *this = (struct console_data *) data;
   char s[2];
   s[1] = 0;
   while (1)
   {
      s[0] = fgetc (this->fIN);
      write_str (module_context,
                 linked_channels (module_context, this->id), s, this->id);
   }
   return 0;
}

void console_class_func (struct console_data *this,
                         const struct context_rmcios *context, int id,
                         enum function_rmcios function,
                         enum type_rmcios paramtype,
                         struct combo_rmcios *returnv,
                         int num_params,
                         const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv, 
                     "console channel help\r\n"
                     "setup console title\r\n"
                     "write console data\r\n"
                     "link console channel\r\n"
                     );
      break;
   case setup_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
         break;
      else
      {
         int plen = param_string_alloc_size (context, paramtype, param, 0);
         {
            char buffer[plen];
            const char *s;
            s = param_to_string (context, paramtype, param, 0, plen, buffer);
            SetConsoleTitleA (s);
         }
      }
      break;

   case write_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
      {
         fflush (this->fOUT);
      }
      else
      {
         // Determine the needed buffer size
         int plen = param_string_alloc_size (context, paramtype, param, 0);
         {
            char buffer[plen];  // allocate buffer
            const char *s = param_to_string (context, paramtype, param, 0, 
                                             plen, buffer);
            fprintf (this->fOUT, "%s", s);
            fflush (this->fOUT);
         }
      }
      break;
   }
}


/////////////////////////////////////////////////////////
// Clock to get elapsed time                    
/////////////////////////////////////////////////////////
struct clock_data
{
   DWORD start;
};

void clock_class_func (struct clock_data *this,
                       const struct context_rmcios *context, int id,
                       enum function_rmcios function,
                       enum type_rmcios paramtype, 
                       struct combo_rmcios *returnv,
                       int num_params, const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv, 
                     "clock channel help\r\n"
                     " create clock ch_name\r\n"
                     " read ch_name \r\n"
                     "  # read elapsed time (s)\r\n"
                     " write ch_name \r\n"
                     "  #read time, send to linked and reset time\r\n"
                     " link ch_name linked #link time output on reset\r\n"
                     );
      break;

   case create_rmcios:
      if (num_params < 1)
         break;
      // allocate new data
      this = (struct clock_data *) malloc (sizeof (struct clock_data)); 
      
      // create channel
      create_channel_param (context, paramtype, param, 0, 
                            (class_rmcios) clock_class_func, this); 

      //default values :
      this->start = GetTickCount ();

      break;
   case read_rmcios:
      if (this == NULL)
         break;
      DWORD ticknow;
      ticknow = GetTickCount ();
      float elapsed = ((float) (ticknow - this->start)) / 1000.0;
      return_float (context, returnv, elapsed);
      break;
   case write_rmcios:
      if (this == NULL)
         break;
      else
      {
         DWORD ticknow;
         ticknow = GetTickCount ();
         float elapsed = ((float) (ticknow - this->start)) / 1000.0;
         return_float (context, returnv, elapsed);
         write_f (context, linked_channels (context, id), elapsed);
         this->start = ticknow;
      }
      break;
   }
}

/////////////////////////////////////////////////////////
// Clock to get elapsed time with higher precicion                      
/////////////////////////////////////////////////////////
LARGE_INTEGER performance_frequency;

struct fast_clock_data
{
   LARGE_INTEGER start;
};

void fast_clock_class_func (struct fast_clock_data *this,
                            const struct context_rmcios *context, int id,
                            enum function_rmcios function,
                            enum type_rmcios paramtype,
                            struct combo_rmcios *returnv,
                            int num_params,
                            const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv,
                     "fast clock channel - Uses windows performance counter.\r\n"
                     " create fast_clock ch_name\r\n"
                     " read ch_name \r\n"
                     "   #read elapsed time (s)\r\n"
                     " write ch_name \r\n"
                     "   #read time, send to linked and reset time\r\n"
                     " link ch_name linked #link time output on reset\r\n"
                     );
      break;

   case create_rmcios:
      if (num_params < 1)
         break;
      
      // allocate new data
      this = (struct fast_clock_data *) 
             malloc (sizeof (struct fast_clock_data)); 
      
      // create channel
      create_channel_param (context, paramtype, param, 0, 
                            (class_rmcios) fast_clock_class_func, this);  

      //default values :
      QueryPerformanceCounter (&this->start);

      break;
   case read_rmcios:
      if (this == NULL)
         break;
      LARGE_INTEGER ticknow;
      QueryPerformanceCounter (&ticknow);
      float elapsed;
      elapsed =
         ((double) (ticknow.QuadPart - this->start.QuadPart)) /
         performance_frequency.QuadPart;
      return_float (context, returnv, elapsed);
      break;
   case write_rmcios:
      if (this == NULL)
         break;
      else
      {
         LARGE_INTEGER ticknow;
         QueryPerformanceCounter (&ticknow);
         float elapsed;
         elapsed =
            ((double) (ticknow.QuadPart - this->start.QuadPart)) /
            performance_frequency.QuadPart;
         return_float (context, returnv, elapsed);
         write_f (context, linked_channels (context, id), elapsed);
         this->start = ticknow;
      }
      break;
   }
}

struct subconsole_data
{
   HANDLE hconsole;
   COORD origin;
   COORD pos;
};

void subconsole_class_func (struct subconsole_data *this,
                            const struct context_rmcios *context, int id,
                            enum function_rmcios function,
                            enum type_rmcios paramtype,
                            struct combo_rmcios *returnv,
                            int num_params,
                            const union param_rmcios param)
{
   float wait_time;
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv, 
                     "subconsole cursor help\r\n"
                     " create subconsole newname\r\n"
                     " setup newname X Y \r\n"
                     "   # Set subconsole home position\r\n"
                     " setup newname \r\n"
                     "   # Set cursor to home\r\n"
                     " write newname data\r\n");
      break;

   case create_rmcios:
      if (num_params < 1)
         break;
      
      // allocate new data
      this = (struct subconsole_data *) 
             malloc (sizeof (struct subconsole_data));       
      if (this == NULL)
         break;
      //default values :
      this->origin.X = 0;
      this->origin.Y = 0;
      this->pos.X = 0;
      this->pos.Y = 0;

      this->hconsole = GetStdHandle (STD_OUTPUT_HANDLE);
      // Store the current console cursor position
      CONSOLE_SCREEN_BUFFER_INFO coninfo;
      if (!GetConsoleScreenBufferInfo (this->hconsole, &coninfo))
         printf ("GetConsoleScreenBufferInfo (%d)\n", GetLastError ());
      this->origin = coninfo.dwCursorPosition;
      this->pos = this->origin;

      // create channel
      create_channel_param (context, paramtype, param, 0, 
                            (class_rmcios) subconsole_class_func, this);  
      break;

   case setup_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
      {
         this->pos = this->origin;
         break;
      }
      if (num_params < 2)
         break;
      this->origin.X = param_to_int (context, paramtype, param, 0);
      this->origin.Y = param_to_int (context, paramtype, param, 1);
      this->pos = this->origin;

      break;

   case write_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
      {
         //fflush(this->f) ;
      }
      else
      {
         COORD lastpos;
         int plen;
         // Store the current console cursor position
         CONSOLE_SCREEN_BUFFER_INFO coninfo;
         
         if (!GetConsoleScreenBufferInfo (this->hconsole, &coninfo))
            printf ("GetConsoleScreenBufferInfo (%d)\n", GetLastError ());
         lastpos = coninfo.dwCursorPosition;

         // Set the subconsole cursor position
         SetConsoleCursorPosition (this->hconsole, this->pos);

         // Determine the needed buffer size
         plen = param_string_alloc_size (context, paramtype, param, 0);
         {
            char buffer[plen];  // allocate buffer
            const char *s;
            DWORD dwWritten;
            s = param_to_string (context, paramtype, param, 0, plen, buffer);
            WriteFile (this->hconsole, s, strlen (s), &dwWritten, NULL);
         }
         // Store the subconsole cursor position
         GetConsoleScreenBufferInfo (this->hconsole, &coninfo);
         this->pos = coninfo.dwCursorPosition;

         // Restore the console cursor position
         SetConsoleCursorPosition (this->hconsole, lastpos);
      }
      break;
   }
}


void wait_class_func (void *data, const struct context_rmcios *context,
                      int id, enum function_rmcios function,
                      enum type_rmcios paramtype, 
                      struct combo_rmcios *returnv,
                      int num_params, const union param_rmcios param)
{
   float wait_time;
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv, 
                     "wait channel help\r\n"
                     " write wait time # waits time (s)\r\n"
                     );
      break;

   case write_rmcios:
      if (num_params < 1)
         break;
      wait_time = param_to_float (context, paramtype, param, 0);
      Sleep (wait_time * 1000); // wait in ms
      break;
   }
}

struct delay_data
{
   int delay;                   // in ms 
};

void delay_class_func (struct delay_data *this,
                       const struct context_rmcios *context, int id,
                       enum function_rmcios function,
                       enum type_rmcios paramtype, 
                       struct combo_rmcios *returnv,
                       int num_params, const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv,
                     "delay channel help - Delay signals\r\n"
                     " create delay newname\r\n"
                     " setup newname s \r\n"
                     "    # set the delay time in seconds.\r\n"
                     " write newname | data | ... \r\n"
                     "    # Waits delay and sends data to linked channels \r\n"
                     " link newname linked \r\n"
                     );
      break;
   case create_rmcios:
      if (num_params < 1)
         break;
      
      // allocate new data 
      this = (struct delay_data *) 
             allocate_storage (context, sizeof (struct delay_data), 0);   
      
      if (this == NULL)
         break;
      this->delay = 0;
      
      // create channel
      create_channel_param (context, paramtype, param, 0, 
                            (class_rmcios) delay_class_func, this); 
      break;

   case setup_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
         break;
      this->delay = param_to_float (context, paramtype, param, 0) * 1000;
      break;

   case write_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
         break;
      
      // wait in ms
      Sleep (this->delay); 
      context->run_channel (context, linked_channels (context, id),
                            function, paramtype, returnv, num_params, param);
      break;
   }
}


UINT wTimerRes;
void terminate_windows_channels (void)
{
   timeEndPeriod (wTimerRes);
}

void exit_class_func (void *data, const struct context_rmcios *context,
                      int id, enum function_rmcios function,
                      enum type_rmcios paramtype, 
                      struct combo_rmcios *returnv,
                      int num_params, const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv,
                     "exit channel to exit system\r\n"
                     "write exit\r\n");
      break;
   case write_rmcios:
      exit (0);
      break;
   }
}


void hide_class_func (void *data, const struct context_rmcios *context,
                      int id, enum function_rmcios function,
                      enum type_rmcios paramtype, 
                      struct combo_rmcios *returnv,
                      int num_params, const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv, 
                     "hide console window\r\n"
                     "write hide console\r\n"
                     " -hides the console\r\n");
      break;
   case write_rmcios:
      if (num_params < 1)
         break;
      else
      {
         char buffer[32];
         buffer[0] = 0;
         param_to_string (context, paramtype, param, 0,
                          sizeof (buffer), buffer);
         if (strcmp (buffer, "console") == 0)
            ShowWindow (GetConsoleWindow (), SW_HIDE);
      }
      break;
   }
}

void show_class_func (void *data, const struct context_rmcios *context,
                      int id, enum function_rmcios function,
                      enum type_rmcios paramtype, 
                      struct combo_rmcios *returnv,
                      int num_params, const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv, 
                     "show window\r\n"
                     "write show console\r\n"
                     " -shows the console\r\n"
                     );
      break;
   case write_rmcios:
      if (num_params < 1)
         break;
      else
      {
         char buffer[32];
         buffer[0] = 0;
         param_to_string (context, paramtype, param, 0,
                          sizeof (buffer), buffer);
         if (strcmp (buffer, "console") == 0)
            ShowWindow (GetConsoleWindow (), SW_SHOW);
      }
      break;
   }
}

void init_windows_channels (const struct context_rmcios *context)
{
   printf ("Windows module\r\n[" VERSION_STR "]\r\n");
   module_context = context;

#define TARGET_RESOLUTION 1     // 1-millisecond target resolution

   TIMECAPS tc;
   if (timeGetDevCaps (&tc, sizeof (TIMECAPS)) != TIMERR_NOERROR)
   {
      // Error; application can't continue.
   }

   wTimerRes = min (max (tc.wPeriodMin, TARGET_RESOLUTION), tc.wPeriodMax);
   timeBeginPeriod (wTimerRes);

   atexit (terminate_windows_channels);
   QueryPerformanceFrequency (&performance_frequency);
   printf ("Windows timer resolution:%dms\r\n", wTimerRes);
   printf ("Windows Performance timer frequency: %ldHz\r\n",
           performance_frequency.QuadPart);

   // console output
   console.fOUT = fopen ("CONOUT$", "w");       
   
   // console input
   console.fIN = fopen ("CONIN$", "r"); 

   create_channel_str (context, "timer", (class_rmcios) timer_class_func, NULL);
   create_channel_str (context, "rtc", (class_rmcios) rtc_class_func, NULL);

   struct rtc_str_data;
   create_channel_str (context, "rtc_str", (class_rmcios) rtc_str_class_func,
                       &default_rtc_str_data);
   create_channel_str (context, "rtc_timer",
                       (class_rmcios) rtc_timer_class_func, NULL);

   time_t rawtime;
   time (&rawtime);
   timezone_offset = tz_offset_second (rawtime);

   console.id = create_channel_str (context, "console",
                                    (class_rmcios) console_class_func, 
                                    &console);
   
   create_channel_str (context, "subconsole",
                       (class_rmcios) subconsole_class_func, NULL);
   
   create_channel_str (context, "file", (class_rmcios) file_class_func, NULL);
   create_channel_str (context, "clock", (class_rmcios) clock_class_func, NULL);

   create_channel_str (context, "fast_clock",
                       (class_rmcios) fast_clock_class_func, NULL);
   create_channel_str (context, "wait", (class_rmcios) wait_class_func, NULL);
   create_channel_str (context, "delay", (class_rmcios) delay_class_func, NULL);
   create_channel_str (context, "exit", (class_rmcios) exit_class_func, NULL);
   create_channel_str (context, "hide", (class_rmcios) hide_class_func, NULL);
   create_channel_str (context, "show", (class_rmcios) show_class_func, NULL);

   // Check if stdin has been redirected:
   HANDLE hStdin;
   DWORD cMode;
   
   // Get stdin handle (can be redirected)
   hStdin = GetStdHandle (STD_INPUT_HANDLE); 
   if (GetConsoleMode (hStdin, &cMode) == 0)
   {
      // Create control channels for the console window.
      write_str (context, context->control, "create control conctl", 0);
      write_str (context, context->control, "link console conctl", 0);

      printf ("Console is redirected!\r\n");
      // Create thread for console reception
      DWORD myThreadID;
      
      // Start console control thread
      HANDLE myHandle = CreateThread (0, 0, console_rx_thread, &console, 
                                      0, &myThreadID);       
   }

   // Create the timer queue for rtc timer.
   HANDLE hTimerQueue = NULL;
   hTimerQueue = CreateTimerQueue ();
   HANDLE rtc_timer;
   CreateTimerQueueTimer (&rtc_timer,   //_Out_ PHANDLE phNewTimer,
                          hTimerQueue,  //_In_opt_ HANDLE TimerQueue,
                          //_In_ WAITORTIMERCALLBACK Callback:
                          (WAITORTIMERCALLBACK) rtc_ticker, 
                          NULL, //_In_opt_ PVOID Parameter,
                          100,  //_In_     DWORD DueTime,
                          100,  //_In_     DWORD Period,
                          0     //_In_     ULONG Flags
                         );
}

#ifdef INDEPENDENT_CHANNEL_MODULE
// function for dynamically loading the module
void API_ENTRY_FUNC init_channels (const struct context_rmcios *context)
{
   init_windows_channels (context);
}
#endif

