/* miscellaneous mappers */

/* --- dB-Soft Cross Blaim (single game), discrete logic, board: (no label) */
static void __fastcall mapwrite_crossblaim(int slot,u16 a,u8 v)
{
	/* 2nd page switchable at $0000-$ffff */
	/* xxxxxxrb: x=don't care, r=romchip, b=bank */
	if (v&2) {
		/* 32KB ROM 1 */
		cartbank[slot][0]=cartbank[slot][1]=cartbank[slot][6]=cartbank[slot][7]=ROMPAGE_DUMMY_U;
		cartbank[slot][4]=v<<1&7&cartmask[slot];
		cartbank[slot][5]=(v<<1&7&cartmask[slot])|1;
	}
	else {
		/* 32KB ROM 0, fixed, and mirrored to page 0 and page 3 */
		cartbank[slot][0]=cartbank[slot][4]=cartbank[slot][6]=2&cartmask[slot];
		cartbank[slot][1]=cartbank[slot][5]=cartbank[slot][7]=3&cartmask[slot];
	}
	
	refresh_cb(slot);
}

static void mapinit_crossblaim(int slot)
{
	/* two 32KB romchips. 2 pages of 16KB at $4000-$bfff */
	cartbank[slot][2]=0; cartbank[slot][3]=1; /* bank 0 fixed to page 1 */
	cartbank[slot][0]=cartbank[slot][4]=cartbank[slot][6]=2&cartmask[slot];
	cartbank[slot][1]=cartbank[slot][5]=cartbank[slot][7]=3&cartmask[slot];
	
	mapsetcustom_write(slot,0,8,mapwrite_crossblaim);
}





/* --- MicroCabin Harry Fox (single game: Harry Fox - Yuki no Maou Hen), discrete logic, board: DSK-1 */
static void __fastcall mapwrite_harryfox(int slot,u16 a,u8 v)
{
	/* page 1 switchable at $6xxx, page 2 switchable at $7xxx, bit 0:select 32KB ROM */
	int p=a>>11&2;
	int b=((v<<2&4)|p)&cartmask[slot];
	p+=2;
	
	cartbank[slot][p++]=b++;
	cartbank[slot][p]=b;
	refresh_cb(slot);
}

static void mapinit_harryfox(int slot)
{
	int i;
	
	/* two 32KB romchips. 2 pages of 16KB at $4000-$bfff, init to 0,1 */
	for (i=0;i<4;i++) cartbank[slot][i+2]=i&cartmask[slot];
	
	mapsetcustom_write(slot,3,1,mapwrite_harryfox);
}





/* --- Irem TAM-S1 (single game: R-Type), mapper chip: Irem TAM-S1, board: MSX-004 */
static void __fastcall mapwrite_irem(int slot,u16 a,u8 v)
{
	/* 2nd page switchable at $4000-$7fff */
	/* xxxrbbbb: x=don't care, b=bank, r=romchip (xxx1xbbb for 2nd ROM) */
	int mask=(v&0x10)+0x1e;
	
	cartbank[slot][2]=mask;			cartbank[slot][3]=mask|1;
	cartbank[slot][4]=v<<1&mask;	cartbank[slot][5]=(v<<1&mask)|1;
	refresh_cb(slot);
}

static void mapinit_irem(int slot)
{
	/* 2 romchips: one 256KB, one 128KB. 2 pages of 16KB at $4000-$bfff, 1st page fixed to last bank of current romchip */
	cartbank[slot][2]=0x1e;	cartbank[slot][3]=0x1f;
	cartbank[slot][4]=0;	cartbank[slot][5]=1;
	
	mapsetcustom_write(slot,2,2,mapwrite_irem);
}





/* --- Al-Alamiah Al-Qur'an (single program), mapper chip: Yamaha XE297A0 (protection is separate via resistor network), board: GCMK-16X, 2 ROM chips */
/* (without having been hardware reverse engineered, and only 1 program to test it with, I'm not sure if this implementation is accurate) */
static u8 quran_lutp[0x100];
static int quran_m1[2];

