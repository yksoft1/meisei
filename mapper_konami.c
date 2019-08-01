/* Konami mappers (and SCC clones) */

/* --- Konami VRC, mapper chip: Konami VRC007431, board: AICA A-2 (or custom KONAMI [number])
	Konami MegaROMs without SCC, eg.
	Gradius (Nemesis), Maze of Galious, Penguin Adventure, Shalom

From what is known, Zemina MegaROM cartridges behave the same way,
though they use discrete logic instead of this (or a) mapper chip.
The known similarities are: mirroring, bank size and locations,
write addresses, and the fact that the 1st bank is fixed.

Some Zemina bootlegs use a different system, maybe pre-configured
for one of the (unemulated) ZMB RAM boxes.
*/
static int konamivrc_mask[2];

static void __fastcall mapwrite_konamivrc(int slot,u16 a,u8 v)
{
	/* last 3 pages switchable at $6000-$bfff */
	cartbank[slot][a>>13]=v&konamivrc_mask[slot];
	refresh_cb(slot);
}

static void mapinit_konamivrc(int slot)
{
	int i;
	
	/* 4 pages of 8KB at $4000-$bfff */
	for (i=2;i<6;i++) cartbank[slot][i]=i-2;
	
	/* $0000-$3fff is a mirror of $4000-$7fff, $c000-$ffff is a mirror of $8000-$bfff */
	i=mapcart_getpslot(slot);
	mapread_std[i][0]=mapread_std[i][2]; mapread_std[i][1]=mapread_std[i][3];
	mapread_std[i][6]=mapread_std[i][4]; mapread_std[i][7]=mapread_std[i][5];
	
	mapsetcustom_write(slot,3,3,mapwrite_konamivrc);
	
	/* 256KB mapper, but allow hacks too if it turns out the ROM is larger */
	/* in reality, bit 4 selects the ROM chip in the case of 256KB games */
	if (cartmask[slot]<0x1f) konamivrc_mask[slot]=0x1f;
	else konamivrc_mask[slot]=cartmask[slot];
}





/* --- Konami Game Master 2 (single game), Konami VRC007431 mapper and some discrete logic to handle SRAM */
static int gm2_is_sram[2][4];
static int gm2_sram_bank[2][4];

/* i/o */
static u8 __fastcall mapread_gm2(int slot,u16 a)
{
	int bank=a>>13;
	
	/* page 0,3 (read-only, like VRC) */
	if (a<0x4000||a>0xbfff) bank^=2;
	
	/* read from sram ($6000-$bfff) */
	if (gm2_is_sram[slot][bank-2]) return cartsram[slot][(a&0xfff)|gm2_sram_bank[slot][bank-2]];
	
	/* normal read */
	return cartmap[slot][bank][a&0x1fff];
}

static void __fastcall mapwrite_gm2(int slot,u16 a,u8 v)
{
	int bank=a>>13;
	
	if (a&0x1000) {
		bank-=2;
		
		/* write to sram (only writable in the 2nd half of the last bank) */
		if (bank==3&&gm2_is_sram[slot][bank]) cartsram[slot][(a&0xfff)|gm2_sram_bank[slot][bank]]=v;
	}
	
	else {
		/* standard bankswitch */
		cartbank[slot][bank]=v&cartmask[slot];
		refresh_cb(slot);
		
		/* switch sram, 2*4K */
		bank-=2;
		gm2_is_sram[slot][bank]=v&(cartmask[slot]+1);
		gm2_sram_bank[slot][bank]=((v>>1&(cartmask[slot]+1))!=0)<<12;
	}
}

/* state					size
sram bank enabled			3*1
sram bank numbers			3*2

==							9
*/
#define STATE_VERSION_GM2	3 /* mapper.c */
/* version history:
1: initial
2: no changes (mapper.c)
3: no changes (mapper.c)
*/
#define STATE_SIZE_GM2		9

static int __fastcall mapgetstatesize_gm2(int slot)
{
	return STATE_SIZE_GM2;
}

