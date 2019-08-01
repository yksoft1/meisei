/* tile editor (tileview.c), similar to Wolf's Polka */

enum {
	TILEEDIT_ACT_NOTHING=0,
	TILEEDIT_ACT_FG,	/* zoom foreground draw */
	TILEEDIT_ACT_BG,	/* zoom background draw */
	TILEEDIT_ACT_MOVE,	/* zoom move */
	TILEEDIT_ACT_FC,	/* foreground column draw */
	TILEEDIT_ACT_BC		/* background column draw */
};

static struct {
	/* generic */
	int busy;
	int act;
	int act_done;
	int rclick;
	int pal[0x10];
	int curpal;
	HBRUSH brush[0x10];
	int screen3;
	u8 p[8];
	u8 c[8];
	u8 p_open[8];
	u8 c_open[8];
	int copy_open;
	int colour;
	int xy;
	int allblocks_vis;
	
	int ukey_prev;
	int fkey_prev;
	HCURSOR cursor_cross;
	HCURSOR cursor_size;
	HANDLE tickh;
	HANDLE tickv;
	
	/* local state */
	u8 vdp_ram[0x4000];
	int vdp_regs[8];
	int pattern_orig;
	int block_orig;
	int row_orig;
	int mode;
	
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
	
	/* fg bmp */
	struct {
		HWND wnd;
		HDC wdc;
		HDC bdc;
		int* screen;
		HBITMAP bmp;
		BITMAPINFO info;
	} fg;
	
	int in_fg;
	int in_fg_prev;
	POINT p_fg;
	POINT p_fg_prev;
	
	/* bg bmp */
	struct {
		HWND wnd;
		HDC wdc;
		HDC bdc;
		int* screen;
		HBITMAP bmp;
		BITMAPINFO info;
	} bg;
	
	int in_bg;
	int in_bg_prev;
	POINT p_bg;
	POINT p_bg_prev;
	
} tileedit;

typedef struct _tileedit_undo {
	u8 p[8];
	u8 c[8];
	struct _tileedit_undo* next;
	struct _tileedit_undo* prev;
} _tileedit_undo;
static _tileedit_undo* _tileedit_undo_begin;
static _tileedit_undo* _tileedit_undo_cursor;

/* non-volatile */
static char tileedit_dir[STRING_SIZE]={0};
static int tileedit_fi; /* 1: .pattern, 2: any */
static int tileedit_allblocks;


/* helper functions */
static void tileedit_download(void)
{
	/* send tile data+mode from edit ram/regs to edit p/c */
	const int mode=(tileedit.vdp_regs[1]>>4&1)|(tileedit.vdp_regs[1]>>1&4)|(tileedit.vdp_regs[0]&2);
	const int ct=tileedit.vdp_regs[3]<<6;
	int pg=tileedit.vdp_regs[4]<<11&0x3fff;
	int i;
	
	/* fill pattern */
	if (mode&2) pg=(tileedit.block_orig<<11&pg)|(pg&0x2000);
	memcpy(tileedit.p,tileedit.vdp_ram+(pg|tileedit.pattern_orig<<3),8);
	if (mode&4) {
		/* screen 3 */
		memset(tileedit.p,0xf0,8);
		for (i=0;i<4;i++) tileedit.c[i]=tileedit.vdp_ram[pg|tileedit.pattern_orig<<3|tileedit.row_orig<<1];
		for (i=4;i<8;i++) tileedit.c[i]=tileedit.vdp_ram[pg|tileedit.pattern_orig<<3|tileedit.row_orig<<1|1];
	}
	else if (mode==2) memcpy(tileedit.c,tileedit.vdp_ram+(((ct&0x2000)|(tileedit.block_orig<<11&0x1800)|tileedit.pattern_orig<<3)&(ct|0x3f)),8); /* screen 2 */
	else if (mode&1) for (i=0;i<8;i++) tileedit.c[i]=tileedit.vdp_regs[7]; /* screen 0 */
	else for (i=0;i<8;i++) tileedit.c[i]=tileedit.vdp_ram[ct|tileedit.pattern_orig>>3]; /* screen 1 */
	
	tileedit.mode=mode;
}

static int tileedit_upload(int* reg_local,u8* p,u8* c,int allblocks,int s01c)
{
	int i,success=TRUE;
	int modeo=(reg_local[1]>>4&1)|(reg_local[1]>>1&4)|(reg_local[0]&2);
	int pgo=reg_local[4]<<11&0x3fff;
	int cto=reg_local[3]<<6;
	if (modeo&2) pgo=(tileedit.block_orig<<11&pgo)|(pgo&0x2000);
	
	if (modeo&4) {
		/* screen 3 */
		success&=vdp_upload(pgo|tileedit.pattern_orig<<3|tileedit.row_orig<<1,&c[3],2);
	}
	else {
		/* upload pattern data */
		pgo|=(tileedit.pattern_orig<<3);
		if (allblocks&&modeo&2) {
			pgo&=~0x1800;
			for (i=0;i<0x1800;i+=0x800) success&=vdp_upload(pgo|i,p,8);
		}
		else success&=vdp_upload(pgo,p,8);
		
		/* upload colour data */
		if (modeo==2) {
			/* screen 2 */
			cto=((cto&0x2000)|(tileedit.block_orig<<11&0x1800)|tileedit.pattern_orig<<3)&(cto|0x3f);
			
			if (allblocks) {
				cto&=~0x1800;
				for (i=0;i<0x1800;i+=0x800) success&=vdp_upload(cto|i,c,8);
			}
			else success&=vdp_upload(cto,c,8);
		}
		else if ((modeo&5)==0&&s01c) {
			/* screen 1 */
			success&=vdp_upload(cto|tileedit.pattern_orig>>3,c,1);
		}
		
		/* upload register data (screen 0) */
		else if (s01c) {
			success&=vdp_upload(7|VDP_UPLOAD_REG,c,1);
			reg_local[7]=c[0];
		}
	}
	
	return success;
}

static int tileedit_changepal(HWND dialog,u32 i)
{
	HWND w;
	i&=0xf;
	
	if (i==tileedit.curpal) return FALSE;
	
	/* down */
	w=GetDlgItem(dialog,i+IDC_PALEDIT_00);
	SetWindowLongPtr(w,GWL_EXSTYLE,WS_EX_CLIENTEDGE);
	SetWindowPos(w,NULL,0,0,0,0,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOOWNERZORDER|SWP_NOSIZE);
	
	/* prev up */
	w=GetDlgItem(dialog,tileedit.curpal+IDC_PALEDIT_00);
	SetWindowLongPtr(w,GWL_EXSTYLE,WS_EX_DLGMODALFRAME);
	SetWindowPos(w,NULL,0,0,0,0,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOOWNERZORDER|SWP_NOSIZE);
	
	tileedit.curpal=i;
	
	return TRUE;
}