/* i/o */
static u8 __fastcall mapread_quran(int slot,u16 a)
{
	if (quran_m1[slot]) return quran_lutp[cartmap[slot][a>>13][a&0x1fff]];
	
	/* M1 pulse (pin 9) disables protection */
	quran_m1[slot]=a==z80_get_pc();
	return cartmap[slot][a>>13][a&0x1fff];
}

static void __fastcall mapwrite_quran(int slot,u16 a,u8 v)
{
	if (a&0x1000) {
		/* all pages switchable at $5000-$5fff */
		cartbank[slot][(a>>10&3)+2]=v&cartmask[slot];
		refresh_cb(slot);
	}
}

/* state					size
m1							1

==							1
*/
#define STATE_VERSION_QURAN	3 /* mapper.c */
/* version history:
1: (didn't exist yet)
2: (didn't exist yet)
3: initial
*/
#define STATE_SIZE_QURAN	1

static int __fastcall mapgetstatesize_quran(int slot)
{
	return STATE_SIZE_QURAN;
}

/* save */
static void __fastcall mapsavestate_quran(int slot,u8** s)
{
	STATE_SAVE_1(quran_m1[slot]);
}

/* load */
static void __fastcall maploadstatecur_quran(int slot,u8** s)
{
	STATE_LOAD_1(quran_m1[slot]);
}

static int __fastcall maploadstate_quran(int slot,int v,u8** s)
{
	switch (v) {
		case STATE_VERSION_QURAN:
			maploadstatecur_quran(slot,s);
			break;
		
		default: return FALSE;
	}
	
	return TRUE;
}

/* init */
static void mapinit_quran(int slot)
{
	int i;
	
	/* 4 pages of 8KB at $4000-$bfff, init to 0 */
	for (i=2;i<6;i++) cartbank[slot][i]=0;
	
	mapgetstatesize[slot]=mapgetstatesize_quran;
	mapsavestate[slot]=mapsavestate_quran;
	maploadstatecur[slot]=maploadstatecur_quran;
	maploadstate[slot]=maploadstate_quran;
	
	mapsetcustom_write(slot,2,1,mapwrite_quran);
	mapsetcustom_read(slot,2,4,mapread_quran);
	quran_m1[slot]=FALSE;
	/* protection uses a simple rotation on databus, some lines inverted:
		D0   D4			D4   D5
		D1 ~ D3			D5 ~ D2
		D2 ~ D6			D6   D7
		D3 ~ D0			D7   D1 */
	for (i=0;i<0x100;i++) quran_lutp[i]=((i<<4&0x50)|(i>>3&5)|(i<<1&0xa0)|(i<<2&8)|(i>>6&2))^0x4d;
}





/* --- Matra INK (single game), AMD Am29F040B flash memory */
static _am29f* ink_chip[2]={NULL,NULL};

/* i/o */
static u8 __fastcall mapread_matraink(int slot,u16 a)
{
	return am29f_read(ink_chip[slot],a);
}

static void __fastcall mapwrite_matraink(int slot,u16 a,u8 v)
{
	am29f_write(ink_chip[slot],a,v);
}

/* state					size
amd mode					2
amd inc						1

==							3
*/
#define STATE_VERSION_INK	3 /* mapper.c */
/* version history:
1: initial
2: no changes (mapper.c)
3: no changes (mapper.c)
*/
#define STATE_SIZE_INK		3

static int __fastcall mapgetstatesize_matraink(int slot)
{
	return STATE_SIZE_INK;
}

/* save */
static void __fastcall mapsavestate_matraink(int slot,u8** s)
{
	STATE_SAVE_2(ink_chip[slot]->mode);
	STATE_SAVE_1(ink_chip[slot]->inc);
}

/* load */
static void __fastcall maploadstatecur_matraink(int slot,u8** s)
{
	STATE_LOAD_2(ink_chip[slot]->mode);
	STATE_LOAD_1(ink_chip[slot]->inc);
}

