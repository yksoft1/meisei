#include <windows.h>

#include "global.h"
#include "mapper.h"
#include "file.h"
#include "settings.h"
#include "sample.h"
#include "state.h"
#include "scc.h"
#include "z80.h"
#include "crystal.h"
#include "sound.h"
#include "psg.h"
#include "netplay.h"
#include "am29f040b.h"
#include "io.h"
#include "tape.h"
#include "reverse.h"
#include "resource.h"
#include "main.h"
#include "cont.h"
#include "msx.h"

/* declarations */
typedef __fastcall u8(*fp_mapread_custom)(int,u16);
typedef __fastcall void(*fp_mapwrite_custom)(int,u16,u8);
static void mapsetcustom_read(int,int,int,fp_mapread_custom);
static void mapsetcustom_write(int,int,int,fp_mapwrite_custom);
static int mapcart_getpslot(int);

static int sram_load(int,u8**,u32,const char*,u8);
static void sram_save(int,u8*,u32,const char*);
static void mapper_set_bios_default(void);
static __inline__ void refresh_cb(int);

/* value RAM is filled with on power on. alternating strings of 00s and $ffs on most MSXes */
#define RAMINIT			0xff

/* unmapped read return value, $ff on almost all MSXes, undefined on some. */
/* MSXes known to return an undefined value:
 Canon V-20 (UK): last opcode (or opcode operand) ORed with some value (open-bus) */
#define UNMAPPED		0xff

/* defaults */
#define DEFAULT_BIOS	"cbios_main_msx1_0.23.rom"
#define DEFAULT_RAMSIZE	64
#define DEFAULT_RAMSLOT	3

static int pslot=0;						/* PPI primary slot register */

static fp_mapread mapread_std[4][8];	/* 4 slots standard handlers */
static fp_mapwrite mapwrite_std[4][8];

static char* fn_default_bios;
static char* fn_bios=NULL; static char* n_bios=NULL;
static u32 bioscrc; static u32 default_bioscrc;
static int autodetect_rom;

static int ramsize=DEFAULT_RAMSIZE;		/* RAM size in KB */
static int ramslot=DEFAULT_RAMSLOT;		/* RAM primary slot */
static u8 ram[0x10000];					/* 64KB */
static u8 bios[0x10000];				/* 64KB, normally 32KB, but room for more (eg. cbios + logo) */
static u8 default_bios[0x8000];			/* 32KB, default C-BIOS */
static u8 dummy_u[0x2000];				/* 8KB filled with UNMAPPED */
static u8 dummy_ff[0x2000];				/* 8KB filled with $ff */
static u8* tempfile[ROM_MAXPAGES];

static u8* cart[2][ROM_MAXPAGES+2+2];	/* max 4MB (+2 padding +2 dummy) */
static u8* cartmap[2][8];				/* cart memmaps */
static int cartbank[2][8];				/* cart banknumbers */
static u8* cartsram[2]={NULL,NULL};		/* sram */
static u8* cartsramcopy[2]={NULL,NULL};	/* sram copy */
static int cartsramsize[2]={0,0};		/* sram mem size */
static char* cartfile[2]={NULL,NULL};	/* path+filename */
static u32 cartcrc[2]={0,0};			/* checksum */
static int carttype[2];					/* romtype */
static int cartsize[2];					/* file size */
static int cartpages[2];				/* number of pages */
static int cartmask[2];					/* and mask for megaroms */

/* extra config references */
static int mapper_extra_ref_slot=0;
static int mapper_extra_ref_type=CARTTYPE_NOMAPPER;
static POINT mapper_extra_ref_pos;

/* specific save/load state */
typedef int __fastcall (*_mapgetstatesize)(int);
static int __fastcall mapgetstatesize_nothing(int slot) { return 0; }
static _mapgetstatesize mapgetstatesize[2]={mapgetstatesize_nothing,mapgetstatesize_nothing};

typedef void __fastcall (*_mapsavestate)(int,u8**);
static void __fastcall mapsavestate_nothing(int slot,u8** s) { ; }
static _mapsavestate mapsavestate[2]={mapsavestate_nothing,mapsavestate_nothing};

typedef void __fastcall (*_maploadstatecur)(int,u8**);
static void __fastcall maploadstatecur_nothing(int slot,u8** s) { ; }
static _maploadstatecur maploadstatecur[2]={maploadstatecur_nothing,maploadstatecur_nothing};

typedef int __fastcall (*_maploadstate)(int,int,u8**);
static int __fastcall maploadstate_nothing(int slot,int v,u8** s) { return TRUE; }
static _maploadstate maploadstate[2]={maploadstate_nothing,maploadstate_nothing};

static int loadstate_skip_sram[2]={0,0};
static int mel_error=0;
int mapper_get_mel_error(void) { return mel_error; }

/* specific new/end frame */
typedef void (*_mapnf)(int);
static void mapnf_nothing(int slot) { ; }
static _mapnf mapnf[2]={mapnf_nothing,mapnf_nothing};
void mapper_new_frame(void) { mapnf[0](0); mapnf[1](1); }

typedef void (*_mapef)(int);
static void mapef_nothing(int slot) { ; }
static _mapef mapef[2]={mapef_nothing,mapef_nothing};
void mapper_end_frame(void) { mapef[0](0); mapef[1](1); }

/* specific sound set buffercounter */
typedef void (*_mapsetbc)(int,int);
static void mapsetbc_nothing(int slot,int bc) { ; }
static _mapsetbc mapsetbc[2]={mapsetbc_nothing,mapsetbc_nothing};
void mapper_set_buffer_counter(int bc) { mapsetbc[0](0,bc); mapsetbc[1](1,bc); }

/* specific soundstream */
typedef void (*_mapstream)(int,signed short*,int);
static void mapstream_nothing(int slot,signed short* stream,int len) { ; }
static _mapstream mapstream[2]={mapstream_nothing,mapstream_nothing};
void mapper_sound_stream(signed short* stream,int len) { mapstream[0](0,stream,len); mapstream[1](1,stream,len); }

/* specific cleanup */
typedef void (*_mapclean)(int);
static void mapclean_nothing(int slot) { ; }
static _mapclean mapclean[2]={mapclean_nothing,mapclean_nothing};

#include "mapper_table.h" /* in a separate file for readability.. only usable by mapper*.c */

typedef struct {
	int uid;
	char* longname;
	char* shortname;
	void (*init)(int);
	u32 flags;
} _maptype;

/* deprecated uids, for savestate backward compatibility, don't reuse */
/* removed in meisei 1.3: */
#define DEPRECATED_START0000		1	/* small, start at $0000 */
#define DEPRECATED_START4000		2	/* small, start at $4000 */
#define DEPRECATED_START8000		3	/* small, start at $8000 (BASIC) */
#define DEPRECATED_STARTC000		26	/* small, start at $c000 */
#define DEPRECATED_ASCII8			4	/* ASCII8 std */
#define DEPRECATED_ASCII16			5	/* ASCII16 std */
#define DEPRECATED_ASCII16_2		15	/* ASCII16 + 2KB SRAM */
#define DEPRECATED_ASCII8_8			16	/* ASCII8 + 8KB SRAM */
#define DEPRECATED_ASCII8_32		17	/* ASCII8 + 32KB SRAM (Koei) */
#define DEPRECATED_ZEMINAZMB		25	/* Zemina "ZMB" Box */

