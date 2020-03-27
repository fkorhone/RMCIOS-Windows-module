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
 * Pipeserver channel module.
 * Broadcasting pipe server in windows.
 *
 * Changelog: (date,who,description)
 */
#include <windows.h>
#include <stdio.h>
#include "RMCIOS-functions.h"

#define CONNECTING_STATE 0
#define READING_STATE 1
#define WRITING_STATE 2
#define INSTANCES 8
#define PIPE_TIMEOUT 5000
#define BUFSIZE 4096

const struct context_rmcios *module_context;

typedef struct
{
   OVERLAPPED oOverlap;
   OVERLAPPED wOverlap;
   HANDLE hPipeInst;
   char chRequest[BUFSIZE];
   DWORD cbRead;
   char chReply[BUFSIZE];
   DWORD cbToWrite;
   DWORD dwState;
   BOOL fPendingIO;
} PIPEINST, *LPPIPEINST;

struct pipeserver_data
{
   int id;
   HANDLE hin;
   HANDLE hout;
   char *pipename;
   PIPEINST pipes[INSTANCES];
   int echo;
   int timeout;
};

VOID DisconnectAndReconnect (LPPIPEINST);
BOOL ConnectToNewClient (HANDLE, LPOVERLAPPED);
VOID GetAnswerToRequest (LPPIPEINST);

