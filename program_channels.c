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
 * External program remote control channels
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

struct child_data
{
   HANDLE stdin_rd;
   HANDLE stdin_wr;
   HANDLE stdout_rd;
   HANDLE stdout_wr;
   HANDLE hProcess;
   int delimiter;
   unsigned int timeout;
   char *rx_buffer;
   unsigned int rx_buffer_len;
   unsigned int rx_index;
};

struct child_data *start_program (struct child_data *program,
                                  const char *work_dir, char *program_command)
{
   struct child_data *p = program;

   SECURITY_ATTRIBUTES saAttr;
   PROCESS_INFORMATION piProcInfo;
   STARTUPINFO siStartInfo;
   DWORD exitCode;

   // Set the workingdir
   chdir (work_dir);    

   if (p != 0)  
   // program structure has already been created
   {
      GetExitCodeProcess (p->hProcess, &exitCode);
      if (exitCode == STILL_ACTIVE)
         // program still running. Return existing id
         return p;      
   }
   else // Allocate new child program structure
   {
      //printf("allocating new!\n") ;
      p = malloc (sizeof (struct child_data));
      p->stdin_rd = NULL;
      p->stdin_wr = NULL;
      p->stdout_rd = NULL;
      p->stdout_wr = NULL;
      p->delimiter = '\n';
      p->timeout = 500;
      p->rx_buffer = malloc (1024);
      p->rx_buffer_len = 1024;
      p->rx_index = 0;


      // Set the bInheritHandle flag so pipe handles are inherited. 
      saAttr.nLength = sizeof (SECURITY_ATTRIBUTES);
      saAttr.bInheritHandle = TRUE;
      saAttr.lpSecurityDescriptor = NULL;

      // Create a pipe for the child process's STDOUT. 
      CreatePipe (&p->stdout_rd, &p->stdout_wr, &saAttr, 0);
      // Ensure the read handle to the pipe for STDOUT is not inherited.
      SetHandleInformation (p->stdout_rd, HANDLE_FLAG_INHERIT, 0);

      // Create a pipe for the child process's STDIN. 
      CreatePipe (&p->stdin_rd, &p->stdin_wr, &saAttr, 0);

      // Ensure the write handle to the pipe for STDIN is not inherited. 
      SetHandleInformation (p->stdin_wr, HANDLE_FLAG_INHERIT, 0);
   }

   BOOL bSuccess = FALSE;
   // Set up members of the PROCESS_INFORMATION structure. 
   ZeroMemory (&piProcInfo, sizeof (PROCESS_INFORMATION));

   // Set up members of the STARTUPINFO structure. 
   // This structure specifies the STDIN and STDOUT handles for redirection.
   ZeroMemory (&siStartInfo, sizeof (STARTUPINFO));
   siStartInfo.cb = sizeof (STARTUPINFO);
   siStartInfo.hStdError = p->stdout_wr;
   siStartInfo.hStdOutput = p->stdout_wr;
   siStartInfo.hStdInput = p->stdin_rd;
   siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

   bSuccess = CreateProcess (NULL, program_command, // command line 
                             NULL,      // process security attributes 
                             NULL,      // primary thread security attributes 
                             TRUE,      // handles are inherited 
                             0,         // creation flags 
                             NULL,      // use parent's environment 
                             NULL,      // use parent's current directory 
                             &siStartInfo, // STARTUPINFO pointer 
                             &piProcInfo); // receives PROCESS_INFORMATION 

   // If an error occurs, exit the application. 
   if (!bSuccess)
      printf ("Error:CreateProcess\n");
   else
   {
      // Close handles to the child process primary thread. 
      // Thread hanlde is left open for later use
      // Some applications might keep these handles to monitor the status
      // of the child process, for example. 
      //CloseHandle(piProcInfo.hProcess);
      p->hProcess = piProcInfo.hProcess;
      CloseHandle (piProcInfo.hThread);
   }
   return p;
}