/* save */
static void __fastcall mapsavestate_gm2(int slot,u8** s)
{
	int i=3;
	while (i--) {
		STATE_SAVE_1(gm2_is_sram[slot][i+1]);
		STATE_SAVE_2(gm2_sram_bank[slot][i+1]);
	}
}

/* load */
static void __fastcall maploadstatecur_gm2(int slot,u8** s)
{
	int i=3;
	while (i--) {
		STATE_LOAD_1(gm2_is_sram[slot][i+1]);
		STATE_LOAD_2(gm2_sram_bank[slot][i+1]);
	}
}

static int __fastcall maploadstate_gm2(int slot,int v,u8** s)
{
	switch (v) {
		case 1: case 2: case STATE_VERSION_GM2:
			maploadstatecur_gm2(slot,s);
			break;
		
		default: return FALSE;
	}
	
	return TRUE;
}

/* init */
static void mapinit_gm2(int slot)
{
	int i;
	
	sram_load(slot,&cartsram[slot],0x2000,NULL,0xff);
	for (i=0;i<4;i++) gm2_is_sram[slot][i]=FALSE;
	
	mapgetstatesize[slot]=mapgetstatesize_gm2;
	mapsavestate[slot]=mapsavestate_gm2;
	maploadstatecur[slot]=maploadstatecur_gm2;
	maploadstate[slot]=maploadstate_gm2;
	
	/* similar to standard Konami */
	for (i=2;i<6;i++) cartbank[slot][i]=i-2;
	mapsetcustom_read(slot,0,8,mapread_gm2);
	mapsetcustom_write(slot,3,3,mapwrite_gm2);
}





/* --- Konami SCC, mapper chip: Konami 051649 2212P003, board: custom KONAMI [number]
	Konami MegaROMs with SCC, eg.
	F1 Spirit, Gofer no Yabou 2 (Nemesis 3), Gradius 2, King's Valley 2, Parodius, Salamander
*/
static int scc_enabled[2];
static _scc* scc_scc[2]={NULL,NULL};

/* i/o */
static u8 __fastcall mapread_konamiscc(int slot,u16 a)
{
	/* read from SCC */
	if (scc_enabled[slot]&&a&0x1800) return scc_read(scc_scc[slot],a);
	
	/* default */
	return cartmap[slot][4][a&0x1fff];
}

static void __fastcall mapwrite_konamiscc(int slot,u16 a,u8 v)
{
	if ((a&0xf000)==0x9000) {
		if (a&0x800) {
			/* write to SCC */
			if (scc_enabled[slot]) scc_write(scc_scc[slot],a,v);
			return;
		}
		
		scc_enabled[slot]=(v&0x3f)==0x3f;
	}
	
	if ((a&0x1800)==0x1000) {
		/* all pages switchable at $4000-$bfff */
		cartbank[slot][a>>13]=v&cartmask[slot];
		refresh_cb(slot);
	}
}

static void mapnf_konamiscc(int slot) { scc_new_frame(scc_scc[slot]); }
static void mapef_konamiscc(int slot) { scc_end_frame(scc_scc[slot]); }
static void mapsetbc_konamiscc(int slot,int bc) { scc_set_buffer_counter(scc_scc[slot],bc); }

/* state: size 1 (enabled) + scc */
#define STATE_VERSION_KSCC	3 /* mapper.c */
/* version history:
1: initial
2: (changes in scc.c)
3: no changes (mapper.c)
*/
#define STATE_SIZE_KSCC		1 /* + SCC */

static int __fastcall mapgetstatesize_konamiscc(int slot)
{
	return STATE_SIZE_KSCC+scc_state_get_size();
}

/* save */
static void __fastcall mapsavestate_konamiscc(int slot,u8** s)
{
	STATE_SAVE_1(scc_enabled[slot]);
	
	scc_state_save(scc_scc[slot],s);
}

/* load */
static void __fastcall maploadstatecur_konamiscc_inc(int slot,u8** s)
{
	STATE_LOAD_1(scc_enabled[slot]);
}

static void __fastcall maploadstatecur_konamiscc(int slot,u8** s)
{
	maploadstatecur_konamiscc_inc(slot,s);
	
	scc_state_load_cur(scc_scc[slot],s);
}