#include "mapper_nomapper.c"
#include "mapper_misc.c"
#include "mapper_konami.c"
#include "mapper_ascii.c"
#include "mapper_panasonic.c"
#include "mapper_bootleg.c"

#define UMAX CARTUID_MAX /* mapper.h */
static const _maptype maptype[CARTTYPE_MAX]={
/* uid	long name					short name	init function		flags */
{ UMAX,	"Al-Alamiah Al-Qur'an",		"Qur'an",	mapinit_quran,		0 },
{ 27,	"ASCII MegaROM",			"ASCII",	mapinit_ascii,		MCF_EXTRA },
{ 21,	"Bootleg 80-in-1",			"BTL80",	mapinit_btl80,		0 },
{ 22,	"Bootleg 90-in-1",			"BTL90",	mapinit_btl90,		0 },
{ 23,	"Bootleg 126-in-1",			"BTL126",	mapinit_btl126,		0 },
{ 9,	"dB-Soft Cross Blaim",		"CBlaim",	mapinit_crossblaim,	0 },
{ 6,	"Irem TAM-S1",				"TAM-S1",	mapinit_irem,		0 },
{ 19,	"Konami Game Master 2",		"GM2",		mapinit_gm2,		MCF_SRAM },
{ 8,	"Konami SCC",				"SCC",		mapinit_konamiscc,	0 },
{ 24,	"Konami Sound Cartridge",	"SCC-I",	mapinit_konamiscci,	MCF_EXTRA },
{ 13,	"Konami Synthesizer",		"KSyn",		mapinit_konamisyn,	0 },
{ 7,	"Konami VRC",				"VRC",		mapinit_konamivrc,	0 },
{ 14,	"Matra INK",				"INK",		mapinit_matraink,	0 },
{ 10,	"MicroCabin Harry Fox",		"HFox",		mapinit_harryfox,	0 },
{ 0,	"No mapper",				"NM",		mapinit_nomapper,	MCF_EXTRA },
{ 18,	"Panasoft PAC",				"PAC",		mapinit_pac,		MCF_SRAM | MCF_EMPTY },
{ 20,	"Pazos Mega Flash ROM SCC",	"MFRSCC",	mapinit_mfrscc,		MCF_SRAM },
{ 12,	"Sony Playball",			"Playball",	mapinit_playball,	0 },
{ 11,	"Vincent DSK2ROM",			"DSK2ROM",	mapinit_dsk2rom,	0 }
};
#undef UMAX

int mapper_get_type_uid(u32 type)
{
	if (type>=CARTTYPE_MAX) type=CARTTYPE_NOMAPPER;
	return maptype[type].uid;
}

int mapper_get_uid_type(u32 uid)
{
	int i,slot=((uid&0x80000000)!=0);
	uid&=0x7fffffff;
	if (uid>CARTUID_MAX) uid=0;
	
	/* look in maptype list */
	for (i=0;i<CARTTYPE_MAX;i++) if (maptype[i].uid==uid) return i;
	
	/* look in deprecated list (only accessed from msx.c) */
	switch (uid) {
		case DEPRECATED_START0000: case DEPRECATED_START4000: case DEPRECATED_START8000: case DEPRECATED_STARTC000: return CARTTYPE_NOMAPPER;
		case DEPRECATED_ASCII8: case DEPRECATED_ASCII16: return CARTTYPE_ASCII;
		case DEPRECATED_ZEMINAZMB: return CARTTYPE_KONAMIVRC;
		
		/* error if sram size is not the same */
		case DEPRECATED_ASCII16_2: return CARTTYPE_ASCII|((ascii_board[slot]&A_SS_MASK)!=A_SS_02)<<30;
		case DEPRECATED_ASCII8_8: return CARTTYPE_ASCII|((ascii_board[slot]&A_SS_MASK)!=A_SS_08)<<30;
		case DEPRECATED_ASCII8_32: return CARTTYPE_ASCII|((ascii_board[slot]&A_SS_MASK)!=A_SS_32)<<30;
		
		default: break;
	}
	
	return CARTTYPE_NOMAPPER;
}

const char* mapper_get_type_longname(u32 type)
{
	if (type>=CARTTYPE_MAX) type=CARTTYPE_NOMAPPER;
	return maptype[type].longname;
}

const char* mapper_get_type_shortname(u32 type)
{
	if (type>=CARTTYPE_MAX) type=CARTTYPE_NOMAPPER;
	if (maptype[type].shortname) return maptype[type].shortname;
	else return maptype[type].longname;
}

u32 mapper_get_type_flags(u32 type)
{
	if (type>=CARTTYPE_MAX) type=CARTTYPE_NOMAPPER;
	return maptype[type].flags;
}

void mapper_set_carttype(int slot,u32 type)
{
	carttype[slot&1]=type;
	mapper_extra_configs_apply(slot&1);
}

int mapper_get_carttype(int slot) { return carttype[slot&1]; }
int mapper_get_cartsize(int slot) { return cartsize[slot&1]; }
u32 mapper_get_cartcrc(int slot) { return cartcrc[slot&1]; }
u32 mapper_get_bioscrc(void) { return bioscrc; }
int mapper_get_autodetect_rom(void) { return autodetect_rom; }
const char* mapper_get_file(int slot) { if (cartfile[slot&1]&&strlen(cartfile[slot&1])) return cartfile[slot&1]; else return NULL; }
const char* mapper_get_defaultbios_file(void) { return fn_default_bios; }
void mapper_get_defaultbios_name(char* c) { strcpy(c,DEFAULT_BIOS); }
const char* mapper_get_bios_file(void) { return fn_bios; }
const char* mapper_get_bios_name(void) { return n_bios; }
void mapper_flush_ram(void) { memset(ram,RAMINIT,0x10000); }
int mapper_get_ramsize(void) { return ramsize; }
int mapper_get_ramslot(void) { return ramslot; }
int mapper_get_default_ramsize(void) { return DEFAULT_RAMSIZE; }
int mapper_get_default_ramslot(void) { return DEFAULT_RAMSLOT; }

char* mapper_get_current_name(char* c)
{
	MEM_CLEAN(file->name); /* set to NULL */
	if (mapper_get_file(1)) file_setfile(NULL,mapper_get_file(1),NULL,NULL);
	if (mapper_get_file(0)) file_setfile(NULL,mapper_get_file(0),NULL,NULL);
	
	if (file->name) sprintf(c,"%s",file->name);
	else {
		/* tape */
		if (tape_get_fn_cur()) file_setfile(NULL,tape_get_fn_cur(),NULL,NULL);
		if (file->name) sprintf(c,"%s",file->name);
		else {
			/* bios */
			if (fn_bios) file_setfile(NULL,fn_bios,NULL,NULL);
			if (file->name) sprintf(c,"%s",file->name);
			else sprintf(c,"none");
		}
	}
	
	return c;
}

void mapper_log_type(int slot,int one)
{
	/* only called from media.c */
	int s=mapper_get_cartsize(slot);
	char ls[0x10];
	
	if (s==0) sprintf(ls,"empty");
	else if (s<0x2000) sprintf(ls,"%dKB",(s>>10)+((s>>10)==0)); /* multiples of 1KB */
	else sprintf(ls,"%dKB",8*((s>>13)+((s&0x1fff)!=0))); /* multiples of 8KB */
	
	LOG(LOG_MISC,"%s%d: %s %s",one?"s":"slot ",slot+1,ls,mapper_get_type_shortname(mapper_get_carttype(slot)));
}

