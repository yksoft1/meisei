/* Simple no-mapper types, used by almost all small ROMs, page layout is configurable.
This also supports RAM cartridges, and (officially very rare) mixed ROM/RAM.

0-7   = 8KB page in the 1st 64KB
u($f) = unmapped
e($e) = empty section (filled with $ff) (ROM)

mirrored RAM < 8KB:
o($a) = 8 * 1KB
q($a) = 4 * 2KB
d($a) = 2 * 4KB

The most common ones are 16KB or 32KB ROMs that start at $4000, with page layout
uu01uuuu or uu0123uu, eg. Antarctic Adventure, Arkanoid, Doki Doki Penguin Land,
Eggerland Mystery, Knightmare, Magical Tree, Road Fighter, The Castle, The Goonies,
Thexder, Warroid, Yie ar Kung Fu 1, 2, Zanac, ...

2nd most common type is ROMs that start at $8000, aka BASIC ROMs, with page layout
uuuu01uu, eg. Candoo Ninja, Choro Q, Hole in One, Rise Out, Roller Ball, Rotors, ...

*/

static u32 nomapper_rom_layout_temp[2];
static u32 nomapper_ram_layout_temp[2];
static u32 nomapper_rom_layout[2];
static u32 nomapper_ram_layout[2];
static int nomapper_ram_mask[2];

static int nomapper_ram_page[2][8];
static int nomapper_ram_minmax[2][2];
static u8 nomapper_ram[2][0x10000];

static void nomapper_rom_config_default(int slot,int pages,int start)
{
	/* mirrored */
	if (start==-1) {
		if (pages>8) pages=8;
		
		switch (pages) {
			/* 8KB: mirrored 8 times */
			case 1: nomapper_rom_layout_temp[slot]=0x00000000; break;
			
			/* 16KB: mirrored 4 times */
			case 2: nomapper_rom_layout_temp[slot]=0x01010101; break;
			
			/* 24KB/32KB/40KB: start at $4000, mirror rest */
			case 3: nomapper_rom_layout_temp[slot]=0x22012201; break;
			case 4: nomapper_rom_layout_temp[slot]=0x23012301; break;
			case 5: nomapper_rom_layout_temp[slot]=0x23012341; break;
			
			/* >=48KB: start at 0, mirror rest */
			case 6: nomapper_rom_layout_temp[slot]=0x01234501; break;
			case 7: nomapper_rom_layout_temp[slot]=0x01234560; break;
			case 8: nomapper_rom_layout_temp[slot]=0x01234567; break;
			
			default: break;
		}
	}
	
	/* by startpage */
	else {
		int i;
		
		nomapper_rom_layout_temp[slot]=~0;
		
		start>>=13;
		pages+=start;
		if (pages>8) pages=8;
		
		for (i=start;i<pages;i++) nomapper_rom_layout_temp[slot]=(nomapper_rom_layout_temp[slot]^(0xf<<((i^7)<<2)))|((i-start)<<((i^7)<<2));
	}
}

/* i/o */
static u8 __fastcall mapread_nomapper_ram(int slot,u16 a)
{
	return nomapper_ram[slot][nomapper_ram_page[slot][a>>13]|(a&nomapper_ram_mask[slot])];
}

static void __fastcall mapwrite_nomapper_ram(int slot,u16 a,u8 v)
{
	nomapper_ram[slot][nomapper_ram_page[slot][a>>13]|(a&nomapper_ram_mask[slot])]=v;
}

/* state						size
rom layout						4
ram layout						4
ram								variable

==								8 (+ram)
*/
#define STATE_VERSION_NOMAPPER	3 /* mapper.c */
/* version history:
1: (no savestates yet)
2: (no savestates yet)
3: initial
*/
#define STATE_SIZE_NOMAPPER		8

static int __fastcall mapgetstatesize_nomapper(int slot)
{
	return STATE_SIZE_NOMAPPER+(nomapper_ram_minmax[slot][1]-nomapper_ram_minmax[slot][0]);
}

