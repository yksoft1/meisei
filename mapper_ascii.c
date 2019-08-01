/* ASCII type mappers, 8KB or 16KB, with or without SRAM
used by most MegaROMs, see mapper_table.h for more information */

static u32 ascii_mainw_temp[2];
static u32 ascii_board_temp[2];
static u32 ascii_board[2];
static u32 ascii_mainw[2];

static int ascii_bankbit[2];
static int ascii_bankmask[2];

static int ascii_srammask[2];
static int ascii_srambit[2];
static int ascii_is_sram[2][4];
static int ascii_protectbit[2];
static int ascii_sramwrite[2][4];
static int ascii_srambank[2][4];

/* i/o */
static __inline__ void ascii_bankswitch_rom(int slot,int bank,int is16,int v)
{
	cartbank[slot][bank]=v<<is16&cartmask[slot];
	cartbank[slot][bank|is16]=(v<<is16&cartmask[slot])|is16;
	refresh_cb(slot);
}

static void __fastcall mapwrite_ascii(int slot,u16 a,u8 v)
{
	/* 16KB: no effect on $6800-$6fff or $7800-$7fff */
	if (a&ascii_bankbit[slot]&0x800) return;
	
	ascii_bankswitch_rom(slot,(a>>11&3)+2,ascii_bankbit[slot]&1,v);
}

static void __fastcall mapwrite_ascii_sram(int slot,u16 a,u8 v)
{
	/* mapper (rom bankswitch is like normal ascii) */
	if ((a&0xe000)==0x6000) {
		int bank,is16;
		
		if (a&ascii_bankbit[slot]&0x800) return;
		
		bank=a>>11&3;
		is16=ascii_bankbit[slot]&1;
		
		/* bankswitch sram/normal */
		if ((ascii_is_sram[slot][bank]=ascii_is_sram[slot][bank|is16]=((v&ascii_srambit[slot])!=0)&(((v^ascii_srambit[slot])<<is16&ascii_bankmask[slot])==0))) {
			ascii_srambank[slot][bank]=v<<(13+is16)&(cartsramsize[slot]-1);
			ascii_srambank[slot][bank|is16]=(v<<(13+is16)|(ascii_bankbit[slot]&0x2000))&(cartsramsize[slot]-1);
			
			/* write-protect bit */
			ascii_sramwrite[slot][bank]=ascii_sramwrite[slot][bank|is16]=((v&ascii_protectbit[slot])==0);
		}
		else ascii_bankswitch_rom(slot,bank+2,is16,v);
	}
	
	/* write to sram */
	else {
		int bank=(a>>13)-2;
		
		if (ascii_is_sram[slot][bank]&ascii_sramwrite[slot][bank]) cartsram[slot][ascii_srambank[slot][bank]|(a&ascii_srammask[slot])]=v;
	}
}

static u8 __fastcall mapread_ascii_sram(int slot,u16 a)
{
	int bank=a>>13;
	
	/* read from sram */
	if (ascii_is_sram[slot][bank-2]) return cartsram[slot][ascii_srambank[slot][bank-2]|(a&ascii_srammask[slot])];
	
	/* normal read */
	return cartmap[slot][bank][a&0x1fff];
}

/* state					size
main bank size				2
main config					4
board config				4
sram bank enabled			4*1
sram bank write enabled		4*1
sram bank numbers			4*3

==							30
*/
#define STATE_VERSION_ASCII	3 /* mapper.c */
/* version history:
1: initial
2: no changes (mapper.c)
3: extra config, merged ascii8/16/sram into one type
*/
#define STATE_SIZE_ASCII	30 /* (sram in mapper.c) */

static int __fastcall mapgetstatesize_ascii(int slot)
{
	return STATE_SIZE_ASCII;
}