/* type extra configs */
void mapper_extra_configs_reset(int slot)
{
	/* no mapper */
	nomapper_rom_layout_temp[slot]=~0;	/* unmapped */
	nomapper_ram_layout_temp[slot]=~0;	/* no RAM */
	
	/* ascii */
	ascii_mainw_temp[slot]=0;			/* mirrored */
	ascii_board_temp[slot]=0;			/* default */
	
	/* SCC-I */
	scci_ram_layout_temp[slot]=3;		/* 128KB */
}

void mapper_extra_configs_apply(int slot)
{
	/* temp to cur */
	/* no mapper */
	nomapper_rom_layout[slot]=nomapper_rom_layout_temp[slot];
	nomapper_ram_layout[slot]=nomapper_ram_layout_temp[slot];
	
	/* ascii */
	ascii_mainw[slot]=ascii_mainw_temp[slot];
	ascii_board[slot]=ascii_board_temp[slot];
	
	/* SCC-I */
	scci_ram_layout[slot]=scci_ram_layout_temp[slot];
}

void mapper_extra_configs_revert(int slot)
{
	/* cur to temp */
	/* no mapper */
	nomapper_rom_layout_temp[slot]=nomapper_rom_layout[slot];
	nomapper_ram_layout_temp[slot]=nomapper_ram_layout[slot];
	
	/* ascii */
	ascii_mainw_temp[slot]=ascii_mainw[slot];
	ascii_board_temp[slot]=ascii_board[slot];
	
	/* SCC-I */
	scci_ram_layout_temp[slot]=scci_ram_layout[slot];
}

int mapper_extra_configs_differ(int slot,int type)
{
	if (~mapper_get_type_flags(type)&MCF_EXTRA) return FALSE;
	
	switch (type) {
		case CARTTYPE_NOMAPPER:
			if (nomapper_rom_layout_temp[slot]!=nomapper_rom_layout[slot]) return TRUE;
			if (nomapper_ram_layout_temp[slot]!=nomapper_ram_layout[slot]) return TRUE;
			break;
		case CARTTYPE_ASCII:
			if (ascii_mainw_temp[slot]!=ascii_mainw[slot]) return TRUE;
			if (ascii_board_temp[slot]!=ascii_board[slot]) return TRUE;
			break;
		case CARTTYPE_KONAMISCCI:
			if (scci_ram_layout_temp[slot]!=scci_ram_layout[slot]) return TRUE;
			break;
		default: break;
	}
	
	return FALSE;
}

void mapper_get_extra_configs(int slot,int type,u32* p)
{
	if (~mapper_get_type_flags(type)&MCF_EXTRA) return;
	
	/* max 64 bits */
	switch (type) {
		case CARTTYPE_NOMAPPER:
			p[0]=nomapper_rom_layout_temp[slot]; p[1]=nomapper_ram_layout_temp[slot]; break;
		case CARTTYPE_ASCII:
			p[0]=ascii_mainw_temp[slot]; p[1]=ascii_board_temp[slot]; break;
		case CARTTYPE_KONAMISCCI:
			p[0]=scci_ram_layout_temp[slot]; break;
		default: break;
	}
}

void mapper_extra_configs_dialog(HWND callwnd,int slot,int type,int x,int y)
{
	if (~mapper_get_type_flags(type)&MCF_EXTRA) return;
	
	mapper_extra_ref_slot=slot;
	mapper_extra_ref_type=type;
	mapper_extra_ref_pos.x=x; mapper_extra_ref_pos.y=y;
	
	switch (type) {
		case CARTTYPE_NOMAPPER:
			DialogBox(MAIN->module,MAKEINTRESOURCE(IDD_MEDIACART_E_NM),callwnd,mce_nm_dialog); break;
		case CARTTYPE_ASCII:
			DialogBox(MAIN->module,MAKEINTRESOURCE(IDD_MEDIACART_E_A),callwnd,mce_a_dialog); break;
		case CARTTYPE_KONAMISCCI:
			DialogBox(MAIN->module,MAKEINTRESOURCE(IDD_MEDIACART_E_SCCI),callwnd,mce_scci_dialog); break;
		default: break;
	}
}

static u8 __fastcall mapreadnull(u16 a) { return UNMAPPED; }
static void __fastcall mapwritenull(u16 a,u8 v) { ; }

static u8 __fastcall mapreadbios(u16 a) { return bios[a]; }

/* only active when tape is inserted */
static int bh_tapion_offset=0;	static int bh_tapin_offset=0;	static int bh_tapoon_offset=0;	static int bh_tapout_offset=0;
static u8 __fastcall mapreadbioshack(u16 a)
{
	if (bh_tapin_offset==a&&bh_tapin_offset==z80_get_pc()) { tape_read_byte(); return 0xc9; }
	else if (bh_tapout_offset==a&&bh_tapout_offset==z80_get_pc()) { tape_write_byte(); return 0xc9; }
	else if (bh_tapion_offset==a&&bh_tapion_offset==z80_get_pc()) { tape_read_header(); return 0xc9; }
	else if (bh_tapoon_offset==a&&bh_tapoon_offset==z80_get_pc()) { tape_write_header(); return 0xc9; }
	
	return bios[a];
}

static int mapper_patch_bios(u32 crc,u8* data)
{
	/* patch C-BIOS 0.21 PSG bug that's critical since meisei 1.2 (due to emulating the pin 6/7 quirk) */
	/* fixed since C-BIOS 0.22, I'll leave the 0.21 patch in for backwards compatibility */
	if (crc==0x1b3ab47f) {
		/* 2 instances of writing $80 to psg reg 15, change to $8f */
		data[0x152e]=data[0x15f1]=0x8f;
		return TRUE;
	}
	
	return FALSE;
}

static u8 __fastcall mapreadram(u16 a) { return ram[a]; }
static void __fastcall mapwriteram(u16 a,u8 v) { ram[a]=v; }

/* standard cart read (fast) */
static u8 __fastcall mapreadc10(u16 a) { return cartmap[0][0][a&0x1fff]; }	static u8 __fastcall mapreadc20(u16 a) { return cartmap[1][0][a&0x1fff]; }
static u8 __fastcall mapreadc12(u16 a) { return cartmap[0][1][a&0x1fff]; }	static u8 __fastcall mapreadc22(u16 a) { return cartmap[1][1][a&0x1fff]; }
static u8 __fastcall mapreadc14(u16 a) { return cartmap[0][2][a&0x1fff]; }	static u8 __fastcall mapreadc24(u16 a) { return cartmap[1][2][a&0x1fff]; }
static u8 __fastcall mapreadc16(u16 a) { return cartmap[0][3][a&0x1fff]; }	static u8 __fastcall mapreadc26(u16 a) { return cartmap[1][3][a&0x1fff]; }
static u8 __fastcall mapreadc18(u16 a) { return cartmap[0][4][a&0x1fff]; }	static u8 __fastcall mapreadc28(u16 a) { return cartmap[1][4][a&0x1fff]; }
static u8 __fastcall mapreadc1a(u16 a) { return cartmap[0][5][a&0x1fff]; }	static u8 __fastcall mapreadc2a(u16 a) { return cartmap[1][5][a&0x1fff]; }
static u8 __fastcall mapreadc1c(u16 a) { return cartmap[0][6][a&0x1fff]; }	static u8 __fastcall mapreadc2c(u16 a) { return cartmap[1][6][a&0x1fff]; }
static u8 __fastcall mapreadc1e(u16 a) { return cartmap[0][7][a&0x1fff]; }	static u8 __fastcall mapreadc2e(u16 a) { return cartmap[1][7][a&0x1fff]; }