/* save */
static void __fastcall mapsavestate_nomapper(int slot,u8** s)
{
	STATE_SAVE_4(nomapper_rom_layout[slot]);
	STATE_SAVE_4(nomapper_ram_layout[slot]);
	
	if (nomapper_ram_minmax[slot][1]) {
		STATE_SAVE_C(nomapper_ram[slot]+nomapper_ram_minmax[slot][0],nomapper_ram_minmax[slot][1]-nomapper_ram_minmax[slot][0]);
	}
}

/* load */
static void __fastcall maploadstatecur_nomapper(int slot,u8** s)
{
	u32 i;
	
	/* don't care about rom layout with normal loadstate */
	STATE_LOAD_4(i);
	if (i!=nomapper_rom_layout[slot]) mel_error|=2;
	
	STATE_LOAD_4(i);
	if (i!=nomapper_ram_layout[slot]) {
		mel_error|=2;
		
		/* if no ram, no 1 error */
		if (i!=~0) mel_error|=1;
	}
	else if (nomapper_ram_minmax[slot][1]) {
		/* load ram */
		STATE_LOAD_C(nomapper_ram[slot]+nomapper_ram_minmax[slot][0],nomapper_ram_minmax[slot][1]-nomapper_ram_minmax[slot][0]);
	}
}

static int __fastcall maploadstate_nomapper(int slot,int v,u8** s)
{
	switch (v) {
		case 1: case 2: break; /* nothing */
		case STATE_VERSION_NOMAPPER:
			maploadstatecur_nomapper(slot,s);
			break;
		
		default: return FALSE;
	}
	
	return TRUE;
}

/* init */
static void mapinit_nomapper(int slot)
{
	int i,page;
	int min=0xc,max=0;
	
	mapgetstatesize[slot]=mapgetstatesize_nomapper;
	mapsavestate[slot]=mapsavestate_nomapper;
	maploadstatecur[slot]=maploadstatecur_nomapper;
	maploadstate[slot]=maploadstate_nomapper;
	
	memset(nomapper_ram[slot],RAMINIT,0x10000);
	
	for (i=0;i<8;i++) {
		/* ram */
		page=nomapper_ram_layout[slot]>>((i^7)<<2)&0xf;
		if (page<8) {
			if (page<min) min=page;
			if (page>max) max=page;
			
			nomapper_ram_page[slot][i]=page<<13;
			mapsetcustom_read(slot,i,1,mapread_nomapper_ram);
			mapsetcustom_write(slot,i,1,mapwrite_nomapper_ram);
		}
		else if (page<0xd) {
			min=max=page;
			nomapper_ram_page[slot][i]=0;
			mapsetcustom_read(slot,i,1,mapread_nomapper_ram);
			mapsetcustom_write(slot,i,1,mapwrite_nomapper_ram);
		}
		
		/* rom */
		page=nomapper_rom_layout[slot]>>((i^7)<<2)&0xf;
		switch (page) {
			case 0xf: cartbank[slot][i]=ROMPAGE_DUMMY_U;
			case 0xe: cartbank[slot][i]=ROMPAGE_DUMMY_FF;
			default: cartbank[slot][i]=page;
		}
	}
	
	if (nomapper_ram_layout[slot]!=~0) {
		/* has ram */
		if (max&&min==max) {
			/* < 8KB */
			nomapper_ram_minmax[slot][0]=0;
			nomapper_ram_minmax[slot][1]=(max-9)<<10;
			nomapper_ram_mask[slot]=nomapper_ram_minmax[slot][1]-1;
		}
		else {
			nomapper_ram_minmax[slot][0]=min<<13;
			nomapper_ram_minmax[slot][1]=(max+1)<<13;
			nomapper_ram_mask[slot]=0x1fff;
		}
		
		/* if it has no rom, but a file was loaded anyway, copy it to ram */
		if (nomapper_rom_layout[slot]==~0&&cartpages[slot]) {
			page=cartpages[slot]; if (page>8) page=8;
			for (i=0;i<page;i++) memcpy(nomapper_ram[slot]+0x2000*i,cart[slot][i],0x2000);
		}
	}
	else nomapper_ram_minmax[slot][0]=nomapper_ram_minmax[slot][1]=0;
}

