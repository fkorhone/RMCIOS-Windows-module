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
 * Windows serial channel implementation
 */
#define DLL

#define _WIN32_WINNT 0x0500

#include <stdio.h>
#include <windows.h>

#include "RMCIOS-functions.h"

const struct context_rmcios *module_context;

void setDCB (HANDLE h, DCB * dcb)
{
   // Maintain dummy and reserved values and set our values
   DCB temp;
   temp.DCBlength = sizeof (DCB);
   if (!GetCommState (h, &temp))
   {
      printf ("Error! GetCommState\n");
   }
   dcb->fDummy2 = temp.fDummy2;
   dcb->wReserved = temp.wReserved;
   dcb->wReserved1 = temp.wReserved1;
   if (!SetCommState (h, dcb))
   {
      printf ("Error! SetCommState\n");
   }
}

//////////////////////////////////////////////////////////
// Serial class
/////////////////////////////////////////////////////////
struct serial_data
{
   unsigned int id;
   HANDLE serial_handle;
   char *rxbuffer;
   int bufflen;
   int rindex;
   char p_name[50];
   int halt_serial;
   DCB ss_dcb;
   int ctl_mode;
   int config_changed;
};

DWORD WINAPI serial_rx_thread (LPVOID data)
{
   struct serial_data *this = (struct serial_data *) data;
   DWORD dwRead;
   char s[2];
   s[1] = 0;
   char open_error = 0;
   while (1)
   {
      if(this->config_changed == 1)
      {
         this->config_changed = 0;
         CloseHandle (this->serial_handle);
         Sleep(10); // Give driver some time to close
         this->serial_handle = INVALID_HANDLE_VALUE;
      }

      dwRead = 0;
      if (this->serial_handle != INVALID_HANDLE_VALUE)
      {
         if (ReadFile (this->serial_handle, s, 1, &dwRead, NULL) == 0)
         {
            CloseHandle (this->serial_handle);
            this->serial_handle = INVALID_HANDLE_VALUE;
            // Set to invalid for usb disconnection -> reconnection
         }
      }
      else      
      // Not open -> attemp to reopen
      {
         if (this->p_name[0] != 0)
         {
            this->serial_handle = CreateFile (this->p_name, 
                                              GENERIC_READ | GENERIC_WRITE, 
                                              0,   
                                              NULL,  // no security attrs
                                              OPEN_EXISTING,    
                                              0,     // not overlapped I/O
                                              NULL); 

            if (this->serial_handle != INVALID_HANDLE_VALUE)
            {
               open_error = 0;

               setDCB (this->serial_handle, &this->ss_dcb);

               COMMTIMEOUTS timeouts = { 0 };
               timeouts.ReadIntervalTimeout = 50;
               timeouts.ReadTotalTimeoutConstant = 50;
               timeouts.ReadTotalTimeoutMultiplier = 10;
               timeouts.WriteTotalTimeoutConstant = 50;
               timeouts.WriteTotalTimeoutMultiplier = 10;
               if (!SetCommTimeouts (this->serial_handle, &timeouts))
               {
                  //handle error
               }
            }
            else
            {
               if (open_error == 0)
               {
                  printf ("Error! Could not open serial handle!\n");
                  open_error = 1;
               }
            }
         }
      }

      if (dwRead != 0)
      {
         if (this->bufflen > this->rindex - 1)
         {
            this->rxbuffer[this->rindex] = *s;
            this->rindex += dwRead;
            this->rxbuffer[this->rindex] = 0;
         }
         write_str (module_context,
                    linked_channels (module_context, this->id), s, this->id);
      }
      else
      {
         Sleep (10);
      }
   }
   return 0;
}