static const fp_mapread lut_mapread[2][8]={
{ mapreadc10,	mapreadc12,	mapreadc14,	mapreadc16,	mapreadc18,	mapreadc1a,	mapreadc1c,	mapreadc1e },
{ mapreadc20,	mapreadc22,	mapreadc24,	mapreadc26,	mapreadc28,	mapreadc2a,	mapreadc2c,	mapreadc2e }
};

/* custom cart read/write */
static fp_mapread_custom mapread_custom_c1;					static fp_mapread_custom mapread_custom_c2;
static u8 __fastcall mapreadc1x(u16 a) { return mapread_custom_c1(0,a); }	static u8 __fastcall mapreadc2x(u16 a) { return mapread_custom_c2(1,a); }
static fp_mapwrite_custom mapwrite_custom_c1;					static fp_mapwrite_custom mapwrite_custom_c2;
static void __fastcall mapwritec1x(u16 a,u8 v) { mapwrite_custom_c1(0,a,v); }	static void __fastcall mapwritec2x(u16 a,u8 v) { mapwrite_custom_c2(1,a,v); }

static void mapsetcustom_read(int slot,int rb,int re,fp_mapread_custom f)
{
	int i; re+=rb;
	if (slot) {	slot=mapcart_getpslot(slot); for (i=rb;i<re;i++) mapread_std[slot][i]=mapreadc2x; mapread_custom_c2=f; }
	else {		slot=mapcart_getpslot(slot); for (i=rb;i<re;i++) mapread_std[slot][i]=mapreadc1x; mapread_custom_c1=f; }
}

static void mapsetcustom_write(int slot,int rb,int re,fp_mapwrite_custom f)
{
	int i; re+=rb;
	if (slot) {	slot=mapcart_getpslot(slot); for (i=rb;i<re;i++) mapwrite_std[slot][i]=mapwritec2x; mapwrite_custom_c2=f; }
	else {		slot=mapcart_getpslot(slot); for (i=rb;i<re;i++) mapwrite_std[slot][i]=mapwritec1x; mapwrite_custom_c1=f; }
}

static int mapcart_getpslot(int slot)
{
	/* ram  c1  c2
	   0    1   2
	   1    2   3
	   2    1   3
	   3    1   2 */
	if (slot) return 3-((ramslot==3)|(ramslot==0));
	else return 1+(ramslot==1);
}


int mapper_init_ramslot(int slot,int size)
{
	fp_mapread mapread_cc[2][8];
	fp_mapwrite mapwrite_cc[2][8];
	int i,j,c;
	
	if (slot==ramslot&&size==ramsize) return FALSE;
	
	/* remember cartridge handlers */
	for (i=0;i<2;i++) {
		c=mapcart_getpslot(i);
		for (j=0;j<8;j++) {
			mapread_cc[i][j]=mapread_std[c][j];
			mapwrite_cc[i][j]=mapwrite_std[c][j];
		}
	}
	
	/* reset standard handlers */
	for (j=1;j<4;j++)
	for (i=0;i<8;i++) {
		mapread_std[j][i]=mapreadnull;
		mapwrite_std[j][i]=mapwritenull;
	}
	/* slot 0: bios */
	for (i=4;i<8;i++) mapread_std[0][i]=mapreadbios;
	
	/* relocate cartridge handlers */
	ramslot=slot;
	for (i=0;i<2;i++) {
		c=mapcart_getpslot(i);
		for (j=0;j<8;j++) {
			mapread_std[c][j]=mapread_cc[i][j];
			mapwrite_std[c][j]=mapwrite_cc[i][j];
		}
	}
	
	/* set ram slot */
	ramsize=size;
	c=size>>3;
	
	if (c!=8) memset(ram,RAMINIT,0x10000-(0x200<<c));
	
	for (i=8-c;i<8;i++) {
		mapread_std[slot][i]=mapreadram;
		mapwrite_std[slot][i]=mapwriteram;
	}
	
	mapper_refresh_pslot_read();
	mapper_refresh_pslot_write();
	
	return TRUE;
}

void mapper_init(void)
{
	int i,j;
	int ramslot_set,ramsize_set;
	
#if 0
	/* check mapper table for duplicates */
	u32 c; i=0;
	for (;;) {
		if (lutmaptype[i].type==CARTTYPE_MAX) break;
		c=lutmaptype[i].crc;
		
		j=0;
		for (;;) {
			if (lutmaptype[j].type==CARTTYPE_MAX) break;
			if (c==lutmaptype[j].crc&&i!=j) printf("dup %d %d - %08X\n",i,j,c);
			j++;
		}
		
		i++;
	}
#endif
	
	/* open default bios */
	file_setfile(&file->appdir,DEFAULT_BIOS,NULL,NULL);
	if (!file_open()||file->size!=0x8000||!file_read(default_bios,0x8000)) {
		file_close();
		LOG(LOG_MISC|LOG_ERROR,"Couldn't open default BIOS ROM.\nEnsure that %s\nis in the application directory.",DEFAULT_BIOS); exit(1);
	}
	default_bioscrc=file->crc32;
	MEM_CREATE(fn_default_bios,strlen(file->filename)+1);
	strcpy(fn_default_bios,file->filename);
	file_close();
	
	mapper_patch_bios(default_bioscrc,default_bios);
	
	memset(dummy_u,UNMAPPED,0x2000);
	memset(dummy_ff,0xff,0x2000);
	
	autodetect_rom=TRUE; i=settings_get_yesnoauto(SETTINGS_AUTODETECTROM); if (i==FALSE||i==TRUE) autodetect_rom=i;
	
	scc_init_volume();
	
	/* ram settings */
	ramsize_set=DEFAULT_RAMSIZE;
	if (SETTINGS_GET_INT(settings_info(SETTINGS_RAMSIZE),&i)) {
		if (i!=0&&i!=8&&i!=16&&i!=32&&i!=64) i=DEFAULT_RAMSIZE;
		ramsize_set=i;
	}
	
	ramslot_set=DEFAULT_RAMSLOT;
	if (SETTINGS_GET_INT(settings_info(SETTINGS_RAMSLOT),&i)) {
		CLAMP(i,0,3);
		ramslot_set=i;
	}
	
	/* get bios filename */
	n_bios=NULL; SETTINGS_GET_STRING(settings_info(SETTINGS_BIOS),&n_bios);
	if (n_bios==NULL||strlen(n_bios)==0) mapper_set_bios_default();
	else {
		file_setfile(&file->biosdir,n_bios,NULL,NULL);
		MEM_CREATE(fn_bios,strlen(file->filename)+1);
		strcpy(fn_bios,file->filename);
	}
	
	/* set standard handlers */
	for (j=0;j<4;j++)
	for (i=0;i<8;i++) {
		mapread_std[j][i]=mapreadnull;
		mapwrite_std[j][i]=mapwritenull;
	}
	/* slot 0: bios */
	for (i=0;i<8;i++) mapread_std[0][i]=mapreadbios;
	
	/* carts */
	for (j=0;j<2;j++)
	for (i=0;i<ROM_MAXPAGES;i++) {
		cart[j][i]=dummy_u;
	}
	cart[0][ROMPAGE_DUMMY_U]=cart[1][ROMPAGE_DUMMY_U]=dummy_u;
	cart[0][ROMPAGE_DUMMY_FF]=cart[1][ROMPAGE_DUMMY_FF]=dummy_ff;
	
	/* ram */
	ramsize=-1;
	if (ramsize_set==64&&ramslot_set==0) ramsize_set=32; /* first half of slot 0 must be bios */
	mapper_init_ramslot(ramslot_set,ramsize_set);
}

