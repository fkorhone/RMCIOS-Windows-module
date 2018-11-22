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
 * Windows GUI channels module
 *
 * Changelog: (date,who,description)
 * 2018-11-03 FK, Experimental version. expected to change in the future.
 */

#define DLL
#define _WIN32_WINNT 0x0500a

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <sys/time.h>

#include "RMCIOS-functions.h"

const struct context_rmcios *module_context;
struct draw_data;

struct draw_data
{
   HWND hWnd;
   HDC *hdc;                    // Device context
   PAINTSTRUCT *paint;          // Paint information
   enum
   { DRAW_VOID, DRAW_PIXEL } type;
   
   // Function for drawing the object:   
   void (*draw_func) (struct draw_data * this); 
   void *data;                  // Data for drawing the object
   struct draw_data *next;      // Pointer to next object to be drawn
};

struct pixel_data
{
   int X;
   int Y;
   COLORREF crColor;
};

void draw_pixel (struct draw_data *this)
{
   struct pixel_data *draw;
   draw = (struct pixel_data *) (this->data);
   SetPixel (*this->hdc, draw->X, draw->Y, draw->crColor);
}

void draw_class_func (struct draw_data *this,
                      const struct context_rmcios *context, int id,
                      enum function_rmcios function,
                      enum type_rmcios paramtype, union param_rmcios returnv,
                      int num_params, const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, paramtype, returnv,
                     "GDI graphics channel\r\n"
                     "create name pixel\r\n"
                     "setup pixel X | Y | R G B\r\n"
                     "write pixel | Y \n");
      break;

   case create_rmcios:
      if (this == NULL)
         break;
      if (num_params < 1)
         break;
      struct draw_data *parent = this;

      // Get the type of the created window object
      char buffer[20];
      if (num_params > 1)
      {
         param_to_string (context, paramtype, param, 0,
                          sizeof (buffer), buffer);

         if (strcmp (buffer, "pixel") == 0)
         {
            this = (struct draw_data *) malloc (sizeof (struct draw_data));
            if (this == NULL)
            {
               printf ("Unable to allocate memory for draw data!\n");
               break;
            }
            this->data =
               (struct pixel_data *) malloc (sizeof (struct pixel_data));
            this->draw_func = draw_pixel;
            this->type = DRAW_PIXEL;
            while (parent->next != NULL)
               parent = parent->next;
            parent->next = this;

            // Device context
            this->hdc = parent->hdc;    
            // Paint information
            this->paint = parent->paint;  
            // window handle
            this->hWnd = parent->hWnd;  

            create_channel_param (context, paramtype, param,
                                  num_params - 1,
                                  (class_rmcios) draw_class_func, this);
         }
      }

      break;

   case setup_rmcios:
      if (this == NULL)
         break;
      if (num_params > 0)
      {
         switch (this->type)
         {
         case DRAW_PIXEL:
            ((struct pixel_data *) (this->data))->X =
               param_to_integer (context, paramtype, param, 0);
            break;
         }
      }
      if (num_params > 1)
      {
         switch (this->type)
         {
         case DRAW_PIXEL:
            ((struct pixel_data *) (this->data))->Y =
               param_to_integer (context, paramtype, param, 1);
            break;
         }
      }
      if (num_params > 4)
      {
         float r, g, b;
         switch (this->type)
         {
         case DRAW_PIXEL:
            r = param_to_integer (context, paramtype, param, 2);
            g = param_to_integer (context, paramtype, param, 3);
            b = param_to_integer (context, paramtype, param, 4);
            ((struct pixel_data *) (this->data))->crColor = RGB (r, g, b);
            break;
         }
      }

      RedrawWindow (this->hWnd, NULL, 0, RDW_INTERNALPAINT | RDW_INVALIDATE);

      break;
   }
}


HMODULE HIn;

LRESULT CALLBACK WndProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
int Register (HINSTANCE HIn);

struct window_data
{
   int id;
   DWORD idThread;
   HWND hWnd;
   struct window_data *parent;
   char lpClassName[256];
   int x;
   int y;
   int nWidth;
   int nHeight;
   DWORD dwStyle;
   DWORD dwExStyle;
   HMENU hMenu;
   WNDPROC superWndProc;

   int placement_x;
   int placement_y;

   enum
   { WIN_WINDOW,
      WIN_DIALOG,
      WIN_TEXT,
      WIN_EDIT,
      WIN_BUTTON,
      WIN_CHECK,
      WIN_GROUPBOX,
      WIN_RADIO
   } type;