void serial_port_subchan_func (struct serial_data *this,
                               const struct context_rmcios *context, int id,
                               enum function_rmcios function,
                               enum type_rmcios paramtype,
                               struct combo_rmcios *returnv,
                               int num_params, const union param_rmcios param)
{
   switch (function)
   {
   case write_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
      {
         SetCommBreak (this->serial_handle);
         Sleep (100);
         ClearCommBreak (this->serial_handle);
      }
      else
      {
         int oper = param_to_integer (context, paramtype, param, 0);
         int sigbreak = oper >> 2 & 1;
         this->ss_dcb.fDtrControl = oper & 1;
         this->ss_dcb.fRtsControl = (oper >> 1) & 1;
         if (sigbreak == 1)
         {
            SetCommBreak (this->serial_handle);
            Sleep (100);
            ClearCommBreak (this->serial_handle);
         }
         setDCB (this->serial_handle, &this->ss_dcb);
      }

      break;

   case setup_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
         break;
      if (num_params > 1)
         this->ss_dcb.fDtrControl = param_to_int (context, paramtype, param, 1);
      if (num_params > 2)
         this->ss_dcb.fRtsControl = param_to_int (context, paramtype, param, 2);
      strcpy (this->p_name, "\\\\.\\");
      param_to_string (context, paramtype, param, 0,
                       sizeof (this->p_name) - strlen (this->p_name),
                       this->p_name + strlen (this->p_name));
      if (this->serial_handle != INVALID_HANDLE_VALUE)
         // Close the handle -> thread will reopen
         CloseHandle (this->serial_handle);     
      break;
   }
}

