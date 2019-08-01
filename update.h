#ifndef UPDATE_H
#define UPDATE_H

int update_download_thread_is_active(void);
BOOL CALLBACK update_dialog(HWND,UINT,WPARAM,LPARAM);

void update_clean(void);

#endif /* UPDATE_H */