static int __fastcall maploadstate_konamiscc(int slot,int v,u8** s)
{
	switch (v) {
		case 1:
			maploadstatecur_konamiscc_inc(slot,s);
			scc_state_load(scc_scc[slot],v,s);
			break;
		case 2: case STATE_VERSION_KSCC:
			maploadstatecur_konamiscc(slot,s);
			break;
		
		default: return FALSE;
	}
	
	return TRUE;
}

/* init/clean */
static void mapclean_konamiscc(int slot)
{
	scc_clean(scc_scc[slot]);
	scc_scc[slot]=NULL;
}

static void mapinit_konamiscc(int slot)
{
	int i;
	
	if (mapclean[slot]==mapclean_nothing) {
		scc_scc[slot]=scc_init(SCC_VF_DEFAULT,SCC_MODEL);
		mapclean[slot]=mapclean_konamiscc;
	}
	
	mapgetstatesize[slot]=mapgetstatesize_konamiscc;
	mapsavestate[slot]=mapsavestate_konamiscc;
	maploadstatecur[slot]=maploadstatecur_konamiscc;
	maploadstate[slot]=maploadstate_konamiscc;
	
	mapnf[slot]=mapnf_konamiscc;
	mapef[slot]=mapef_konamiscc;
	mapsetbc[slot]=mapsetbc_konamiscc;
	
	/* 4 pages of 8KB at $4000-$bfff */
	for (i=2;i<6;i++) cartbank[slot][i]=i-2;
	
	/* $0000-$3fff is a mirror of $8000-$bfff, $c000-$ffff is a mirror of $4000-$7fff */
	i=mapcart_getpslot(slot);
	mapread_std[i][0]=mapread_std[i][4]; mapread_std[i][1]=mapread_std[i][5];
	mapread_std[i][6]=mapread_std[i][2]; mapread_std[i][7]=mapread_std[i][3];
	
	mapsetcustom_read(slot,4,1,mapread_konamiscc);
	mapsetcustom_write(slot,2,4,mapwrite_konamiscc);
	
	scc_poweron(scc_scc[slot]);
	scc_enabled[slot]=FALSE;
}





/* --- Konami Sound Cartridge, mapper chip: Konami 052539 SCC-I 2312P001, board: KONAMI
	SCC-I with (configurable) RAM, supported by the Game Collections (see below) (and some hacked ROMs)
GC 1: 1:Knightmare(YES), 2:Antarctic Adventure(YES), 3:Yie ar Kung-Fu(YES), 4:Yie ar Kung-Fu 2(YES), 5:King's Valley(NO)
GC 2: 1:Konami's Boxing(YES), 2:Konami's Tennis(NO), 3:Video Hustler(YES), 4:Hyper Olympic 1(NO), 5:Hyper Sports 2(YES)
GC 3: 1:Twinbee(YES), 2:Super Cobra(YES), 3:Sky Jaguar(YES), 4:Time Pilot(NO), 5:Nemesis(YES)
GC 4: 1:Konami's Soccer(YES), 2:Konami's Ping Pong(YES), 3:Konami's Golf(NO), 4:Hyper Olympic 2(NO), 5:Hyper Sports 3(YES)
GC S: 1.1:Pippols(YES), 1.2:Hyper Rally(YES), 1.3:Road Fighter(YES) (other choices are MSX2-only)
*/
static int scci_mode[2];
static int scci_bank[2][4];
static int scci_we[2][4]; /* write enabled */
static u8 scci_ram[2][0x20000]; /* 128KB */

static u32 scci_ram_layout_temp[2];
static u32 scci_ram_layout[2];

/* i/o */
static u8 __fastcall mapread_konamiscci(int slot,u16 a)
{
	int bank=a>>13;
	
	/* read from SCC-I, SCC mode */
	if ((a&0xf800)==0x9800&&scc_enabled[slot]&1&&!(scci_mode[slot]&0x20)) return scc_read(scc_scc[slot],a);
	
	/* read from SCC-I, SCC-I mode */
	if ((a&0xf800)==0xb800&&scc_enabled[slot]&0x80&scci_mode[slot]<<2) return scci_read(scc_scc[slot],a);
	
	/* page 0,3 (read-only, like SCC) */
	if (a<0x4000||a>0xbfff) bank^=4;
	
	/* read from ram */
	return scci_ram[slot][scci_bank[slot][bank-2]|(a&0x1fff)];
}