int write_program (struct child_data *child, char *buffer,
                   unsigned int buffer_len)
{
   struct child_data *p = (struct child_data *) child;
   DWORD dwWritten = 0;
   DWORD exitCode;
   GetExitCodeProcess (p->hProcess, &exitCode);
   if (exitCode == STILL_ACTIVE)        
   // make sure process is running)
   {
      if (WriteFile (p->stdin_wr, buffer, buffer_len, &dwWritten, NULL) < 1)
         return 0;
   }
   return dwWritten;
}

struct program_data
{
   struct child_data *child;
   int id;
   HANDLE hThread;
};

// Thread for reception
DWORD WINAPI program_rx_thread (LPVOID data)
{
   struct program_data *this = (struct program_data *) data;
   DWORD dwRead = 0;
   DWORD total_available_bytes;
   DWORD exitCode;
   while (1)
   {
      if (this->child != NULL)
      {
         exitCode = 9999;
         dwRead = 0;
         GetExitCodeProcess (this->child->hProcess, &exitCode);
         if (exitCode == STILL_ACTIVE)  // make sure process is running
         {
            if (PeekNamedPipe
                (this->child->stdout_rd, 0, 0, 0,
                 &total_available_bytes, 0) != 0 && total_available_bytes > 0)
            {
               char buffer[total_available_bytes * 2];
               if (ReadFile
                   (this->child->stdout_rd, buffer,
                    total_available_bytes * 2, &dwRead, 0) == 0)
               {
                  printf ("Error: ReadFile error\n");
               }
               else
               {
                  write_buffer (module_context,
                                linked_channels
                                (module_context, this->id), buffer, dwRead, 0);
               }
            }
            else
            {
               Sleep (10);
            }
         }
      }
      else
         Sleep (10);
   }
}

void program_class_func (struct program_data *this,
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
                     "program channel"
                     " - Channel for remotely executing programs\r\n");
      return_string (context, paramtype, returnv, "create program newname\r\n");
      return_string (context, paramtype, returnv,
                     "setup newname program_start_cmd | work_dir \r\n");
      return_string (context, paramtype, returnv, "write newname data \r\n");
      return_string (context, paramtype, returnv, "write newname data \r\n");
      return_string (context, paramtype, returnv, "link newname channel \r\n");
      break;

   case create_rmcios:
      if (num_params < 1)
         break;
      // allocate new data
      this = (struct program_data *) malloc (sizeof (struct program_data)); 
      
      //default values :
      this->child = NULL;
      this->hThread = NULL;
      
      // create channel :
      this->id = create_channel_param (context, paramtype, param, 0, 
                                      (class_rmcios) program_class_func, this);  
      break;

   case setup_rmcios:
      if (num_params < 1)
         break;
      {
         int cmd_len = param_string_length (context, paramtype, param, 0) + 1;
         int wd_len = 0;
         const char *workdir = "./";
         if (num_params >= 2)
            wd_len = param_string_alloc_size (context, paramtype, param, 1);
         {
            char command[cmd_len];
            char workdir_buf[wd_len];
            param_to_string (context, paramtype, param, 0, cmd_len, command);
            if (num_params >= 2)
               workdir =
                  param_to_string (context, paramtype, param, 1,
                                   wd_len, workdir_buf);
            this->child = start_program (this->child, workdir, command);

            // Create thread for reception
            if (this->hThread != NULL)
               TerminateThread (this->hThread, 0);
            DWORD myThreadID = 0;
            this->hThread =
               CreateThread (0, 0, program_rx_thread, this, 0, &myThreadID);
         }
      }
      break;

   case write_rmcios:
      if (num_params < 1)
         break;
      {
         int plen = param_buffer_alloc_size (context, paramtype, param, 0);
         {
            char buffer[plen];
            struct buffer_rmcios pbuffer;
            pbuffer =
               param_to_buffer (context, paramtype, param, 0, plen, buffer);
            write_program (this->child, pbuffer.data, pbuffer.length);
         }
      }
      break;
   }
}

void __declspec (dllexport)
     __cdecl init_channels (const struct context_rmcios *context)
{
   printf ("Program execution module\r\n[" VERSION_STR "]\r\n");
   module_context = context;
   // create channel
   create_channel_str (context, "program", 
                       (class_rmcios) program_class_func, NULL);    
}

