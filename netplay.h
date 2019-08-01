#ifndef NETPLAY_H
#define NETPLAY_H

void netplay_init(void);
void netplay_clean(void);
int __fastcall netplay_is_active(void);
int netplay_keyboard_enabled(void);
void netplay_open(void);
void netplay_frame(u8*,int*,u8*);

int WINAPI netplay_starts(char*,int,int);
void WINAPI netplay_drop(char*,int);

#endif /* NETPLAY_H */
