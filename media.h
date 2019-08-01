#ifndef MEDIA_H
#define MEDIA_H

int media_get_filterindex_bios(void);
int media_get_filterindex_tape(void);
int media_get_filterindex_cart(void);
void media_init(void);


BOOL CALLBACK media_dialog(HWND,UINT,WPARAM,LPARAM);
int media_open_single(const char*);
void media_drop(WPARAM);


#endif /* MEDIA_H */