DWORD WINAPI pipeserver (LPVOID lpvParam)
{
   struct pipeserver_data *this = (struct pipeserver_data *) lpvParam;

   DWORD i, j, dwWait, cbRet, dwErr;
   BOOL fSuccess;
   PIPEINST *Pipe = this->pipes;

   HANDLE hEvents[INSTANCES];
   HANDLE wEvents[INSTANCES];

   //LPTSTR lpszPipename = TEXT("\\\\.\\pipe\\mynamedpipe"); 

   // The initial loop creates several instances of a named pipe 
   // along with an event object for each instance.  An 
   // overlapped ConnectNamedPipe operation is started for 
   // each instance. 

   for (i = 0; i < INSTANCES; i++)
   {

      // Create an event object for this instance. 
      hEvents[i] = CreateEvent (NULL,   // default security attribute 
                                TRUE,   // manual-reset event 
                                TRUE,   // initial state = signaled 
                                NULL);  // unnamed event object 

      if (hEvents[i] == NULL)
      {
         printf ("CreateEvent failed with %d.\n", GetLastError ());
         return 0;
      }

      // Create an write event object for this instance. 
      wEvents[i] = CreateEvent (NULL,   // default security attribute 
                                TRUE,   // manual-reset event 
                                TRUE,   // initial state = signaled 
                                NULL);  // unnamed event object 

      if (wEvents[i] == NULL)
      {
         printf ("CreateEvent failed with %d.\n", GetLastError ());
         return 0;
      }

      memset(&Pipe[i].oOverlap, 0, sizeof(Pipe[i].oOverlap)); 
      memset(&Pipe[i].wOverlap, 0, sizeof(Pipe[i].wOverlap)); 
      Pipe[i].oOverlap.hEvent = hEvents[i];
      Pipe[i].wOverlap.hEvent = wEvents[i];

      Pipe[i].hPipeInst = CreateNamedPipeA (
                              this->pipename, // pipe name 
                              PIPE_ACCESS_DUPLEX | // read/write access 
                              FILE_FLAG_OVERLAPPED, // overlapped mode 
                              PIPE_TYPE_MESSAGE | // message-type pipe 
                              PIPE_READMODE_MESSAGE | // message-read mode 
                              PIPE_WAIT,  // blocking mode 
                              INSTANCES,  // number of instances 
                              BUFSIZE * sizeof (TCHAR), // output buffer size 
                              BUFSIZE * sizeof (TCHAR), // input buffer size 
                              PIPE_TIMEOUT,// client time-out 
                              NULL); // default security attributes 

      if (Pipe[i].hPipeInst == INVALID_HANDLE_VALUE)
      {
         int error = GetLastError ();
         printf ("CreateNamedPipe failed with %d.\n", error);
         if (error == 231)
            printf
               ("Maybe you have multiple programs running simultanously?\n");
         return 0;
      }

      // Call the subroutine to connect to the new client
      Pipe[i].fPendingIO = ConnectToNewClient (Pipe[i].hPipeInst,
                                               &Pipe[i].oOverlap);

      // still connecting 
      Pipe[i].dwState = Pipe[i].fPendingIO ? CONNECTING_STATE : 
         READING_STATE; // ready to read 
   }

   while (1)
   {
      // Wait for the event object to be signaled, indicating 
      // completion of an overlapped read, write, or 
      // connect operation. 
      if (this->timeout < 0)
      {
         dwWait = WaitForMultipleObjects (
                                 INSTANCES,    // number of event objects 
                                 hEvents,      // array of event objects 
                                 FALSE,        // does not wait for all 
                                 INFINITE);    // timeout in ms, 
                                               // INFINITE waits infinitely 
      }
      else
      {
         dwWait = WaitForMultipleObjects (INSTANCES, // number of event objects 
                                          hEvents,   // array of event objects 
                                          FALSE,     // does not wait for all 
                                          this->timeout); // timeout in ms, 
                                                 // INFININITE waits infinitely 
      }

      if (dwWait == WAIT_TIMEOUT)
      {
         // Make empty writes to all connected pipes 
         // (Make them exit from blocking read functions)
         for (j = 0; j < INSTANCES; j++)
         {
            //SetEvent(this->pipes[j].oOverlap.hEvent ) ;
            WriteFile (this->pipes[j].hPipeInst,
                       "", 0, NULL, &this->pipes[j].wOverlap);

         }
         continue;
      }
      else
      {
         // dwWait shows which pipe completed the operation. 
         i = dwWait - WAIT_OBJECT_0;    // determines which pipe 
         if (i < 0 || i > (INSTANCES - 1))
         {
            printf ("Index out of range.\n");
            return 0;
         }
      }

      // Get the result if the operation was pending. 
      if (Pipe[i].fPendingIO)
      {
         fSuccess = GetOverlappedResult (Pipe[i].hPipeInst, // handle to pipe 
                                         &Pipe[i].oOverlap, // OVERLAPPED struct
                                         &cbRet,        // bytes transferred 
                                         FALSE);        // do not wait 

         switch (Pipe[i].dwState)
         {
            // Pending connect operation 
         case CONNECTING_STATE:
            if (!fSuccess)
            {
               printf ("Error %d.\n", GetLastError ());
               return 0;
            }
            Pipe[i].dwState = READING_STATE;
            break;

            // Pending read operation 
         case READING_STATE:
            if (!fSuccess || cbRet == 0)
            {
               DisconnectAndReconnect (Pipe + i);
               continue;
            }
            Pipe[i].cbRead = cbRet;
            Pipe[i].dwState = WRITING_STATE;
            break;

         default:
            {
               printf ("Invalid pipe state.\n");
               return 0;
            }
         }
      }

      // The pipe state determines which operation to do next. 

      switch (Pipe[i].dwState)
      {
         // READING_STATE: 
         // The pipe instance is connected to the client 
         // and is ready to read a request from the client. 
      case WRITING_STATE:

         // Local echo before processing
         if (this->echo == 1)
         {
            // Write message to all connected pipes
            for (j = 0; j < INSTANCES; j++)
            {
               if (Pipe[j].dwState != CONNECTING_STATE)
               {
                  BOOL success;

                  // write contents to file
                  success = WriteFile (Pipe[j].hPipeInst,
                                       Pipe[i].chRequest,
                                       Pipe[i].cbRead,
                                       &cbRet, &Pipe[j].wOverlap);
               }

            }
         }

         // Send data to linked channels:
         write_buffer (module_context,
                       linked_channels (module_context, this->id),
                       Pipe[i].chRequest, Pipe[i].cbRead, this->id);

         Pipe[i].chRequest[0] = 0;
         Pipe[i].cbRead = 0;
         Pipe[i].dwState = READING_STATE;

      case READING_STATE:

         fSuccess = ReadFile (Pipe[i].hPipeInst,
                              Pipe[i].chRequest,
                              BUFSIZE * sizeof (TCHAR),
                              &Pipe[i].cbRead, &Pipe[i].oOverlap);

         // The read operation completed successfully. 
         if (fSuccess && Pipe[i].cbRead != 0)
         {
            Pipe[i].fPendingIO = FALSE;
            Pipe[i].dwState = WRITING_STATE;
            continue;
         }

         // The read operation is still pending. 

         dwErr = GetLastError ();
         if (!fSuccess && (dwErr == ERROR_IO_PENDING))
         {
            Pipe[i].fPendingIO = TRUE;
            continue;
         }

         // An error occurred; disconnect from the client. 

         DisconnectAndReconnect (Pipe + i);
         break;

      default:
         {
            printf ("Invalid pipe state.\n");
            return 0;
         }
      }
   }

   return 0;
}


// DisconnectAndReconnect(DWORD) 
// This function is called when an error occurs or when the client 
// closes its handle to the pipe. Disconnect from this client, then 
// call ConnectNamedPipe to wait for another client to connect. 

VOID DisconnectAndReconnect (LPPIPEINST Pipe)
{
   // Disconnect the pipe instance. 

   if (!DisconnectNamedPipe (Pipe->hPipeInst))
   {
      printf ("DisconnectNamedPipe failed with %d.\n", GetLastError ());
   }

   // Call a subroutine to connect to the new client. 

   Pipe->fPendingIO = ConnectToNewClient (Pipe->hPipeInst, &Pipe->oOverlap);

   // still connecting 
   Pipe->dwState = Pipe->fPendingIO ? CONNECTING_STATE :        
      READING_STATE;    // ready to read 
}

// ConnectToNewClient(HANDLE, LPOVERLAPPED) 
// This function is called to start an overlapped connect operation. 
// It returns TRUE if an operation is pending or FALSE if the 
// connection has been completed. 