static void tileedit_action_done(void)
{
	tileedit.act=TILEEDIT_ACT_NOTHING;
	tileedit.act_done=TRUE;
}

static void tileedit_clean_undo(_tileedit_undo* u)
{
	int b=(u==_tileedit_undo_begin);
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
		_tileedit_undo_cursor=_tileedit_undo_begin;
		_tileedit_undo_cursor->next=NULL;
		memcpy(tileedit.p,_tileedit_undo_cursor->p,8);
		memcpy(tileedit.c,_tileedit_undo_cursor->c,8);
	}
}

static int tileedit_findcommonct(u8* ct)
{
	u8 t[0x100];
	int i,j=0,k=0;
	
	memset(t,0,0x100);
	for (i=0;i<8;i++) t[ct[i]]++;
	
	/* find most common colour */
	for (i=0;i<0x100;i++) {
		if ((t[i]==k&&(j&0xf)==(j>>4&0xf))||t[i]>k) { j=i; k=t[i]; }
	}
	
	return j;
}

static int tileedit_screen0cdiff(int mode,u8* ct,int c)
{
	int i;
	
	if (mode==2||(mode&5)!=1) return FALSE;
	i=tileedit_findcommonct(ct);
	
	memset(ct,i,8);
	
	return (c!=i);
}

static int tileedit_screen1cdiff(int mode,u8* ct,int cto,u8* ram)
{
	int i;
	
	if (mode==2||(mode&5)!=0) return FALSE;
	i=tileedit_findcommonct(ct);
	
	memset(ct,i,8);
	
	return (ram[cto|tileedit.pattern_orig>>3]!=i);
}

static int tileedit_apply(HWND dialog,const int ok)
{
	int i,s0=FALSE;
	u8 ram_local[0x4000];
	int reg_local[8];
	int modeo,cto;
	u8 p[8]; u8 c[8]; u8 c2[8];
	
	/* copy local state */
	memcpy(ram_local,vdp_ram,0x4000);
	for (i=0;i<8;i++) reg_local[i]=vdp_regs[i];
	memcpy(p,tileedit.p,8);
	memcpy(c,tileedit.c,8); memcpy(c2,c,8);
	
	/* bogus mode */
	if ((reg_local[1]&0x18)==0x18) {
		return MessageBox(dialog,"Invalid mixed mode!\nChanges won't be applied.","I am Error.",MB_ICONEXCLAMATION|(ok?(MB_OKCANCEL|MB_DEFBUTTON2):MB_OK));
	}
	
	/* screen 3 <-> not screen 3 */
	if ((reg_local[1]>>3^tileedit.screen3)&1) {
		return MessageBox(dialog,"Incompatible screen mode!\nChanges won't be applied.","I am Error.",MB_ICONEXCLAMATION|(ok?(MB_OKCANCEL|MB_DEFBUTTON2):MB_OK));
	}
	
	modeo=(reg_local[1]>>4&1)|(reg_local[1]>>1&4)|(reg_local[0]&2);
	cto=reg_local[3]<<6;
	
	/* mode changed */
	if (modeo!=tileedit.mode) {
		if ((s0=tileedit_screen0cdiff(modeo,c,reg_local[7]))) i=MessageBox(dialog,"Screen mode is different, with colour changes affecting\nthe entire surface. Apply anyway?","meisei",MB_ICONEXCLAMATION|(ok?(MB_YESNOCANCEL|MB_DEFBUTTON3):(MB_YESNO|MB_DEFBUTTON2)));
		else if (tileedit_screen1cdiff(modeo,c,cto,ram_local)) i=MessageBox(dialog,"Screen mode is different, with colour changes affecting\nsurrounding tiles. Apply anyway?","meisei",MB_ICONEXCLAMATION|(ok?(MB_YESNOCANCEL|MB_DEFBUTTON3):(MB_YESNO|MB_DEFBUTTON2)));
		else i=MessageBox(dialog,"Screen mode is different.\nApply changes anyway?","meisei",MB_ICONEXCLAMATION|(ok?(MB_YESNOCANCEL|MB_DEFBUTTON3):(MB_YESNO|MB_DEFBUTTON2)));
		if (i!=IDYES) return i;
	}
	
	/* global colour */
	else if ((s0=tileedit_screen0cdiff(modeo,c,reg_local[7]))) {
		i=MessageBox(dialog,"Colour changes will affect the entire screen.\nApply anyway?","meisei",MB_ICONEXCLAMATION|(ok?(MB_YESNOCANCEL|MB_DEFBUTTON3):(MB_YESNO|MB_DEFBUTTON2)));
		if (i!=IDYES) return i;
	}
	else if (tileedit_screen1cdiff(modeo,c,cto,ram_local)) {
		i=MessageBox(dialog,"Colour changes will affect surrounding tiles too.\nApply anyway?","meisei",MB_ICONEXCLAMATION|(ok?(MB_YESNOCANCEL|MB_DEFBUTTON3):(MB_YESNO|MB_DEFBUTTON2)));
		if (i!=IDYES) return i;
	}
	
	if (!tileedit_upload(reg_local,p,c,tileedit_allblocks,((modeo&5)==0)?TRUE:s0)) LOG_ERROR_WINDOW(dialog,"Couldn't upload VDP data!");
	
	for (i=0;i<8;i++) tileedit.vdp_regs[i]=reg_local[i];
	if (tileedit.mode!=modeo) {
		char t[0x100];
		char tr[0x10]={0};
		char tb[0x10]={0};
		
		if (modeo&2) sprintf(tb,"/%d",tileedit.block_orig);
		if (modeo&4) sprintf(tr,", row %d",tileedit.row_orig);
		sprintf(t,"Pattern $%02X%s%s",tileedit.pattern_orig,tb,tr);
		SetDlgItemText(dialog,IDC_TILEEDIT_ZOOMT,t);
		
		tileedit.mode=modeo;
	}
	
	/* screen degraded, undo invalidated */
	if (!ok&&memcmp(c,c2,8)) {
		memcpy(tileedit.p_open,p,8);
		memcpy(tileedit.c_open,c,8);
		tileedit.copy_open=TRUE;
	}
	
	return IDYES;
}


