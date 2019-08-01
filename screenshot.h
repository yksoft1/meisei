#ifndef SCREENSHOT_H
#define SCREENSHOT_H

enum {
	SCREENSHOT_TYPE_32BPP=0,
	SCREENSHOT_TYPE_8BPP_INDEXED,
	SCREENSHOT_TYPE_1BPP_INDEXED
};

int screenshot_save(const int,const int,const int,const void*,const void*,const char*);

#endif /* SCREENSHOT_H */