static int __fastcall maploadstate_matraink(int slot,int v,u8** s)
{
	switch (v) {
		case 1: case 2: case STATE_VERSION_INK:
			maploadstatecur_matraink(slot,s);
			break;
		
		default: return FALSE;
	}
	
	return TRUE;
}

/* init/clean */
static void mapclean_matraink(int slot)
{
	am29f_clean(ink_chip[slot]);
	ink_chip[slot]=NULL;
}

static void mapinit_matraink(int slot)
{
	int i;
	
	ink_chip[slot]=am29f_init(TRUE);
	mapclean[slot]=mapclean_matraink;
	
	/* upload to flash */
	for (i=0;i<0x40;i++) memcpy(ink_chip[slot]->data+0x2000*i,cart[slot][i],0x2000);
	
	for (i=0;i<8;i++) ink_chip[slot]->unprotect[i]=TRUE;
	ink_chip[slot]->readsector=0;
	ink_chip[slot]->writesector=0x40000;
	am29f_reset(ink_chip[slot]);
	
	mapgetstatesize[slot]=mapgetstatesize_matraink;
	mapsavestate[slot]=mapsavestate_matraink;
	maploadstatecur[slot]=maploadstatecur_matraink;
	maploadstate[slot]=maploadstate_matraink;
	
	mapsetcustom_read(slot,0,8,mapread_matraink);
	mapsetcustom_write(slot,0,8,mapwrite_matraink);
}





/* --- Sony Playball (single game), discrete logic, board: Sony 1-621-028

NOTE: If samples are used, it can cause reverse and speed up problems, unable to fix it for now.
 (eg. reverse -> instant replay, can give different playback due to sample/buffer offset)

*/
static _sample* playball_sample[2]={NULL,NULL};

static u8 __fastcall mapread_playball(int slot,u16 a)
{
	/* register at $bfff, bit 0: 1=ready, 0=busy */
	if (a==0xbfff) return playball_sample[slot]->playing^0xff;
	
	return cartmap[slot][5][a&0x1fff];
}

static void __fastcall mapwrite_playball(int slot,u16 a,u8 v)
{
	if (a==0xbfff&&v<15) {
		/* register at $bfff, play sample if ready */
		if (!playball_sample[slot]->playing) sample_play(playball_sample[slot],slot,v);
	}
}

static void mapstream_playball(int slot,signed short* stream,int len)
{
	sample_stream(playball_sample[slot],slot,stream,len);
}

/* state: sample.c */
static int __fastcall mapgetstatesize_playball(int slot) { return sample_state_get_size(); }
static void __fastcall mapsavestate_playball(int slot,u8** s) { sample_state_save(playball_sample[slot],s); }
static void __fastcall maploadstatecur_playball(int slot,u8** s) { sample_state_load_cur(playball_sample[slot],s); }
static int __fastcall maploadstate_playball(int slot,int v,u8** s) { return sample_state_load(playball_sample[slot],v,s); }

static void mapclean_playball(int slot)
{
	sample_stop(playball_sample[slot]);
	sample_clean(playball_sample[slot]);
	playball_sample[slot]=NULL;
}

static void mapinit_playball(int slot)
{
	int i,j;
	
	playball_sample[slot]=sample_init();
	MEM_CLEAN(file->name);
	if (mapper_get_file(slot)) file_setfile(NULL,mapper_get_file(slot),NULL,NULL);
	sample_load(playball_sample[slot],slot,file->name);
	
	mapclean[slot]=mapclean_playball;
	mapgetstatesize[slot]=mapgetstatesize_playball;
	mapsavestate[slot]=mapsavestate_playball;
	maploadstatecur[slot]=maploadstatecur_playball;
	maploadstate[slot]=maploadstate_playball;
	mapstream[slot]=mapstream_playball;
	
	mapsetcustom_read(slot,5,1,mapread_playball);
	mapsetcustom_write(slot,5,1,mapwrite_playball);
	
	/* 32KB uu0123uu */
	j=cartpages[slot]; if (j>6) j=6;
	for (i=0;i<j;i++) cartbank[slot][i+2]=i;
}