void mapper_clean(void)
{
	mapper_close_cart(0);
	mapper_close_cart(1);
	
	MEM_CLEAN(fn_default_bios);
	MEM_CLEAN(fn_bios); MEM_CLEAN(n_bios);
}


/* bios */
static void mapper_patch_bios_comply_standards(void)
{
#if COMPLY_STANDARDS_TEST
	bios[6]=COMPLY_STANDARDS_VDPR;
	bios[7]=COMPLY_STANDARDS_VDPW;
#endif
	;
}

static void mapper_set_bios_default(void)
{
	memset(bios+0x8000,UNMAPPED,0x8000);
	memcpy(bios,default_bios,0x8000);
	mapper_patch_bios_comply_standards();
	bioscrc=default_bioscrc;
	
	MEM_CLEAN(n_bios);
	MEM_CREATE(n_bios,strlen(DEFAULT_BIOS)+1);
	sprintf(n_bios,DEFAULT_BIOS);
	
	MEM_CLEAN(fn_bios);
	MEM_CREATE(fn_bios,strlen(fn_default_bios)+1);
	strcpy(fn_bios,fn_default_bios);
	
	file_setfile(NULL,fn_bios,NULL,NULL);
	file_setdirectory(&file->biosdir);
}

void mapper_open_bios(const char* f)
{
	if (f==NULL) file_setfile(&file->biosdir,n_bios,NULL,"rom");
	else file_setfile(NULL,f,NULL,"rom");
	
	memset(bios,UNMAPPED,0x10000);
	
	if (!file_open()||file->size>0x10000||file->size<2||!file_read(bios,file->size)) {
		file_close();
		LOG(LOG_MISC|LOG_WARNING,"couldn't open BIOS, reverting to default\n");
		mapper_set_bios_default();
	}
	else {
		/* user specified */
		int len=strlen(file->name)+1;
		int ext=FALSE;
		if (file->ext&&strlen(file->ext)) {
			ext=TRUE;
			len+=strlen(file->ext)+1;
		}
		bioscrc=file->crc32;
		
		mapper_patch_bios(bioscrc,bios);
		
		MEM_CLEAN(n_bios);
		MEM_CREATE(n_bios,len);
		strcpy(n_bios,file->name);
		if (ext) {
			strcat(n_bios,".");
			strcat(n_bios,file->ext);
		}
		
		MEM_CLEAN(fn_bios);
		MEM_CREATE(fn_bios,strlen(file->filename)+1);
		strcpy(fn_bios,file->filename);
		
		file_close();
		file_setdirectory(&file->biosdir);
		
		mapper_patch_bios_comply_standards();
	}
	
	if (cont_get_auto_region()) {
		/* autodetect keyboard region */
		int i=cont_get_region();
		
		/* info in bios */
		switch (bios[0x2c]&0xf) {
			case 0: {
				/* japanese/korean */
				u16 a=bios[5]<<8|bios[4]; /* CGTABL */
				u8 e[0x10]; memset(e,0,0x10);
				
				/* korean characters $fc and $fd are empty */
				if (a<=0x3800&&!memcmp(bios+a+0x7e0,e,0x10)) i=CONT_REGION_KOREAN;
				else i=CONT_REGION_JAPANESE;
				
				break;
			}
			
			case 3: i=CONT_REGION_UK; break;	/* UK */
			case 1:								/* international */
			case 2:								/* french */
			case 4:								/* DIN (german) */
			case 6:								/* spanish */
			default: /* unknown */
				i=CONT_REGION_INTERNATIONAL; break;
		}
		
		cont_set_region(i);
	}
}

void mapper_update_bios_hack(void)
{
	int i;
	
	if (tape_is_inserted()) {
		/* hacked */
		for (i=0;i<4;i++) mapread_std[0][i]=mapreadbioshack;
		
		/* set offsets */
		i=bios[0xe2]|bios[0xe3]<<8;
		if (bios[0xe1]==0xc3&&i<0x4000) bh_tapion_offset=i;
		else bh_tapion_offset=0xe1;
		
		i=bios[0xe5]|bios[0xe6]<<8;
		if (bios[0xe4]==0xc3&&i<0x4000) bh_tapin_offset=i;
		else bh_tapin_offset=0xe4;
		
		i=bios[0xeb]|bios[0xec]<<8;
		if (bios[0xea]==0xc3&&i<0x4000) bh_tapoon_offset=i;
		else bh_tapoon_offset=0xea;
		
		i=bios[0xee]|bios[0xef]<<8;
		if (bios[0xed]==0xc3&&i<0x4000) bh_tapout_offset=i;
		else bh_tapout_offset=0xed;
	}
	else {
		/* normal */
		for (i=0;i<4;i++) mapread_std[0][i]=mapreadbios;
	}
}


