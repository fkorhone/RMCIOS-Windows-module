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
 * Tcp Socket module
 * Changelog: (date,who,description)
 *
 */
#include <stdio.h>
#include "channel_functions.h"

#include<io.h>
#include<winsock2.h>
#include <windows.h>
#pragma comment(lib,"ws2_32.lib")       //Winsock Library

const struct context_rmcios *module_context;

// tcpserver channel data
struct tcpserver_data
{
   int port;
   int id;
   int linked_channels;
   DWORD threadID;
   SOCKET connection;
};

// Server main thread
DWORD WINAPI tcpserver_thread (LPVOID lpvParam)
{
   struct tcpserver_data *this = (struct tcpserver_data *) lpvParam;

   SOCKET s;
   struct sockaddr_in server;

   ////////////////////////////////////////////////////////////////////
   // Open the socket
   ////////////////////////////////////////////////////////////////////
   if ((s = socket (AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
   {
      printf ("Could not create socket : %d", WSAGetLastError ());
      return 1;
   }
   ///////////////////////////////////////////////////////////////////
   // Bind the socket
   ///////////////////////////////////////////////////////////////////
   
   //Prepare the sockaddr_in structure:
   server.sin_family = AF_INET;
   server.sin_addr.s_addr = INADDR_ANY;
   server.sin_port = htons (this->port);
   if (bind (s, (struct sockaddr *) &server, sizeof (server)) == SOCKET_ERROR)
   {
      printf ("Bind failed with error code : %d", WSAGetLastError ());
      return 1;
   }

   ///////////////////////////////////////////////////////////////////
   // Start litening to incoming connections.
   ///////////////////////////////////////////////////////////////////
   listen (s, 3);
   struct sockaddr_in client;
   int cs;
   cs = sizeof (struct sockaddr_in);
   while ((this->connection =
           accept (s, (struct sockaddr *) &client, &cs)) != INVALID_SOCKET)
   {
      char buffer[1500];

      // empty write to signal new connection :
      write_iv (module_context, 
                linked_channels (module_context, this->id), 0, NULL) ;

      // Receive loop:
      while (1)
      {
         int bytes;
         bytes = recv (this->connection, buffer, sizeof (buffer), 0);
         if (bytes < 1)
            break;
         write_buffer (module_context,
                       linked_channels (module_context, this->id),
                       buffer, bytes, this->id);
      }
      
      if (this->connection != 0)
         shutdown (this->connection, SD_SEND);
   
   }
   printf ("Invalud socket -> closing server.\n");

   closesocket (s);
}


// Tcp server implementation function:
void tcpserver_class_func (struct tcpserver_data *this,
                           const struct context_rmcios *context, int id,
                           enum function_rmcios function,
                           enum type_rmcios paramtype,
                           union param_rmcios returnv, int num_params,
                           const union param_rmcios param)
{
   int plen;
   switch (function)
   {
   case help_rmcios:
      return_string (context, paramtype, returnv, "TCP server channel\n");
      return_string (context, paramtype, returnv, "create tcpserver newname\n");
      return_string (context, paramtype, returnv, "setup newname port\n");
      return_string (context, paramtype, returnv, "write newname data\n");
      return_string (context, paramtype, returnv, "link newname channel\n");
      break;

   case create_rmcios:
      if (num_params < 1)
         break;
      
      // allocate new data
      this = (struct tcpserver_data *) malloc (sizeof (struct tcpserver_data)); 
      
      //default values :
      this->port = 0;
      this->linked_channels = 0;
      this->threadID = 0;
      // create channel
      this->id = create_channel_param (context, paramtype, param, 0, 
                                    (class_rmcios) tcpserver_class_func, this);  
      break;

   case setup_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
      {
         if (this->connection != 0)
            shutdown (this->connection, SD_SEND);
         break;
      }
      this->port = param_to_integer (context, paramtype, param, 0);
      // Start thread
      CreateThread (0, 0, tcpserver_thread, this, 0, &this->threadID);
      break;

   case write_rmcios:
      if (this == NULL)
         break;
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
         if (send (this->connection, pbuffer.data, pbuffer.length, 0) < 0)
         {
            return_int (context, paramtype, returnv, -1);
            closesocket (this->connection);
         }
      }
      break;
   }
}

/***********************************************************************
 * Tcp client channel
 **********************************************************************/

// tcpclient channel data
struct tcpclient_data
{
   int port;
   char address[50];
   int id;
   DWORD threadID;
   SOCKET connection;
};

// TCP Client reception thread
DWORD WINAPI tcpclient_thread (LPVOID lpvParam)
{
   struct tcpclient_data *this = (struct tcpclient_data *) lpvParam;
   char buffer[1500];
   while (1)
   {
      int bytes;
      if (this->connection != 0)
         bytes = recv (this->connection, buffer, sizeof (buffer), 0);
      else
         Sleep (10);
      write_buffer (module_context,
                    linked_channels (module_context, this->id), buffer,
                    bytes, this->id);
   }
}

// Tcp client implementation function:
void tcpclient_class_func (struct tcpclient_data *this,
                           const struct context_rmcios *context, int id,
                           enum function_rmcios function,
                           enum type_rmcios paramtype,
                           union param_rmcios returnv, int num_params,
                           const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, paramtype, returnv, 
                     "TCP client channel\n"
                     "create tcpclient newname\n"
                     " setup newname ip port "
                     "  # Open connection\r\n"
                     " setup newname \r\n"
                     "  # Close connection\r\n"
                     " write newname data \r\n"
                     "  # -Write data to the connection. Reconnect if needed\r\n"
                     " link newname channel # Link received data to channel\r\n"
                      );
      break;

   case create_rmcios:
      if (num_params < 1)
         break;
      // allocate new data
      this = (struct tcpclient_data *) malloc (sizeof (struct tcpclient_data)); 
      
      //default values :
      this->port = 0;
      this->address[0] = 0;
      this->threadID = 0;
      
      // create channel
      this->id = create_channel_param (context, paramtype, param, 0, 
                                    (class_rmcios) tcpclient_class_func, this);

      // Open the socket
      if ((this->connection =
           socket (AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET)
      {
         printf ("Could not create socket : %d", WSAGetLastError ());
         break;
      }

      // Create receiving thread.
      CreateThread (0, 0, tcpclient_thread, this, 0, &this->threadID);
      break;

   case setup_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
      {
         if (this->connection != 0)
            shutdown (this->connection, SD_SEND);
         break;
      }
      if (num_params < 2)
         break;
      param_to_string (context, paramtype, param, 0,
                       sizeof (this->address), this->address);
      this->port = param_to_integer (context, paramtype, param, 1);
      {
         struct sockaddr_in server;

         server.sin_family = AF_INET;
         server.sin_addr.s_addr = inet_addr (this->address);
         server.sin_port = htons (this->port);

         //Connect to remote server
         if (connect
             (this->connection, (struct sockaddr *) &server,
              sizeof (server)) < 0)
         {
            puts ("TCP client connect error!\n");
         }
      }
      break;

   case write_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
         break;
      int plen = param_buffer_alloc_size (context, paramtype, param, 0);
      // Determine the needed buffer size
      {
         char buffer[plen];     
         // allocate buffer
         struct buffer_rmcios pbuffer;  
         
         // structure pointer to buffer data
         pbuffer = param_to_buffer (context, paramtype, param, 0, plen, buffer);
         if (send (this->connection, pbuffer.data, pbuffer.length, 0) < 0)
         {
            return_int (context, paramtype, returnv, -1);
            closesocket (this->connection);
         }
      }
      break;
   }
}

/***********************************************************************
 * UDP client channel
 **********************************************************************/

// udpclient channel data
struct client_data
{
   int port;
   char address[50];
   int id;
   DWORD threadID;
   SOCKET connection;
   struct sockaddr_in destination;
   int slen;
};

// UDP Client reception thread
DWORD WINAPI udpclient_thread (LPVOID lpvParam)
{
   struct client_data *this = (struct client_data *) lpvParam;
   char buffer[1500];
   while (1)
   {
      int bytes = 0;

      if (this->connection != 0)
      {
         if ((bytes =
              recvfrom (this->connection, buffer, sizeof (buffer), 0,
                        (struct sockaddr *) &this->destination,
                        &this->slen)) == SOCKET_ERROR)
         {
            Sleep (10);
         }
         else
         {
            write_buffer (module_context,
                          linked_channels (module_context,
                                           this->id), buffer, bytes, this->id);
         }
         Sleep (10);
      }
      else
      {
         printf ("No connection\n");
         Sleep (10);
      }
   }
}

// Tcp client implementation function:
void udpclient_class_func (struct client_data *this,
                           const struct context_rmcios *context, int id,
                           enum function_rmcios function,
                           enum type_rmcios paramtype,
                           union param_rmcios returnv, int num_params,
                           const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, paramtype, returnv, 
                     "UDP client channel\n"
                     "create udpclient newname\n"
                     " setup newname ip port # Set connection destination\r\n"
                     " write newname data # Send data to the destination \r\n"
                     " link newname channel # Link received data to channel\r\n"
                     );
      break;

   case create_rmcios:
      if (num_params < 1)
         break;

      // allocate new data
      this = (struct client_data *) malloc (sizeof (struct client_data));       

      //default values :
      this->port = 0;
      this->address[0] = 0;
      this->threadID = 0;
      this->slen = sizeof (this->destination);
      memset ((char *) &this->destination, 0, sizeof (this->destination));
      
      // create channel
      this->id = create_channel_param (context, paramtype, param, 0, 
                           (class_rmcios) udpclient_class_func, this);        

      // Open the socket
      if ((this->connection =
           socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SOCKET_ERROR)
      {
         printf ("Could not create socket : %d", WSAGetLastError ());
         break;
      }

      // Create receiving thread.
      CreateThread (0, 0, udpclient_thread, this, 0, &this->threadID);
      break;

   case setup_rmcios:
      if (this == NULL)
         break;
      if (num_params < 2)
         break;
      param_to_string (context, paramtype, param, 0,
                       sizeof (this->address), this->address);
      this->port = param_to_integer (context, paramtype, param, 1);
      {
         this->destination.sin_family = AF_INET;
         this->destination.sin_addr.S_un.S_addr = inet_addr (this->address);
         this->destination.sin_port = htons (this->port);
      }
      break;

   case write_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
         break;
      int plen = param_buffer_alloc_size (context, paramtype, param, 0);       
      // Determine the needed buffer size
      {
         // allocate buffer
         char buffer[plen];     
         struct buffer_rmcios pbuffer;  
         // structure pointer to buffer data
         pbuffer = param_to_buffer (context, paramtype, param, 0, plen, buffer);
         //send the message
         if (sendto
             (this->connection, pbuffer.data, pbuffer.length, 0,
              (struct sockaddr *) &this->destination,
              this->slen) == SOCKET_ERROR)
         {
            printf ("sendto() failed with error code : %d", WSAGetLastError ());
         }
      }
      break;
   }
}


/***********************************************************************
 * UDP server channel
 **********************************************************************/

// udpserver channel data
struct udpserver_data
{
   int port;
   int id;
   DWORD threadID;
   SOCKET connection;
   struct sockaddr_in server, last_client;
   int slen;
};

// UDP Client reception thread
DWORD WINAPI udpserver_thread (LPVOID lpvParam)
{
   struct udpserver_data *this = (struct udpserver_data *) lpvParam;
   char buffer[1500];
   while (1)
   {
      int bytes = 0;

      if (this->connection != 0)
      {
         if ((bytes = recvfrom (this->connection,
                                buffer,
                                sizeof (buffer),
                                0,
                                (struct sockaddr *) &this->last_client,
                                &this->slen)) == SOCKET_ERROR)
         {
            Sleep (10);
         }
         else
         {
            write_buffer (module_context,
                          linked_channels (module_context,
                                           this->id), buffer, bytes, this->id);
         }
      }
      else
         Sleep (10);
   }
}

// Tcp client implementation functiona
void udpserver_class_func (struct udpserver_data *this,
                           const struct context_rmcios *context, int id,
                           enum function_rmcios function,
                           enum type_rmcios paramtype,
                           union param_rmcios returnv, int num_params,
                           const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, paramtype, returnv, 
                     "UDP server channel\n"
                     "create udpclient newname\n"
                     " setup newname port # Set receiving port\r\n"
                     " write newname data # Send data to the latest clienta\r\n"
                     " link newname channel # Link received data to channel.\r\n"
                     );
      break;

   case create_rmcios:
      if (num_params < 1)
         break;
      
      // allocate new data
      this = (struct udpserver_data *) malloc (sizeof (struct udpserver_data)); 

      //default values :
      this->port = 0;
      this->threadID = 0;
      this->slen = sizeof (this->server);
      memset ((char *) &this->server, 0, sizeof (this->server));
      this->id = create_channel_param (context, paramtype, param, 0, 
                                 (class_rmcios) udpserver_class_func, this);
      // create channel

      // Open the socket
      if ((this->connection =
           socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET)
      {
         printf ("Could not create socket : %d", WSAGetLastError ());
         break;
      }

      // Create receiving thread.
      CreateThread (0, 0, udpserver_thread, this, 0, &this->threadID);
      break;

   case setup_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
         break;
      this->port = param_to_integer (context, paramtype, param, 0);
      this->server.sin_family = AF_INET;
      this->server.sin_addr.s_addr = INADDR_ANY;
      this->server.sin_port = htons (this->port);
      //Bind
      if (bind
          (this->connection, (struct sockaddr *) &this->server,
           sizeof (this->server)) == SOCKET_ERROR)
      {
         printf ("Bind failed with error code : %d", WSAGetLastError ());
      }
      break;

   case write_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
         break;
      int plen = param_buffer_alloc_size (context, paramtype, param, 0);
      // Determine the needed buffer size
      {
         // allocate buffer
         char buffer[plen]; 
         
         // structure pointer to buffer data
         struct buffer_rmcios pbuffer; 
         pbuffer = param_to_buffer (context, paramtype, param, 0, plen, buffer);
         
         // send the message
         if (sendto
             (this->connection, pbuffer.data, pbuffer.length, 0,
              (struct sockaddr *) &this->last_client,
              this->slen) == SOCKET_ERROR)
         {
            printf ("sendto() failed with error code : %d", WSAGetLastError ());
         }
      }
      break;
   }
}

void init_socket_channels (const struct context_rmcios *context)
{
   printf ("Windows socket channel module\r\n[" VERSION_STR "]\r\n");
   module_context = context;

   WSADATA wsa;
   //Initialize the socket system
   if (WSAStartup (MAKEWORD (2, 2), &wsa) != 0)
   {
      printf ("Failed. Error Code : %d", WSAGetLastError ());
      return;
   }

   create_channel_str (context, "tcpserver",
                       (class_rmcios) tcpserver_class_func, NULL);
   create_channel_str (context, "tcpclient",
                       (class_rmcios) tcpclient_class_func, NULL);
   create_channel_str (context, "udpserver",
                       (class_rmcios) udpserver_class_func, NULL);
   create_channel_str (context, "udpclient",
                       (class_rmcios) udpclient_class_func, NULL);
}

#ifdef INDEPENDENT_CHANNEL_MODULE
// function for dynamically loading the module
void API_ENTRY_FUNC init_channels (const struct context_rmcios *context)
{
   module_context = context;
   init_socket_channels (context);
}
#endif

