#ifndef SPRITEVIEW_H
#define SPRITEVIEW_H

int spriteview_get_open_fi(void);
int spriteview_get_save_fi(void);

void spriteview_init(void);
void spriteview_clean(void);
BOOL CALLBACK spriteview_window(HWND,UINT,WPARAM,LPARAM);

#endif /* SPRITEVIEW_H */