BOOL ConnectToNewClient (HANDLE hPipe, LPOVERLAPPED lpo)
{
   BOOL fConnected, fPendingIO = FALSE;

   // Start an overlapped connection for this pipe instance. 
   fConnected = ConnectNamedPipe (hPipe, lpo);

   // Overlapped ConnectNamedPipe should return zero. 
   if (fConnected)
   {
      printf ("ConnectNamedPipe failed with %d.\n", GetLastError ());
      return 0;
   }

   switch (GetLastError ())
   {
      // The overlapped connection in progress. 
   case ERROR_IO_PENDING:
      fPendingIO = TRUE;
      break;

      // Client is already connected, so signal an event. 

   case ERROR_PIPE_CONNECTED:
      if (SetEvent (lpo->hEvent))
         break;

      // If an error occurs during the connect operation... 
   default:
      {
         printf ("ConnectNamedPipe failed with %d.\n", GetLastError ());
         return 0;
      }
   }

   return fPendingIO;
}

void pipeserver_class_func (struct pipeserver_data *this,
                            const struct context_rmcios *context, int id,
                            enum function_rmcios function,
                            enum type_rmcios paramtype,
                            struct combo_rmcios *returnv,
                            int num_params, const union param_rmcios param)
{
   int plen;
   char *s;
   DWORD dwRead, dwWritten;

   switch (function)
   {
   case help_rmcios:
      return_string (context, returnv,
                     "pipeserver channel "
                     "  -Windows pipeserver broadcasting data."
                     "  -data is tramsmitted all connected pipe clients.\r\n"
                     " create pipeserver newname\r\n"
                     " setup newname pipename | client_echo(0) \r\n"
                     "                        | client_read_timeout(1) \r\n"
                     "  -Creates new pipeserver to pipename\r\n"
                     " when client_read_timeout > 0: \r\n"
                     "  client return after specified timeout in seconds\r\n"
                     " when client_read_timeout == 0: \r\n" 
                     "  client return immediatelly\r\n"
                     " when client_read_timeout < 0: \r\n"
                     "  client waits forewer\r\n"
                     " NOTE: This implementation allow only 8 clients\r\n"
                     " NOTE: implementation allow only single call to setup\r\n"
                     " write newname data\r\n"
                     "Broadcast data through pipeserver\r\n"
                     " read newname\r\n"
                     "  -Read data received from pipe since last write\r\n"
                     " link newname channel\r\n ");

      break;
   case create_rmcios:
      if (num_params < 1)
         break;

      // allocate new data
      this = (struct pipeserver_data *) 
             malloc (sizeof (struct pipeserver_data));       
      memset (this, 0, sizeof (struct pipeserver_data)); 
      //default values :
      this->hin = INVALID_HANDLE_VALUE;
      this->hout = INVALID_HANDLE_VALUE;
      this->pipename = NULL;
      this->echo = 0;
      this->timeout = 1000;

      // create channel :
      this->id = create_channel_param (context, paramtype, param, 0, 
                                   (class_rmcios) pipeserver_class_func, this);
      break;

   case setup_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
         break;

      int nlen = param_string_length (context, paramtype, param, 0) + 1;
      if (this->pipename != NULL)
         free (this->pipename);

      this->pipename = malloc (sizeof (char) * (nlen));

      // Store the pipename
      param_to_string (context, paramtype, param, 0, nlen, this->pipename);
      DWORD dwThreadId;

      // Start the pipe server thread:
      CreateThread (NULL,       // no security attribute 
                    0,  // default stack size 
                    pipeserver, // thread proc
                    this,       // thread parameter 
                    0,  // not suspended 
                    &dwThreadId);       // returns thread ID

      if (num_params < 2)
         break;
      this->echo = param_to_int (context, paramtype, param, 1);

      if (num_params < 3)
         break;
      this->timeout =
         (int) (param_to_float (context, paramtype, param, 2) * 1000);

      break;
   case write_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
         break;
      // Determine the needed buffer size
      plen = param_buffer_alloc_size (context, paramtype, param, 0);    
      {
         // allocate buffer
         char buffer[plen];     
         // structure pointer to buffer data
         struct buffer_rmcios pbuffer;  
         pbuffer = param_to_buffer (context, paramtype, param, 0, plen, buffer);

         int j;
         // Write message to all connected pipes
         for (j = 0; j < INSTANCES; j++)
         {
            // Check that pipe is connected:
            if (this->pipes[j].dwState != CONNECTING_STATE)
            {
               BOOL success;

               // Write contents to the pipe end.
               success = WriteFile (this->pipes[j].hPipeInst,
                                    pbuffer.data,
                                    pbuffer.length,
                                    NULL, &this->pipes[j].wOverlap);
            }
         }
      }
      break;
   }
}

void __declspec (dllexport)
     __cdecl init_channels (const struct context_rmcios *context)
{
   printf ("Windows pipe module\r\n[" VERSION_STR "]\r\n");
   module_context = context;
   create_channel_str (context, "pipeserver",
                       (class_rmcios) pipeserver_class_func, NULL);
}

