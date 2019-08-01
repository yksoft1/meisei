/* tool window */
#include <windows.h>
#include <commctrl.h>

#include "global.h"
#include "tool.h"
#include "file.h"
#include "vdp.h"
#include "main.h"
#include "resource.h"
#include "input.h"
#include "screenshot.h"
#include "movie.h"
#include "netplay.h"
#include "settings.h"

static struct {
	int busy;
	HCURSOR cursor_cross;
	int win_width;
	int win_height;
	
	/* popup menu */
	int rclickdown;
	int rclick;
	int popup_active;
	POINT popup_p;
	UINT ext_dialog;
	HWND ext_wnd;
	
	/* copy/paste */
	u8 copy_data[0x20];
	int paste_valid;
	
	/* change spat */
	int spatc;
	
	/* info */
	int is16;
	int ismag;
	int iscol;
	int is5s;
	int snum;
	int sgo;
	int spata;
	int blank;
	DWORD active_ticks;
	int active_timer_enabled;
	
	/* spritemap bmp */
	struct {
		HWND wnd;
		HDC wdc;
		HDC bdc;
		u8* screen;
		HBITMAP bmp;
		BITMAPINFO info;
		u32 pal[2];
	} sv;
	
	/* highlight */
	int in_sv;
	POINT p_sv;
	int hlvis;
	int sg;
	int sinspat;
	
	/* highlight bmp */
	struct {
		HWND wnd;
		HDC wdc;
		HDC bdc;
		u8* screen;
		HBITMAP bmp;
		BITMAPINFO info;
		u32 pal[2];
	} h;
	
	/* spat bmp */
	struct {
		HWND wnd;
		HDC wdc;
		HDC bdc;
		int* screen;
		HBITMAP bmp;
		BITMAPINFO info;
	} spat;
	
	/* highlight */
	int in_spat;
	POINT p_spat;
	int spatvis;
	int spatdata[4];
	int hspat;
	int s208;
	int cx;
	int cy;
	
	/* details */
	HFONT sfont;
	int dclick;
	
	/* local state */
	u8 vdp_ram[0x4000];
	int vdp_regs[8];
	
} spriteview;

static u8* vdp_ram;
static int* vdp_regs;
static int* draw_palette;

/* initial window size */
#define SPRITEVIEW_WIN_WIDTH	278
#define SPRITEVIEW_WIN_HEIGHT	488

/* initial middle dialog item Y */
static int spriteview_dmp_initial[][2]={
	{0,IDC_SPRITEVIEW_SPATA},
	{0,IDC_SPRITEVIEW_SEPARATOR2},
	{0,IDC_SPRITEVIEW_SPAT},
	{0,IDC_SPRITEVIEW_SPATT},
	{0,IDC_SPRITEVIEW_SPATS},
	{0,IDC_SPRITEVIEW_SPATSAT},
	{0,IDC_SPRITEVIEW_SPATSA},
	{0,IDC_SPRITEVIEW_SPATXY},
	{0,IDC_SPRITEVIEW_SPATXYH},
	{0,IDC_SPRITEVIEW_SPATXYD},
	{0,IDC_SPRITEVIEW_SPATCT},
	{0,IDC_SPRITEVIEW_SPATC},
	{0,-1}
};

static int spriteview_dmp_find(int id)
{
	int i=0;
	
	for (;;) {
		if (spriteview_dmp_initial[i][1]==-1) return 0;
		if (spriteview_dmp_initial[i][1]==id) return spriteview_dmp_initial[i][0];
		i++;
	}
}

/* non-volatile */
static char spriteview_dir[STRING_SIZE]={0};
static int spriteview_open_fi; /* 1: all supp. 2: .spgt, 3: .spat, 4: any */
static int spriteview_save_fi; /* 1: .png, 2: .spgt, 3: .spat */

#include "spriteedit.c"

int spriteview_get_open_fi(void) { return spriteview_open_fi; }
int spriteview_get_save_fi(void) { return spriteview_save_fi; }


/* change spat entry dialog */
static BOOL CALLBACK spriteview_changespat_dialog(HWND dialog,UINT msg,WPARAM wParam,LPARAM lParam)
{
	switch (msg) {
		
		case WM_INITDIALOG: {
			RECT r;
			char t[0x100]={0};
			const int spat=(spriteedit.vdp_regs[5]<<7&0x3fff)|spriteview.spatc<<2;
			
			sprintf(t,"Sprite %d",spriteview.spatc);
			SetDlgItemText(dialog,IDC_SPRITEVIEW_CS_GROUP,t);
			
			/* init editboxes */
			SendDlgItemMessage(dialog,IDC_SPRITEVIEW_CS_PATTERNS,UDM_SETRANGE,0,(LPARAM)MAKELONG(255,0));
			SendDlgItemMessage(dialog,IDC_SPRITEVIEW_CS_PATTERN,EM_LIMITTEXT,3,0);
			SetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_PATTERN,spriteedit.vdp_ram[spat|2],FALSE);
			sprintf(t,"($%02X)",spriteedit.vdp_ram[spat|2]);
			SetDlgItemText(dialog,IDC_SPRITEVIEW_CS_PATTERNX,t);
			
			SendDlgItemMessage(dialog,IDC_SPRITEVIEW_CS_POSX,EM_LIMITTEXT,3,0);
			SetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_POSX,spriteedit.vdp_ram[spat|1],FALSE);
			
			SendDlgItemMessage(dialog,IDC_SPRITEVIEW_CS_POSY,EM_LIMITTEXT,3,0);
			SetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_POSY,spriteedit.vdp_ram[spat],FALSE);
			
			SendDlgItemMessage(dialog,IDC_SPRITEVIEW_CS_COLOUR,EM_LIMITTEXT,2,0);
			SetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_COLOUR,spriteedit.vdp_ram[spat|3]&0xf,FALSE);
			
			CheckDlgButton(dialog,IDC_SPRITEVIEW_CS_EC,(spriteedit.vdp_ram[spat|3]&0x80)?BST_CHECKED:BST_UNCHECKED);
			
			/* position window on popup menu location */
			GetWindowRect(GetParent(dialog),&r);
			r.top=spriteview.popup_p.y-r.top; if (r.top<0) r.top=0;
			r.left=spriteview.popup_p.x-r.left; if (r.left<0) r.left=0;
			main_parent_window(dialog,MAIN_PW_LEFT,MAIN_PW_LEFT,r.left,r.top,0);
			
			PostMessage(dialog,WM_NEXTDLGCTL,(WPARAM)GetDlgItem(dialog,IDC_SPRITEVIEW_CS_PATTERN),TRUE);
			
			spriteview.ext_wnd=dialog;
			break;
		}
		
		case WM_DESTROY:
			spriteview.ext_wnd=NULL;
			break;
		
		case WM_COMMAND:
			
			switch (LOWORD(wParam)) {
				
				/* change editboxes */
				case IDC_SPRITEVIEW_CS_PATTERN:
					if (spriteview.ext_wnd&&GetDlgItem(dialog,IDC_SPRITEVIEW_CS_PATTERN)&&GetDlgItem(dialog,IDC_SPRITEVIEW_CS_PATTERNX)) {
						char t[0x100]={0};
						u32 i=GetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_PATTERN,NULL,FALSE);
						if (i>255) { i=255; SetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_PATTERN,i,FALSE); }
						sprintf(t,"($%02X)",i);
						SetDlgItemText(dialog,IDC_SPRITEVIEW_CS_PATTERNX,t);
					}
					break;
				
				case IDC_SPRITEVIEW_CS_POSX:
					if (spriteview.ext_wnd&&GetDlgItem(dialog,IDC_SPRITEVIEW_CS_POSX)) {
						u32 i=GetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_POSX,NULL,FALSE);
						if (i>255) { i=255; SetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_POSX,i,FALSE); }
					}
					break;
				
				case IDC_SPRITEVIEW_CS_POSY:
					if (spriteview.ext_wnd&&GetDlgItem(dialog,IDC_SPRITEVIEW_CS_POSY)) {
						u32 i=GetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_POSY,NULL,FALSE);
						if (i>255) { i=255; SetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_POSY,i,FALSE); }
					}
					break;
				
				case IDC_SPRITEVIEW_CS_COLOUR:
					if (spriteview.ext_wnd&&GetDlgItem(dialog,IDC_SPRITEVIEW_CS_COLOUR)) {
						u32 i=GetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_COLOUR,NULL,FALSE);
						if (i>15) { i=15; SetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_COLOUR,i,FALSE); }
					}
					break;
				
				/* close dialog */
				case IDOK: {
					u8 data[4];
					
					if (netplay_is_active()||movie_get_active_state()) break;
					
					data[0]=GetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_POSY,NULL,FALSE);
					data[1]=GetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_POSX,NULL,FALSE);
					data[2]=GetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_PATTERN,NULL,FALSE);
					data[3]=(GetDlgItemInt(dialog,IDC_SPRITEVIEW_CS_COLOUR,NULL,FALSE)&0xf)|((IsDlgButtonChecked(dialog,IDC_SPRITEVIEW_CS_EC)==BST_CHECKED)?0x80:0);
					
					if (!vdp_upload((vdp_regs[5]<<7&0x3fff)|spriteview.spatc<<2,data,4)) {
						/* error */
						EndDialog(dialog,1);
					}
					else EndDialog(dialog,0);
					break;
				}
				
				case IDCANCEL:
					EndDialog(dialog,0);
					break;
				
				default: break;
			}
			
			break;
		
		case TOOL_MENUCHANGED:
			EnableWindow(GetDlgItem(dialog,IDOK),(netplay_is_active()|movie_get_active_state())==0);
			break;
		
		default: break;
	}
	
	return 0;
}

