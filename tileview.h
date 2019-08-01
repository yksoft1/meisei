#ifndef TILEVIEW_H
#define TILEVIEW_H

enum {
	TILEVIEW_SOURCE_PT=0,
	TILEVIEW_SOURCE_NT,
	TILEVIEW_SOURCE_NTO,
	
	TILEVIEW_SOURCE_MAX
};

const char* tileview_get_source_name(u32);
int tileview_get_source(void);
int tileview_get_open_fi(void);
int tileview_get_save_fi(void);
int tileview_get_openi_nt(void);
int tileview_get_openi_pt(void);
int tileview_get_openi_ct(void);
int tileview_get_open_sim(void);
int tileview_get_save_mask(void);
int tileview_get_save_header(void);
int tileedit_get_allblocks(void);
int __fastcall tileview_get_raw_font(int);

void tileview_init(void);
void tileview_clean(void);
BOOL CALLBACK tileview_window(HWND,UINT,WPARAM,LPARAM);

#endif /* TILEVIEW_H */