/* carts */
int mapper_open_cart(int slot,const char* f,int auto_patch)
{
	#define ROM_CLOSE(x)					\
		file_patch_close(); file_close();	\
		if (!x) LOG(LOG_MISC|LOG_WARNING,"slot %d: couldn't open file\n",slot+1); \
		return x
	
	int i=TRUE;
	u32 crc,crc_p=0;
	char n[STRING_SIZE]={0};
	
	slot&=1;
	
	/* open file, just to get checksum before patching */
	file_setfile(NULL,f,NULL,"rom");
	if (!file_open()) { ROM_CLOSE(FALSE); }
	crc=file->crc32;
	file_close();
	
	if (auto_patch) {
		/* open optional ips (ones that enlarge filesize are unsupported in this case) */
		strcpy(n,file->name);
		file_setfile(&file->patchdir,n,"ips",NULL);
		if (!file_open()) {
			file_close();
			file_setfile(&file->patchdir,n,"zip","ips");
			if (!file_open()) { i=FALSE; file_close(); }
		}
		crc_p=file->crc32;
		if (i&(crc_p!=crc)) {
			LOG(LOG_MISC,"slot %d: applying patch (%sIPS)\n",slot+1,file->is_zip?"zipped ":"");
			if (!file_patch_init()) { file_patch_close(); crc_p=0; }
		}
		else crc_p=0;
		file_close();
		
		/* set to rom file again */
		file_setfile(NULL,f,NULL,"rom");
	}
	
	if (file_open()) {
		char* temp_fn;
		u32 temp_crc32;
		u32 temp_size;
		
		int pages=(file->size>>13)+((file->size&0x1fff)!=0);
		if (pages>ROM_MAXPAGES) { ROM_CLOSE(FALSE); }
		
		/* read rom */
		for (i=0;i<pages;i++) {
			MEM_CREATE(tempfile[i],0x2000); memset(tempfile[i],UNMAPPED,0x2000);
			if (!file_read(tempfile[i],0x2000)) {
				i++;
				while (i--) { MEM_CLEAN(tempfile[i]); }
				
				ROM_CLOSE(FALSE);
			}
		}
		
		/* success */
		MEM_CREATE(temp_fn,strlen(file->filename)+1);
		strcpy(temp_fn,file->filename);
		temp_crc32=file->crc32;
		temp_size=file->size;
		
		file_close();
		mapper_close_cart(slot); /* close previous */
		
		cartfile[slot]=temp_fn;
		cartcrc[slot]=temp_crc32;
		if (crc_p) cartcrc[slot]=(cartcrc[slot]<<1|(cartcrc[slot]>>31&1))^crc_p; /* combine with patch crc */
		
		/* set cart */
		for (i=0;i<pages;i++) cart[slot][i]=tempfile[i];
		for (cartmask[slot]=1;cartmask[slot]<pages;cartmask[slot]<<=1) { ; }
		cartmask[slot]--; cartsize[slot]=temp_size; cartpages[slot]=pages;
	}
	else { ROM_CLOSE(FALSE); }
	
	ROM_CLOSE(TRUE);
}

static void mapper_clean_custom(slot)
{
	if (mapclean[slot]==mapclean_nothing&&cartsramsize[slot]) {
		/* default sram save */
		sram_save(slot,cartsram[slot],cartsramsize[slot],NULL);
	}
	else mapclean[slot](slot);
	
	mapclean[slot]=mapclean_nothing;
	mapgetstatesize[slot]=mapgetstatesize_nothing;
	mapsavestate[slot]=mapsavestate_nothing;
	maploadstatecur[slot]=maploadstatecur_nothing;
	maploadstate[slot]=maploadstate_nothing;
	mapsetbc[slot]=mapsetbc_nothing;
	mapstream[slot]=mapstream_nothing;
	mapnf[slot]=mapnf_nothing;
	mapef[slot]=mapef_nothing;
}

void mapper_close_cart(int slot)
{
	int i;
	
	slot&=1;
	mapper_clean_custom(slot);
	
	for (i=0;i<ROM_MAXPAGES;i++) {
		if (cart[slot][i]!=dummy_u&&cart[slot][i]!=dummy_ff) { MEM_CLEAN(cart[slot][i]); }
		cart[slot][i]=dummy_u;
	}
	
	MEM_CLEAN(cartfile[slot]);
	cartcrc[slot]=cartpages[slot]=cartmask[slot]=cartsize[slot]=cartsramsize[slot]=0;
	cartsram[slot]=NULL;
}

u32 mapper_autodetect_cart(int slot,const char* fn)
{
	int i,pages;
	u8* f=NULL;
	u32 ret;
	
	mapper_extra_configs_reset(slot);
	
	/* open */
	file_setfile(NULL,fn,NULL,"rom");
	if (!file_open()||file->size>ROM_MAXSIZE) {
		file_close();
		return CARTTYPE_NOMAPPER;
	}
	
	pages=(file->size>>13)+((file->size&0x1fff)!=0);
	MEM_CREATE_N(f,pages*0x2000);
	
	if (!file_read(f,file->size)) {
		MEM_CLEAN(f); file_close();
		return CARTTYPE_NOMAPPER;
	}
	file_close();
	
	/* set extra configs to default */
	/* (not needed with ascii or SCC-I) */
	nomapper_rom_config_default(slot,pages,-1);
	
	/* try parse, {x} in filename where x=maptype shortname */
	if (file->name&&strlen(file->name)) {
		int begin,end,len=strlen(file->name),found=0;
		
		for (end=len-1;end;end--) if (file->name[end]=='}') { found|=1; break; }
		for (begin=end;begin>=0;begin--) if (file->name[begin]=='{') { found|=2; break; }
		begin++;
		
		if (begin<end&&found==3&&(end-begin)<0x10) {
			char shortname_fn[0x10];
			memset(shortname_fn,0,0x10);
			memcpy(shortname_fn,file->name+begin,end-begin);
			
			/* special case for ascii */
			if (!stricmp(shortname_fn,"ASCII8"))  { ascii_board_temp[slot]=A_08; return CARTTYPE_ASCII; }
			if (!stricmp(shortname_fn,"ASCII16")) { ascii_board_temp[slot]=A_16; return CARTTYPE_ASCII; }
			
			for (i=0;i<CARTTYPE_MAX;i++)
			if (!stricmp(shortname_fn,mapper_get_type_shortname(i))) {
				MEM_CLEAN(f);
				return i;
			}
		}
	}
	
	if (!autodetect_rom) return CARTTYPE_NOMAPPER;
	
	/* try lookuptable */
	for (i=0;lutmaptype[i].type!=CARTTYPE_MAX;i++) {
		if (lutmaptype[i].crc==file->crc32&&lutmaptype[i].size==file->size) {
			MEM_CLEAN(f);
			
			/* copy extra configs */
			switch (lutmaptype[i].type) {
				case CARTTYPE_NOMAPPER:
					nomapper_rom_layout_temp[slot]=lutmaptype[i].extra1;
					nomapper_ram_layout_temp[slot]=lutmaptype[i].extra2;
					break;
				case CARTTYPE_ASCII:
					ascii_mainw_temp[slot]=lutmaptype[i].extra1;
					ascii_board_temp[slot]=lutmaptype[i].extra2;
					break;
				case CARTTYPE_KONAMISCCI:
					scci_ram_layout_temp[slot]=lutmaptype[i].extra1;
					break;
				default: break;
			}
			
			return lutmaptype[i].type;
		}
	}
	
	if (file->size&0x1fff) memset(f+file->size,0xff,0x2000-(file->size&0x1fff));
	ret=CARTTYPE_NOMAPPER;
	
	/* guess type */
	switch (pages) {
		/* small ROMs are almost always detected correctly */
		
		/* 8KB/16KB */
		case 1: case 2: {
			u16 start=f[3]<<8|f[2];
			
			/* start address of $0000: call address in the $4000 region: $4000, else $8000 */
			if (start==0) {
				if ((f[5]&0xc0)==0x40) nomapper_rom_config_default(slot,pages,0x4000);
				else nomapper_rom_config_default(slot,pages,0x8000);
			}
			
			/* start address in the $8000 region: $8000, else default */
			else if ((start&0xc000)==0x8000) nomapper_rom_config_default(slot,pages,0x8000);
			
			break;
		}
		
		/* 32KB */
		case 4:
			/* no "AB" at $0000, but "AB" at $4000 */
			if (f[0]!='A'&&f[1]!='B'&&f[0x4000]=='A'&&f[0x4001]=='B') {
				u16 start=f[0x4003]<<8|f[0x4002];
				
				/* start address of $0000 and call address in the $4000 region, or start address outside the $8000 region: $0000, else default */
				if ((start==0&&(f[0x4005]&0xc0)==0x40)||start<0x8000||start>=0xc000) nomapper_rom_config_default(slot,4,0x0000);
			}
			
			break;
		
		/* 48KB */
		case 6:
			/* "AB" at $0000, but no "AB" at $4000, not "AB": $0000 */
			if (f[0]=='A'&&f[1]=='B'&&f[0x4000]!='A'&&f[0x4001]!='B') nomapper_rom_config_default(slot,6,0x4000);
			else nomapper_rom_config_default(slot,6,0x0000);
			
			break;
		
		/* 384KB, only game with that size is R-Type */
		case 0x30: ret=CARTTYPE_IREMTAMS1; break;
		
		/* other */
		default:
			/* disk sizes (360KB and 720KB are most common) */
			if (file->size==163840||file->size==184320||file->size==327680||file->size==368640||file->size==655360||file->size==737280||file->size==1474560) ret=CARTTYPE_DSK2ROM;
			
			/* 64KB and no "AB" at start, or smaller than 64KB: no mapper */
			else if (pages<8||(pages==8&&f[0]!='A'&&f[1]!='B')) break;
			
			/* megarom */
			else {
				/* Idea from fMSX, assume that games write to standard mapper addresses
				(which almost all of them do) and count ld (address),a ($32) occurences.
				Excluding unique or SRAM types, it guesses correctly ~95% of the time. */
				
				/* incomplete list of tested roms:
				- set -				- tested -	- exceptions -
				hacked tape-to-A16	126/128		MSX1: Bubbler, Spirits  --  MSX2: - (they're all MSX1)
				homebrew			20/23		MSX1: Monster Hunter (English, Spanish) (works ok, just no SCC), Mr. Mole (SCC version)  --  MSX2: -
				official ASCII 8	142/152		MSX1: Bomber King, MSX English-Japanese Dictionary  --  MSX2: Aoki Ookami to Shiroki Mejika - Genchou Hishi, Genghis Khan, Fleet Commander II, Japanese MSX-Write II, Mad Rider, Nobunaga no Yabou - Sengoku Gunyuu Den, Nobunaga no Yabou - Zenkokuban, Royal Blood
				official ASCII 16	54/59		MSX1: The Black Onyx II  --  MSX2: Daisenryaku, Ikari, Penguin-Kun Wars 2, Zoids - Chuuoudai Riku no Tatakai
				official Konami's	48/48		-
				Zemina A/K clones	12/12		- */
				#define GUESS_AS8	0 /* $6000, $6800, $7000, $7800 */
				#define GUESS_KOV	1 /* $6000, $8000, $a000 */
				#define GUESS_A16	2 /* $6000, $7000 */
				#define GUESS_SCC	3 /* $5000, $7000, $9000, $b000 */
				int type[]={0,0,0,0};
				int guess=GUESS_AS8;
				
				for (i=0;i<(file->size-2);i++) {
					if (f[i]==0x32)
					switch (f[i+2]<<8|f[i+1]) {
						case 0x6000: type[GUESS_AS8]+=3; type[GUESS_KOV]+=2; type[GUESS_A16]+=4; break;
						case 0x7000: type[GUESS_AS8]+=3; type[GUESS_A16]+=4; type[GUESS_SCC]+=2; break;
						case 0x6800: case 0x7800: type[GUESS_AS8]+=3; break;
						case 0x8000: case 0xa000: type[GUESS_KOV]+=3; break;
						case 0x5000: case 0x9000: case 0xb000: type[GUESS_SCC]+=2; break;
						case 0x77ff: type[GUESS_A16]+=1; break; /* used sometimes by A16 */
						default: break;
					}
				}
				
				/* check winner */
				for (i=1;i<4;i++) {
					if (type[i]>type[guess]) guess=i;
				}
				
				switch (guess) {
					case GUESS_KOV: ret=CARTTYPE_KONAMIVRC; break;
					case GUESS_A16: ret=CARTTYPE_ASCII; ascii_board_temp[slot]=A_16; break;
					case GUESS_SCC: ret=CARTTYPE_KONAMISCC; break;
					default: ret=CARTTYPE_ASCII; ascii_board_temp[slot]=A_08; break;
				}
			}
			
			break;
	}
	
	MEM_CLEAN(f);
	return ret;
}