/* sprite patterns subwindow */
static BOOL CALLBACK spriteview_sub_patterns(HWND wnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	if (!spriteview.sv.wnd) return DefWindowProc(wnd,msg,wParam,lParam);
	
	switch (msg) {
		
		case WM_SETCURSOR:
			SetCursor(spriteview.cursor_cross);
			return 1;
			break;
		
		case WM_MOUSEMOVE: {
			int in;
			POINT p;
			
			in=input_mouse_in_client(wnd,NULL,&p,TRUE);
			toolwindow_relative_clientpos(wnd,&p,128,128);
			spriteview.in_sv=in;
			spriteview.p_sv.x=p.x; spriteview.p_sv.y=p.y;
			
			break;
		}
		
		case WM_LBUTTONDOWN:
			spriteview.dclick=TRUE;
			break;
		
		case WM_RBUTTONDOWN:
			spriteview.rclickdown=1;
			break;
		
		case WM_RBUTTONUP:
			spriteview.rclick=1&spriteview.rclickdown;
			spriteview.rclickdown=0;
			break;
		
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HGDIOBJ obj;
			HDC dc;
			RECT r;
			
			if (spriteview.busy) break;
			
			GetClientRect(wnd,&r);
			dc=BeginPaint(wnd,&ps);
			
			obj=SelectObject(spriteview.sv.bdc,spriteview.sv.bmp);
			if (r.right==128&&r.bottom==128) BitBlt(dc,0,0,128,128,spriteview.sv.bdc,0,0,SRCCOPY);
			else StretchBlt(dc,0,0,r.right,r.bottom,spriteview.sv.bdc,0,0,128,128,SRCCOPY);
			SelectObject(spriteview.sv.bdc,obj);
			
			EndPaint(wnd,&ps);
			
			break;
		}
		
		default: break;
	}
	
	return DefWindowProc(wnd,msg,wParam,lParam);
}

/* spat subwindow */
static BOOL CALLBACK spriteview_sub_spat(HWND wnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	if (!spriteview.spat.wnd) return DefWindowProc(wnd,msg,wParam,lParam);
	
	switch (msg) {
		
		case WM_SETCURSOR:
			SetCursor(spriteview.cursor_cross);
			return 1;
			break;
		
		case WM_MOUSEMOVE: {
			int in;
			POINT p;
			
			in=input_mouse_in_client(wnd,NULL,&p,TRUE);
			toolwindow_relative_clientpos(wnd,&p,128,64);
			spriteview.in_spat=in;
			spriteview.p_spat.x=p.x; spriteview.p_spat.y=p.y;
			
			break;
		}
		
		case WM_LBUTTONDOWN:
			spriteview.dclick=TRUE;
			break;
		
		case WM_RBUTTONDOWN:
			spriteview.rclickdown=2;
			break;
		
		case WM_RBUTTONUP:
			spriteview.rclick=2&spriteview.rclickdown;
			spriteview.rclickdown=0;
			break;
		
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HGDIOBJ obj;
			HDC dc;
			RECT r;
			
			if (spriteview.busy) break;
			
			GetClientRect(wnd,&r);
			dc=BeginPaint(wnd,&ps);
			
			obj=SelectObject(spriteview.spat.bdc,spriteview.spat.bmp);
			if (spriteview.is16&&r.right==128&&r.bottom==64) BitBlt(dc,0,0,128,64,spriteview.spat.bdc,0,0,SRCCOPY);
			else StretchBlt(dc,0,0,r.right,r.bottom,spriteview.spat.bdc,0,0,64<<spriteview.is16,32<<spriteview.is16,SRCCOPY);
			SelectObject(spriteview.spat.bdc,obj);
			
			EndPaint(wnd,&ps);
			
			break;
		}
		
		default: break;
	}
	
	return DefWindowProc(wnd,msg,wParam,lParam);
}

/* highlight subwindow */
static BOOL CALLBACK spriteview_sub_highlight(HWND wnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	switch (msg) {
		
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HGDIOBJ obj;
			HDC dc;
			
			if (!spriteview.h.wnd||spriteview.busy) break;
			
			dc=BeginPaint(wnd,&ps);
			
			obj=SelectObject(spriteview.h.bdc,spriteview.h.bmp);
			StretchBlt(dc,0,0,64,64,spriteview.h.bdc,0,0,8<<spriteview.is16,8<<spriteview.is16,SRCCOPY);
			SelectObject(spriteview.h.bdc,obj);
			
			EndPaint(wnd,&ps);
			
			break;
		}
		
		default: break;
	}
	
	return DefWindowProc(wnd,msg,wParam,lParam);
}