static void __fastcall mapwrite_konamiscci(int slot,u16 a,u8 v)
{
	int bank=a>>13;
	
	if ((a&0xfffe)==0xbffe) {
		/* mode register */
		scci_mode[slot]=v;
		
		scci_we[slot][0]=v&0x11;
		scci_we[slot][1]=v&0x12;
		scci_we[slot][2]=(v>>3&v&4)|(v&0x10);
		scci_we[slot][3]=v&0x10;
		
		/* RAM in this bank is unaffected */
		return;
	}
	
	bank-=2;
	
	if (scci_we[slot][bank]) {
		/* write to RAM */
		bank=scci_bank[slot][bank];
		if (scci_ram_layout[slot]<<16&~bank||scci_ram_layout[slot]<<15&bank&0x10000) scci_ram[slot][bank|(a&0x1fff)]=v;
	}
	else {
		switch (a&0xf800) {
			case 0x9000:
				/* enable SCC mode */
				scc_enabled[slot]=(scc_enabled[slot]&0x80)|((v&0x3f)==0x3f);
				break;
			case 0x9800:
				/* write to SCC-I, SCC mode */
				if (scc_enabled[slot]&1&&!(scci_mode[slot]&0x20)) scc_write(scc_scc[slot],a,v);
				break;
			case 0xb000:
				/* enable SCC-I mode */
				scc_enabled[slot]=(scc_enabled[slot]&1)|(v&0x80);
				break;
			case 0xb800:
				/* write to SCC-I, SCC-I mode */
				if (scc_enabled[slot]&0x80&scci_mode[slot]<<2) scci_write(scc_scc[slot],a,v);
				break;
			
			default: break;
		}
		
		if ((a&0x1800)==0x1000) {
			/* bankswitch */
			scci_bank[slot][bank]=v<<13&0x1ffff;
		}
	}
}

/* state					size
enabled						1
mode register				1
banks						4*3
banks write enable			4
ram layout					1
ram (after scc)				variable (0, 0x10000, or 0x20000)

==							19 (+scc +ram)
*/
#define STATE_VERSION_KSCCI	3 /* mapper.c */
/* version history:
1: (didn't exist yet)
2: initial
3: custom RAM layout
*/
#define STATE_SIZE_KSCCI	19

static int __fastcall mapgetstatesize_konamiscci(int slot)
{
	int size=STATE_SIZE_KSCCI;
	if (scci_ram_layout[slot]&1) size+=0x10000;
	if (scci_ram_layout[slot]&2) size+=0x10000;
	return size+scc_state_get_size();
}

/* save */
static void __fastcall mapsavestate_konamiscci(int slot,u8** s)
{
	int i=4;
	while (i--) {
		STATE_SAVE_3(scci_bank[slot][i]);
		STATE_SAVE_1(scci_we[slot][i]);
	}
	
	STATE_SAVE_1(scc_enabled[slot]);
	STATE_SAVE_1(scci_mode[slot]);
	
	scc_state_save(scc_scc[slot],s);
	
	/* ram */
	STATE_SAVE_1(scci_ram_layout[slot]);
	if (scci_ram_layout[slot]&1) { STATE_SAVE_C(scci_ram[slot],0x10000); }
	if (scci_ram_layout[slot]&2) { STATE_SAVE_C(scci_ram[slot]+0x10000,0x10000); }
}

