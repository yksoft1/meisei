/* sprite editor (spriteview.c) */

enum {
	SPRITEEDIT_ACT_NOTHING=0,
	SPRITEEDIT_ACT_FG,	/* zoom foreground draw */
	SPRITEEDIT_ACT_BG,	/* zoom background draw */
	SPRITEEDIT_ACT_MOVE	/* zoom move */
};

static struct {
	/* generic */
	int busy;
	int act;
	int act_done;
	int is16;
	int cell;
	int colour;
	int c_open;
	int pal;
	int copy_open;
	u8 p[0x20];
	u8 p_open[0x20];
	
	int ukey_prev;
	int ckey_prev;
	int fkey_prev;
	HCURSOR cursor_cross;
	HCURSOR cursor_size;
	HANDLE tickh;
	HANDLE tickhw;
	HANDLE tickv;
	HANDLE tickvw;
	
	/* local state */
	u8 vdp_ram[0x4000];
	int vdp_regs[8];
	
	/* move */
	POINT p_move;
	POINT p_move_prev;
	
	/* zoom bmp */
	struct {
		HWND wnd;
		HDC wdc;
		HDC bdc;
		int* screen;
		HBITMAP bmp;
		BITMAPINFO info;
	} zoom;
	
	int in_zoom;
	int in_zoom_prev;
	POINT p_zoom;
	POINT p_zoom_prev;
	
} spriteedit;

typedef struct _spriteedit_undo {
	u8 p[0x20];
	struct _spriteedit_undo* next;
	struct _spriteedit_undo* prev;
} _spriteedit_undo;
static _spriteedit_undo* _spriteedit_undo_begin;
static _spriteedit_undo* _spriteedit_undo_cursor;

/* non-volatile */
static char spriteedit_dir[STRING_SIZE]={0};
static int spriteedit_fi; /* 1: .pattern, 2: any */


/* helper functions */
static void spriteedit_rotate(u8* p,int i)
{
	u8 p2[0x20];
	const int size=8<<spriteedit.is16;
	int j,k;
	
	if (i<=0||!p) return;
	
	/* rotate 90 degrees to the right */
	while (i--) {
		memset(p2,0,0x20);
		
		j=size;
		while (j--) {
			k=size;
			while (k--) p2[(j^(size-1))|(~k<<1&0x10&size)]|=((p[k|(~j<<1&0x10&size)]>>(j&7)&1)<<(k&7));
		}
		
		memcpy(p,p2,8<<(spriteedit.is16<<1));
	}
}

static void spriteedit_mirv(u8* p)
{
	u8 p2[0x20];
	int i=8<<spriteedit.is16;
	
	memcpy(p2,p,8<<(spriteedit.is16<<1));
	
	/* mirror vertical */
	while (i--) {
		p[i^((8<<spriteedit.is16)-1)]=p2[i];
		if (spriteedit.is16) p[(i^0xf)|0x10]=p2[i|0x10];
	}
}

static void spriteedit_action_done(void)
{
	spriteedit.act=SPRITEEDIT_ACT_NOTHING;
	spriteedit.act_done=TRUE;
}

static void spriteedit_clean_undo(_spriteedit_undo* u)
{
	int b=(u==_spriteedit_undo_begin);
	if (!u->next) return;
	
	u=u->next;
	for (;;) {
		if (!u->next) {
			MEM_CLEAN(u);
			break;
		}
		u=u->next;
		MEM_CLEAN(u->prev);
	}
	
	/* all */
	if (b) {
		_spriteedit_undo_cursor=_spriteedit_undo_begin;
		_spriteedit_undo_cursor->next=NULL;
		memcpy(spriteedit.p,_spriteedit_undo_cursor->p,0x20);
	}
}

static int spriteedit_apply(HWND dialog,const int ok)
{
	u8 p[0x20];
	int sgo=vdp_regs[6]<<11&0x3fff;
	int is16=vdp_regs[1]>>1&1;
	memcpy(p,spriteedit.p,0x20);
	
	if (is16!=spriteedit.is16) {
		int i=MessageBox(dialog,"Sprite size is different.\nApply changes anyway?","meisei",MB_ICONEXCLAMATION|(ok?(MB_YESNOCANCEL|MB_DEFBUTTON3):(MB_YESNO|MB_DEFBUTTON2)));
		if (i!=IDYES) return i;
	}
	
	if (!vdp_upload(sgo|spriteedit.cell<<3,p,8<<(spriteedit.is16<<1))) LOG_ERROR_WINDOW(dialog,"Couldn't upload VDP data!");
	
	return IDYES;
}