void serial_class_func (struct serial_data *this,
                        const struct context_rmcios *context, int id,
                        enum function_rmcios function,
                        enum type_rmcios paramtype,
                        struct combo_rmcios *returnv,
                        int num_params, const union param_rmcios param)
{
   char *s;
   DWORD dwRead, dwWritten;
   int plen;
   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv,
                     "Serial channel help.\r\n"
                     " create serial newname | comX\r\n"
                     " read serial # read list of system serialports\r\n"
                     " creates subchannel: \r\n"
                     "   newname_port for port special functions\r\n"
                     " setup newname_port comX | dtr| rts \r\n"
                     "  # -set physical com port and control line states\r\n"
                     "  rts: 0=rts-deactive \r\n"
                     "	1=rts-active \r\n"
                     "	2=low-active-rts_transmit \r\n"
                     "	3=high-active-rts_transmit \r\n"
                     "	4=low-active-cts_flow_allow \r\n"
                     "	5=high-active-cts_flow_allow \r\n"
                     "	6=low_active-rts_transmit, high-active_cts_flow_allow"
                     "7=high_active-rts_transmit, low-active_cts_flow_allow"
                     " write newname_port 0 # dtr=off\r\n"
                     " write newname_port 1 # dtr=on rts=off \r\n"
                     " write newname_port 2 # dtr=off rts=on \r\n"
                     " write newname_port 3 # dtr=on rts=on \r\n"
                     " write newname_port 4 # send break; \r\n"
                     " write newname_port # Send break signal\r\n"
                     " setup newname baud_rate \r\n"
                     "               | data_bits parity(0=NONE 1=ODD 2=EVEN)\r\n"
                     "                 stop_bits \r\n"
                     "               | rx_buff_len | ctl_mode\r\n"
                     " 	ctl_mode = flow and transmit control mode bitmask: \r\n"
                     "	1= DTR_ON_DURING_TRANSMIT"
                     "	2= DTR_OFF_DURING_TRANSMIT"
                     " 	4= RTS_ON_DURING_TRANSMIT"
                     " 	8= RTS_OFF_DURING_TRANSMIT"
                     "	16= DSR_LOW_TO_TRANSMIT"
                     "	32= DSR_HIGH_TO_TRANSMIT"
                     "	64= CTS_LOW_TO_TRANSMIT"
                     "	128= CTS_HIGH_TO_TRANSMIT"
                     " write newname data \r\n"
                     "    # -Transmit data to serial. \\r=CR \\n=LF \r\n"
                     " read newname \r\n"
                     "    # -Read data in receive buffer since last write.\r\n"
                     " link newname channel \r\n"
                     "    # -writes all arriving bytes to channel.\r\n");
      break;

   case create_rmcios:
      if (num_params < 1) break;
      // allocate new data
      this = (struct serial_data *) 
             calloc (1, sizeof (struct serial_data));    
      
      if (this == NULL) break;

      //default values :
      
      // allocate new memory
      this->rxbuffer = (char *) malloc (sizeof (char) * 1024);  
      this->rxbuffer[0] = 0;
      this->bufflen = 1024;
      this->rindex = 0;
      this->halt_serial = 0;
      this->p_name[0] = 0;
      this->serial_handle = INVALID_HANDLE_VALUE;
      this->ss_dcb.DCBlength = sizeof (DCB);
      this->ss_dcb.fBinary = 1;
      this->ss_dcb.BaudRate = CBR_9600;
      this->ss_dcb.ByteSize = 8;
      this->ss_dcb.Parity = NOPARITY;
      this->ss_dcb.StopBits = ONESTOPBIT;
      this->ss_dcb.fDtrControl = 1;
      this->ss_dcb.fRtsControl = 1;
      this->ctl_mode = 0;
      this->config_changed = 0;

      this->id =
         create_channel_param (context, paramtype, param, 0,
                               (class_rmcios) serial_class_func, this);
      create_subchannel_str (context, this->id, "_port",
                             (class_rmcios) serial_port_subchan_func, this);

      // Set serial port name:
      if (num_params >= 2)
      {
         strcpy (this->p_name, "\\\\.\\");
         param_to_string (context, paramtype, param, 1,
                          sizeof (this->p_name) -
                          strlen (this->p_name),
                          this->p_name + strlen (this->p_name));
      }

      // Create thread for serial reception
      DWORD myThreadID = 0;
      HANDLE myHandle =
         CreateThread (0, 0, serial_rx_thread, this, 0, &myThreadID);

      break;

   case setup_rmcios:
      // 0.baud_rate | 1.data_bits 2.parity 3.stop_bits | 4.rx_buff_len
      if (num_params < 1 || this == NULL)
         break;
      else
      {
         this->ss_dcb.BaudRate = param_to_int (context, paramtype, param, 0);
         if (num_params > 3)
         {
            this->ss_dcb.ByteSize = param_to_int (context, paramtype, param, 1);
            this->ss_dcb.Parity = param_to_int (context, paramtype, param, 2);
            this->ss_dcb.StopBits =
               (param_to_int (context, paramtype, param, 3) - 1) >> 1;
         }
         // Signal thread to reopen and configure
         this->config_changed = 1;

         if (num_params < 5) break;
         
         // receive buffer length
         unsigned int bufflen;
         // temporarily disable buffering in thread:
         this->bufflen = 0;     
         
         //4.rx_buff_len 
         bufflen = param_to_int (context, paramtype, param, 4); 
         if (this->rxbuffer != NULL)
            // release memory
            free (this->rxbuffer);      
         
         // allocate new memory
         this->rxbuffer = (char *) malloc (sizeof (char) * bufflen);    
         this->rxbuffer[0] = 0;
         this->bufflen = bufflen;
         if (num_params < 6) break; 
         this->ctl_mode = param_to_int (context, paramtype, param, 5);
         break;
      }

   case read_rmcios:
      if (this == NULL)
      {
         // List system serial port identifiers from windows registry.
         HKEY hey;
         DWORD dwRet;
         dwRet = RegOpenKeyExA (HKEY_LOCAL_MACHINE,// _In_ HKEY hKey,
                                // _In_opt_ LPCTSTR lpSubKey:
                                "HARDWARE\\DEVICEMAP\\SERIALCOMM",      
                                0,      //_In_ DWORD ulOptions,
                                KEY_READ,// _In_ REGSAM samDesired,
                                &hey    //_Out_ PHKEY phkResult
            );

         DWORD values;
         DWORD max_namelen;
         DWORD max_valuelen;

         dwRet = RegQueryInfoKey (hey,  //_In_        HKEY     hKey,
                                  NULL, // _Out_opt_   LPTSTR  lpClass,
                                  NULL, // _Inout_opt_ LPDWORD lpcClass,
                                  NULL, //_Reserved_  LPDWORD  lpReserved,
                                  NULL, //_Out_opt_   LPDWORD  lpcSubKeys,
                                  NULL, // _Out_opt_   LPDWORD lpcMaxSubKeyLen,
                                  NULL, // _Out_opt_   LPDWORD lpcMaxClassLen,
                                  &values,      // _Out_opt_ LPDWORD lpcValues,
                                  // _Out_opt_ LPDWORD lpcMaxValueNameLen:
                                  &max_namelen, 
                                  // _Out_opt_ LPDWORD lpcMaxValueLen:
                                  &max_valuelen,
                                  // _Out_opt_ LPDWORD lpcbSecurityDescriptor:
                                  NULL, 
                                  NULL  // _Out_opt_ PFILETIME lpftLastWriteTime
                                 );

         if (values)
         {
            DWORD retCode;
            int i;
            for (i = 0, retCode = ERROR_SUCCESS; i < values; i++)
            {
               char value[max_valuelen + 2];
               char name[max_namelen + 2];
               DWORD valuelen = max_valuelen + 1;
               DWORD namelen = max_namelen + 1;
               value[0] = 0;

               retCode = RegEnumValueA (hey, i,
                                        name,
                                        &namelen, NULL, NULL, value, &valuelen);


               if (retCode == ERROR_SUCCESS)
               {
                  return_string (context, returnv, value);
                  return_string (context, returnv, " ");
               }
            }
         }
         RegCloseKey (hey);
      }
      else
         return_string (context, returnv, this->rxbuffer);
      break;

   case write_rmcios:
      if (this == NULL)
         break;
      // reset receive buffer:
      this->rindex = 0; 
      // reset receive buffer:
      this->rxbuffer[0] = 0;    

      if (num_params < 1)
         break;

      plen = param_buffer_alloc_size (context, paramtype, param, 0);    
      // Determine the needed buffer size
      {
         // allocate buffer
         char buffer[plen];     
         // structure pointer to buffer data
         struct buffer_rmcios pbuffer;  
         pbuffer = param_to_buffer (context, paramtype, param, 0, plen, buffer);
         if (this->serial_handle != INVALID_HANDLE_VALUE)
         {
            WriteFile (this->serial_handle, pbuffer.data,
                       pbuffer.length, &dwWritten, NULL);
         }
      }
      break;
   }
}