/* main window */
BOOL CALLBACK spriteview_window(HWND dialog,UINT msg,WPARAM wParam,LPARAM lParam)
{
	switch (msg) {
		
		case WM_INITDIALOG: {
			int i,j;
			HDC d_dc;
			HWND d_wnd;
			char t[0x100];
			
			if (spriteview.sv.wnd) break; /* shouldn't happen :) */
			
			spriteview.busy=TRUE;
			
			/* init info */
			spriteview.blank=spriteview.active_timer_enabled=FALSE;
			ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_BLANK),SW_HIDE);
			ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_M1),SW_HIDE);
			ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_ACTIVE),SW_HIDE);
			spriteview.is16=spriteview.ismag=spriteview.iscol=spriteview.is5s=spriteview.snum=-1;
			
			/* init spritemap highlight */
			spriteview.sg=spriteview.sinspat=-1;
			spriteview.hlvis=FALSE;
			ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_HLA),SW_HIDE);
			spriteview.h.wnd=GetDlgItem(dialog,IDC_SPRITEVIEW_HL);
			toolwindow_resize(spriteview.h.wnd,64,64);
			spriteview.h.wdc=GetDC(spriteview.h.wnd);
			spriteview.h.bdc=CreateCompatibleDC(spriteview.h.wdc);
			tool_init_bmi(&spriteview.h.info,32,16,1);
			spriteview.h.bmp=CreateDIBSection(spriteview.h.wdc,&spriteview.h.info,DIB_RGB_COLORS,(void*)&spriteview.h.screen,NULL,0);
			SetWindowLongPtr(spriteview.h.wnd,GWLP_WNDPROC,(LONG_PTR)spriteview_sub_highlight);
			
			/* init spat */
			spriteview.in_spat=FALSE; spriteview.p_spat.x=spriteview.p_spat.y=0;
			spriteview.spata=-1;
			spriteview.spat.wnd=GetDlgItem(dialog,IDC_SPRITEVIEW_SPAT);
			toolwindow_resize(spriteview.spat.wnd,128,64);
			spriteview.spat.wdc=GetDC(spriteview.spat.wnd);
			spriteview.spat.bdc=CreateCompatibleDC(spriteview.spat.wdc);
			tool_init_bmi(&spriteview.spat.info,128,64,32);
			spriteview.spat.bmp=CreateDIBSection(spriteview.spat.wdc,&spriteview.spat.info,DIB_RGB_COLORS,(void*)&spriteview.spat.screen,NULL,0);
			SetWindowLongPtr(spriteview.spat.wnd,GWLP_WNDPROC,(LONG_PTR)spriteview_sub_spat);
			
			/* init spat highlight */
			spriteview.spatvis=FALSE;
			ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATC),SW_HIDE);
			ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATXYD),SW_HIDE);
			ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATXYH),SW_HIDE);
			ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATSA),SW_HIDE);
			for (i=0;i<4;i++) spriteview.spatdata[i]=-1;
			spriteview.hspat=spriteview.s208=-1;
			spriteview.cx=spriteview.cy=-100000;
			
			/* pattern details box, fixed width font */
			d_wnd=GetDlgItem(dialog,IDC_SPRITEVIEW_SDETAILS);
			d_dc=GetDC(d_wnd);
			if ((spriteview.sfont=CreateFont(-MulDiv(10,GetDeviceCaps(d_dc,LOGPIXELSY),72),0,0,0,FW_BOLD,0,0,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,FIXED_PITCH|FF_MODERN,"Courier New\0"))!=NULL) {
				SendDlgItemMessage(dialog,IDC_SPRITEVIEW_SDETAILS,WM_SETFONT,(WPARAM)spriteview.sfont,TRUE);
			}
			ReleaseDC(d_wnd,d_dc);
			
			spriteview.dclick=FALSE;
			sprintf(t,"Click on a sprite to show.");
			SetDlgItemText(dialog,IDC_SPRITEVIEW_SDETAILS,t);
			
			/* init spritemap */
			spriteview.in_sv=FALSE; spriteview.p_sv.x=spriteview.p_sv.y=0;
			spriteview.sgo=-1;
			spriteview.sv.wnd=GetDlgItem(dialog,IDC_SPRITEVIEW_TILES);
			toolwindow_resize(spriteview.sv.wnd,128,128);
			spriteview.sv.wdc=GetDC(spriteview.sv.wnd);
			spriteview.sv.bdc=CreateCompatibleDC(spriteview.sv.wdc);
			tool_init_bmi(&spriteview.sv.info,128,128,1);
			spriteview.sv.bmp=CreateDIBSection(spriteview.sv.wdc,&spriteview.sv.info,DIB_RGB_COLORS,(void*)&spriteview.sv.screen,NULL,0);
			SetWindowLongPtr(spriteview.sv.wnd,GWLP_WNDPROC,(LONG_PTR)spriteview_sub_patterns);
			
			spriteview.cursor_cross=LoadCursor(NULL,IDC_CROSS);
			EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_OPEN),(netplay_is_active()|movie_get_active_state())==0);
			spriteview.popup_active=spriteview.rclick=spriteview.rclickdown=FALSE;
			spriteview.ext_dialog=0;
			spriteview.ext_wnd=NULL;
			toolwindow_setpos(dialog,TOOL_WINDOW_SPRITEVIEW,MAIN_PW_OUTERR,MAIN_PW_OUTERR,8,0,0);
			
			i=spriteview.win_width; j=spriteview.win_height;
			spriteview.win_width=SPRITEVIEW_WIN_WIDTH; spriteview.win_height=SPRITEVIEW_WIN_HEIGHT;
			if (i!=0&&j!=0) toolwindow_resize(dialog,i,j);
			else {
				POINT p;
				RECT r;
				
				/* fill initial middle dialog item Y */
				i=0;
				for (;;) {
					if (spriteview_dmp_initial[i][1]==-1) break;
					GetWindowRect(GetDlgItem(dialog,spriteview_dmp_initial[i][1]),&r);
					p.x=r.left; p.y=r.top; ScreenToClient(dialog,&p);
					spriteview_dmp_initial[i][0]=p.y;
					i++;
				}
			}
			
			spriteview.busy=FALSE;
			
			return 1;
			break;
		}
		
		case WM_CLOSE:
			if (tool_get_window(TOOL_WINDOW_SPRITEVIEW)) {
				
				GdiFlush();
				spriteview.busy=TRUE;
				
				toolwindow_savepos(dialog,TOOL_WINDOW_SPRITEVIEW);
				main_menu_check(IDM_SPRITEVIEW,FALSE);
				if (spriteview.sfont) { DeleteObject(spriteview.sfont); spriteview.sfont=NULL; }
				
				/* clean up spritemap */
				spriteview.sv.screen=NULL;
				if (spriteview.sv.bmp) { DeleteObject(spriteview.sv.bmp); spriteview.sv.bmp=NULL; }
				if (spriteview.sv.bdc) { DeleteDC(spriteview.sv.bdc); spriteview.sv.bdc=NULL; }
				if (spriteview.sv.wdc) { ReleaseDC(spriteview.sv.wnd,spriteview.sv.wdc); spriteview.sv.wdc=NULL; }
				spriteview.sv.wnd=NULL;
				
				/* clean up highlight */
				spriteview.h.screen=NULL;
				if (spriteview.h.bmp) { DeleteObject(spriteview.h.bmp); spriteview.h.bmp=NULL; }
				if (spriteview.h.bdc) { DeleteDC(spriteview.h.bdc); spriteview.h.bdc=NULL; }
				if (spriteview.h.wdc) { ReleaseDC(spriteview.h.wnd,spriteview.h.wdc); spriteview.h.wdc=NULL; }
				spriteview.h.wnd=NULL;
				
				/* clean up spat */
				spriteview.spat.screen=NULL;
				if (spriteview.spat.bmp) { DeleteObject(spriteview.spat.bmp); spriteview.spat.bmp=NULL; }
				if (spriteview.spat.bdc) { DeleteDC(spriteview.spat.bdc); spriteview.spat.bdc=NULL; }
				if (spriteview.spat.wdc) { ReleaseDC(spriteview.spat.wnd,spriteview.spat.wdc); spriteview.spat.wdc=NULL; }
				spriteview.spat.wnd=NULL;
				
				main_menu_enable(IDM_SPRITEVIEW,TRUE);
				DestroyWindow(dialog);
				tool_reset_window(TOOL_WINDOW_SPRITEVIEW);
			}
			
			break;
		
		case WM_SIZING:
			if (spriteview.sv.wnd) {
				InvalidateRect(dialog,NULL,FALSE);
				return toolwindow_restrictresize(dialog,wParam,lParam,SPRITEVIEW_WIN_WIDTH,SPRITEVIEW_WIN_HEIGHT);
			}
			
			break;
		
		case WM_SIZE:
			if (wParam!=SIZE_MINIMIZED&&spriteview.sv.wnd) {
				RECT rc,rcw,r;
				POINT p;
				
				GetClientRect(dialog,&rc);
				
				/* resize separator 1 */
				GetWindowRect(GetDlgItem(dialog,IDC_SPRITEVIEW_SEPARATOR1),&r); OffsetRect(&r,-r.left,-r.top);
				SetWindowPos(GetDlgItem(dialog,IDC_SPRITEVIEW_SEPARATOR1),NULL,0,0,r.right+(rc.right-spriteview.win_width),r.bottom,SWP_NOMOVE|SWP_NOZORDER);
				
				/* relocate close button */
				GetWindowRect(GetDlgItem(dialog,IDOK),&r);
				p.x=r.left; p.y=r.top; ScreenToClient(dialog,&p);
				SetWindowPos(GetDlgItem(dialog,IDOK),NULL,p.x+(rc.right-spriteview.win_width),p.y+(rc.bottom-spriteview.win_height),0,0,SWP_NOSIZE|SWP_NOZORDER);
				
				/* relocate/resize sprite details editbox */
				GetWindowRect(GetDlgItem(dialog,IDC_SPRITEVIEW_SDETAILS),&r);
				p.x=r.left; p.y=r.top; ScreenToClient(dialog,&p);
				GetWindowRect(GetDlgItem(dialog,IDC_SPRITEVIEW_SDETAILS),&r); OffsetRect(&r,-r.left,-r.top);
				SetWindowPos(GetDlgItem(dialog,IDC_SPRITEVIEW_SDETAILS),NULL,p.x,p.y+(rc.bottom-spriteview.win_height),r.right+(rc.right-spriteview.win_width),r.bottom,SWP_NOZORDER);
				
				/* relocate items on full Y */
				#define LOCATE(i)										\
					GetWindowRect(GetDlgItem(dialog,i),&r);				\
					p.x=r.left; p.y=r.top; ScreenToClient(dialog,&p);	\
					SetWindowPos(GetDlgItem(dialog,i),NULL,p.x,p.y+(rc.bottom-spriteview.win_height),0,0,SWP_NOSIZE|SWP_NOZORDER)
				
				LOCATE(IDC_SPRITEVIEW_DETAILST);
				LOCATE(IDC_SPRITEVIEW_OPEN);
				LOCATE(IDC_SPRITEVIEW_SAVE);
				
				#undef LOCATE
				
				/* relocate items on X */
				#define LOCATE(i)										\
					GetWindowRect(GetDlgItem(dialog,i),&r);				\
					p.x=r.left; p.y=r.top; ScreenToClient(dialog,&p);	\
					SetWindowPos(GetDlgItem(dialog,i),NULL,p.x+(rc.right-spriteview.win_width),p.y,0,0,SWP_NOSIZE|SWP_NOZORDER)
				
				LOCATE(IDC_SPRITEVIEW_STATUS);
				LOCATE(IDC_SPRITEVIEW_C);
				LOCATE(IDC_SPRITEVIEW_5S);
				LOCATE(IDC_SPRITEVIEW_HL);
				LOCATE(IDC_SPRITEVIEW_HLINFO);
				LOCATE(IDC_SPRITEVIEW_HLPT);
				LOCATE(IDC_SPRITEVIEW_HLP);
				LOCATE(IDC_SPRITEVIEW_HLAT);
				LOCATE(IDC_SPRITEVIEW_HLA);
				LOCATE(IDC_SPRITEVIEW_HLST);
				LOCATE(IDC_SPRITEVIEW_HLS);
				
				#undef LOCATE
				
				/* relocate SPAT address */
				GetWindowRect(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATA),&r);
				p.x=r.left; p.y=r.top; ScreenToClient(dialog,&p);
				SetWindowPos(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATA),NULL,p.x,spriteview_dmp_find(IDC_SPRITEVIEW_SPATA)+(rc.bottom-SPRITEVIEW_WIN_HEIGHT)*0.667+0.5,0,0,SWP_NOSIZE|SWP_NOZORDER);
				
				/* relocate/resize separator 2 */
				GetWindowRect(GetDlgItem(dialog,IDC_SPRITEVIEW_SEPARATOR2),&r);
				p.x=r.left; p.y=r.top; ScreenToClient(dialog,&p);
				GetWindowRect(GetDlgItem(dialog,IDC_SPRITEVIEW_SEPARATOR2),&r); OffsetRect(&r,-r.left,-r.top);
				SetWindowPos(GetDlgItem(dialog,IDC_SPRITEVIEW_SEPARATOR2),NULL,p.x,spriteview_dmp_find(IDC_SPRITEVIEW_SEPARATOR2)+(rc.bottom-SPRITEVIEW_WIN_HEIGHT)*0.667+0.5,r.right+(rc.right-spriteview.win_width),r.bottom,SWP_NOZORDER);
				
				/* relocate/resize SPAT */
				GetWindowRect(GetDlgItem(dialog,IDC_SPRITEVIEW_SPAT),&r);
				p.x=r.left; p.y=r.top; ScreenToClient(dialog,&p);
				GetWindowRect(GetDlgItem(dialog,IDC_SPRITEVIEW_SPAT),&r); OffsetRect(&r,-r.left,-r.top);
				GetClientRect(GetDlgItem(dialog,IDC_SPRITEVIEW_SPAT),&rcw);
				SetWindowPos(GetDlgItem(dialog,IDC_SPRITEVIEW_SPAT),NULL,p.x,spriteview_dmp_find(IDC_SPRITEVIEW_SPAT)+(rc.bottom-SPRITEVIEW_WIN_HEIGHT)*0.667+0.5,r.right+(rc.right-spriteview.win_width),64+(r.bottom-rcw.bottom)+(rc.bottom-SPRITEVIEW_WIN_HEIGHT)*0.333+0.5,SWP_NOZORDER);
				
				/* relocate SPAT info */
				rcw.bottom-=64; if (rcw.bottom>6) rcw.bottom=6;
				
				#define LOCATE(i)										\
					GetWindowRect(GetDlgItem(dialog,i),&r);				\
					p.x=r.left; p.y=r.top; ScreenToClient(dialog,&p);	\
					SetWindowPos(GetDlgItem(dialog,i),NULL,p.x+(rc.right-spriteview.win_width),spriteview_dmp_find(i)+rcw.bottom+(rc.bottom-SPRITEVIEW_WIN_HEIGHT)*0.667+0.5,0,0,SWP_NOSIZE|SWP_NOZORDER)
				
				LOCATE(IDC_SPRITEVIEW_SPATT);
				LOCATE(IDC_SPRITEVIEW_SPATS);
				LOCATE(IDC_SPRITEVIEW_SPATSAT);
				LOCATE(IDC_SPRITEVIEW_SPATSA);
				LOCATE(IDC_SPRITEVIEW_SPATXY);
				LOCATE(IDC_SPRITEVIEW_SPATXYH);
				LOCATE(IDC_SPRITEVIEW_SPATXYD);
				LOCATE(IDC_SPRITEVIEW_SPATCT);
				LOCATE(IDC_SPRITEVIEW_SPATC);
				
				#undef LOCATE
				
				/* resize SPGT */
				GetWindowRect(GetDlgItem(dialog,IDC_SPRITEVIEW_TILES),&r); OffsetRect(&r,-r.left,-r.top);
				GetClientRect(GetDlgItem(dialog,IDC_SPRITEVIEW_TILES),&rcw);
				SetWindowPos(GetDlgItem(dialog,IDC_SPRITEVIEW_TILES),NULL,0,0,r.right+(rc.right-spriteview.win_width),128+(r.bottom-rcw.bottom)+(rc.bottom-SPRITEVIEW_WIN_HEIGHT)*0.667+0.5,SWP_NOMOVE|SWP_NOZORDER);
				
				spriteview.win_width=rc.right; spriteview.win_height=rc.bottom;
				
				InvalidateRect(dialog,NULL,FALSE);
			}
			break;
		
		case WM_COMMAND:
			
			switch (LOWORD(wParam)) {
				
				/* load sprite patterns */
				case IDC_SPRITEVIEW_OPEN: {
					const char* filter="All Supported Files\0*.spgt;*.spat\0SPGT Dumps (*.spgt)\0*.spgt\0SPAT Dumps (*.spat)\0*.spat\0All Files (*.*)\0*.*\0\0";
					const char* title="Open Sprite Data";
					char fn[STRING_SIZE]={0};
					OPENFILENAME of;
					
					if (!spriteview.sv.wnd||spriteview.ext_dialog||spriteview.popup_active||netplay_is_active()||movie_get_active_state()) break;
					
					spriteview.ext_dialog=1;
					spriteview.in_sv=spriteview.in_spat=spriteview.rclickdown=spriteview.rclick=FALSE;
					
					memset(&of,0,sizeof(OPENFILENAME));
					
					of.lStructSize=sizeof(OPENFILENAME);
					of.hwndOwner=dialog;
					of.hInstance=MAIN->module;
					of.lpstrFile=fn;
					of.lpstrFilter=filter;
					of.lpstrTitle=title;
					of.nMaxFile=STRING_SIZE;
					of.nFilterIndex=spriteview_open_fi;
					of.Flags=OFN_ENABLESIZING|OFN_HIDEREADONLY|OFN_FILEMUSTEXIST;
					of.lpstrInitialDir=strlen(spriteview_dir)?spriteview_dir:file->tooldir?file->tooldir:file->appdir;
					
					main_menu_enable(IDM_SPRITEVIEW,FALSE); /* resource leak if forced to close */
					if (GetOpenFileName(&of)) {
						int success=FALSE;
						FILE* fd=NULL;
						u32 fi=of.nFilterIndex;
						int size;
						
						if (!spriteview.sv.wnd) {
							/* cleaned up already (check again since time passed) */
							main_menu_enable(IDM_SPRITEVIEW,TRUE);
							break;
						}
						
						spriteview_open_fi=fi;
						fi--; if (fi<1) fi=3; /* all supported == all files */
						
						if (!netplay_is_active()&&!movie_get_active_state()&&strlen(fn)&&(size=file_open_custom(&fd,fn))>10) {
							u8 data[0x4010];
							memset(data,0,0x4010);
							
							if (size<0x4010&&file_read_custom(fd,data,size)) {
								int realsize,offset=0;
								if (size&1&&data[0]==0xfe) offset+=7; /* assume bload header */
								realsize=size-offset;
								
								file_close_custom(fd);
								fd=NULL;
								
								/* any file, autodetect type */
								if (fi>2) {
									if (realsize==128) fi=2;
									else fi=1;
								}
								
								switch (fi) {
									/* spat */
									case 2:
										if (!vdp_upload(vdp_regs[5]<<7&0x3fff,data,128)) LOG_ERROR_WINDOW(dialog,"Couldn't load sprite attributes!");
										else success=TRUE;
										break;
									
									/* spgt */
									default:
										if (!vdp_upload(vdp_regs[6]<<11&0x3fff,data,0x800)) LOG_ERROR_WINDOW(dialog,"Couldn't load sprite patterns!");
										else success=TRUE;
										break;
								}
							}
							else LOG_ERROR_WINDOW(dialog,"Couldn't load sprite data!");
						}
						else LOG_ERROR_WINDOW(dialog,"Couldn't load sprite data!");
						
						file_close_custom(fd);
						if (success&&strlen(fn+of.nFileOffset)) {
							char wintitle[STRING_SIZE]={0};
							sprintf(wintitle,"Sprite Viewer - %s",fn+of.nFileOffset);
							SetWindowText(dialog,wintitle);
						}
						if (strlen(fn)&&of.nFileOffset) {
							fn[of.nFileOffset]=0; strcpy(spriteview_dir,fn);
						}
					}
					
					spriteview.ext_dialog=0;
					PostMessage(dialog,WM_NEXTDLGCTL,(WPARAM)GetDlgItem(dialog,IDOK),TRUE);
					main_menu_enable(IDM_SPRITEVIEW,TRUE);
					
					break;
				}
				
				/* save sprite patterns */
				case IDC_SPRITEVIEW_SAVE: {
					u8 spat[128];
					u8 spgt[0x800];
					u8 dibdata[0x800]; /* 128/8*128 */
					const int pal[2]={0,0xffffff};
					const char* filter="PNG Image (*.png)\0*.png\0SPGT Dump (*.spgt)\0*.spgt\0SPAT Dump (*.spat)\0*.spat\0\0";
					const char* defext="spgt";
					const char* title="Save Sprite Data As";
					char fn[STRING_SIZE]={0};
					OPENFILENAME of;
					
					if (!spriteview.sv.wnd||spriteview.ext_dialog||spriteview.popup_active) break;
					
					spriteview.ext_dialog=1;
					spriteview.in_sv=spriteview.in_spat=spriteview.rclickdown=spriteview.rclick=FALSE;
					
					/* create local-local copy of screen/sprite ram */
					memcpy(spat,vdp_ram+(vdp_regs[5]<<7&0x3fff),128);
					memcpy(spgt,vdp_ram+(vdp_regs[6]<<11&0x3fff),0x800);
					GdiFlush();
					memcpy(dibdata,spriteview.sv.screen,0x800);
					
					memset(&of,0,sizeof(OPENFILENAME));
					
					of.lStructSize=sizeof(OPENFILENAME);
					of.hwndOwner=dialog;
					of.hInstance=MAIN->module;
					of.lpstrFile=fn;
					of.lpstrDefExt=defext;
					of.lpstrFilter=filter;
					of.lpstrTitle=title;
					of.nMaxFile=STRING_SIZE;
					of.nFilterIndex=spriteview_save_fi;
					of.Flags=OFN_ENABLESIZING|OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
					of.lpstrInitialDir=strlen(spriteview_dir)?spriteview_dir:file->tooldir?file->tooldir:file->appdir;
					
					main_menu_enable(IDM_SPRITEVIEW,FALSE); /* resource leak if forced to close */
					if (GetSaveFileName(&of)) {
						int success=FALSE;
						FILE* fd=NULL;
						
						if (!spriteview.sv.wnd) {
							/* cleaned up already (check again since time passed) */
							main_menu_enable(IDM_SPRITEVIEW,TRUE);
							break;
						}
						
						spriteview_save_fi=of.nFilterIndex;
						
						switch (spriteview_save_fi) {
							/* spgt */
							case 2:
								if (!strlen(fn)||!file_save_custom(&fd,fn)||!file_write_custom(fd,spgt,0x800)) LOG_ERROR_WINDOW(dialog,"Couldn't save sprite patterns!");
								else success=TRUE;
								break;
							
							/* spat */
							case 3:
								if (!strlen(fn)||!file_save_custom(&fd,fn)||!file_write_custom(fd,spat,128)) LOG_ERROR_WINDOW(dialog,"Couldn't save sprite attributes!");
								else success=TRUE;
								break;
							
							/* png */
							default:
								if (!screenshot_save(128,128,SCREENSHOT_TYPE_1BPP_INDEXED,(void*)dibdata,(void*)pal,fn)) LOG_ERROR_WINDOW(dialog,"Couldn't save screenshot!");
								/* no "success" */
								break;
						}
						
						file_close_custom(fd);
						if (success&&strlen(fn+of.nFileOffset)) {
							char wintitle[STRING_SIZE]={0};
							sprintf(wintitle,"Sprite Viewer - %s",fn+of.nFileOffset);
							SetWindowText(dialog,wintitle);
						}
						if (strlen(fn)&&of.nFileOffset) {
							fn[of.nFileOffset]=0; strcpy(spriteview_dir,fn);
						}
					}
					
					spriteview.ext_dialog=0;
					PostMessage(dialog,WM_NEXTDLGCTL,(WPARAM)GetDlgItem(dialog,IDOK),TRUE);
					main_menu_enable(IDM_SPRITEVIEW,TRUE);
					
					break;
				}
				
				/* close dialog manually */
				case IDOK: case IDCANCEL:
					PostMessage(dialog,WM_CLOSE,0,0);
					break;
				
				default: break;
			} /* WM_COMMAND */
			
			break;
		
		case TOOL_REPAINT: {
			int i,j,k,l,m,n,o;
			
			const int vdp_status=tool_get_local_vdp_status();
			const int is16=vdp_regs[1]>>1&1;
			const int ismag=vdp_regs[1]&1;
			const int sgo=vdp_regs[6]<<11&0x3fff;
			const int spato=vdp_regs[5]<<7&0x3fff;
			const int blank=((vdp_regs[1]>>5&2)|(vdp_regs[1]>>4&1))^2;
			int shift,mask,smask,s208,sg,spat=0,spatvis=0,hlvis=0,cell=-1,hspat=-1;
			POINT spos;
			int* screen;
			u8* screen8;
			
			if (!spriteview.sv.wnd||spriteview.busy) break;
			spriteview.busy=TRUE;
			
			/* get highlighted cell */
			/* in case mouse moved too fast/to another window */
			if (spriteview.in_sv) {
				i=input_mouse_in_client(spriteview.sv.wnd,dialog,&spos,GetForegroundWindow()==dialog);
				toolwindow_relative_clientpos(spriteview.sv.wnd,&spos,128,128);
				spriteview.p_sv.x=spos.x; spriteview.p_sv.y=spos.y;
				if (!i) spriteview.in_sv=spriteview.rclickdown=spriteview.rclick=0;
			}
			if (spriteview.in_spat) {
				i=input_mouse_in_client(spriteview.spat.wnd,dialog,&spos,GetForegroundWindow()==dialog);
				toolwindow_relative_clientpos(spriteview.spat.wnd,&spos,128,64);
				spriteview.p_spat.x=spos.x; spriteview.p_spat.y=spos.y;
				if (!i) spriteview.in_spat=spriteview.rclickdown=spriteview.rclick=0;
			}
			
			/* sprite pattern map */
			if (spriteview.in_sv) {
				hlvis=TRUE;
				cell=(spriteview.p_sv.x>>2&0x1c)|(spriteview.p_sv.y<<1&0xe0)|(spriteview.p_sv.x>>2&2)|(spriteview.p_sv.y>>3&1);
				if (is16) cell&=0xfc;
			}
			/* spat */
			if (spriteview.in_spat) {
				hlvis=spatvis=TRUE;
				hspat=(spriteview.p_spat.x>>4&7)|(spriteview.p_spat.y>>1&0x18);
				cell=vdp_ram[spato|hspat<<2|2];
			}
			
			GdiFlush();
			
			/* find spat end */
			for (i=0;i<0x80;i+=4) {
				if (vdp_ram[spato|i]==208) break;
			}
			s208=i>>2;
			
			/* draw spritemap */
			screen8=spriteview.sv.screen;
			
			for (i=0;i<0x800;i+=0x100) {
				for (j=0;j<16;j++) {
					o=sgo|i|j;
					for (k=0;k<0x100;k+=16) {
						*screen8++=vdp_ram[o|k];
					}
				}
			}
			
			
			/* draw highlight */
			if (cell>=0) {
				char t[0x1000];
				char d[0x100];
				int spatdata[4]={0,0,0,0};
				spatdata[2]=cell;
				
				if (is16) {
					/* 16*16 */
					i=16; sg=sgo|(cell<<3&0xfe0);
					while (i--) {
						spriteview.h.screen[i<<2]=vdp_ram[sg|i];
						spriteview.h.screen[i<<2|1]=vdp_ram[sg|i|16];
					}
				}
				
				else {
					/* 8*8 */
					i=8; sg=sgo|cell<<3;
					while (i--) spriteview.h.screen[i<<2]=vdp_ram[sg|i];
				}
				
				/* highlight info */
				if (spatvis) {
					for (i=0;i<4;i++) spatdata[i]=vdp_ram[spato|hspat<<2|i];
					
					/* spat address */
					if (spato!=spriteview.spata||hspat!=spriteview.hspat) {
						sprintf(t,"$%04X",spato|hspat<<2);
						SetDlgItemText(dialog,IDC_SPRITEVIEW_SPATSA,t);
						/* set struct values later */
					}
					
					/* position */
					k=(spatdata[0]!=spriteview.spatdata[0])|(spatdata[1]!=spriteview.spatdata[1])|((spatdata[3]&0x80)!=(spriteview.spatdata[3]&0x80))|(spriteview.spatdata[3]==-1);
					if (k) {
						if (spatdata[3]&0x80) sprintf(d," EC");
						else d[0]=0;
						sprintf(t,"($%02X,$%02X)%s",spatdata[1],spatdata[0],d);
						SetDlgItemText(dialog,IDC_SPRITEVIEW_SPATXYH,t);
					}
					
					k=0; l=(8<<is16)<<ismag;
					
					if (s208==hspat) { i=j=-1000; k=1; }
					else if (s208<hspat) { i=j=-2000; k=2; }
					
					else {
						i=spatdata[1]-(spatdata[3]>>2&0x20);
						j=(spatdata[0]+1)&0xff;
						if (j>191) j=-(0x100-j);
						
						if ((i+l)<=0||(j+l)<=0) k=4;
					}
					
					if (i!=spriteview.cx||j!=spriteview.cy) {
						switch (k&3) {
							case 1: sprintf(t,"at SPAT end"); break;
							case 2: sprintf(t,"past SPAT end"); break;
							
							default:
								if (k==4) sprintf(d," OB");
								else d[0]=0;
								sprintf(t,"(%d,%d)%s",i,j,d);
								break;
						}
						
						SetDlgItemText(dialog,IDC_SPRITEVIEW_SPATXYD,t);
						spriteview.cx=i; spriteview.cy=j;
					}
					
					/* sprite number */
					if (hspat!=spriteview.hspat) {
						if (hspat>9) sprintf(d," (%d)",hspat);
						else d[0]=0;
						sprintf(t,"$%02X%s",hspat,d);
						SetDlgItemText(dialog,IDC_SPRITEVIEW_SPATS,t);
						spriteview.hspat=hspat;
					}
					
					/* colour */
					k=spatdata[3]&0xf;
					if (k!=(spriteview.spatdata[3]&0xf)||spriteview.spatdata[3]==-1) {
						if (k>9) sprintf(d," (%d)",k);
						else d[0]=0;
						sprintf(t,"$%X%s",k,d);
						SetDlgItemText(dialog,IDC_SPRITEVIEW_SPATC,t);
					}
					
					spriteview.spatdata[0]=spatdata[0];
					spriteview.spatdata[1]=spatdata[1];
					spriteview.spatdata[3]=spatdata[3];
				}
				
				/* pattern */
				if (spatdata[2]!=spriteview.spatdata[2]) {
					sprintf(t,"$%02X (%d)",spatdata[2],spatdata[2]);
					SetDlgItemText(dialog,IDC_SPRITEVIEW_HLP,t);
					spriteview.spatdata[2]=spatdata[2];
				}
				
				/* sprite pattern address */
				if (sg!=spriteview.sg) {
					sprintf(t,"$%04X",sg);
					SetDlgItemText(dialog,IDC_SPRITEVIEW_HLA,t);
					spriteview.sg=sg;
				}
				
				smask=is16?0xfc:0xff;
				cell&=smask;
				
				/* in spat? */
				j=0; k=s208<<2;
				for (i=2;i<0x80;i+=4) {
					if (i>k) break; /* spat end */
					j+=((vdp_ram[spato|i]&smask)==cell);
				}
				if (j!=spriteview.sinspat) {
					if (j) sprintf(t,"%d",j);
					else sprintf(t,"no");
					SetDlgItemText(dialog,IDC_SPRITEVIEW_HLS,t);
					spriteview.sinspat=j;
				}
				
				/* clicked */
				if (spriteview.dclick) {
					char dinfo[0x1000]={0};
					
					sprintf(t,"&Pattern $%02X Data",cell);
					SetDlgItemText(dialog,IDC_SPRITEVIEW_DETAILST,t);
					
					for (j=0;j<(8<<is16);j++) {
						
						/* extra enter after 8 lines */
						if (j==8) strcat(dinfo,"\r\n\r\n");
						
						for (k=0;k<(1+is16);k++) {
							if (is16&k) strcat(dinfo," - ");
							
							/* sprite pattern address */
							sprintf(t,"%04X: ",sg|j|k<<4);
							strcat(dinfo,t);
							
							/* binary data */
							i=8;
							while (i--) {
								sprintf(t,"%c",(vdp_ram[sg|j|k<<4]>>i&1)?'1':TOOL_DBIN0);
								strcat(dinfo,t);
							}
							
							/* hex data */
							sprintf(t," %02X",vdp_ram[sg|j|k<<4]);
							strcat(dinfo,t);
						}
						
						/* next line */
						if ((j&7)!=7) strcat(dinfo,"\r\n");
					}
					
					t[0]=0; SetDlgItemText(dialog,IDC_SPRITEVIEW_SDETAILS,t);
					SendMessage(dialog,WM_NEXTDLGCTL,(WPARAM)GetDlgItem(dialog,IDC_SPRITEVIEW_SDETAILS),TRUE);
					SetDlgItemText(dialog,IDC_SPRITEVIEW_SDETAILS,dinfo);
				}
				
				/* right click, popup menu */
				if (spriteview.rclick) PostMessage(dialog,TOOL_POPUP,cell|(spriteview.rclick<<7&0x100)|(is16<<9&0x200),(spriteview.hspat&0x1f)|(spriteview.spatdata[3]<<8&0xf00));
			}
			else {
				if (!spriteview.popup_active) memset(spriteview.h.screen,0,0x40);
			}
			
			
			/* draw spat */
			screen=spriteview.spat.screen;
			smask=is16?0xfe0:0xfff;
			n=8<<is16;
			
			/* process sprites */
			for (i=0;i<32;i++) {
				if (s208<=i) { shift=1; mask=0x3f7f7f7f; }
				else { shift=0; mask=0x7fffffff; }
				spat=spato|i<<2|2;
				m=vdp_ram[spat|1]&0xf;
				k=draw_palette[m]>>shift&mask;
				sg=sgo|(vdp_ram[spat]<<3&smask);
				
				#define UPDS()								\
				if ((l<<=1)&0x100) screen[o]=k;				\
				else screen[o]=TOOL_DBM_EMPTY>>shift&mask;	\
				screen++
				
				l=0; j=n;
				while (j--) {
					o=j<<7;
					if (m) l=vdp_ram[sg|j];
					UPDS(); UPDS(); UPDS(); UPDS(); UPDS(); UPDS(); UPDS(); UPDS();
					screen-=8;
				}
				
				screen+=8;
				
				if (is16) {
					sg|=n; j=n;
					while (j--) {
						o=j<<7;
						if (m) l=vdp_ram[sg|j];
						UPDS(); UPDS(); UPDS(); UPDS(); UPDS(); UPDS(); UPDS(); UPDS();
						screen-=8;
					}
					
					screen+=8;
				}
				
				if ((i&7)==7) screen+=(960<<is16);
				
				#undef UPDS
			}
			
			
			/* highlight info visibility */
			if (hlvis!=spriteview.hlvis&&!spriteview.popup_active) {
				i=hlvis?SW_NORMAL:SW_HIDE;
				
				EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_HLAT),hlvis);
				EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_HLST),hlvis);
				
				ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_HLA),i);
				ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_HLS),i);
				
				if (!hlvis) {
					char t[0x100];
					
					sprintf(t,"none targeted");
					SetDlgItemText(dialog,IDC_SPRITEVIEW_HLP,t);
					spriteview.spatdata[2]=-1;
				}
				
				spriteview.hlvis=hlvis;
			}
			if (spatvis!=spriteview.spatvis&&!spriteview.popup_active) {
				i=spatvis?SW_NORMAL:SW_HIDE;
				
				EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATSAT),spatvis);
				EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATXY),spatvis);
				EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATCT),spatvis);
				
				ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATC),i);
				ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATXYD),i);
				ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATXYH),i);
				ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_SPATSA),i);
				
				if (!spatvis) {
					char t[0x100];
					
					sprintf(t,"none targeted");
					SetDlgItemText(dialog,IDC_SPRITEVIEW_SPATS,t);
					spriteview.hspat=-1;
				}
				
				spriteview.spatvis=spatvis;
			}
			
			/* info */
			/* blank/active */
			if (blank!=spriteview.blank) {
				EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_BLANK),(blank&2)!=0);
				EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_M1),(blank&1)!=0);
				
				if (blank==0) {
					ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_BLANK),SW_HIDE);
					ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_M1),SW_HIDE);
					ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_ACTIVE),SW_NORMAL);
					
					spriteview.active_timer_enabled=TRUE;
					spriteview.active_ticks=GetTickCount()+2000; /* 2 seconds "Active" */
				}
				else if (spriteview.blank==0) {
					ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_BLANK),SW_NORMAL);
					ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_M1),SW_NORMAL);
					ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_ACTIVE),SW_HIDE);
					
					spriteview.active_timer_enabled=FALSE;
				}
				
				spriteview.blank=blank;
			}
			if (spriteview.active_timer_enabled&&GetTickCount()>spriteview.active_ticks) {
				ShowWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_ACTIVE),SW_HIDE);
				spriteview.active_timer_enabled=FALSE;
			}
			
			/* 16 x 16 */
			if (is16!=spriteview.is16) {
				EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_SIZE),is16!=0);
				
				if (!spriteview.popup_active) {
					char t[0x100];
					sprintf(t,"(%d*%d)",8<<is16,8<<is16);
					SetDlgItemText(dialog,IDC_SPRITEVIEW_HLINFO,t);
					spriteview.is16=is16;
				}
			}
			
			/* magnified */
			if (ismag!=spriteview.ismag) {
				EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_MAG),ismag!=0);
				spriteview.ismag=ismag;
			}
			
			/* collision */
			if ((vdp_status&0x20)!=spriteview.iscol) {
				spriteview.iscol=vdp_status&0x20;
				EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_C),spriteview.iscol!=0);
			}
			
			/* 5th sprite */
			if ((vdp_status&0x40)!=spriteview.is5s) {
				spriteview.is5s=vdp_status&0x40;
				EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_5S),spriteview.is5s!=0);
			}
			if ((vdp_status&0x1f)!=spriteview.snum) {
				char t[0x100];
				
				spriteview.snum=vdp_status&0x1f;
				sprintf(t,"5S:$%02X",spriteview.snum);
				SetDlgItemText(dialog,IDC_SPRITEVIEW_5S,t);
			}
			
			/* address info */
			/* sprite generator patterns */
			if (sgo!=spriteview.sgo) {
				char t[0x100];
				
				sprintf(t,"SPGT ($%04X)",sgo);
				SetDlgItemText(dialog,IDC_SPRITEVIEW_PATA,t);
				spriteview.sgo=sgo;
			}
			/* spat */
			if (spato!=spriteview.spata) {
				char t[0x100];
				
				sprintf(t,"SPAT ($%04X)",spato);
				SetDlgItemText(dialog,IDC_SPRITEVIEW_SPATA,t);
				spriteview.spata=spato;
			}
			
			spriteview.s208=s208;
			spriteview.rclick=spriteview.dclick=FALSE;
			spriteview.busy=FALSE;
			
			InvalidateRect(spriteview.sv.wnd,NULL,FALSE);
			InvalidateRect(spriteview.spat.wnd,NULL,FALSE);
			InvalidateRect(spriteview.h.wnd,NULL,FALSE);
			
			if (spriteview.ext_dialog&&spriteview.ext_wnd) PostMessage(spriteview.ext_wnd,TOOL_REPAINT,0,0);
			
			break;
		} /* TOOL_REPAINT */
		
		case TOOL_POPUP: {
			int i,gray,gray_paste;
			HMENU menu,popup;
			UINT id;
			int in_prev=spriteview.in_sv|spriteview.in_spat<<1;
			
			if (!spriteview.sv.wnd||spriteview.popup_active||spriteview.ext_dialog) break;
			if ((menu=LoadMenu(MAIN->module,(wParam&0x100)?"spriteviewspatpopup":"spriteviewspgtpopup"))==NULL) break;
			
			spriteview.in_sv=spriteview.in_spat=FALSE;
			spriteview.popup_active=TRUE;
			
			/* create local-local copy of vdp state */
			memcpy(spriteedit.vdp_ram,vdp_ram,0x4000);
			for (i=0;i<8;i++) spriteedit.vdp_regs[i]=vdp_regs[i];
			
			gray=netplay_is_active()|movie_get_active_state();
			gray_paste=gray|(spriteview.paste_valid==0);
			if (gray) EnableMenuItem(menu,IDM_SPRITEVIEW_EDIT,MF_GRAYED);
			if (gray&&wParam&0x100) EnableMenuItem(menu,IDM_SPRITEVIEW_SPAT,MF_GRAYED);
			if (gray&&!(wParam&0x100)) EnableMenuItem(menu,IDM_SPRITEVIEW_CUT,MF_GRAYED);
			if (gray_paste&&!(wParam&0x100)) EnableMenuItem(menu,IDM_SPRITEVIEW_PASTE,MF_GRAYED);
			
			popup=GetSubMenu(menu,0);
			spriteview.popup_p.x=spriteview.popup_p.y=0;
			GetCursorPos(&spriteview.popup_p);
			
			#define CLEAN_MENU(x)				\
			if (menu) {							\
				DestroyMenu(menu); menu=NULL;	\
				spriteview.in_sv=(x)&1;			\
				spriteview.in_spat=(x)>>1&1;	\
			}									\
			spriteview.popup_active=FALSE
			
			
			id=TrackPopupMenuEx(popup,TPM_LEFTALIGN|TPM_TOPALIGN|TPM_NONOTIFY|TPM_RETURNCMD|TPM_RIGHTBUTTON,(int)spriteview.popup_p.x,(int)spriteview.popup_p.y,dialog,NULL);
			switch (id) {
				/* cut/copy/paste pattern */
				case IDM_SPRITEVIEW_CUT: {
					u8 s_e[0x20];
					
					if (gray||wParam&0x100) break;
					memset(spriteview.copy_data,0,0x20);
					spriteview.paste_valid=TRUE;
					memcpy(spriteview.copy_data,spriteedit.vdp_ram+((spriteedit.vdp_regs[6]<<11&0x3fff)|(wParam<<3&0x7ff)),8<<(wParam>>8&2));
					
					memset(s_e,0,0x20);
					if (!vdp_upload((spriteedit.vdp_regs[6]<<11&0x3fff)|(wParam<<3&0x7ff),s_e,8<<(spriteedit.vdp_regs[1]&2))) LOG_ERROR_WINDOW(dialog,"Couldn't cut sprite pattern!");
					
					break;
				}
				
				case IDM_SPRITEVIEW_COPY:
					if (wParam&0x100) break;
					memset(spriteview.copy_data,0,0x20);
					spriteview.paste_valid=TRUE;
					memcpy(spriteview.copy_data,spriteedit.vdp_ram+((spriteedit.vdp_regs[6]<<11&0x3fff)|(wParam<<3&0x7ff)),8<<(wParam>>8&2));
					break;
				
				case IDM_SPRITEVIEW_PASTE:
					if (gray_paste||wParam&0x100) break;
					
					if (!vdp_upload((spriteedit.vdp_regs[6]<<11&0x3fff)|(wParam<<3&0x7ff),spriteview.copy_data,8<<(spriteedit.vdp_regs[1]&2))) LOG_ERROR_WINDOW(dialog,"Couldn't paste sprite pattern!");
					break;
				
				/* change spat value */
				case IDM_SPRITEVIEW_SPAT:
					if (gray||!(wParam&0x100)) break;
					CLEAN_MENU(FALSE);
					
					spriteview.spatc=lParam&0x1f;
					spriteview.ext_dialog=IDD_SPRITEVIEW_CHANGESPAT;
					if (DialogBox(MAIN->module,MAKEINTRESOURCE(IDD_SPRITEVIEW_CHANGESPAT),dialog,spriteview_changespat_dialog)==1) LOG_ERROR_WINDOW(dialog,"Couldn't change SPAT!");
					spriteview.ext_dialog=0;
					
					break;
				
				/* sprite editor */
				case IDM_SPRITEVIEW_EDIT:
					if (gray) break;
					CLEAN_MENU(FALSE);
					
					if (!(wParam&0x100)||!(lParam>>8&0xf)) spriteedit.colour=0xf;
					else spriteedit.colour=lParam>>8&0xf;
					spriteedit.is16=wParam>>9&1;
					spriteedit.cell=wParam&0xff;
					spriteview.ext_dialog=IDD_SPRITEEDIT;
					DialogBox(MAIN->module,MAKEINTRESOURCE(IDD_SPRITEEDIT),dialog,spriteview_editor_dialog);
					spriteview.ext_dialog=0;
					
					PostMessage(dialog,WM_NEXTDLGCTL,(WPARAM)GetDlgItem(dialog,IDOK),TRUE);
					break;
				
				/* cancel */
				default: break;
			}
			
			CLEAN_MENU(in_prev);
			#undef CLEAN_MENU
			
			break;
		}
		
		case TOOL_MENUCHANGED:
			if (!spriteview.sv.wnd) break;
			EnableWindow(GetDlgItem(dialog,IDC_SPRITEVIEW_OPEN),(netplay_is_active()|movie_get_active_state())==0);
			if (spriteview.ext_dialog&&spriteview.ext_wnd) PostMessage(spriteview.ext_wnd,TOOL_MENUCHANGED,0,0);
			break;
		
		default: break;
	}
	
	return 0;
}