   struct draw_data *draw;
   HDC hdc;                // Device context
   PAINTSTRUCT paint;      // Paint information
};

DWORD WINAPI window_thread (LPVOID data)
{
   struct window_data *this = (struct window_data *) data;
   DWORD TCount;
   this->hWnd = CreateWindowExA (0, this->lpClassName, "",
                                 this->dwStyle,
                                 0, 0, 640, 480,
                                 GetDesktopWindow (), this->hMenu, HIn, this);

   if (this->hWnd == NULL)
      return 0;

   MSG Msg;
   PeekMessage (&Msg, NULL, 0, 0, PM_NOREMOVE);
   while (Msg.message != WM_QUIT)
   {

      if (PeekMessage (&Msg, NULL, 0, 0, PM_REMOVE))
      {
         if (Msg.message == WM_APP)
         {
            struct window_data *subw;
            subw = (struct window_data *) Msg.lParam;
            subw->hWnd = CreateWindowExA (subw->dwExStyle,
                                          subw->lpClassName, "",
                                          subw->dwStyle,
                                          subw->x, subw->y,
                                          subw->nWidth,
                                          subw->nHeight,
                                          subw->parent->hWnd,
                                          subw->hMenu, HIn, ((LPVOID) subw));
         }
         TranslateMessage (&Msg);
         DispatchMessage (&Msg);
      }
   }
   return Msg.wParam;
}

LRESULT CALLBACK WndProc (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
   struct window_data *this = NULL;

   if (Msg == WM_CREATE)
   {
      SetWindowLongPtr (hWnd, GWLP_USERDATA,
                        (LONG) (((CREATESTRUCT *) lParam)->lpCreateParams));
   }
   this = (struct window_data *) GetWindowLongPtr (hWnd, GWLP_USERDATA);

   switch (Msg)
   {
   case WM_PAINT:
      this->hdc = BeginPaint (hWnd, &this->paint);
      {
         struct draw_data *ddata = this->draw;
         while (ddata != NULL)
         {
            if (ddata->draw_func != NULL && ddata->data != NULL)
            {
               ddata->draw_func (ddata);
            }
            ddata = ddata->next;
         }
      }

      EndPaint (hWnd, &this->paint);

      DefWindowProc (hWnd, Msg, wParam, lParam);
      break;

   case WM_DESTROY:
      DefWindowProc (hWnd, Msg, wParam, lParam);
      if (this != NULL)
         this->hWnd = NULL;
      break;
   case WM_COMMAND:
      switch (HIWORD (wParam))
      {
         int slen;
         int linked;

      case EN_CHANGE:
      case 0:

         linked = (short int) LOWORD (wParam);
         slen = GetWindowTextLengthA ((HWND) lParam);
         {
            char str[slen + 1];
            str[0] = 0;
            slen = GetWindowTextA ((HWND) lParam, str, slen + 1);
            if (slen > 0)
               str[slen] = 0;
            else
               str[0] = 0;
            if (linked != 0)
               write_str (module_context, linked, str, 0);

         }
         break;
      }
      break;
   case WM_CLOSE:
      write_iv (module_context,
                linked_channels (module_context, this->id), 0, NULL);
      DestroyWindow (hWnd);
      return 0;

   default:
      return DefWindowProc (hWnd, Msg, wParam, lParam);
   }
}

struct menu_data
{
   int id;
   HMENU hMenu;
   enum
   {
      MENU,
      MENU_POPUP,
      MENU_TEXT,
      MENU_SEPARATOR,
      MENU_CHECK,
      MENU_RADIO
   } type;
   struct menu_data *parent;
};