/* load */
static void __fastcall maploadstatecur_konamiscci(int slot,u8** s)
{
	int i=4;
	while (i--) {
		STATE_LOAD_3(scci_bank[slot][i]);
		STATE_LOAD_1(scci_we[slot][i]);
	}
	
	STATE_LOAD_1(scc_enabled[slot]);
	STATE_LOAD_1(scci_mode[slot]);
	
	scc_state_load_cur(scc_scc[slot],s);
	
	/* ram */
	STATE_LOAD_1(i);
	if (i!=scci_ram_layout[slot]) mel_error|=2;
	
	switch (i&3) {
		/* no ram, don't bother */
		case 0: break;
		
		/* lower 64KB */
		case 1:
			if (scci_ram_layout[slot]&1) {
				if (scci_ram_layout[slot]&2) memset(scci_ram[slot]+0x10000,0,0x10000);
				STATE_LOAD_C(scci_ram[slot],0x10000);
			}
			else mel_error|=1;
			
			break;
		
		/* upper 64KB */
		case 2:
			if (scci_ram_layout[slot]&2) {
				if (scci_ram_layout[slot]&1) memset(scci_ram[slot],0,0x10000);
				STATE_LOAD_C(scci_ram[slot]+0x10000,0x10000);
			}
			else mel_error|=1;
			
			break;
		
		/* 128KB */
		case 3:
			if (scci_ram_layout[slot]==3) { STATE_LOAD_C(scci_ram[slot],0x20000); }
			else mel_error|=1;
			break;
	}
}

static void __fastcall maploadstate_konamiscci_2(int slot,u8** s)
{
	/* version 2 */
	int i=4;
	while (i--) {
		STATE_LOAD_3(scci_bank[slot][i]);
		STATE_LOAD_1(scci_we[slot][i]);
	}
	
	STATE_LOAD_1(scc_enabled[slot]);
	STATE_LOAD_1(scci_mode[slot]);
	
	scc_state_load(scc_scc[slot],2,s);
	
	/* due to a bug, version 2 didn't save ram */
}

static int __fastcall maploadstate_konamiscci(int slot,int v,u8** s)
{
	switch (v) {
		case 2:
			maploadstate_konamiscci_2(slot,s);
			break;
		case STATE_VERSION_KSCC:
			maploadstatecur_konamiscci(slot,s);
			break;
		
		default: return FALSE;
	}
	
	return TRUE;
}

/* init */
static void mapinit_konamiscci(int slot)
{
	int i;
	
	scc_scc[slot]=scc_init(SCC_VF_DEFAULT,SCCI_MODEL);
	mapclean[slot]=mapclean_konamiscc;
	
	memset(scci_ram[slot],0,0x20000); /* can be alternating areas of $00 and $ff */
	
	/* 4 pages of 8KB at $4000-$bfff (like Konami SCC) */
	mapsetcustom_read(slot,0,8,mapread_konamiscci);
	mapsetcustom_write(slot,2,4,mapwrite_konamiscci);
	
	for (i=0;i<4;i++) {
		scci_we[slot][i]=FALSE;
		scci_bank[slot][i]=i<<13;
	}
	
	scc_poweron(scc_scc[slot]);
	scc_enabled[slot]=0;
	scci_mode[slot]=0;
	
	mapnf[slot]=mapnf_konamiscc;
	mapef[slot]=mapef_konamiscc;
	mapsetbc[slot]=mapsetbc_konamiscc;
	
	mapgetstatesize[slot]=mapgetstatesize_konamiscci;
	mapsavestate[slot]=mapsavestate_konamiscci;
	maploadstatecur[slot]=maploadstatecur_konamiscci;
	maploadstate[slot]=maploadstate_konamiscci;
	
	/* file -> RAM (normally unsupported) */
	if (cartpages[slot]&&scci_ram_layout[slot]) {
		if (cartpages[slot]>8) {
			if (scci_ram_layout[slot]==3) {
				/* only for 128KB */
				int p=cartpages[slot];
				if (p>0x10) p=0x10;
				for (i=0;i<p;i++) memcpy(scci_ram[slot]+0x2000*i,cart[slot][i],0x2000);
				
				scci_mode[slot]=0x20; /* pre-init */
			}
		}
		else {
			/* mirrored */
			for (i=0;i<cartpages[slot];i++) {
				memcpy(scci_ram[slot]+0x2000*i,cart[slot][i],0x2000);
				memcpy(scci_ram[slot]+0x10000+0x2000*i,cart[slot][i],0x2000);
			}
			
			scci_mode[slot]=0x20;
			
			/* on the SD Snatcher cartridge, boot from the upper bank */
			if (scci_ram_layout[slot]==2) for (i=0;i<4;i++) scci_bank[slot][i]|=0x10000;
		}
	}
	
	/* extra config */
	if (~scci_ram_layout[slot]&1) memset(scci_ram[slot],UNMAPPED,0x10000);
	if (~scci_ram_layout[slot]&2) memset(scci_ram[slot]+0x10000,UNMAPPED,0x10000);
}