/* media config dialog */
static BOOL CALLBACK mce_nm_dialog(HWND dialog,UINT msg,WPARAM wParam,LPARAM lParam)
{
	switch (msg) {
		
		case WM_INITDIALOG: {
			int i;
			char t[0x100];
			
			/* init editboxes */
			#define NM_INIT_EDIT(e,v)							\
				SendDlgItemMessage(dialog,e,EM_LIMITTEXT,8,0);	\
				sprintf(t,"%08x",v);							\
				for (i=0;i<8;i++) {								\
					if (t[i]=='a') t[i]='o';					\
					if (t[i]=='b') t[i]='q';					\
					if (t[i]=='c') t[i]='d';					\
					if (t[i]=='f') t[i]='u';					\
				}												\
				SetDlgItemText(dialog,e,t)
			
			NM_INIT_EDIT(IDC_MCE_NM_ROM,nomapper_rom_layout_temp[mapper_extra_ref_slot]);
			NM_INIT_EDIT(IDC_MCE_NM_RAM,nomapper_ram_layout_temp[mapper_extra_ref_slot]);
			
			#undef NM_INIT_EDIT
			
			sprintf(t,"Slot %d: %s",mapper_extra_ref_slot+1,mapper_get_type_longname(mapper_extra_ref_type));
			SetWindowText(dialog,t);
			
			main_parent_window(dialog,MAIN_PW_LEFT,MAIN_PW_LEFT,mapper_extra_ref_pos.x,mapper_extra_ref_pos.y,0);
			
			break;
		}
		
		case WM_COMMAND:
			
			switch (LOWORD(wParam)) {
				
				/* close dialog */
				case IDOK: {
					int i,mram=0,nram=0;
					char t[0x200]; char trom[0x10]; char tram[0x10];
					memset(t,0,0x200); memset(trom,0,0x10); memset(tram,0,0x10);
					GetDlgItemText(dialog,IDC_MCE_NM_ROM,trom,10);
					GetDlgItemText(dialog,IDC_MCE_NM_RAM,tram,10);
					
					/* error checking */
					if (strlen(trom)!=8||strlen(tram)!=8) {
						LOG_ERROR_WINDOW(dialog,"Input fields must be 8 characters.");
						return 0;
					}
					
					for (i=0;i<8;i++)
					switch (trom[i]) {
						case 'u': case 'U': trom[i]='f'; break;				/* unmapped */
						case 'e': case 'E': trom[i]='e'; break;				/* empty */
						case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': break;
						default:
							sprintf(t,"ROM block '%c' is unknown. Use 0-7 for mapped pages,\n'e' for empty pages, and 'u' for unmapped sections.",trom[i]);
							LOG_ERROR_WINDOW(dialog,t);
							return 0;
					}
					
					for (i=0;i<8;i++)
					switch (tram[i]) {
						case 'u': case 'U': tram[i]='f'; break;				/* unmapped */
						case 'o': case 'O': tram[i]='a'; mram|=1; break;	/* 8 * 1KB */
						case 'q': case 'Q': tram[i]='b'; mram|=2; break;	/* 4 * 2KB */
						case 'd': case 'D': tram[i]='c'; mram|=3; break;	/* 2 * 4KB */
						case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
							nram++; break;									/* 1 * 8KB */
						
						default:
							sprintf(t,"RAM block '%c' is unknown. Use 0-7 for mapped pages,\nand 'u' for unmapped sections. For RAM < 8KB, use\n'o' for 8 * 1KB, 'q' for 4 * 2KB, and 'd' for 2 * 4KB.",tram[i]);
							LOG_ERROR_WINDOW(dialog,t);
							return 0;
					}
					
					if ((mram&&nram)||(mram!=0&&mram!=1&&mram!=2&&mram!=4)) {
						LOG_ERROR_WINDOW(dialog,"Can't combine different RAM sizes.");
						return 0;
					}
					
					for (i=0;i<8;i++)
					if (trom[i]!='f'&&tram[i]!='f') {
						sprintf(t,"ROM-RAM collision at character %d.",i+1);
						LOG_ERROR_WINDOW(dialog,t);
						return 0;
					}
					
					sscanf(trom,"%x",&nomapper_rom_layout_temp[mapper_extra_ref_slot]);
					sscanf(tram,"%x",&nomapper_ram_layout_temp[mapper_extra_ref_slot]);
					
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