void mapper_init_cart(int slot)
{
	int i,p;
	
	slot&=1;
	mapper_clean_custom(slot);
	p=mapcart_getpslot(slot);
	
	for (i=0;i<8;i++) {
		/* default read handlers */
		mapread_std[p][i]=lut_mapread[slot][i];
		
		/* default write handlers */
		mapwrite_std[p][i]=mapwritenull;
		
		/* empty memmap */
		cartbank[slot][i]=ROMPAGE_DUMMY_U;
		cartmap[slot][i]=dummy_u;
	}
	
	maptype[carttype[slot]].init(slot);
	refresh_cb(slot);
	
	reverse_invalidate();
}

static __inline__ void refresh_cb(int slot)
{
	/* refresh cart banks */
	cartmap[slot][0]=cart[slot][cartbank[slot][0]];
	cartmap[slot][1]=cart[slot][cartbank[slot][1]];
	cartmap[slot][2]=cart[slot][cartbank[slot][2]];
	cartmap[slot][3]=cart[slot][cartbank[slot][3]];
	cartmap[slot][4]=cart[slot][cartbank[slot][4]];
	cartmap[slot][5]=cart[slot][cartbank[slot][5]];
	cartmap[slot][6]=cart[slot][cartbank[slot][6]];
	cartmap[slot][7]=cart[slot][cartbank[slot][7]];
}

void __fastcall mapper_refresh_cb(int slot) { refresh_cb(slot&1); }


/* primary slot register */
void __fastcall mapper_refresh_pslot_read(void)
{
	mapread[0]=mapread_std[pslot&3][0];			mapread[1]=mapread_std[pslot&3][1];
	mapread[2]=mapread_std[pslot>>2&3][2];		mapread[3]=mapread_std[pslot>>2&3][3];
	mapread[4]=mapread_std[pslot>>4&3][4];		mapread[5]=mapread_std[pslot>>4&3][5];
	mapread[6]=mapread_std[pslot>>6&3][6];		mapread[7]=mapread_std[pslot>>6&3][7];
}

void __fastcall mapper_refresh_pslot_write(void)
{
	mapwrite[0]=mapwrite_std[pslot&3][0];		mapwrite[1]=mapwrite_std[pslot&3][1];
	mapwrite[2]=mapwrite_std[pslot>>2&3][2];	mapwrite[3]=mapwrite_std[pslot>>2&3][3];
	mapwrite[4]=mapwrite_std[pslot>>4&3][4];	mapwrite[5]=mapwrite_std[pslot>>4&3][5];
	mapwrite[6]=mapwrite_std[pslot>>6&3][6];	mapwrite[7]=mapwrite_std[pslot>>6&3][7];
}

void __fastcall mapper_write_pslot(u8 v)
{
	pslot=v;
	
	mapper_refresh_pslot_read();
	mapper_refresh_pslot_write();
}

u8 __fastcall mapper_read_pslot(void) { return pslot; }


/* SRAM */
static int sram_netplay[2]={0,0};