void menu_class_func (struct menu_data *this,
                      const struct context_rmcios *context, int id,
                      enum function_rmcios function,
                      enum type_rmcios paramtype,
                      union param_rmcios returnv,
                      int num_params, const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, paramtype, returnv,
                     "menu channel - channel for creating windows menus\r\n"
                     "create newname menuname\r\n"
                     "create newname menu menuname\r\n"
                     "create newname menu menuname\r\n"
                     "create windowname popup menuname \r\n"
                     "create menuname text itemname\r\n"
                     "create menuname text itemname\r\n"
                     "setup menuname text\r\n"
                     "write menuname value\r\n" "link menuname linked\r\n");
      break;

   case create_rmcios:
      if (num_params < 1)
         break;
      struct menu_data *parent = this;

      this = (struct menu_data *) malloc (sizeof (struct menu_data));
      if (this == NULL)
      {
         printf ("Unable to allocate memory for window data!\n");
         break;
      }
      this->hMenu = 0;
      this->type = MENU_POPUP;
      this->parent = parent;

      // Get the type of the created window object
      char buffer[20];
      if (num_params > 1)
      {
         param_to_string (context, paramtype, param, 0,
                          sizeof (buffer), buffer);

         if (strcmp (buffer, "menu") == 0)
            this->type = MENU;
         if (strcmp (buffer, "popup") == 0)
            this->type = MENU_POPUP;
         if (strcmp (buffer, "text") == 0)
            this->type = MENU_TEXT;
         if (strcmp (buffer, "separator") == 0)
            this->type = MENU_SEPARATOR;
         if (strcmp (buffer, "check") == 0)
            this->type = MENU_CHECK;
         if (strcmp (buffer, "radio") == 0)
            this->type = MENU_RADIO;
      }

      switch (this->type)
      {
      case MENU:
      case MENU_POPUP:
         printf ("Creating menu!\n");
         this->hMenu = CreateMenu ();
         break;
      default:
         this->hMenu = parent->hMenu;
         break;
      }
      this->id = create_channel_param (context, paramtype, param, num_params - 1, (class_rmcios) menu_class_func, this);        // create the channel
      break;

   case setup_rmcios:
      if (this == NULL)
         break;
      if (this->parent == NULL)
         break;
      int plen = 0;
      if (num_params > 0)
         plen = param_string_alloc_size (context, paramtype, param, 0);
      {
         // Determine the needed buffer size
         const char *s = "";
         char buffer[plen];     // allocate buffer
         if (num_params > 0)
            s = param_to_string (context, paramtype, param, 0, plen, buffer);

         switch (this->type)
         {
         case MENU:
            AppendMenuA (this->parent->hMenu, MF_POPUP,
                         (UINT_PTR) this->hMenu, s);
            break;
         case MENU_POPUP:
            AppendMenuA (this->parent->hMenu, MF_POPUP,
                         (UINT_PTR) this->hMenu, s);
            break;
         case MENU_TEXT:
            AppendMenuA (this->parent->hMenu, MF_STRING,
                         (UINT_PTR) this->id, s);
            break;

         case MENU_SEPARATOR:
            AppendMenuA (this->parent->hMenu, MF_SEPARATOR, 0, NULL);
            break;

         case MENU_CHECK:
            AppendMenuA (this->parent->hMenu, MF_STRING,
                         (UINT_PTR) this->id, s);
            break;

         case MENU_RADIO:
            AppendMenuA (this->parent->hMenu, MF_STRING,
                         (UINT_PTR) this->id, s);
            break;

         }
      }
      break;

   case write_rmcios:
      if (this == NULL)
         break;
      write_iv (context, linked_channels (context, id), 0, NULL);
      break;
   }
}