/* zoom subwindow */
static BOOL CALLBACK tileedit_sub_zoom(HWND wnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	if (!tileedit.zoom.wnd) return DefWindowProc(wnd,msg,wParam,lParam);
	
	switch (msg) {
		
		case WM_SETCURSOR:
			SetCursor(tileedit.cursor_cross);
			return 1;
			break;
		
		case WM_MOUSEMOVE:
			tileedit.in_zoom=input_mouse_in_client(wnd,NULL,&tileedit.p_zoom,TRUE);
			if (tileedit.act==TILEEDIT_ACT_MOVE&&!(wParam&MK_SHIFT)) {
				/* end move (let go of shift) */
				tileedit_action_done();
				if (GetCapture()==wnd) ReleaseCapture();
			}
			break;
		
		case WM_LBUTTONDOWN:
			/* end whatever */
			tileedit_action_done();
			if (!tileedit.in_zoom) {
				if (GetCapture()==wnd) ReleaseCapture();
				break;
			}
			
			if (GetCapture()!=wnd) SetCapture(wnd);
			if (wParam&MK_SHIFT) {
				/* start move */
				GetCursorPos(&tileedit.p_move);
				tileedit.p_move_prev.x=tileedit.p_move.x;
				tileedit.p_move_prev.y=tileedit.p_move.y;
				tileedit.act=TILEEDIT_ACT_MOVE;
				SetCursor(tileedit.cursor_size);
			}
			else {
				/* start fg draw */
				tileedit.act=TILEEDIT_ACT_FG;
				SetCursor(tileedit.cursor_cross);
			}
			break;
		
		case WM_RBUTTONDOWN:
			/* end whatever */
			tileedit_action_done();
			if (!tileedit.in_zoom) {
				if (GetCapture()==wnd) ReleaseCapture();
				break;
			}
			
			if (tileedit.screen3) {
				/* pick colour */
				tileedit.rclick=TRUE;
			}
			else {
				/* start bg draw */
				if (GetCapture()!=wnd) SetCapture(wnd);
				tileedit.act=TILEEDIT_ACT_BG;
				SetCursor(tileedit.cursor_cross);
			}
			break;
		
		case WM_LBUTTONUP:
			if (tileedit.act==TILEEDIT_ACT_BG) break;
			tileedit_action_done();
			if (GetCapture()==wnd) ReleaseCapture();
		
		case WM_RBUTTONUP:
			if (tileedit.act==TILEEDIT_ACT_FG||tileedit.act==TILEEDIT_ACT_MOVE) break;
			tileedit_action_done();
			if (GetCapture()==wnd) ReleaseCapture();
			break;
		
		case WM_CAPTURECHANGED:
			tileedit_action_done();
			break;
		
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HGDIOBJ obj;
			HDC dc;
			
			if (tileedit.busy) break;
			
			dc=BeginPaint(wnd,&ps);
			
			obj=SelectObject(tileedit.zoom.bdc,tileedit.zoom.bmp);
			StretchBlt(dc,0,0,128,128,tileedit.zoom.bdc,0,0,8,8,SRCCOPY);
			SelectObject(tileedit.zoom.bdc,obj);
			
			EndPaint(wnd,&ps);
			
			break;
		}
		
		default: break;
	}
	
	return DefWindowProc(wnd,msg,wParam,lParam);
}

/* fg subwindow */
static BOOL CALLBACK tileedit_sub_fg(HWND wnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	if (!tileedit.fg.wnd||tileedit.screen3) return DefWindowProc(wnd,msg,wParam,lParam);
	
	switch (msg) {
		
		case WM_SETCURSOR:
			SetCursor(tileedit.cursor_cross);
			return 1;
			break;
		
		case WM_MOUSEMOVE:
			tileedit.in_fg=input_mouse_in_client(wnd,NULL,&tileedit.p_fg,TRUE);
			break;
		
		case WM_LBUTTONDOWN:
			/* end whatever */
			tileedit_action_done();
			if (!tileedit.in_fg) {
				if (GetCapture()==wnd) ReleaseCapture();
				break;
			}
			
			/* start draw */
			if (GetCapture()!=wnd) SetCapture(wnd);
			tileedit.act=TILEEDIT_ACT_FC;
			SetCursor(tileedit.cursor_cross);
			break;
		
		case WM_RBUTTONDOWN:
			tileedit.rclick=TRUE;
		case WM_LBUTTONUP: case WM_RBUTTONUP:
			if (GetCapture()==wnd) ReleaseCapture();
		case WM_CAPTURECHANGED:
			tileedit_action_done();
			break;
		
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HGDIOBJ obj;
			HDC dc;
			
			if (tileedit.busy) break;
			
			dc=BeginPaint(wnd,&ps);
			
			obj=SelectObject(tileedit.fg.bdc,tileedit.fg.bmp);
			StretchBlt(dc,0,0,22,128,tileedit.fg.bdc,0,0,1,8,SRCCOPY);
			SelectObject(tileedit.fg.bdc,obj);
			
			EndPaint(wnd,&ps);
			
			break;
		}
		
		default: break;
	}
	
	return DefWindowProc(wnd,msg,wParam,lParam);
}

/* bg subwindow */
static BOOL CALLBACK tileedit_sub_bg(HWND wnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	if (!tileedit.bg.wnd||tileedit.screen3) return DefWindowProc(wnd,msg,wParam,lParam);
	
	switch (msg) {
		
		case WM_SETCURSOR:
			SetCursor(tileedit.cursor_cross);
			return 1;
			break;
		
		case WM_MOUSEMOVE:
			tileedit.in_bg=input_mouse_in_client(wnd,NULL,&tileedit.p_bg,TRUE);
			break;
		
		case WM_LBUTTONDOWN:
			/* end whatever */
			tileedit_action_done();
			if (!tileedit.in_bg) {
				if (GetCapture()==wnd) ReleaseCapture();
				break;
			}
			
			/* start draw */
			if (GetCapture()!=wnd) SetCapture(wnd);
			tileedit.act=TILEEDIT_ACT_BC;
			SetCursor(tileedit.cursor_cross);
			break;
		
		case WM_RBUTTONDOWN:
			tileedit.rclick=TRUE;
		case WM_LBUTTONUP: case WM_RBUTTONUP:
			if (GetCapture()==wnd) ReleaseCapture();
		case WM_CAPTURECHANGED:
			tileedit_action_done();
			break;
		
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HGDIOBJ obj;
			HDC dc;
			
			if (tileedit.busy) break;
			
			dc=BeginPaint(wnd,&ps);
			
			obj=SelectObject(tileedit.bg.bdc,tileedit.bg.bmp);
			StretchBlt(dc,0,0,31,128,tileedit.bg.bdc,0,0,1,8,SRCCOPY);
			SelectObject(tileedit.bg.bdc,obj);
			
			EndPaint(wnd,&ps);
			
			break;
		}
		
		default: break;
	}
	
	return DefWindowProc(wnd,msg,wParam,lParam);
}