static int sram_load(int slot,u8** d,u32 size,const char* fnc,u8 fill)
{
	int ret=FALSE;
	char fn[STRING_SIZE];
	
	if (size==0) return FALSE;
	
	MEM_CREATE_N(*d,size);
	memset(*d,fill,size);
	cartsram[slot]=*d; cartsramsize[slot]=size;
	
	if (!fnc&&!mapper_get_file(slot)) return FALSE; /* empty slot */
	
	if (netplay_is_active()) {
		/* don't load (or save) if netplay */
		LOG(LOG_MISC|LOG_WARNING|LOG_TYPE(LT_NETPLAYSRAM),"SRAM is volatile during netplay");
		sram_netplay[slot]=TRUE;
		return FALSE;
	}
	
	if (fnc) file_setfile(&file->batterydir,fnc,NULL,NULL);
	else {
		file_setfile(NULL,mapper_get_file(slot),NULL,NULL);
		strcpy(fn,file->name);
		file_setfile(&file->batterydir,fn,"sav",NULL);
	}
	
	if (!file_accessible()) {
		/* SRAM file didn't exist yet */
		if (!file_save()||!file_write(*d,size)) LOG(LOG_MISC|LOG_WARNING,"couldn't init slot %d battery backed SRAM\n",slot+1);
	}
	
	/* load */
	else if (!file_open()||file->size!=size||!file_read(*d,size)) {
		LOG(LOG_MISC|LOG_WARNING,"couldn't load slot %d battery backed SRAM\n",slot+1);
		memset(*d,fill,size);
	}
	
	else ret=TRUE;
	
	file_close();
	
	MEM_CREATE(cartsramcopy[slot],size);
	memcpy(cartsramcopy[slot],*d,size);
	
	return ret;
}

static void sram_save(int slot,u8* d,u32 size,const char* fnc)
{
	int err=FALSE;
	char fn[STRING_SIZE];
	
	if (sram_netplay[slot]||d==NULL||size==0||(!fnc&&!mapper_get_file(slot))||(cartsramcopy[slot]&&memcmp(cartsramcopy[slot],d,size)==0)) err=TRUE;
	
	sram_netplay[slot]=FALSE;
	MEM_CLEAN(cartsramcopy[slot]);
	
	cartsramsize[slot]=0;
	if (err) { MEM_CLEAN(d); return; }
	
	if (fnc) file_setfile(&file->batterydir,fnc,NULL,NULL);
	else {
		file_setfile(NULL,mapper_get_file(slot),NULL,NULL);
		strcpy(fn,file->name);
		file_setfile(&file->batterydir,fn,"sav",NULL);
	}
	
	/* save */
	if (!file_save()||!file_write(d,size)) LOG(LOG_MISC|LOG_WARNING,"couldn't save slot %d battery backed SRAM\n",slot+1);
	file_close();
	
	/* clean */
	MEM_CLEAN(d);
}


/* state				size
ram slot				1
ram size				1
ram						0x10000
pslot					1

cart banknums			2*2*8 (32)

==						0x10000+35

type specific			?

*/
/* version derivatives: mapper*.c, am29f040b.c, sample.c, scc.c */
#define STATE_VERSION	3
/* version history:
1: initial
2: SRAM crash bugfix + changes in scc.c
3: configurable RAM size/slot, cartridge extra configs (ascii, no mapper, scc-i), changed order
*/
#define STATE_SIZE		(0x10000+35)

int __fastcall mapper_state_get_version(void)
{
	return STATE_VERSION;
}

int __fastcall mapper_state_get_size(void)
{
	return STATE_SIZE+cartsramsize[0]+cartsramsize[1]+mapgetstatesize[0](0)+mapgetstatesize[1](1);
}

/* shortcuts */
#define SCB(x,y) STATE_SAVE_2(cartbank[x][y])
#define LCB(x,y) STATE_LOAD_2(cartbank[x][y])

/* save */
void __fastcall mapper_state_save(u8** s)
{
	/* msx ram, just save the whole 64KB */
	STATE_SAVE_1(ramslot);
	STATE_SAVE_1(ramsize);
	STATE_SAVE_C(ram,0x10000);
	STATE_SAVE_1(pslot);
	
	/* cartridge banks */
	SCB(0,0); SCB(0,1); SCB(0,2); SCB(0,3); SCB(0,4); SCB(0,5); SCB(0,6); SCB(0,7);
	SCB(1,0); SCB(1,1); SCB(1,2); SCB(1,3); SCB(1,4); SCB(1,5); SCB(1,6); SCB(1,7);
	
	/* cartridge custom */
	mapsavestate[0](0,s); if (cartsramsize[0]) { STATE_SAVE_C(cartsram[0],cartsramsize[0]); }
	mapsavestate[1](1,s); if (cartsramsize[1]) { STATE_SAVE_C(cartsram[1],cartsramsize[1]); }
}

/* load */
void __fastcall mapper_state_load_cur(u8** s)
{
	int i;
	
	mel_error=0;
	
	/* msx ram */
	STATE_LOAD_1(i); if (i!=ramslot) mel_error|=3;
	STATE_LOAD_1(i); if (i!=ramsize) mel_error|=2;
	STATE_LOAD_C(ram,0x10000);
	STATE_LOAD_1(pslot);
	
	/* cartridge banks */
	LCB(0,0); LCB(0,1); LCB(0,2); LCB(0,3); LCB(0,4); LCB(0,5); LCB(0,6); LCB(0,7);
	LCB(1,0); LCB(1,1); LCB(1,2); LCB(1,3); LCB(1,4); LCB(1,5); LCB(1,6); LCB(1,7);
	
	/* cartridge custom */
	maploadstatecur[0](0,s); if (cartsramsize[0]&&!loadstate_skip_sram[0]) { STATE_LOAD_C(cartsram[0],cartsramsize[0]); }
	maploadstatecur[1](1,s); if (cartsramsize[1]&&!loadstate_skip_sram[1]) { STATE_LOAD_C(cartsram[1],cartsramsize[1]); }
	
	loadstate_skip_sram[0]=loadstate_skip_sram[1]=FALSE;
}

void __fastcall mapper_state_load_12(int v,u8** s)
{
	/* version 1,2 */
	
	/* msx ram */
	STATE_LOAD_C(ram,0x10000);
	STATE_LOAD_1(pslot);
	
	/* cartridge banks */
	LCB(0,0); LCB(0,1); LCB(0,2); LCB(0,3); LCB(0,4); LCB(0,5); LCB(0,6); LCB(0,7);
	LCB(1,0); LCB(1,1); LCB(1,2); LCB(1,3); LCB(1,4); LCB(1,5); LCB(1,6); LCB(1,7);
	
	/* cartridge custom */
	if (cartsramsize[0]) { STATE_LOAD_C(cartsram[0],cartsramsize[0]); }
	if (cartsramsize[1]) { STATE_LOAD_C(cartsram[1],cartsramsize[1]); }
	
	maploadstate[0](0,v,s);
	maploadstate[1](1,v,s);
}

int __fastcall mapper_state_load(int v,u8** s)
{
	mel_error=0;
	
	switch (v) {
		case 1: case 2:
			mapper_state_load_12(v,s);
			break;
		case STATE_VERSION:
			mapper_state_load_cur(s);
			break;
		
		default: return FALSE;
	}
	
	return TRUE;
}

#undef SCB
#undef LCB