void window_class_func (struct window_data *this,
                        const struct context_rmcios *context, int id,
                        enum function_rmcios function,
                        enum type_rmcios paramtype,
                        union param_rmcios returnv,
                        int num_params, const union param_rmcios param)
{
   switch (function)
   {
   case help_rmcios:
      return_string (context, paramtype, returnv,
                     "window channel \r\n" 
                     "channel for creating windows and controls\r\n"
                     "create window newname\r\n"
                     " -Creates new application main window"
                     "create newname childname\r\n"
                     "create newname dialog childname\r\n"
                     "create newname button childname\r\n"
                     "create newname edit childname\r\n"
                     "create newname text childname\r\n"
                     "create newname check childname\r\n"
                     "create newname radio childname \r\n"
                     "create newname menu menuname \r\n"
                     "create newname popup menuname \r\n"
                     "create newname draw childname \r\n"
                     "  # Creates new child window for an window.\r\n"
                     "   (control or subwindow)\n"
                     "setup newname | x y | nwidth nheight \r\n"
                     "              | window_class | STYLE_FLAGS \r\n"
                     "              | EXSTYLE_FLAGS\r\n"
                     "setup menuname | x y | nwidth nheight | \r\n"
                     "               | window_class | STYLE_FLAGS \r\n"
                     "               | EXSTYLE_FLAGS\r\n"
                     " -Make window visible\r\n"
                     " -Set position of the window. \r\n"
                     " -Use +- on front of a value for relative positioning.\r\n"
                     "   (to the last positioned item)\r\n"
                     " -Set size of the window\r\n"
                     " -Set the window class\r\n"
                     " -Set the style flags\r\n"
                     " -Set the extended style flags\r\n"
                     "write newname \r\n" " -Set the extended style flags\r\n");
      break;

   case create_rmcios:
      if (num_params < 1)
         break;
      else
      {
         // Get the type of the created window object
         char buffer[20];
         int typee = WIN_WINDOW;
         struct window_data *parent = this;

         if (num_params > 1)
         {
            param_to_string (context, paramtype, param, 0,
                             sizeof (buffer), buffer);

            if (strcmp (buffer, "window") == 0)
               typee = WIN_WINDOW;
            if (strcmp (buffer, "dialog") == 0)
               typee = WIN_DIALOG;
            if (strcmp (buffer, "text") == 0)
               typee = WIN_TEXT;
            if (strcmp (buffer, "edit") == 0)
               typee = WIN_EDIT;
            if (strcmp (buffer, "button") == 0)
               typee = WIN_BUTTON;
            if (strcmp (buffer, "check") == 0)
               typee = WIN_CHECK;
            if (strcmp (buffer, "group") == 0)
               typee = WIN_GROUPBOX;
            if (strcmp (buffer, "radio") == 0)
               typee = WIN_RADIO;
            if (strcmp (buffer, "draw") == 0)
            {
               if (this == NULL)
                  break;
               if (num_params > 1)
               {

                  struct draw_data *draw =
                     (struct draw_data *) malloc (sizeof (struct draw_data));
                  if (this == NULL)
                  {
                     printf ("Unable to allocate memory for draw data!\n");
                     break;
                  }
                  draw->data = NULL;
                  draw->draw_func = NULL;
                  draw->type = DRAW_VOID;
                  draw->hdc = &(this->hdc);
                  draw->hWnd = this->hWnd;

                  draw->paint = &this->paint;
                  draw->next = NULL;
                  this->draw = draw;
                  create_channel_param (context, paramtype,
                                        param, num_params - 1,
                                        (class_rmcios) draw_class_func, draw);
               }
               break;
            }
            if (strcmp (buffer, "menu") == 0 || strcmp (buffer, "popup") == 0)
            {
               struct menu_data *menu =
                  (struct menu_data *) malloc (sizeof (struct menu_data));
               if (menu == NULL)
               {
                  printf ("Unable to allocate memory for menu data!\n");
                  break;
               }

               // Get the type of the created window object
               char buffer[20];
               if (num_params > 1)
               {
                  param_to_string (context, paramtype, param,
                                   0, sizeof (buffer), buffer);

                  if (strcmp (buffer, "menu") == 0)
                  {
                     menu->type = MENU;
                     menu->hMenu = CreateMenu ();
                     SetMenu (parent->hWnd, menu->hMenu);
                  }
                  if (strcmp (buffer, "popup") == 0)
                  {
                     menu->type = MENU_POPUP;
                     menu->hMenu = CreateMenu ();
                     printf ("Setting menu!\n");
                     SetMenu (parent->hWnd, menu->hMenu);
                  }
               }

               menu->parent = menu;
               menu->id = create_channel_param (context,
                                                paramtype,
                                                param,
                                                num_params - 1,
                                                (class_rmcios)
                                                menu_class_func, menu);
               break;
            }
         }

         this = (struct window_data *) malloc (sizeof (struct window_data));
         if (this == NULL)
         {
            printf ("Unable to allocate memory for window data!\n");
            break;
         }
         this->hWnd = 0;
         this->parent = parent;

         this->lpClassName[0] = 0;
         this->dwStyle = 0;
         this->dwExStyle = 0;
         this->x = 0;
         this->y = 0;
         this->nWidth = 640;
         this->nHeight = 480;
         this->placement_x = 0;
         this->placement_y = 0;
         this->type = typee;
         this->draw = NULL;
         this->hMenu = 0;

         // create the channel
         this->id = create_channel_param (context, paramtype, 
                                          param, num_params - 1, 
                                          (class_rmcios) window_class_func, this);   

         if (this->parent == NULL)      
         // Create new application window
         {
            strcpy (this->lpClassName, "MyWin");
            this->dwStyle = WS_OVERLAPPEDWINDOW;
            HANDLE myHandle = CreateThread (0, 0,
                                            window_thread,
                                            this,
                                            0,
                                            &this->idThread);

         }
         else
         {

            WNDCLASSEXA wndc = { 0 };
            wndc.cbSize = sizeof (WNDCLASSEXW);


            switch (this->type)
            {
            case WIN_WINDOW:
               this->idThread = this->parent->idThread;
               this->nWidth = 640;
               this->nHeight = 480;
               this->dwExStyle = 0;
               this->dwStyle = WS_VISIBLE | WS_OVERLAPPEDWINDOW;
               strcpy (this->lpClassName, "MyWin");
               break;
            case WIN_DIALOG:
               this->idThread = this->parent->idThread;
               this->nWidth = 320;
               this->nHeight = 240;
               this->dwExStyle = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST;
               this->dwStyle = WS_VISIBLE | WS_SYSMENU | WS_CAPTION;
               strcpy (this->lpClassName, "DialogClass");
               break;
            case WIN_TEXT:
               this->idThread = this->parent->idThread;
               this->nWidth = 100;
               this->nHeight = 15;
               this->dwStyle = WS_CHILD | WS_VISIBLE | SS_LEFT;
               this->hMenu = (HMENU) this->id;
               strcpy (this->lpClassName, "Static");
               break;
            case WIN_EDIT:
               this->idThread = this->parent->idThread;
               this->nWidth = 100;
               this->nHeight = 15;
               this->dwStyle = WS_CHILD | WS_VISIBLE | WS_BORDER;
               this->hMenu = (HMENU) this->id;
               strcpy (this->lpClassName, "Edit");
               break;
            case WIN_BUTTON:
               this->idThread = this->parent->idThread;
               this->nWidth = 100;
               this->nHeight = 20;
               this->dwStyle = WS_VISIBLE | WS_CHILD;
               this->hMenu = (HMENU) this->id;
               strcpy (this->lpClassName, "Button");
               break;
            case WIN_CHECK:
               this->idThread = this->parent->idThread;
               this->nWidth = 100;
               this->nHeight = 20;
               this->dwStyle = WS_VISIBLE | WS_CHILD | BS_CHECKBOX;
               this->hMenu = (HMENU) this->id;
               strcpy (this->lpClassName, "Button");
               break;
            case WIN_GROUPBOX:
               this->idThread = this->parent->idThread;
               this->nWidth = 100;
               this->nHeight = 100;
               this->dwStyle = WS_CHILD | WS_VISIBLE | BS_GROUPBOX;
               this->hMenu = (HMENU) this->id;
               strcpy (this->lpClassName, "Button");
               break;
            case WIN_RADIO:
               this->idThread = this->parent->idThread;
               this->nWidth = 100;
               this->nHeight = 20;
               this->hMenu = (HMENU) this->id;
               this->dwStyle = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON;
               strcpy (this->lpClassName, "Button");
               break;

            }
         }
      }
      break;
   case setup_rmcios:
      if (this == NULL)
         break;

      if (num_params > 1) 
      // Change position
      {
         char c = 0;
         param_to_buffer (context, paramtype, param, 0, 1, &c);
         if (this->parent != NULL && c == '+' || c == '-')      
         // relative insertion
         {
            this->parent->placement_x +=
               param_to_integer (context, paramtype, param, 0);
            if (this->parent != NULL)
               this->x = this->parent->placement_x;
         }
         else
         {
            this->x = param_to_integer (context, paramtype, param, 0);
            if (this->parent != NULL)
               this->parent->placement_x = this->x;
         }

         c = 0;
         param_to_buffer (context, paramtype, param, 1, 1, &c);
         if (this->parent != NULL && c == '+' || c == '-')
         // relative insertion
         {
            this->parent->placement_y +=
               param_to_integer (context, paramtype, param, 1);
            if (this->parent != NULL)
               this->y = this->parent->placement_y;
         }
         else
         {
            this->y = param_to_integer (context, paramtype, param, 1);
            if (this->parent != NULL)
               this->parent->placement_y = this->y;
         }
      }


      if (num_params > 3) 
      // Change size
      {
         this->nWidth = param_to_integer (context, paramtype, param, 2);
         this->nHeight = param_to_integer (context, paramtype, param, 3);
      }
      if (num_params > 4) 
      // window_class
      {
         param_to_string (context, paramtype, param, 4,
                          sizeof (this->lpClassName), this->lpClassName);
      }
      if (num_params > 5)
      {
         this->dwStyle = param_to_integer (context, paramtype, param, 5);
      }
      if (num_params > 6)
      {
         this->dwExStyle = param_to_integer (context, paramtype, param, 6);
      }
      SetWindowPos (this->hWnd, //_In_ HWND hWnd,
                    0,  //_In_opt_ HWND hWndInsertAfter,
                    this->x,    //_In_ int  X,
                    this->y,    //_In_ int  Y,
                    this->nWidth, //_In_     int  cx,
                    this->nHeight, //_In_     int  cy,
                    0   // _In_    UINT uFlags
         );

      if (this->hWnd == NULL)   // Create the subwindow
      {
         PostThreadMessage (this->idThread, WM_APP, //_In_ UINT   Msg,
                            0,  //_In_ WPARAM wParam,
                            (LPARAM) this       //_In_ LPARAM lParam
                           );
      }

      if (this->parent == NULL)
      {
         ShowWindow (this->hWnd, SW_SHOW);
         UpdateWindow (this->hWnd);
         SetFocus (this->hWnd);
      }
      break;
   case read_rmcios:
      if (this == NULL)
         break;
      int len = GetWindowTextLength (this->hWnd);
      {
         char buffer[len + 1];
         GetWindowTextA (this->hWnd, buffer, len + 1);
         return_string (context, paramtype, returnv, buffer);
      }
      break;

   case write_rmcios:
      {
         if (this == NULL)
            break;
         int len = GetWindowTextLength (this->hWnd);
         if (num_params < 1)
         {
            char buffer[len + 1];
            GetWindowTextA (this->hWnd, buffer, len + 1);
            write_str (context, linked_channels (context, id), buffer, 0);
         }
         else
         {
            int plen = param_string_alloc_size (context, paramtype, param,
                                                0);
            {
               // Determine the needed buffer size
               const char *s;
               char cbuffer[len + 1];
               // allocate buffer
               char buffer[plen];       
               s = param_to_string (context, paramtype, param, 0, plen, buffer);
               GetWindowTextA (this->hWnd, cbuffer, len + 1);

               if (strcmp (s, cbuffer) != 0)
               {
                  SetWindowTextA (this->hWnd, //_In_ HWND hWnd,
                                  s //_In_opt_ LPCTSTR lpString
                                 );
               }
               if (this->type != WIN_WINDOW)
                  write_str (context, linked_channels (context, id), buffer, 0);
               break;
            }
         }

      }
      break;
   }
}