/* media config dialog */
static BOOL CALLBACK mce_scci_dialog(HWND dialog,UINT msg,WPARAM wParam,LPARAM lParam)
{
	switch (msg) {
		
		case WM_INITDIALOG: {
			char t[0x100];
			
			/* init combobox */
			HWND c=GetDlgItem(dialog,IDC_MCE_SCCI_RAM);
			
			#define SCCI_COMBO(x)	\
				sprintf(t,x);		\
				SendMessage(c,CB_ADDSTRING,0,(LPARAM)((LPCTSTR)t))
			
			SCCI_COMBO("No RAM");
			SCCI_COMBO("64KB (lower, Snatcher)");
			SCCI_COMBO("64KB (upper, SD Snatcher)");
			SCCI_COMBO("128KB");
			
			#undef SCCI_COMBO
			
			SendMessage(c,CB_SETCURSEL,scci_ram_layout_temp[mapper_extra_ref_slot],0);
			
			sprintf(t,"Slot %d: %s",mapper_extra_ref_slot+1,mapper_get_type_longname(mapper_extra_ref_type));
			SetWindowText(dialog,t);
			
			main_parent_window(dialog,MAIN_PW_LEFT,MAIN_PW_LEFT,mapper_extra_ref_pos.x,mapper_extra_ref_pos.y,0);
			
			break;
		}
		
		case WM_COMMAND:
			
			switch (LOWORD(wParam)) {
				
				/* close dialog */
				case IDOK: {
					int i;
					
					if ((i=SendDlgItemMessage(dialog,IDC_MCE_SCCI_RAM,CB_GETCURSEL,0,0))==CB_ERR) i=3;
					scci_ram_layout_temp[mapper_extra_ref_slot]=i;
					
					/* fall through */
				}
				case IDCANCEL:
					EndDialog(dialog,0);
					break;
				
				default: break;
			}
			
			break;
		
		default: break;
	}
	
	return 0;
}





/* --- Manuel Mega Flash ROM SCC, Am29F040B + SCC mapper
	Lotus F3, ..
*/
static int mfrscc_sector[2][4];
static int mfrscc_bank[2][4];
static _am29f* mfrscc_chip[2]={NULL,NULL};

/* i/o */
static u8 __fastcall mapread_mfrscc(int slot,u16 a)
{
	/* like Konami SCC */
	int bank=a>>13;
	
	if (scc_enabled[slot]&&(a&0xf800)==0x9800) return scc_read(scc_scc[slot],a);
	
	/* page 0,3 */
	if (a<0x4000||a>0xbfff) bank^=4;
	
	bank-=2;
	
	/* from flash */
	mfrscc_chip[slot]->readsector=mfrscc_chip[slot]->writesector=mfrscc_sector[slot][bank];
	return am29f_read(mfrscc_chip[slot],mfrscc_bank[slot][bank]|(a&0x1fff));
}

static void __fastcall mapwrite_mfrscc(int slot,u16 a,u8 v)
{
	/* like Konami SCC */
	int bank=a>>13;
	
	/* page 0,3 */
	if (a<0x4000||a>0xbfff) bank^=4;
	
	/* 2212P003 / 051649 */
	else {
		if ((a&0xf000)==0x9000) {
			if (a&0x800) {
				/* write to SCC */
				if (scc_enabled[slot]) scc_write(scc_scc[slot],a,v);
			}
			else scc_enabled[slot]=(v&0x3f)==0x3f;
		}
		
		if ((a&0x1800)==0x1000) {
			/* bankswitch */
			int s=v<<13;
			mfrscc_sector[slot][bank-2]=s&0x70000;
			mfrscc_bank[slot][bank-2]=s&0xf000;
		}
	}
	
	bank-=2;
	
	/* to flash */
	mfrscc_chip[slot]->readsector=mfrscc_chip[slot]->writesector=mfrscc_sector[slot][bank];
	am29f_write(mfrscc_chip[slot],mfrscc_bank[slot][bank]|(a&0x1fff),v);
}