/* save */
static void __fastcall mapsavestate_ascii(int slot,u8** s)
{
	int i;
	
	STATE_SAVE_2(ascii_bankbit[slot]);
	STATE_SAVE_4(ascii_mainw[slot]);
	STATE_SAVE_4(ascii_board[slot]);
	
	i=4; while (i--) {
		STATE_SAVE_1(ascii_is_sram[slot][i]);
		STATE_SAVE_1(ascii_sramwrite[slot][i]);
		STATE_SAVE_3(ascii_srambank[slot][i]);
	}
}

/* load */
static void __fastcall maploadstatecur_ascii(int slot,u8** s)
{
	u32 i,j,b;
	
	STATE_LOAD_2(i); /* not used yet */
	STATE_LOAD_4(i);
	STATE_LOAD_4(b);
	if (i!=ascii_mainw[slot]||b!=ascii_board[slot]) mel_error|=2;
	
	i=4; while (i--) {
		STATE_LOAD_1(ascii_is_sram[slot][i]);
		STATE_LOAD_1(ascii_sramwrite[slot][i]);
		STATE_LOAD_3(ascii_srambank[slot][i]);
	}
	
	/* sram here instead of mapper.c */
	loadstate_skip_sram[slot]=TRUE;
	
	if (b&A_SS_MASK) {
		i=0x200<<((b&A_SS_MASK)>>A_SS_SHIFT);
		if (i>cartsramsize[slot]) {
			/* source sram larger than destination */
			j=4; while (j--) ascii_srambank[slot][j]&=(cartsramsize[slot]-1);
			if (cartsramsize[slot]) { STATE_LOAD_C(cartsram[slot],cartsramsize[slot]); }
			else { j=4; while (j--) ascii_is_sram[slot][j]=ascii_sramwrite[slot][j]=FALSE; }
			(*s)+=(i-cartsramsize[slot]); /* skip rest */
		}
		else { STATE_LOAD_C(cartsram[slot],i); }
	}
}

static void __fastcall maploadstate_ascii_12(int slot,u8** s)
{
	/* version 1,2 (SRAM was already loaded) */
	int i,j,uid;
	
	_msx_state* msxstate=(_msx_state*)state_get_msxstate();
	uid=slot?msxstate->cart2_uid:msxstate->cart1_uid;
	
	switch (uid) {
		/* ASCII16 + 2KB SRAM */
		case DEPRECATED_ASCII16_2:
			if ((ascii_board[slot]&A_SS_MASK)==A_SS_02) {
				STATE_LOAD_1(j); ascii_is_sram[slot][0]=ascii_is_sram[slot][1]=(j!=0);
				STATE_LOAD_1(j); ascii_is_sram[slot][2]=ascii_is_sram[slot][3]=(j!=0);
			}
			else mel_error|=3;
			break;
		
		/* ASCII8 + 8KB SRAM */
		case DEPRECATED_ASCII8_8:
			if ((ascii_board[slot]&A_SS_MASK)==A_SS_08) {
				for (i=0;i<4;i++) { STATE_LOAD_1(j); ascii_is_sram[slot][i]=(j!=0); }
			}
			else mel_error|=3;
			break;
		
		/* ASCII8 + 32KB SRAM (Koei) */
		case DEPRECATED_ASCII8_32:
			if ((ascii_board[slot]&A_SS_MASK)==A_SS_32) {
				i=4; while (i--) {
					STATE_LOAD_1(j); ascii_is_sram[slot][i]=(j!=0);
					STATE_LOAD_2(ascii_srambank[slot][i]);
				}
			}
			else mel_error|=3;
			break;
		
		/* no SRAM */
		default:
			if (cartsramsize[slot]) {
				/* undo garbage sram load (fails if both slots were in use though) */
				memset(cartsram[slot],cartsramsize[slot],0xff);
				(*s)-=cartsramsize[slot];
			}
			
			i=4; while (i--) ascii_is_sram[slot][i]=0;
			break;
	}
	
	/* enable write */
	i=4; while (i--) ascii_sramwrite[slot][i]=1;
}