int Register (HINSTANCE HIn)
{
   WNDCLASSEX Wc;

   Wc.cbSize = sizeof (WNDCLASSEX);
   Wc.style = 0;
   Wc.lpfnWndProc = WndProc;
   Wc.cbClsExtra = 0;
   Wc.cbWndExtra = 0;
   Wc.hInstance = HIn;
   Wc.hIcon = LoadIcon (NULL, IDI_APPLICATION);
   Wc.hCursor = LoadCursor (NULL, IDC_ARROW);
   //GetStockObject(BLACK_BRUSH);
   Wc.hbrBackground = (HBRUSH) (COLOR_BTNFACE + 1);     
   Wc.lpszMenuName = NULL;
   Wc.lpszClassName = "MyWin";
   Wc.hIconSm = LoadIcon (NULL, IDI_APPLICATION);

   return RegisterClassEx (&Wc);
}

// function for dynamically loading the module
void init_windows_gui_channels (const struct context_rmcios *context)
{
   printf ("Windows GUI channels module\r\n[" VERSION_STR "]\r\n");

   module_context = context;

   // Register windows handles for gui:
   HIn = GetModuleHandle (NULL);
   if (!Register (HIn))
      return;

   create_channel_str (context, "window", (class_rmcios) window_class_func,
                       NULL);

   // Register Dialog class: 
   WNDCLASSEXW wc = { 0 };
   wc.lpszClassName = L"DialogClass";
   wc.lpfnWndProc = (WNDPROC) WndProc;
   wc.hInstance = HIn;
   wc.hbrBackground = GetSysColorBrush (COLOR_3DFACE);
   wc.cbSize = sizeof (WNDCLASSEXW);
   RegisterClassExW (&wc);

}

#ifdef INDEPENDENT_CHANNEL_MODULE
// function for dynamically loading the module
void API_ENTRY_FUNC init_channels (const struct context_rmcios *context)
{
   init_windows_gui_channels (context);
}
#endif