/* --- Konami Synthesizer (single game), discrete logic and lots of resistors */
#define KSYN_VOL_FACTOR 12 /* guessed */
static int* konamisyn_dac[2];
static int konamisyn_output[2];
static int konamisyn_cc[2];
static int konamisyn_bc[2];

/* sound */
static __inline__ void konamisyn_update(int slot)
{
	int i=(konamisyn_cc[slot]-z80_get_rel_cycles())/crystal->rel_cycle;
	if (i<=0) return;
	konamisyn_cc[slot]=z80_get_rel_cycles();
	
	while (i--) konamisyn_dac[slot][konamisyn_bc[slot]++]=konamisyn_output[slot];
}

static void mapsetbc_konamisyn(int slot,int bc)
{
	if (bc!=0&&bc<konamisyn_bc[slot]) {
		/* copy leftover to start */
		int i=bc,j=konamisyn_bc[slot];
		while (i--) konamisyn_dac[slot][i]=konamisyn_dac[slot][--j];
	}
	
	konamisyn_bc[slot]=bc;
}

static void mapnf_konamisyn(int slot) { konamisyn_cc[slot]+=crystal->frame; }
static void mapef_konamisyn(int slot) { konamisyn_update(slot); }

/* i/o */
static void __fastcall mapwrite_konamisyn(int slot,u16 a,u8 v)
{
	/* 8 bit PCM at $4000-$7fff (bit 4 clear) */
	if (~a&0x10) {
		konamisyn_update(slot);
		konamisyn_output[slot]=v*KSYN_VOL_FACTOR;
	}
}

/* state					size
cc							4
output						4

==							8
*/
#define STATE_VERSION_KSYN	3 /* mapper.c */
/* version history:
1: initial
2: no changes (mapper.c)
3: no changes (mapper.c)
*/
#define STATE_SIZE_KSYN		8

static int __fastcall mapgetstatesize_konamisyn(int slot)
{
	return STATE_SIZE_KSYN;
}

/* save */
static void __fastcall mapsavestate_konamisyn(int slot,u8** s)
{
	STATE_SAVE_4(konamisyn_cc[slot]);
	STATE_SAVE_4(konamisyn_output[slot]);
}

/* load */
static void __fastcall maploadstatecur_konamisyn(int slot,u8** s)
{
	STATE_LOAD_4(konamisyn_cc[slot]);
	STATE_LOAD_4(konamisyn_output[slot]);
}

static int __fastcall maploadstate_konamisyn(int slot,int v,u8** s)
{
	switch (v) {
		case 1: case 2: case STATE_VERSION_KSYN:
			maploadstatecur_konamisyn(slot,s);
			break;
		
		default: return FALSE;
	}
	
	return TRUE;
}

/* init/clean */
static void mapclean_konamisyn(int slot)
{
	sound_clean_dac(konamisyn_dac[slot]);
	konamisyn_dac[slot]=NULL;
}

static void mapinit_konamisyn(int slot)
{
	int i,j;
	
	konamisyn_dac[slot]=sound_create_dac();
	
	mapclean[slot]=mapclean_konamisyn;
	mapnf[slot]=mapnf_konamisyn;
	mapef[slot]=mapef_konamisyn;
	mapsetbc[slot]=mapsetbc_konamisyn;
	mapgetstatesize[slot]=mapgetstatesize_konamisyn;
	mapsavestate[slot]=mapsavestate_konamisyn;
	maploadstatecur[slot]=maploadstatecur_konamisyn;
	maploadstate[slot]=maploadstate_konamisyn;
	
	konamisyn_output[slot]=0;
	
	/* sync */
	konamisyn_cc[slot]=psg_get_cc();
	konamisyn_bc[slot]=psg_get_buffer_counter();
	
	mapsetcustom_write(slot,2,2,mapwrite_konamisyn);
	
	/* 32KB uu0123uu */
	j=cartpages[slot]; if (j>6) j=6;
	for (i=0;i<j;i++) cartbank[slot][i+2]=i;
}