/* init/clean (only once) */
void spriteview_init(void)
{
	vdp_ram=tool_get_local_vdp_ram_ptr();
	vdp_regs=tool_get_local_vdp_regs_ptr();
	draw_palette=tool_get_local_draw_palette_ptr();
	
	memset(&spriteview,0,sizeof(spriteview));
	memset(&spriteedit,0,sizeof(spriteedit));
	
	MEM_CREATE_T(_spriteedit_undo_begin,sizeof(_spriteedit_undo),_spriteedit_undo*);
	_spriteedit_undo_begin->prev=_spriteedit_undo_begin;
	_spriteedit_undo_cursor=_spriteedit_undo_begin;
	
	/* settings */
	spriteedit_fi=tool_get_pattern_fi_shared();
	spriteview_open_fi=1;	SETTINGS_GET_INT(settings_info(SETTINGS_FILTERINDEX_OPENSPRT),&spriteview_open_fi);	CLAMP(spriteview_open_fi,1,4);
	spriteview_save_fi=2;	SETTINGS_GET_INT(settings_info(SETTINGS_FILTERINDEX_SAVESPRT),&spriteview_save_fi);	CLAMP(spriteview_save_fi,1,3);
}

void spriteview_clean(void)
{
	MEM_CLEAN(_spriteedit_undo_begin);
	_spriteedit_undo_cursor=NULL;
}