void init_serial_channels (const struct context_rmcios *context)
{
   printf ("Windows serial module\r\n[" VERSION_STR "]\r\n");
   module_context = context;

   create_channel_str (context, "serial", (class_rmcios) serial_class_func,
                       NULL);
/* Removed : seen not to work on some machines.
   // List system serial port identifiers from windows registry.
   HKEY hey;
   DWORD dwRet;
   dwRet = RegOpenKeyExA (HKEY_LOCAL_MACHINE,   //_In_ HKEY hKey,
                          //_In_opt_ LPCTSTR lpSubKey:
                          "HARDWARE\\DEVICEMAP\\SERIALCOMM", 
                          0,    //_In_     DWORD   ulOptions,
                          KEY_READ,     //  _In_     REGSAM  samDesired,
                          &hey  //_Out_    PHKEY   phkResult
      );

   DWORD values;
   DWORD max_namelen;
   DWORD max_valuelen;

   dwRet = RegQueryInfoKey (hey,        //_In_        HKEY hKey,
                            NULL,       // _Out_opt_   LPTSTR lpClass,
                            NULL,       // _Inout_opt_ LPDWORD lpcClass,
                            NULL,       //_Reserved_  LPDWORD lpReserved,
                            NULL,       //_Out_opt_   LPDWORD   lpcSubKeys,
                            NULL,       // _Out_opt_   LPDWORD   lpcMaxSubKeyLen,
                            NULL,       // _Out_opt_   LPDWORD   lpcMaxClassLen,
                            &values,    // _Out_opt_   LPDWORD   lpcValues,
                            &max_namelen,       // _Out_opt_   LPDWORD   lpcMaxValueNameLen,
                            &max_valuelen,      // _Out_opt_   LPDWORD   lpcMaxValueLen,
                            NULL,       // _Out_opt_   LPDWORD   lpcbSecurityDescriptor,
                            NULL        // _Out_opt_   PFILETIME lpftLastWriteTime
      );

   if (values)
   {
      DWORD retCode;
      int i;
      for (i = 0, retCode = ERROR_SUCCESS; i < values; i++)
      {
         char value[max_valuelen + 2];
         char name[max_namelen + 2];
         DWORD valuelen = max_valuelen + 1;
         DWORD namelen = max_namelen + 1;
         value[0] = 0;

         retCode = RegEnumValueA (hey, i,
                                  name, &namelen, NULL, NULL, value, &valuelen);


         if (retCode == ERROR_SUCCESS)
         {
            printf ("%s %s\n", value, name);
         }
         else
            printf ("ERROR:%d\n", retCode);
      }
   }
   RegCloseKey (hey);*/
}

#ifdef INDEPENDENT_CHANNEL_MODULE
// function for dynamically loading the module
void API_ENTRY_FUNC init_channels (const struct context_rmcios *context)
{
   init_serial_channels (context);
}
#endif