static int __fastcall maploadstate_ascii(int slot,int v,u8** s)
{
	switch (v) {
		case 1: case 2:
			maploadstate_ascii_12(slot,s);
			break;
		case STATE_VERSION_ASCII:
			maploadstatecur_ascii(slot,s);
			break;
		
		default: return FALSE;
	}
	
	return TRUE;
}

/* init */
static void mapinit_ascii(int slot)
{
	int f=0,i,m=0,p,s=8;
	
	/* init board */
	for (i=0;i<8;i++) {
		p=ascii_mainw[slot]>>(i<<2)&0xf;
		if (p==5) s=i; /* sram bit */
		m=m|(p&1)<<i; /* range mask */
	}
	
	ascii_bankbit[slot]=((ascii_board[slot]&A_BS_MASK)==A_16);
	ascii_bankmask[slot]=m<<ascii_bankbit[slot];
	cartmask[slot]|=ascii_bankmask[slot];
	
	/* sram */
	if (~s&8&&ascii_board[slot]&A_SS_MASK) {
		sram_load(slot,&cartsram[slot],0x200<<((ascii_board[slot]&A_SS_MASK)>>A_SS_SHIFT),NULL,0xff);
		
		ascii_srammask[slot]=(cartsramsize[slot]-1)&0x1fff;
		ascii_protectbit[slot]=((ascii_board[slot]&A_SP_MASK)!=0)<<(((ascii_board[slot]&A_SP_MASK)>>A_SP_SHIFT)-1)&0xff;
		ascii_srambit[slot]=1<<s;
	}
	else ascii_srambit[slot]=0;
	
	/* init handlers */
	mapgetstatesize[slot]=mapgetstatesize_ascii;
	mapsavestate[slot]=mapsavestate_ascii;
	maploadstatecur[slot]=maploadstatecur_ascii;
	maploadstate[slot]=maploadstate_ascii;
	
	for (i=0;i<4;i++) {
		/* 4 pages of 8KB or 2 pages of 16KB at $4000-$bfff, init to 0 */
		cartbank[slot][i+2]=i&ascii_bankbit[slot];
		
		ascii_is_sram[slot][i]=FALSE;
		ascii_sramwrite[slot][i]=TRUE;
		ascii_srambank[slot][i]=0;
	}
	
	/* flags */
	for (i=0;lutasciichip[i].uid!=(ascii_board[slot]&A_MC_MASK);i++) { ; }
	f=lutasciichip[i].flags;
	
	/* mapper chip at $6000-$7fff, sram at $4000-$bfff (if there) */
	if (cartsramsize[slot]) {
		mapsetcustom_read(slot,2,4,mapread_ascii_sram);
		mapsetcustom_write(slot,2+((f&A_MCF_SWLOW)==0),4-((f&A_MCF_SWLOW)==0),mapwrite_ascii_sram);
	}
	else mapsetcustom_write(slot,3,1,mapwrite_ascii);
	
	/* 16KB: bit 0,11,13 */
	if (ascii_bankbit[slot]) ascii_bankbit[slot]=0x2801;
}

/* media config dialog */
static void mce_a_setboard(HWND dialog,u32 w,u32 b)
{
	char t[0x10];
	int i;
	
	/* mapper chip */
	for (i=0;lutasciichip[i].uid!=(b&A_MC_MASK);i++) { ; }
	SendDlgItemMessage(dialog,IDC_MCE_A_MAPPER,CB_SETCURSEL,i,0);
	
	/* bank size */
	SendDlgItemMessage(dialog,IDC_MCE_A_BANKS,CB_SETCURSEL,(b&A_BS_MASK)==A_16,0);
	
	/* write editbox */
	sprintf(t,"%08x",w);
	for (i=0;i<8;i++)
	switch (t[i]) {
		case '5': t[i]='s'; break;
		case 'f': t[i]='u'; break;
		default:  t[i]='x'; break;
	}
	if (b&A_SP_MASK) t[(((b&A_SP_MASK)>>A_SP_SHIFT)-1)^7]='p';
	SetDlgItemText(dialog,IDC_MCE_A_WRITE,t);
	
	/* sram size */
	if (b&A_SS_MASK) SetDlgItemInt(dialog,IDC_MCE_A_SRAM,1<<(((b&A_SS_MASK)>>A_SS_SHIFT)-1),FALSE);
	else SetDlgItemInt(dialog,IDC_MCE_A_SRAM,0,FALSE);
}