/* zoom subwindow */
static BOOL CALLBACK spriteedit_sub_zoom(HWND wnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	if (!spriteedit.zoom.wnd) return DefWindowProc(wnd,msg,wParam,lParam);
	
	switch (msg) {
		
		case WM_SETCURSOR:
			SetCursor(spriteedit.cursor_cross);
			return 1;
			break;
		
		case WM_MOUSEMOVE:
			spriteedit.in_zoom=input_mouse_in_client(wnd,NULL,&spriteedit.p_zoom,TRUE);
			if (spriteedit.act==SPRITEEDIT_ACT_MOVE&&!(wParam&MK_SHIFT)) {
				/* end move (let go of shift) */
				spriteedit_action_done();
				if (GetCapture()==wnd) ReleaseCapture();
			}
			break;
		
		case WM_LBUTTONDOWN:
			/* end whatever */
			spriteedit_action_done();
			if (!spriteedit.in_zoom) {
				if (GetCapture()==wnd) ReleaseCapture();
				break;
			}
			
			if (GetCapture()!=wnd) SetCapture(wnd);
			if (wParam&MK_SHIFT) {
				/* start move */
				GetCursorPos(&spriteedit.p_move);
				spriteedit.p_move_prev.x=spriteedit.p_move.x;
				spriteedit.p_move_prev.y=spriteedit.p_move.y;
				spriteedit.act=SPRITEEDIT_ACT_MOVE;
				SetCursor(spriteedit.cursor_size);
			}
			else {
				/* start fg draw */
				spriteedit.act=SPRITEEDIT_ACT_FG;
				SetCursor(spriteedit.cursor_cross);
			}
			break;
		
		case WM_RBUTTONDOWN:
			/* end whatever */
			spriteedit_action_done();
			if (!spriteedit.in_zoom) {
				if (GetCapture()==wnd) ReleaseCapture();
				break;
			}
			
			/* start bg draw */
			if (GetCapture()!=wnd) SetCapture(wnd);
			spriteedit.act=SPRITEEDIT_ACT_BG;
			SetCursor(spriteedit.cursor_cross);
			break;
		
		case WM_LBUTTONUP:
			if (spriteedit.act==SPRITEEDIT_ACT_BG) break;
			spriteedit_action_done();
			if (GetCapture()==wnd) ReleaseCapture();
		
		case WM_RBUTTONUP:
			if (spriteedit.act==SPRITEEDIT_ACT_FG||spriteedit.act==SPRITEEDIT_ACT_MOVE) break;
			spriteedit_action_done();
			if (GetCapture()==wnd) ReleaseCapture();
			break;
		
		case WM_CAPTURECHANGED:
			spriteedit_action_done();
			break;
		
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HGDIOBJ obj;
			HDC dc;
			
			if (spriteedit.busy) break;
			
			dc=BeginPaint(wnd,&ps);
			
			obj=SelectObject(spriteedit.zoom.bdc,spriteedit.zoom.bmp);
			StretchBlt(dc,0,0,256,256,spriteedit.zoom.bdc,0,0,8<<spriteedit.is16,8<<spriteedit.is16,SRCCOPY);
			SelectObject(spriteedit.zoom.bdc,obj);
			
			EndPaint(wnd,&ps);
			
			break;
		}
		
		default: break;
	}
	
	return DefWindowProc(wnd,msg,wParam,lParam);
}