/* state						size
sector							3*4
bank							2*4
SCC enabled						1

(+SCC+amf29f)

==								21
*/
#define STATE_VERSION_MFRSCC	3 /* mapper.c */
/* version history:
1: initial
2: (changes in scc.c)
3: no changes (mapper.c)
*/
#define STATE_SIZE_MFRSCC		21

static int __fastcall mapgetstatesize_mfrscc(int slot)
{
	return STATE_SIZE_MFRSCC+scc_state_get_size()+am29f_state_get_size();
}

/* save */
static void __fastcall mapsavestate_mfrscc(int slot,u8** s)
{
	int i=4;
	while (i--) {
		STATE_SAVE_3(mfrscc_sector[slot][i]);
		STATE_SAVE_2(mfrscc_bank[slot][i]);
	}
	STATE_SAVE_1(scc_enabled[slot]);
	
	scc_state_save(scc_scc[slot],s);
	am29f_state_save(mfrscc_chip[slot],s);
}

/* load */
static void __fastcall maploadstatecur_mfrscc_inc(int slot,u8** s)
{
	int i=4;
	while (i--) {
		STATE_LOAD_3(mfrscc_sector[slot][i]);
		STATE_LOAD_2(mfrscc_bank[slot][i]);
	}
	STATE_LOAD_1(scc_enabled[slot]);
}

static void __fastcall maploadstatecur_mfrscc(int slot,u8** s)
{
	maploadstatecur_mfrscc_inc(slot,s);
	
	scc_state_load_cur(scc_scc[slot],s);
	am29f_state_load_cur(mfrscc_chip[slot],s);
}

static int __fastcall maploadstate_mfrscc(int slot,int v,u8** s)
{
	switch (v) {
		case 1:
			maploadstatecur_mfrscc_inc(slot,s);
			scc_state_load(scc_scc[slot],v,s);
			am29f_state_load(mfrscc_chip[slot],v,s);
			break;
		case 2: case STATE_VERSION_MFRSCC:
			maploadstatecur_mfrscc(slot,s);
			break;
		
		default: return FALSE;
	}
	
	return TRUE;
}

/* init/clean */
static void mapclean_mfrscc(int slot)
{
	mapclean_konamiscc(slot);
	sram_save(slot,mfrscc_chip[slot]->data,0x80000,NULL);
	am29f_clean(mfrscc_chip[slot]);
	mfrscc_chip[slot]=NULL;
}

static void mapinit_mfrscc(int slot)
{
	int i;
	
	mfrscc_chip[slot]=am29f_init(FALSE);
	for (i=0;i<8;i++) mfrscc_chip[slot]->unprotect[i]=TRUE;
	
	if (!sram_load(slot,&mfrscc_chip[slot]->data,0x80000,NULL,0xff)) {
		/* upload game to flash */
		for (i=0;i<0x40;i++) memcpy(mfrscc_chip[slot]->data+0x2000*i,cart[slot][i],0x2000);
	}
	
	scc_scc[slot]=scc_init(SCC_VF_MFR,SCC_MODEL);
	mapclean[slot]=mapclean_mfrscc;
	
	mfrscc_chip[slot]->readsector=mfrscc_chip[slot]->writesector=0;
	am29f_reset(mfrscc_chip[slot]);
	scc_poweron(scc_scc[slot]);
	scc_enabled[slot]=FALSE;
	
	for (i=0;i<4;i++) {
		mfrscc_sector[slot][i]=0;
		mfrscc_bank[slot][i]=i<<13;
	}
	
	mapsetcustom_read(slot,0,8,mapread_mfrscc);
	mapsetcustom_write(slot,0,8,mapwrite_mfrscc);
	
	mapnf[slot]=mapnf_konamiscc;
	mapef[slot]=mapef_konamiscc;
	mapsetbc[slot]=mapsetbc_konamiscc;
	
	mapgetstatesize[slot]=mapgetstatesize_mfrscc;
	mapsavestate[slot]=mapsavestate_mfrscc;
	maploadstatecur[slot]=maploadstatecur_mfrscc;
	maploadstate[slot]=maploadstate_mfrscc;
}