static BOOL CALLBACK mce_a_dialog(HWND dialog,UINT msg,WPARAM wParam,LPARAM lParam)
{
	static int initial_board=0;
	static int cur_board=0;
	
	switch (msg) {
		
		case WM_INITDIALOG: {
			char t[0x100];
			HWND c;
			int i;
			
			SendDlgItemMessage(dialog,IDC_MCE_A_WRITE,EM_LIMITTEXT,8,0);
			SendDlgItemMessage(dialog,IDC_MCE_A_SRAM,EM_LIMITTEXT,2,0);
			
			/* fill comboboxes */
			c=GetDlgItem(dialog,IDC_MCE_A_BOARD);
			for (i=0;lutasciiboard[i].board!=~0;i++) {
				sprintf(t,"%s %s",lutasciiboard[i].company,lutasciiboard[i].name);
				SendMessage(c,CB_ADDSTRING,0,(LPARAM)((LPCTSTR)t));
			}
			
			c=GetDlgItem(dialog,IDC_MCE_A_MAPPER);
			for (i=0;lutasciichip[i].uid!=~0;i++) {
				sprintf(t,"%s %s",lutasciichip[i].company,lutasciichip[i].name);
				SendMessage(c,CB_ADDSTRING,0,(LPARAM)((LPCTSTR)t));
			}
			
			sprintf(t,"4*8KB"); SendDlgItemMessage(dialog,IDC_MCE_A_BANKS,CB_ADDSTRING,0,(LPARAM)((LPCTSTR)t));
			sprintf(t,"2*16KB"); SendDlgItemMessage(dialog,IDC_MCE_A_BANKS,CB_ADDSTRING,0,(LPARAM)((LPCTSTR)t));
			
			/* init board properties */
			for (i=0;(lutasciiboard[i].board&AB_MASK)!=(ascii_board_temp[mapper_extra_ref_slot]&AB_MASK);i++) { ; }
			SendDlgItemMessage(dialog,IDC_MCE_A_BOARD,CB_SETCURSEL,i,0); initial_board=cur_board=i;
			mce_a_setboard(dialog,ascii_mainw_temp[mapper_extra_ref_slot],ascii_board_temp[mapper_extra_ref_slot]);
			
			if ((ascii_board_temp[mapper_extra_ref_slot]&AB_MASK)!=AB_CUSTOM) {
				for (i=IDC_MCE_A_GROUPSTART;i<=IDC_MCE_A_GROUPEND;i++) EnableWindow(GetDlgItem(dialog,i),FALSE);
			}
			
			sprintf(t,"Slot %d: %s",mapper_extra_ref_slot+1,mapper_get_type_longname(mapper_extra_ref_type));
			SetWindowText(dialog,t);
			
			main_parent_window(dialog,MAIN_PW_LEFT,MAIN_PW_LEFT,mapper_extra_ref_pos.x,mapper_extra_ref_pos.y,0);
			
			break;
		}
		
		case WM_COMMAND:
			
			switch (LOWORD(wParam)) {
				
				/* change board */
				case IDC_MCE_A_BOARD: {
					int i;
					
					if ((HIWORD(wParam))==CBN_SELCHANGE&&(i=SendDlgItemMessage(dialog,LOWORD(wParam),CB_GETCURSEL,0,0))!=CB_ERR&&i!=cur_board) {
						if ((lutasciiboard[i].board&AB_MASK)!=AB_CUSTOM) mce_a_setboard(dialog,lutasciiboard[i].mainw,lutasciiboard[i].board); cur_board=i;
						for (i=IDC_MCE_A_GROUPSTART;i<=IDC_MCE_A_GROUPEND;i++) EnableWindow(GetDlgItem(dialog,i),(lutasciiboard[cur_board].board&AB_MASK)==AB_CUSTOM);
					}
						
					break;
				}
				
				/* close dialog */
				case IDOK: {
					int i,j,mc;
					char t[0x100];
					u32 b=0;
					memset(t,0,0x100);
					
					/* get properties / error checking */
					if (initial_board==cur_board&&(lutasciiboard[cur_board].board&AB_MASK)!=AB_CUSTOM) EndDialog(dialog,0); /* no change */
					b|=(lutasciiboard[cur_board].board&AB_MASK);
					
					if ((i=SendDlgItemMessage(dialog,IDC_MCE_A_MAPPER,CB_GETCURSEL,0,0))!=CB_ERR) b|=lutasciichip[i].uid;
					for (mc=0;lutasciichip[mc].uid!=(b&A_MC_MASK);mc++) { ; }
					
					if ((i=SendDlgItemMessage(dialog,IDC_MCE_A_BANKS,CB_GETCURSEL,0,0))!=CB_ERR&&i) b|=A_16;
					
					i=GetDlgItemInt(dialog,IDC_MCE_A_SRAM,NULL,FALSE);
					if (i!=0&&i!=1&&i!=2&&i!=4&&i!=8&&i!=16&&i!=32&&i!=64) {
						LOG_ERROR_WINDOW(dialog,"Invalid SRAM size.");
						return 0;
					}
					if (i) {
						for (j=1;~i&1;j++,i>>=1) { ; }
						b|=j<<A_SS_SHIFT;
						
						if (~lutasciichip[mc].flags&A_MCF_SRAM) {
							sprintf(t,"%s can't have SRAM.",lutasciichip[mc].name);
							LOG_ERROR_WINDOW(dialog,t);
							return 0;
						}
					}
					
					j=0;
					GetDlgItemText(dialog,IDC_MCE_A_WRITE,t,10);
					if (strlen(t)!=8) {
						LOG_ERROR_WINDOW(dialog,"Write bitmask must be 8 characters.");
						return 0;
					}
					for (i=0;i<8;i++)
					switch (t[i]) {
						case 'u': case 'U': t[i]='f'; break;
						case 'x': case 'X': t[i]='e'; break;
						
						/* sram bit */
						case 's': case 'S':
							if (j++) {
								LOG_ERROR_WINDOW(dialog,"Only one 's' allowed in write bitmask.");
								return 0;
							}
							t[i]='5'; break;
						
						/* write-protect bit */
						case 'p': case 'P':
							if (~lutasciichip[mc].flags&A_MCF_PROTECT) {
								sprintf(t,"%s can't have write-protect.",lutasciichip[mc].name);
								LOG_ERROR_WINDOW(dialog,t);
								return 0;
							}
							
							if (b&A_SP_MASK) {
								LOG_ERROR_WINDOW(dialog,"Only one 'p' allowed in write bitmask.");
								return 0;
							}
							
							b|=((i^7)+1)<<A_SP_SHIFT;
							t[i]='e'; break;
						
						default:
							sprintf(t,"Write bit '%c' is unknown. Use 'u' for unmapped bits, 'x' for\ndon't care bits, 's' for SRAM bit, and 'p' for write-protect bit.",t[i]);
							LOG_ERROR_WINDOW(dialog,t);
							return 0;
					}
					
					if ((!j&&b&A_SS_MASK)||(j&&!(b&A_SS_MASK))||(!j&&b&A_SP_MASK)) {
						LOG_ERROR_WINDOW(dialog,"Write bitmask SRAM conflict.");
						return 0;
					}
					
					sscanf(t,"%x",&ascii_mainw_temp[mapper_extra_ref_slot]);
					ascii_board_temp[mapper_extra_ref_slot]=b;
					
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