/* main window */
static BOOL CALLBACK spriteview_editor_dialog(HWND dialog,UINT msg,WPARAM wParam,LPARAM lParam)
{
	switch (msg) {
		
		case WM_INITDIALOG: {
			int i;
			char t[0x100];
			
			spriteedit.busy=TRUE;
			
			sprintf(t,"Pattern $%02X",spriteedit.cell);
			SetDlgItemText(dialog,IDC_SPRITEEDIT_ZOOMT,t);
			spriteedit.cursor_cross=LoadCursor(NULL,IDC_CROSS);
			spriteedit.cursor_size=LoadCursor(NULL,IDC_SIZEALL);
			spriteedit.pal=-1;
			
			if (netplay_is_active()||movie_get_active_state()) {
				EnableWindow(GetDlgItem(dialog,IDOK),FALSE);
				EnableWindow(GetDlgItem(dialog,IDC_SPRITEEDIT_APPLY),FALSE);
			}
			
			/* fill pattern */
			memset(spriteedit.p,0,0x20);
			memcpy(spriteedit.p,spriteedit.vdp_ram+((spriteedit.vdp_regs[6]<<11&0x3fff)|(spriteedit.cell<<3)),8<<(spriteedit.is16<<1));
			memcpy(_spriteedit_undo_cursor->p,spriteedit.p,8<<(spriteedit.is16<<1));
			
			/* init zoom */
			spriteedit.in_zoom=FALSE; spriteedit.in_zoom_prev=2; spriteedit.p_zoom_prev.x=1000;
			spriteedit.p_zoom.x=spriteedit.p_zoom.y=0;
			spriteedit.p_move.x=spriteedit.p_move.y=0;
			ShowWindow(GetDlgItem(dialog,IDC_SPRITEEDIT_XY),SW_HIDE);
			
			spriteedit.zoom.wnd=GetDlgItem(dialog,IDC_SPRITEEDIT_ZOOM);
			toolwindow_resize(spriteedit.zoom.wnd,256,256);
			spriteedit.zoom.wdc=GetDC(spriteedit.zoom.wnd);
			spriteedit.zoom.bdc=CreateCompatibleDC(spriteedit.zoom.wdc);
			tool_init_bmi(&spriteedit.zoom.info,8<<spriteedit.is16,8<<spriteedit.is16,32);
			spriteedit.zoom.bmp=CreateDIBSection(spriteedit.zoom.wdc,&spriteedit.zoom.info,DIB_RGB_COLORS,(void*)&spriteedit.zoom.screen,NULL,0);
			SetWindowLongPtr(spriteedit.zoom.wnd,GWLP_WNDPROC,(LONG_PTR)spriteedit_sub_zoom);
			
			/* position ticks */
			if (!spriteedit.tickh) spriteedit.tickh=LoadImage(MAIN->module,MAKEINTRESOURCE(ID_BITMAP_TICKH),IMAGE_BITMAP,0,0,0);
			if (!spriteedit.tickv) spriteedit.tickv=LoadImage(MAIN->module,MAKEINTRESOURCE(ID_BITMAP_TICKV),IMAGE_BITMAP,0,0,0);
			if (spriteedit.is16) {
				/* 16*16 */
				if (!spriteedit.tickhw) spriteedit.tickhw=LoadImage(MAIN->module,MAKEINTRESOURCE(ID_BITMAP_TICKHW),IMAGE_BITMAP,0,0,0);
				if (!spriteedit.tickvw) spriteedit.tickvw=LoadImage(MAIN->module,MAKEINTRESOURCE(ID_BITMAP_TICKVW),IMAGE_BITMAP,0,0,0);
				
				/* up */
				for (i=0;i<0x10;i++) {
					if (!(i&7)) { SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_HU00),NULL,16+0x10*i,25,2,8,SWP_NOZORDER); SendMessage(GetDlgItem(dialog,i+IDC_TICKS_HU00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)spriteedit.tickhw); } /* wide */
					else { SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_HU00),NULL,16+0x10*i,29,2,4,SWP_NOZORDER); SendMessage(GetDlgItem(dialog,i+IDC_TICKS_HU00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)spriteedit.tickh); }
				}
				/* down */
				for (i=0;i<0x10;i++) {
					if (!(i&7)) { SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_HD00),NULL,16+0x10*i,291,2,8,SWP_NOZORDER); SendMessage(GetDlgItem(dialog,i+IDC_TICKS_HD00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)spriteedit.tickhw); } /* wide */
					else { SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_HD00),NULL,16+0x10*i,291,2,4,SWP_NOZORDER); SendMessage(GetDlgItem(dialog,i+IDC_TICKS_HD00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)spriteedit.tickh); }
				}
				/* left */
				for (i=0;i<0x10;i++) {
					if (!(i&7)) { SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_VL00),NULL,7,34+0x10*i,4,2,SWP_NOZORDER); SendMessage(GetDlgItem(dialog,i+IDC_TICKS_VL00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)spriteedit.tickvw); } /* wide */
					else { SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_VL00),NULL,11,34+0x10*i,4,2,SWP_NOZORDER); SendMessage(GetDlgItem(dialog,i+IDC_TICKS_VL00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)spriteedit.tickv); }
				}
				/* right */
				for (i=0;i<0x10;i++) {
					if (!(i&7)) { SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_VR00),NULL,273,34+0x10*i,4,2,SWP_NOZORDER); SendMessage(GetDlgItem(dialog,i+IDC_TICKS_VR00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)spriteedit.tickvw); } /* wide */
					else { SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_VR00),NULL,273,34+0x10*i,4,2,SWP_NOZORDER); SendMessage(GetDlgItem(dialog,i+IDC_TICKS_VR00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)spriteedit.tickv); }
				}
			}
			else {
				/* 8*8 */
				for (i=0;i<8;i++) { SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_HU00),NULL,16+0x20*i,29,2,4,SWP_NOZORDER); SendMessage(GetDlgItem(dialog,i+IDC_TICKS_HU00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)spriteedit.tickh); } /* up */
				for (i=0;i<8;i++) { SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_HD00),NULL,16+0x20*i,291,2,4,SWP_NOZORDER); SendMessage(GetDlgItem(dialog,i+IDC_TICKS_HD00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)spriteedit.tickh); } /* down */
				for (i=0;i<8;i++) { SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_VL00),NULL,11,34+0x20*i,4,2,SWP_NOZORDER); SendMessage(GetDlgItem(dialog,i+IDC_TICKS_VL00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)spriteedit.tickv); } /* left */
				for (i=0;i<8;i++) { SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_VR00),NULL,273,34+0x20*i,4,2,SWP_NOZORDER); SendMessage(GetDlgItem(dialog,i+IDC_TICKS_VR00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)spriteedit.tickv); } /* right */
			}
			
			main_parent_window(dialog,MAIN_PW_OUTERL,MAIN_PW_LEFT,-8,0,0);
			
			spriteedit.ukey_prev=0xc000;
			spriteedit.ckey_prev=0x8000;
			spriteedit.fkey_prev=0xf000;
			spriteedit.copy_open=spriteedit.act_done=FALSE;
			spriteedit.act=SPRITEEDIT_ACT_NOTHING;
			
			spriteview.ext_wnd=dialog;
			spriteedit.busy=FALSE;
			break;
		}
		
		case WM_DESTROY:
			if (spriteview.ext_wnd) {
				
				GdiFlush();
				spriteedit.busy=TRUE;
				spriteview.ext_wnd=NULL;
				
				spriteedit_clean_undo(_spriteedit_undo_begin);
				if (spriteedit.tickh) { DeleteObject(spriteedit.tickh); spriteedit.tickh=NULL; }
				if (spriteedit.tickhw) { DeleteObject(spriteedit.tickhw); spriteedit.tickhw=NULL; }
				if (spriteedit.tickv) { DeleteObject(spriteedit.tickv); spriteedit.tickv=NULL; }
				if (spriteedit.tickvw) { DeleteObject(spriteedit.tickvw); spriteedit.tickvw=NULL; }
				
				/* clean up zoom */
				spriteedit.zoom.screen=NULL;
				if (spriteedit.zoom.bmp) { DeleteObject(spriteedit.zoom.bmp); spriteedit.zoom.bmp=NULL; }
				if (spriteedit.zoom.bdc) { DeleteDC(spriteedit.zoom.bdc); spriteedit.zoom.bdc=NULL; }
				if (spriteedit.zoom.wdc) { ReleaseDC(spriteedit.zoom.wnd,spriteedit.zoom.wdc); spriteedit.zoom.wdc=NULL; }
				spriteedit.zoom.wnd=NULL;
				
				main_menu_enable(IDM_SPRITEVIEW,TRUE);
			}
			break;
		
		case WM_COMMAND:
			
			switch (LOWORD(wParam)) {
				
				/* load pattern */
				case IDC_SPRITEEDIT_OPEN: {
					const char* filter="Pattern Files (*.pattern)\0*.pattern\0All Files (*.*)\0*.*\0\0";
					const char* title="Open Pattern";
					char fn[STRING_SIZE]={0};
					OPENFILENAME of;
					int shift;
					
					if (!spriteview.ext_wnd) break;
					
					spriteedit.in_zoom=FALSE;
					
					if (spriteedit.act) {
						ReleaseCapture();
						spriteedit_action_done();
					}
					
					memset(&of,0,sizeof(OPENFILENAME));
					
					of.lStructSize=sizeof(OPENFILENAME);
					of.hwndOwner=dialog;
					of.hInstance=MAIN->module;
					of.lpstrFile=fn;
					of.lpstrFilter=filter;
					of.lpstrTitle=title;
					of.nMaxFile=STRING_SIZE;
					of.nFilterIndex=spriteedit_fi;
					of.Flags=OFN_ENABLESIZING|OFN_HIDEREADONLY|OFN_FILEMUSTEXIST;
					of.lpstrInitialDir=strlen(spriteedit_dir)?spriteedit_dir:file->tooldir?file->tooldir:file->appdir;
					
					shift=GetAsyncKeyState(VK_SHIFT);
					main_menu_enable(IDM_SPRITEVIEW,FALSE); /* resource leak if forced to close */
					if (GetOpenFileName(&of)) {
						int i,j,k,size;
						u8 data[0x20];
						FILE* fd=NULL;
						int success=FALSE;
						
						if (!spriteview.ext_wnd) {
							/* cleaned up already (check again since time passed) */
							main_menu_enable(IDM_SPRITEVIEW,TRUE);
							break;
						}
						
						spriteedit_fi=of.nFilterIndex;
						tool_set_pattern_fi_shared(spriteedit_fi);
						
						if (strlen(fn)&&(size=file_open_custom(&fd,fn))>0) {
							if ((size==8||size==0x10||size==0x20)&&file_read_custom(fd,data,size)) {
								success=TRUE;
								k=0; j=0;
								if (size==0x10) {
									/* find most common foreground colour */
									u8 lut[0x10];
									memset(lut,0,0x10);
									for (i=8;i<0x10;i++) lut[data[i]>>4&0xf]++;
									for (i=1;i<0x10;i++) {
										if (lut[i]>j) { j=lut[i]; k=i; }
									}
								}
								spriteedit.c_open=k?k:0xf;
								
								if (spriteedit.is16) {
									if (size==0x20) memcpy(spriteedit.p_open,data,0x20);
									else {
										if ((shift|GetAsyncKeyState(VK_SHIFT))&0x8000) {
											/* held shift while opening: enlarge */
											for (i=0;i<8;i++) {
												k=0; j=8;
												while (j--) k|=((data[i]&(1<<j))<<(j+1));
												k|=(k>>1);
												spriteedit.p_open[i<<1]=spriteedit.p_open[i<<1|1]=k>>8;
												spriteedit.p_open[i<<1|0x10]=spriteedit.p_open[i<<1|0x11]=k&0xff;
											}
										}
										else {
											/* upper-left area only */
											memcpy(spriteedit.p_open,spriteedit.p,0x20);
											memcpy(spriteedit.p_open,data,8);
										}
									}
								}
								else memcpy(spriteedit.p_open,data,8);
							}
						}
						
						file_close_custom(fd);
						
						if (success) {
							spriteedit.copy_open=TRUE;
							if (strlen(fn+of.nFileOffset)&&(of.nFileExtension-1)>of.nFileOffset) {
								char wintitle[STRING_SIZE]={0};
								fn[of.nFileExtension-1]=0;
								sprintf(wintitle,"Sprite Editor - %s",fn+of.nFileOffset);
								SetWindowText(dialog,wintitle);
							}
						}
						else LOG_ERROR_WINDOW(dialog,"Couldn't load pattern!");
						
						if (strlen(fn)&&of.nFileOffset) {
							fn[of.nFileOffset]=0; strcpy(spriteedit_dir,fn);
						}
					}
					
					PostMessage(dialog,WM_NEXTDLGCTL,(WPARAM)GetDlgItem(dialog,IDC_SPRITEEDIT_APPLY),TRUE);
					main_menu_enable(IDM_SPRITEVIEW,TRUE);
					
					break;
				}
				
				/* save pattern */
				case IDC_SPRITEEDIT_SAVE: {
					const char* filter="Pattern File (*.pattern)\0*.pattern\0All Files (*.*)\0*.*\0\0";
					const char* defext="\0\0\0\0";
					const char* title="Save Pattern As";
					char fn[STRING_SIZE]={0};
					OPENFILENAME of;
					
					if (!spriteview.ext_wnd) break;
					
					spriteedit.in_zoom=FALSE;
					
					if (spriteedit.act) {
						ReleaseCapture();
						spriteedit_action_done();
					}
					
					memset(&of,0,sizeof(OPENFILENAME));
					
					of.lStructSize=sizeof(OPENFILENAME);
					of.hwndOwner=dialog;
					of.hInstance=MAIN->module;
					of.lpstrFile=fn;
					of.lpstrDefExt=defext;
					of.lpstrFilter=filter;
					of.lpstrTitle=title;
					of.nMaxFile=STRING_SIZE;
					of.nFilterIndex=spriteedit_fi;
					of.Flags=OFN_ENABLESIZING|OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
					of.lpstrInitialDir=strlen(spriteedit_dir)?spriteedit_dir:file->tooldir?file->tooldir:file->appdir;
					
					main_menu_enable(IDM_SPRITEVIEW,FALSE); /* resource leak if forced to close */
					if (GetSaveFileName(&of)) {
						u8 data[0x20];
						FILE* fd=NULL;
						int i,size=8;
						
						if (!spriteview.ext_wnd) {
							/* cleaned up already (check again since time passed) */
							main_menu_enable(IDM_SPRITEVIEW,TRUE);
							break;
						}
						
						spriteedit_fi=of.nFilterIndex;
						tool_set_pattern_fi_shared(spriteedit_fi);
						
						if (spriteedit.is16) {
							size=0x20;
							memcpy(data,spriteedit.p,size);
						}
						else {
							size=8;
							memcpy(data,spriteedit.p,size);
							
							/* save colour data if not white */
							if (spriteedit.colour!=0xf) {
								size=0x10;
								for (i=8;i<0x10;i++) data[i]=spriteedit.colour<<4&0xf0;
							}
						}
						
						if (!strlen(fn)||!file_save_custom(&fd,fn)||!file_write_custom(fd,data,size)) LOG_ERROR_WINDOW(dialog,"Couldn't save pattern!");
						else if (strlen(fn+of.nFileOffset)&&(of.nFileExtension-1)>of.nFileOffset) {
							char wintitle[STRING_SIZE]={0};
							fn[of.nFileExtension-1]=0;
							sprintf(wintitle,"Sprite Editor - %s",fn+of.nFileOffset);
							SetWindowText(dialog,wintitle);
						}
						
						file_close_custom(fd);
						if (strlen(fn)&&of.nFileOffset) {
							fn[of.nFileOffset]=0; strcpy(spriteedit_dir,fn);
						}
					}
					
					PostMessage(dialog,WM_NEXTDLGCTL,(WPARAM)GetDlgItem(dialog,IDC_SPRITEEDIT_APPLY),TRUE);
					main_menu_enable(IDM_SPRITEVIEW,TRUE);
					
					break;
				}
				
				/* apply */
				case IDC_SPRITEEDIT_APPLY:
					if (netplay_is_active()||movie_get_active_state()||!spriteview.ext_wnd) break;
					spriteedit_apply(dialog,FALSE);
					break;
				
				/* close dialog */
				case IDOK:
					if (netplay_is_active()||movie_get_active_state()||!spriteview.ext_wnd||spriteedit_apply(dialog,TRUE)==IDCANCEL) break;
				case IDCANCEL:
					if (!spriteview.ext_wnd) break;
					EndDialog(dialog,0);
					break;
				
				default: break;
			} /* WM_COMMAND */
			
			break;
		
		case TOOL_REPAINT: {
			u8 p[0x20];
			int r,i,j,pal;
			
			if (!spriteview.ext_wnd||spriteedit.busy) break;
			spriteedit.busy=TRUE;
			
			/* coordinates */
			if (spriteedit.in_zoom) {
				spriteedit.in_zoom&=input_mouse_in_client(spriteedit.zoom.wnd,dialog,&spriteedit.p_zoom,GetForegroundWindow()==dialog);
				spriteedit.p_zoom.x=spriteedit.p_zoom.x>>(5-spriteedit.is16)&((8<<spriteedit.is16)-1);
				spriteedit.p_zoom.y=spriteedit.p_zoom.y>>(5-spriteedit.is16)&((8<<spriteedit.is16)-1);
			}
			if (spriteedit.in_zoom!=spriteedit.in_zoom_prev) ShowWindow(GetDlgItem(dialog,IDC_SPRITEEDIT_XY),spriteedit.in_zoom?SW_NORMAL:SW_HIDE);
			if (spriteedit.p_zoom.x!=spriteedit.p_zoom_prev.x||spriteedit.p_zoom.y!=spriteedit.p_zoom_prev.y) {
				char t[0x100];
				sprintf(t,"(%d,%d)",(int)spriteedit.p_zoom.x,(int)spriteedit.p_zoom.y);
				SetDlgItemText(dialog,IDC_SPRITEEDIT_XY,t);
			}
			
			memset(p,0,0x20);
			memcpy(p,spriteedit.p,8<<(spriteedit.is16<<1));
			
			/* hotkey action */
			i=(GetAsyncKeyState(0x52)&0x8000)|(GetAsyncKeyState(0x45)>>1&0x4000)|(GetAsyncKeyState(0x56)>>2&0x2000)|(GetAsyncKeyState(0x48)>>3&0x1000)|(GetAsyncKeyState(0x49)>>4&0x800); /* r,e,v,h,i */
			if (i&&!spriteedit.act_done&&spriteedit.act==SPRITEEDIT_ACT_NOTHING&&spriteedit.fkey_prev==0&&dialog==GetForegroundWindow()) {
				
				switch (i) {
					
					/* rotate */
					case 0x8000:
						if (GetAsyncKeyState(VK_SHIFT)&0x8000) spriteedit_rotate(p,3);
						else spriteedit_rotate(p,1);
						break;
					
					/* erase */
					case 0x4000:
						if (GetAsyncKeyState(VK_SHIFT)&0x8000) memset(p,0,0x20);
						break;
					
					/* vertical flip */
					case 0x2000:
						spriteedit_mirv(p);
						break;
					
					/* horizontal flip */
					case 0x1000:
						spriteedit_rotate(p,1);
						spriteedit_mirv(p);
						spriteedit_rotate(p,3);
						break;
					
					/* invert */
					case 0x800:
						for (j=0;j<(8<<(spriteedit.is16<<1));j++) p[j]^=0xff;
						break;
					
					/* multiple keys, ignore */
					default: break;
				}
				
				spriteedit.act_done=TRUE;
			}
			spriteedit.fkey_prev=i;
			
			/* next */
			if (spriteedit.act_done&&memcmp(p,_spriteedit_undo_cursor->p,8<<(spriteedit.is16<<1))) {
				spriteedit_clean_undo(_spriteedit_undo_cursor);
				MEM_CREATE_T(_spriteedit_undo_cursor->next,sizeof(_spriteedit_undo),_spriteedit_undo*);
				_spriteedit_undo_cursor->next->prev=_spriteedit_undo_cursor;
				_spriteedit_undo_cursor=_spriteedit_undo_cursor->next;
				memcpy(_spriteedit_undo_cursor->p,p,8<<(spriteedit.is16<<1));
			}
			spriteedit.act_done=FALSE;
			
			/* handle action */
			switch (spriteedit.act) {
				case SPRITEEDIT_ACT_FG:
					if (!spriteedit.in_zoom) break;
					p[spriteedit.p_zoom.y|(spriteedit.p_zoom.x<<1&0x10)]|=(1<<(7-(spriteedit.p_zoom.x&7)));
					break;
				
				case SPRITEEDIT_ACT_BG:
					if (!spriteedit.in_zoom) break;
					p[spriteedit.p_zoom.y|(spriteedit.p_zoom.x<<1&0x10)]&=(1<<(7-(spriteedit.p_zoom.x&7))^0xff);
					break;
				
				case SPRITEEDIT_ACT_MOVE: {
					int d[2];
					j=spriteedit.is16;
					
					/* compute difference */
					GetCursorPos(&spriteedit.p_move);
					d[0]=spriteedit.p_move.x-spriteedit.p_move_prev.x;
					d[1]=spriteedit.p_move.y-spriteedit.p_move_prev.y;
					for (i=0;i<2;i++) {
						if (d[i]<0) { d[i]=-d[i]; if (d[i]>0x7f) d[i]=0x7f; d[i]=((8<<j)-(d[i]>>(5-j)))&((8<<j)-1); }
						else { if (d[i]>0x7f) d[i]=0x7f; d[i]>>=(5-j); }
					}
					if (d[0]) spriteedit.p_move_prev.x=spriteedit.p_move.x;
					if (d[1]) spriteedit.p_move_prev.y=spriteedit.p_move.y;
					
					if (spriteedit.is16) {
						u8 p2[0x20];
						
						/* rotate x */
						while (d[0]--) {
							i=0x10;
							while (i--) {
								j=p[i]<<7&0x80;
								p[i]=p[i]>>1|(p[i|0x10]<<7&0x80);
								p[i|0x10]=p[i|0x10]>>1|j;
							}
						}
						
						/* rotate y */
						i=0x10;
						while (i--) {
							p2[(i+d[1])&0xf]=p[i];
							p2[((i+d[1])&0xf)|0x10]=p[i|0x10];
						}
						
						memcpy(p,p2,0x20);
					}
					else {
						u8 p2[8];
						
						/* rotate x */
						while (d[0]--) {
							i=8;
							while (i--) p[i]=p[i]>>1|(p[i]<<7&0x80);
						}
						
						/* rotate y */
						i=8;
						while (i--) p2[(i+d[1])&7]=p[i];
						
						memcpy(p,p2,8);
					}
				}
				
				default: break;
			}
			
			/* undo/redo */
			i=(GetAsyncKeyState(0x5a)&0x8000)|(GetAsyncKeyState(0x59)>>1&0x4000); /* z,x */
			if (spriteedit.act==SPRITEEDIT_ACT_NOTHING&&(i==0x8000||i==0x4000)&&spriteedit.ukey_prev==0&&dialog==GetForegroundWindow()&&GetAsyncKeyState(VK_CONTROL)&0x8000) {
				if (i==0x8000) _spriteedit_undo_cursor=_spriteedit_undo_cursor->prev; /* undo */
				else if (_spriteedit_undo_cursor->next) _spriteedit_undo_cursor=_spriteedit_undo_cursor->next; /* redo */
				memcpy(p,_spriteedit_undo_cursor->p,8<<(spriteedit.is16<<1));
			}
			spriteedit.ukey_prev=i;
			
			/* change colour */
			i=GetAsyncKeyState(0x43)&0x8000; /* c */
			if (i==0x8000&&spriteedit.ckey_prev==0&&dialog==GetForegroundWindow()) {
				if (GetAsyncKeyState(VK_SHIFT)&0x8000) spriteedit.colour--;
				else spriteedit.colour++;
				
				spriteedit.colour&=0xf;
				spriteedit.colour+=(spriteedit.colour==0);
			}
			spriteedit.ckey_prev=i;
			
			/* loaded */
			if (spriteedit.copy_open) {
				if (spriteedit.c_open) spriteedit.colour=spriteedit.c_open;
				memcpy(p,spriteedit.p_open,8<<(spriteedit.is16<<1));
				spriteedit_clean_undo(_spriteedit_undo_begin);
				memcpy(_spriteedit_undo_cursor->p,p,8<<(spriteedit.is16<<1));
				
				spriteedit.copy_open=FALSE;
			}
			
			/* redraw */
			r=FALSE;
			pal=draw_palette[spriteedit.colour&0xf];
			if (pal!=spriteedit.pal) { r=TRUE; spriteedit.pal=pal; }
			if (memcmp(p,spriteedit.p,8<<(spriteedit.is16<<1))) { r=TRUE; memcpy(spriteedit.p,p,8<<(spriteedit.is16<<1)); }
			
			if (r) {
				int* screen=spriteedit.zoom.screen;
				
				if (spriteedit.is16) {
					for (i=0;i<0x10;i++) {
						j=p[i|0x10]|p[i]<<8;
						#define U() *screen++=((j<<=1)&0x10000)?pal:TOOL_DBM_EMPTY;
						U(); U(); U(); U(); U(); U(); U(); U();
						U(); U(); U(); U(); U(); U(); U(); U();
						#undef U
					}
				}
				else {
					for (i=0;i<8;i++) {
						j=p[i];
						#define U() *screen++=((j<<=1)&0x100)?pal:TOOL_DBM_EMPTY;
						U(); U(); U(); U(); U(); U(); U(); U();
						#undef U
					}
				}
			}
			
			spriteedit.in_zoom_prev=spriteedit.in_zoom;
			spriteedit.p_zoom_prev.x=spriteedit.p_zoom.x;
			spriteedit.p_zoom_prev.y=spriteedit.p_zoom.y;
			
			spriteedit.busy=FALSE;
			
			if (r) InvalidateRect(spriteedit.zoom.wnd,NULL,FALSE);
			
			break;
		} /* TOOL_REPAINT */
		
		case TOOL_MENUCHANGED: {
			int i=((netplay_is_active()|movie_get_active_state())==0);
			EnableWindow(GetDlgItem(dialog,IDOK),i);
			EnableWindow(GetDlgItem(dialog,IDC_SPRITEEDIT_APPLY),i);
			break;
		}
		
		default: break;
	}
	
	return 0;
}