/* main window */
static BOOL CALLBACK tileview_editor_dialog(HWND dialog,UINT msg,WPARAM wParam,LPARAM lParam)
{
	switch (msg) {
		
		case WM_INITDIALOG: {
			char t[0x100]={0}; char tb[0x10]={0}; char tr[0x10]={0};
			int i;
			
			tileedit.busy=TRUE;
			
			tileedit.curpal=0;
			for (i=0;i<0x10;i++) tileedit.pal[i]=-1;
			
			tileedit_download();
			memcpy(_tileedit_undo_cursor->p,tileedit.p,8); memcpy(_tileedit_undo_cursor->c,tileedit.c,8);
			
			if (tileedit.mode&2) sprintf(tb,"/%d",tileedit.block_orig);
			if (tileedit.mode&4) sprintf(tr,", row %d",tileedit.row_orig);
			sprintf(t,"Pattern $%02X%s%s",tileedit.pattern_orig,tb,tr);
			SetDlgItemText(dialog,IDC_TILEEDIT_ZOOMT,t);
			
			tileedit.cursor_cross=LoadCursor(NULL,IDC_CROSS);
			tileedit.cursor_size=LoadCursor(NULL,IDC_SIZEALL);
			
			if (netplay_is_active()||movie_get_active_state()) {
				EnableWindow(GetDlgItem(dialog,IDOK),FALSE);
				EnableWindow(GetDlgItem(dialog,IDC_TILEEDIT_APPLY),FALSE);
			}
			
			tileedit.in_fg=FALSE; tileedit.in_fg_prev=2; tileedit.p_fg_prev.x=1000;
			tileedit.in_bg=FALSE; tileedit.in_bg_prev=2; tileedit.p_bg_prev.x=1000;
			tileedit.p_fg.x=tileedit.p_fg.y=tileedit.p_bg.x=tileedit.p_bg.y=0;
			
			if (!tileedit.screen3) {
				/* init fg */
				tileedit.fg.wnd=GetDlgItem(dialog,IDC_TILEEDIT_FG);
				toolwindow_resize(tileedit.fg.wnd,22,128);
				tileedit.fg.wdc=GetDC(tileedit.fg.wnd);
				tileedit.fg.bdc=CreateCompatibleDC(tileedit.fg.wdc);
				tool_init_bmi(&tileedit.fg.info,1,8,32);
				tileedit.fg.bmp=CreateDIBSection(tileedit.fg.wdc,&tileedit.fg.info,DIB_RGB_COLORS,(void*)&tileedit.fg.screen,NULL,0);
				SetWindowLongPtr(tileedit.fg.wnd,GWLP_WNDPROC,(LONG_PTR)tileedit_sub_fg);
				
				/* init bg */
				tileedit.bg.wnd=GetDlgItem(dialog,IDC_TILEEDIT_BG);
				toolwindow_resize(tileedit.bg.wnd,31,128);
				tileedit.bg.wdc=GetDC(tileedit.bg.wnd);
				tileedit.bg.bdc=CreateCompatibleDC(tileedit.bg.wdc);
				tool_init_bmi(&tileedit.bg.info,1,8,32);
				tileedit.bg.bmp=CreateDIBSection(tileedit.bg.wdc,&tileedit.bg.info,DIB_RGB_COLORS,(void*)&tileedit.bg.screen,NULL,0);
				SetWindowLongPtr(tileedit.bg.wnd,GWLP_WNDPROC,(LONG_PTR)tileedit_sub_bg);
			}
			
			/* init zoom */
			tileedit.in_zoom=FALSE; tileedit.in_zoom_prev=2; tileedit.p_zoom_prev.x=1000;
			tileedit.p_zoom.x=tileedit.p_zoom.y=0;
			tileedit.p_move.x=tileedit.p_move.y=0;
			if (!tileedit.screen3) ShowWindow(GetDlgItem(dialog,IDC_TILEEDIT_XY),SW_HIDE);
			tileedit.colour=-1;
			
			tileedit.zoom.wnd=GetDlgItem(dialog,IDC_TILEEDIT_ZOOM);
			toolwindow_resize(tileedit.zoom.wnd,128,128);
			tileedit.zoom.wdc=GetDC(tileedit.zoom.wnd);
			tileedit.zoom.bdc=CreateCompatibleDC(tileedit.zoom.wdc);
			tool_init_bmi(&tileedit.zoom.info,8,8,32);
			tileedit.zoom.bmp=CreateDIBSection(tileedit.zoom.wdc,&tileedit.zoom.info,DIB_RGB_COLORS,(void*)&tileedit.zoom.screen,NULL,0);
			SetWindowLongPtr(tileedit.zoom.wnd,GWLP_WNDPROC,(LONG_PTR)tileedit_sub_zoom);
			
			if (!tileedit.screen3) {
				/* init checkbox */
				CheckDlgButton(dialog,IDC_TILEEDIT_ALLBLOCKS,tileedit_allblocks?BST_CHECKED:BST_UNCHECKED);
				tileedit.allblocks_vis=vdp_regs[0]&2;
				if (!tileedit.allblocks_vis) EnableWindow(GetDlgItem(dialog,IDC_TILEEDIT_ALLBLOCKS),FALSE);
				
				/* position ticks */
				if (!tileedit.tickh) tileedit.tickh=LoadImage(MAIN->module,MAKEINTRESOURCE(ID_BITMAP_TICKH),IMAGE_BITMAP,0,0,0);
				if (!tileedit.tickv) tileedit.tickv=LoadImage(MAIN->module,MAKEINTRESOURCE(ID_BITMAP_TICKV),IMAGE_BITMAP,0,0,0);
				/* up */
				for (i=0;i<8;i++) {
					SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_HU00),NULL,16+0x10*i,29,2,4,SWP_NOZORDER);
					SendMessage(GetDlgItem(dialog,i+IDC_TICKS_HU00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)tileedit.tickh);
				}
				/* down */
				for (i=0;i<8;i++) {
					SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_HD00),NULL,16+0x10*i,163,2,4,SWP_NOZORDER);
					SendMessage(GetDlgItem(dialog,i+IDC_TICKS_HD00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)tileedit.tickh);
				}
				/* left */
				for (i=0;i<8;i++) {
					SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_VL00),NULL,11,34+0x10*i,4,2,SWP_NOZORDER);
					SendMessage(GetDlgItem(dialog,i+IDC_TICKS_VL00),STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)tileedit.tickv);
				}
				/* right */
				for (i=0;i<10;i++) {
					SetWindowPos(GetDlgItem(dialog,i+IDC_TICKS_VR00),NULL,146,25+0x10*i,0,0,SWP_NOSIZE|SWP_NOZORDER);
				}
			}
			
			main_parent_window(dialog,MAIN_PW_OUTERL,MAIN_PW_LEFT,-8,0,0);
			
			tileedit.xy=-1;
			tileedit.ukey_prev=0xc000;
			tileedit.fkey_prev=0xe000;
			tileedit.copy_open=tileedit.rclick=tileedit.act_done=FALSE;
			tileedit.act=TILEEDIT_ACT_NOTHING;
			
			tileview.ext_wnd=dialog;
			tileedit.busy=FALSE;
			break;
		}
		
		case WM_DESTROY:
			if (tileview.ext_wnd) {
				int i;
				
				GdiFlush();
				tileedit.busy=TRUE;
				tileview.ext_wnd=NULL;
				
				tileedit_clean_undo(_tileedit_undo_begin);
				if (tileedit.tickh) { DeleteObject(tileedit.tickh); tileedit.tickh=NULL; }
				if (tileedit.tickv) { DeleteObject(tileedit.tickv); tileedit.tickv=NULL; }
				for (i=0;i<0x10;i++) {
					if (tileedit.brush[i]) { DeleteObject(tileedit.brush[i]); tileedit.brush[i]=NULL; }
				}
				
				/* clean up zoom */
				tileedit.zoom.screen=NULL;
				if (tileedit.zoom.bmp) { DeleteObject(tileedit.zoom.bmp); tileedit.zoom.bmp=NULL; }
				if (tileedit.zoom.bdc) { DeleteDC(tileedit.zoom.bdc); tileedit.zoom.bdc=NULL; }
				if (tileedit.zoom.wdc) { ReleaseDC(tileedit.zoom.wnd,tileedit.zoom.wdc); tileedit.zoom.wdc=NULL; }
				tileedit.zoom.wnd=NULL;
				
				/* clean up fg */
				tileedit.fg.screen=NULL;
				if (tileedit.fg.bmp) { DeleteObject(tileedit.fg.bmp); tileedit.fg.bmp=NULL; }
				if (tileedit.fg.bdc) { DeleteDC(tileedit.fg.bdc); tileedit.fg.bdc=NULL; }
				if (tileedit.fg.wdc) { ReleaseDC(tileedit.fg.wnd,tileedit.fg.wdc); tileedit.fg.wdc=NULL; }
				tileedit.fg.wnd=NULL;
				
				/* clean up bg */
				tileedit.bg.screen=NULL;
				if (tileedit.bg.bmp) { DeleteObject(tileedit.bg.bmp); tileedit.bg.bmp=NULL; }
				if (tileedit.bg.bdc) { DeleteDC(tileedit.bg.bdc); tileedit.bg.bdc=NULL; }
				if (tileedit.bg.wdc) { ReleaseDC(tileedit.bg.wnd,tileedit.bg.wdc); tileedit.bg.wdc=NULL; }
				tileedit.bg.wnd=NULL;
				
				main_menu_enable(IDM_TILEVIEW,TRUE);
			}
			break;
		
		case WM_COMMAND:
			
			switch (LOWORD(wParam)) {
				
				/* load pattern */
				case IDC_TILEEDIT_OPEN: {
					const char* filter="Pattern Files (*.pattern)\0*.pattern\0All Files (*.*)\0*.*\0\0";
					const char* title="Open Pattern";
					char fn[STRING_SIZE]={0};
					OPENFILENAME of;
					
					if (!tileview.ext_wnd) break;
					
					tileedit.in_zoom=tileedit.in_fg=tileedit.in_bg=FALSE;
					
					if (tileedit.act) {
						ReleaseCapture();
						tileedit_action_done();
					}
					
					memset(&of,0,sizeof(OPENFILENAME));
					
					of.lStructSize=sizeof(OPENFILENAME);
					of.hwndOwner=dialog;
					of.hInstance=MAIN->module;
					of.lpstrFile=fn;
					of.lpstrFilter=filter;
					of.lpstrTitle=title;
					of.nMaxFile=STRING_SIZE;
					of.nFilterIndex=tileedit_fi;
					of.Flags=OFN_ENABLESIZING|OFN_HIDEREADONLY|OFN_FILEMUSTEXIST;
					of.lpstrInitialDir=strlen(tileedit_dir)?tileedit_dir:file->tooldir?file->tooldir:file->appdir;
					
					main_menu_enable(IDM_TILEVIEW,FALSE); /* resource leak if forced to close */
					if (GetOpenFileName(&of)) {
						int i,j,size;
						u8 data[0x20];
						FILE* fd=NULL;
						int success=FALSE;
						
						if (!tileview.ext_wnd) {
							/* cleaned up already (check again since time passed) */
							main_menu_enable(IDM_TILEVIEW,TRUE);
							break;
						}
						
						tileedit_fi=of.nFilterIndex;
						tool_set_pattern_fi_shared(tileedit_fi);
						
						if (strlen(fn)&&(size=file_open_custom(&fd,fn))>0) {
							if ((size==8||size==0x10||size==0x20)&&file_read_custom(fd,data,size)) {
								if (size==8||size==0x20) {
									if (!tileedit.screen3) {
										/* sprite/tile, no colour data included */
										success=TRUE;
										memcpy(tileedit.p_open,data,8);
									}
								}
								else {
									success=TRUE;
									memcpy(tileedit.c_open,data+8,8);
									if (tileedit.screen3) {
										/* 4 colours, no pattern */
										memset(tileedit.p_open,0xf0,8);
										for (i=1;i<4;i++) tileedit.c_open[i]=tileedit.c_open[0];
										for (i=5;i<8;i++) tileedit.c_open[i]=tileedit.c_open[4];
									}
									else {
										memcpy(tileedit.p_open,data,8);
										if (tileedit.mode!=2) {
											/* screen 0/1, only 2 colours */
											j=tileedit_findcommonct(tileedit.c_open);
											memset(tileedit.c_open,j,8);
										}
									}
								}
							}
						}
						
						file_close_custom(fd);
						
						if (success) {
							tileedit.copy_open=TRUE;
							if (strlen(fn+of.nFileOffset)&&(of.nFileExtension-1)>of.nFileOffset) {
								char wintitle[STRING_SIZE]={0};
								fn[of.nFileExtension-1]=0;
								sprintf(wintitle,"Tile Editor - %s",fn+of.nFileOffset);
								SetWindowText(dialog,wintitle);
							}
						}
						else LOG_ERROR_WINDOW(dialog,"Couldn't load pattern!");
						
						if (strlen(fn)&&of.nFileOffset) {
							fn[of.nFileOffset]=0; strcpy(tileedit_dir,fn);
						}
					}
					
					PostMessage(dialog,WM_NEXTDLGCTL,(WPARAM)GetDlgItem(dialog,IDC_TILEEDIT_APPLY),TRUE);
					main_menu_enable(IDM_TILEVIEW,TRUE);
					
					break;
				}
				
				/* save pattern */
				case IDC_TILEEDIT_SAVE: {
					const char* filter="Pattern File (*.pattern)\0*.pattern\0All Files (*.*)\0*.*\0\0";
					const char* defext="\0\0\0\0";
					const char* title="Save Pattern As";
					char fn[STRING_SIZE]={0};
					OPENFILENAME of;
					
					if (!tileview.ext_wnd) break;
					
					tileedit.in_zoom=tileedit.in_fg=tileedit.in_bg=FALSE;
					
					if (tileedit.act) {
						ReleaseCapture();
						tileedit_action_done();
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
					of.nFilterIndex=tileedit_fi;
					of.Flags=OFN_ENABLESIZING|OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
					of.lpstrInitialDir=strlen(tileedit_dir)?tileedit_dir:file->tooldir?file->tooldir:file->appdir;
					
					main_menu_enable(IDM_TILEVIEW,FALSE); /* resource leak if forced to close */
					if (GetSaveFileName(&of)) {
						u8 data[0x10];
						FILE* fd=NULL;
						
						if (!tileview.ext_wnd) {
							/* cleaned up already (check again since time passed) */
							main_menu_enable(IDM_TILEVIEW,TRUE);
							break;
						}
						
						tileedit_fi=of.nFilterIndex;
						tool_set_pattern_fi_shared(tileedit_fi);
						
						memcpy(data,tileedit.p,8);
						memcpy(data+8,tileedit.c,8);
						
						if (!strlen(fn)||!file_save_custom(&fd,fn)||!file_write_custom(fd,data,0x10)) LOG_ERROR_WINDOW(dialog,"Couldn't save pattern!");
						else if (strlen(fn+of.nFileOffset)&&(of.nFileExtension-1)>of.nFileOffset) {
							char wintitle[STRING_SIZE]={0};
							fn[of.nFileExtension-1]=0;
							sprintf(wintitle,"Tile Editor - %s",fn+of.nFileOffset);
							SetWindowText(dialog,wintitle);
						}
						
						file_close_custom(fd);
						if (strlen(fn)&&of.nFileOffset) {
							fn[of.nFileOffset]=0; strcpy(tileedit_dir,fn);
						}
					}
					
					PostMessage(dialog,WM_NEXTDLGCTL,(WPARAM)GetDlgItem(dialog,IDC_TILEEDIT_APPLY),TRUE);
					main_menu_enable(IDM_TILEVIEW,TRUE);
					
					break;
				}
				
				/* allblocks checkbox */
				case IDC_TILEEDIT_ALLBLOCKS:
					if (tileedit.screen3||!tileview.ext_wnd) break;
					tileedit_allblocks=(IsDlgButtonChecked(dialog,IDC_TILEEDIT_ALLBLOCKS)==BST_CHECKED);
					break;
				
				/* apply */
				case IDC_TILEEDIT_APPLY:
					if (netplay_is_active()||movie_get_active_state()||!tileview.ext_wnd) break;
					tileedit_apply(dialog,FALSE);
					break;
				
				/* close dialog */
				case IDOK:
					if (netplay_is_active()||movie_get_active_state()||!tileview.ext_wnd||tileedit_apply(dialog,TRUE)==IDCANCEL) break;
				case IDCANCEL:
					if (!tileview.ext_wnd) break;
					EndDialog(dialog,0);
					break;
				
				default: break;
			} /* WM_COMMAND */
			
			break;
		
		case WM_LBUTTONDOWN: {
			/* choose from palette */
			POINT p={0,0};
			RECT r;
			int i;
			
			if (!tileview.ext_wnd) break;
			
			GetCursorPos(&p);
			
			for (i=0;i<0x10;i++) {
				GetWindowRect(GetDlgItem(dialog,i+IDC_PALEDIT_00),&r);
				if (PtInRect(&r,p)) {
					tileedit_changepal(dialog,i);
					break;
				}
			}
			
			break;
		}
		
		case WM_CTLCOLORSTATIC: {
			UINT i=GetDlgCtrlID((HWND)lParam);
			
			/* change palette background */
			if (tileview.ext_wnd&&i>=IDC_PALEDIT_00&&i<=IDC_PALEDIT_15) {
				int j=i-IDC_PALEDIT_00;
				if (tileedit.brush[j]!=NULL) DeleteObject(tileedit.brush[j]);
				i=tileedit.pal[j]; i=(i>>16&0xff)|(i&0xff00)|(i<<16&0xff0000);
				tileedit.brush[j]=CreateSolidBrush(i);
				return (BOOL)tileedit.brush[j];
			}
			break;
		}
		
		case TOOL_REPAINT: {
			int* screen;
			u8 p[8]; u8 c[8];
			int v[3]={FALSE,FALSE,FALSE};
			int i,j,fg,bg,xy=-2;
			int pal[0x10];
			
			if (!tileview.ext_wnd||tileedit.busy) break;
			tileedit.busy=TRUE;
			
			/* coordinates */
			if (tileedit.in_zoom) {
				tileedit.in_zoom&=input_mouse_in_client(tileedit.zoom.wnd,dialog,&tileedit.p_zoom,GetForegroundWindow()==dialog);
				tileedit.p_zoom.x=tileedit.p_zoom.x>>4&7; tileedit.p_zoom.y=tileedit.p_zoom.y>>4&7;
				if (tileedit.in_zoom) xy=tileedit.p_zoom.x|tileedit.p_zoom.y<<4;
			}
			if (tileedit.in_fg) {
				tileedit.in_fg&=input_mouse_in_client(tileedit.fg.wnd,dialog,&tileedit.p_fg,GetForegroundWindow()==dialog);
				tileedit.p_fg.x=0; tileedit.p_fg.y=tileedit.p_fg.y>>4&7;
				if (tileedit.in_fg) xy=8|tileedit.p_fg.y<<4|((tileedit.mode==2)?0:0xf0);
			}
			if (tileedit.in_bg) {
				tileedit.in_bg&=input_mouse_in_client(tileedit.bg.wnd,dialog,&tileedit.p_bg,GetForegroundWindow()==dialog);
				tileedit.p_bg.x=0; tileedit.p_bg.y=tileedit.p_bg.y>>4&7;
				if (tileedit.in_bg) xy=9|tileedit.p_bg.y<<4|((tileedit.mode==2)?0:0xf0);
			}
			
			i=tileedit.in_zoom|tileedit.in_fg|tileedit.in_bg;
			if (i!=(tileedit.in_zoom_prev|tileedit.in_fg_prev|tileedit.in_bg_prev)) {
				if (!tileedit.screen3) {
					EnableWindow(GetDlgItem(dialog,IDC_TILEEDIT_XYT),i);
					ShowWindow(GetDlgItem(dialog,IDC_TILEEDIT_XY),i?SW_NORMAL:SW_HIDE);
				}
				EnableWindow(GetDlgItem(dialog,IDC_TILEEDIT_CT),i);
				EnableWindow(GetDlgItem(dialog,IDC_TILEEDIT_C),i);
			}
			
			if (!tileedit.screen3&&tileedit.xy!=xy) {
				char t[0x100]={0};
				char tx[0x10]={0};
				char ty[0x10]={0};
				
				if (xy>=0) {
					/* x */
					switch (xy&0xf) {
						case 8: sprintf(tx,"FG"); break;
						case 9: sprintf(tx,"BG"); break;
						default: sprintf(tx,"%d",xy&0xf); break;
					}
					
					/* y */
					switch (xy>>4&0xf) {
						case 0xf: sprintf(ty,"Y"); break;
						default: sprintf(ty,"%d",xy>>4&0xf); break;
					}
					
					sprintf(t,"(%s,%s)",tx,ty);
				}
				else sprintf(t," ");
				
				SetDlgItemText(dialog,IDC_TILEEDIT_XY,t);
				tileedit.xy=xy;
			}
			
			/* allblocks checkbox visibility */
			if (!tileedit.screen3&&tileedit.allblocks_vis!=(vdp_regs[0]&2)) {
				tileedit.allblocks_vis=vdp_regs[0]&2;
				EnableWindow(GetDlgItem(dialog,IDC_TILEEDIT_ALLBLOCKS),tileedit.allblocks_vis!=0);
			}
			
			memcpy(pal,draw_palette,0x10*sizeof(int)); pal[0]=pal[vdp_regs[7]&0xf];
			memcpy(p,tileedit.p,8);
			memcpy(c,tileedit.c,8);
			
			/* hotkey action */
			i=(GetAsyncKeyState(0x45)&0x8000)|(GetAsyncKeyState(0x56)>>1&0x4000)|(GetAsyncKeyState(0x48)>>2&0x2000)|(GetAsyncKeyState(0x49)>>3&0x1000); /* e,v,h,i */
			if (i&&!tileedit.act_done&&tileedit.act==TILEEDIT_ACT_NOTHING&&tileedit.fkey_prev==0&&dialog==GetForegroundWindow()) {
				u8 p2[8]; u8 c2[8];
				
				switch (i) {
					
					/* erase */
					case 0x8000:
						if (tileedit.screen3) memset(c,0,8);
						else {
							if (GetAsyncKeyState(VK_SHIFT)&0x8000) memset(c,0xf0,8);
							else memset(p,0,8);
						}
						break;
					
					/* vertical flip */
					case 0x4000:
						j=8;
						while (j--) { p2[j^7]=p[j]; c2[j^7]=c[j]; }
						memcpy(p,p2,8); memcpy(c,c2,8);
						
						break;
					
					/* horizontal flip */
					case 0x2000:
						if (tileedit.screen3) {
							j=8;
							while (j--) c2[j]=(c[j]>>4&0xf)|(c[j]<<4&0xf0);
							memcpy(c,c2,8);
						}
						else {
							int k; j=8; memset(p2,0,8);
							while (j--) {
								k=8;
								while (k--) p2[j]|=((p[j]>>k&1)<<(k^7));
							}
							memcpy(p,p2,8);
						}
						
						break;
					
					/* invert */
					case 0x1000:
						if (!tileedit.screen3) {
							j=8;
							while (j--) p[j]^=0xff;
						}
						break;
					
					/* multiple keys, ignore */
					default: break;
				}
				
				tileedit.act_done=TRUE;
			}
			tileedit.fkey_prev=i;
			
			/* next */
			if (tileedit.act_done&&(memcmp(p,_tileedit_undo_cursor->p,8)|memcmp(c,_tileedit_undo_cursor->c,8))) {
				tileedit_clean_undo(_tileedit_undo_cursor);
				MEM_CREATE_T(_tileedit_undo_cursor->next,sizeof(_tileedit_undo),_tileedit_undo*);
				_tileedit_undo_cursor->next->prev=_tileedit_undo_cursor;
				_tileedit_undo_cursor=_tileedit_undo_cursor->next;
				memcpy(_tileedit_undo_cursor->p,p,8);
				memcpy(_tileedit_undo_cursor->c,c,8);
			}
			tileedit.act_done=FALSE;
			
			/* handle action */
			switch (tileedit.act) {
				case TILEEDIT_ACT_FG:
					if (!tileedit.in_zoom) break;
					if (tileedit.screen3) {
						for (i=tileedit.p_zoom.y&4;i<(tileedit.p_zoom.y&4)+4;i++) c[i]=tileedit.curpal<<(~tileedit.p_zoom.x&4)|(c[i]&(0xf<<(tileedit.p_zoom.x&4)));
					}
					else p[tileedit.p_zoom.y]|=(1<<(7-tileedit.p_zoom.x));
					break;
				
				case TILEEDIT_ACT_BG:
					if (!tileedit.in_zoom) break;
					p[tileedit.p_zoom.y]&=(1<<(7-tileedit.p_zoom.x)^0xff);
					break;
				
				case TILEEDIT_ACT_MOVE: {
					int d[2];
					u8 p2[8]; u8 c2[8];
					j=tileedit.screen3;
					
					/* compute difference */
					GetCursorPos(&tileedit.p_move);
					d[0]=tileedit.p_move.x-tileedit.p_move_prev.x;
					d[1]=tileedit.p_move.y-tileedit.p_move_prev.y;
					for (i=0;i<2;i++) {
						if (d[i]<0) { d[i]=-d[i]; if (d[i]>(0x3f+j)) d[i]=0x3f+j; d[i]=(8-(d[i]>>(4+(j<<1))))&7; }
						else { if (d[i]>(0x3f+j)) d[i]=0x3f+j; d[i]>>=(4+(j<<1)); }
					}
					if (d[0]) tileedit.p_move_prev.x=tileedit.p_move.x;
					if (d[1]) tileedit.p_move_prev.y=tileedit.p_move.y;
					
					if (tileedit.screen3) {
						if (d[1]) d[1]=4;
						
						/* rotate x */
						if (d[0]) {
							i=8;
							while (i--) c[i]=(c[i]>>4&0xf)|(c[i]<<4&0xf0);
						}
					}
					else {
						/* rotate x, screen 0/1/2 */
						while (d[0]--) {
							i=8;
							while (i--) p[i]=p[i]>>1|(p[i]<<7&0x80);
						}
					}
					
					/* rotate y */
					i=8;
					while (i--) {
						p2[(i+d[1])&7]=p[i];
						c2[(i+d[1])&7]=c[i];
					}
					memcpy(p,p2,8);
					memcpy(c,c2,8);
					
					break;
				}
				
				case TILEEDIT_ACT_FC:
					if (!tileedit.in_fg) break;
					if (tileedit.mode==2) c[tileedit.p_fg.y]=(c[tileedit.p_fg.y]&0xf)|tileedit.curpal<<4;
					else {
						i=8;
						while (i--) c[i]=(c[i]&0xf)|tileedit.curpal<<4;
					}
					break;
				
				case TILEEDIT_ACT_BC:
					if (!tileedit.in_bg) break;
					if (tileedit.mode==2) c[tileedit.p_bg.y]=(c[tileedit.p_bg.y]&0xf0)|tileedit.curpal;
					else {
						i=8;
						while (i--) c[i]=(c[i]&0xf0)|tileedit.curpal;
					}
					break;
				
				default: break;
			}
			
			/* undo/redo */
			i=(GetAsyncKeyState(0x5a)&0x8000)|(GetAsyncKeyState(0x59)>>1&0x4000); /* z/y */
			if (tileedit.act==TILEEDIT_ACT_NOTHING&&(i==0x8000||i==0x4000)&&tileedit.ukey_prev==0&&dialog==GetForegroundWindow()&&GetAsyncKeyState(VK_CONTROL)&0x8000) {
				if (i==0x8000) _tileedit_undo_cursor=_tileedit_undo_cursor->prev; /* undo */
				else if (_tileedit_undo_cursor->next) _tileedit_undo_cursor=_tileedit_undo_cursor->next; /* redo */
				
				memcpy(p,_tileedit_undo_cursor->p,8);
				memcpy(c,_tileedit_undo_cursor->c,8);
			}
			tileedit.ukey_prev=i;
			
			/* colour info */
			if (tileedit.in_zoom) {
				i=(p[tileedit.p_zoom.y]>>(7-tileedit.p_zoom.x)&1)<<2;
				i=(c[tileedit.p_zoom.y]>>i&0xf)|i<<2;
			}
			else if (tileedit.in_fg) i=c[tileedit.p_fg.y]>>4|0x10;
			else if (tileedit.in_bg) i=c[tileedit.p_bg.y]&0xf;
			else i=tileedit.curpal|0x20;
			
			if (i!=tileedit.colour) {
				char t[0x10];
				char ci[0x10]={0};
				
				if (!tileedit.screen3&&!(i&0x20)) {
					if (i&0x10) sprintf(ci," FG");
					else sprintf(ci," BG");
				}
				
				sprintf(t,"%d %s",i&0xf,ci);
				SetDlgItemText(dialog,IDC_TILEEDIT_C,t);
				tileedit.colour=i;
			}
			
			if (tileedit.in_fg|tileedit.in_bg|(tileedit.in_zoom&tileedit.screen3)&&tileedit.rclick) tileedit_changepal(dialog,i&0xf);
			tileedit.rclick=FALSE;
			
			/* applied with degrading/loaded */
			if (tileedit.copy_open) {
				memcpy(p,tileedit.p_open,8);
				memcpy(c,tileedit.c_open,8);
				
				tileedit_clean_undo(_tileedit_undo_begin);
				memcpy(_tileedit_undo_cursor->p,p,8);
				memcpy(_tileedit_undo_cursor->c,c,8);
				
				tileedit.copy_open=FALSE;
			}
			
			/* redraw needed? */
			if (memcmp(p,tileedit.p,8)) { memcpy(tileedit.p,p,8); v[0]=TRUE; }
			if (memcmp(c,tileedit.c,8)) { memcpy(tileedit.c,c,8); v[0]=v[1]=v[2]=TRUE; }
			if (memcmp(pal,tileedit.pal,0x10*sizeof(int))) {
				memcpy(tileedit.pal,pal,0x10*sizeof(int));
				v[0]=v[1]=v[2]=TRUE;
				
				for (i=0;i<0x10;i++) InvalidateRect(GetDlgItem(dialog,i+IDC_PALEDIT_00),NULL,FALSE);
			}
			
			/* update screen(s) */
			if (tileedit.screen3) v[1]=v[2]=FALSE;
			if (v[0]|v[1]|v[2]) GdiFlush();
			
			if (v[0]) {
				screen=tileedit.zoom.screen;
				for (i=0;i<8;i++) {
					j=p[i]; fg=pal[c[i]>>4]; bg=pal[c[i]&0xf];
					#define U() *screen++=((j<<=1)&0x100)?fg:bg;
					U(); U(); U(); U(); U(); U(); U(); U();
					#undef U
				}
			}
			
			if (v[1]) for (i=0;i<8;i++) tileedit.fg.screen[i]=pal[c[i]>>4];
			if (v[2]) for (i=0;i<8;i++) tileedit.bg.screen[i]=pal[c[i]&0xf];
			
			
			tileedit.in_zoom_prev=tileedit.in_zoom; tileedit.p_zoom_prev.x=tileedit.p_zoom.x; tileedit.p_zoom_prev.y=tileedit.p_zoom.y;
			tileedit.in_fg_prev=tileedit.in_fg; tileedit.p_fg_prev.x=tileedit.p_fg.x; tileedit.p_fg_prev.y=tileedit.p_fg.y;
			tileedit.in_bg_prev=tileedit.in_bg; tileedit.p_bg_prev.x=tileedit.p_bg.x; tileedit.p_bg_prev.y=tileedit.p_bg.y;
			
			tileedit.busy=FALSE;
			
			if (v[0]) InvalidateRect(tileedit.zoom.wnd,NULL,FALSE);
			if (v[1]) InvalidateRect(tileedit.fg.wnd,NULL,FALSE);
			if (v[2]) InvalidateRect(tileedit.bg.wnd,NULL,FALSE);
			
			break;
		} /* TOOL_REPAINT */
		
		case TOOL_MENUCHANGED: {
			int i=((netplay_is_active()|movie_get_active_state())==0);
			EnableWindow(GetDlgItem(dialog,IDOK),i);
			EnableWindow(GetDlgItem(dialog,IDC_TILEEDIT_APPLY),i);
			break;
		}
		
		default: break;
	}
	
	return 0;
}