/* --- Vincent DSK2ROM (uses Konami SCC)
	any disk file, read-only
*/
static u32 dsk2rom_crcprev[2]={0,0};

/* init/clean */
static void mapclean_dsk2rom(int slot)
{
	int i;
	
	/* restructure (backwards) */
	MEM_CLEAN(cart[slot][0]); MEM_CLEAN(cart[slot][1]);
	for (i=2;i<cartpages[slot];i++) cart[slot][i-2]=cart[slot][i];
	cart[slot][cartpages[slot]-1]=cart[slot][cartpages[slot]-2]=dummy_u;
	cartpages[slot]-=2;
	for (cartmask[slot]=1;cartmask[slot]<cartpages[slot];cartmask[slot]<<=1) { ; }
	cartmask[slot]--;
	
	cartcrc[slot]=dsk2rom_crcprev[slot];
	
	/* konamiscc part */
	mapclean_konamiscc(slot);
}

static void mapinit_dsk2rom(int slot)
{
	int i,yay=FALSE;
	u8* dsk2rom[2];
	
	#define DFN_MAX 4
	
	/* possible dsk2rom filenames */
	const char* fn[DFN_MAX][3]={
	{ "dsk2rom-0.80", "zip", "rom" },
	{ "dsk2rom",      "rom", NULL  },
	{ "dsk2rom",      "zip", "rom" },
	{ "dsk2rom-0.70", "zip", "rom" }
	};
	
	MEM_CREATE(dsk2rom[0],0x2000); MEM_CREATE(dsk2rom[1],0x2000);
	
	/* open dsk2rom */
	for (i=0;i<DFN_MAX;i++) {
		file_setfile(&file->appdir,fn[i][0],fn[i][1],fn[i][2]);
		if (file_open()&&file->size==0x4000&&file_read(dsk2rom[0],0x2000)&&file_read(dsk2rom[1],0x2000)) {
			yay=TRUE; file_close();
			break;
		}
		else file_close();
	}
	
	#undef DFN_MAX
	
	if (!yay) LOG(LOG_MISC|LOG_WARNING|LOG_TYPE(LT_SLOT1FAIL+slot),"slot %d: couldn't open DSK2ROM",slot+1);
	else if (cartpages[slot]!=0&&(cartpages[slot]+2)<=ROM_MAXPAGES&&memcmp(cart[slot][0],dsk2rom[0],0x2000)!=0&&memcmp(cart[slot][1],dsk2rom[1],0x2000)!=0) {
		/* restructure */
		int i=cartpages[slot];
		while (i--) cart[slot][i+2]=cart[slot][i];
		cart[slot][0]=dsk2rom[0]; cart[slot][1]=dsk2rom[1];
		dsk2rom[0]=dsk2rom[1]=NULL;
		cartpages[slot]+=2;
		for (cartmask[slot]=1;cartmask[slot]<cartpages[slot];cartmask[slot]<<=1) { ; }
		cartmask[slot]--;
		
		dsk2rom_crcprev[slot]=cartcrc[slot];
		cartcrc[slot]=(cartcrc[slot]<<1|(cartcrc[slot]>>31&1))^file->crc32; /* combine crc with dsk2rom crc */
		
		mapclean[slot]=mapclean_dsk2rom;
		
		/* konamiscc part */
		scc_scc[slot]=scc_init(SCC_VF_DEFAULT,SCC_MODEL);
	}
	
	MEM_CLEAN(dsk2rom[0]); MEM_CLEAN(dsk2rom[1]);
	
	mapinit_konamiscc(slot);
}
