/* Korean bootleg carts */

/* multicarts */
/*
	interesting games (don't know of existing stand-alone dumps):
	90-in-1 -> page 2 -> 01:			"Cannon Turbo" (Takeru)
	90-in-1 -> page 2 -> 18:			"Apollo Technica" (?)
	
	64-in-1 -> page 3 -> col 3 row 1:	"Safari-X" (Prosoft, 1987)
	126-in-1 -> page 6 -> col 2 row 1:	"Safari-X" (Prosoft, 1987)
	90-in-1 -> page 2 -> 05:			"Super Columns/Tetris" (Hi-Com, 1990)
	30-in-1 -> page 1 -> 03:			"Tetris II" (Prosoft, 1989)
	64-in-1 -> page 1 -> col 1 row 3:	"Tetris II" (Prosoft, 1989)
	126-in-1 -> page 3 -> col 2 row 6:	"Tetris II" (Prosoft, 1989)
*/

/* --- Bootleg 80-in-1
	used by 30-in-1, 64-in-1, 80-in-1
*/
/* (without having been hardware reverse engineered, and not many games to test it with, I'm not sure if this implementation is accurate) */
static void __fastcall mapwrite_btl80(int slot,u16 a,u8 v)
{
	/* all pages switchable at $4000-$7fff */
	cartbank[slot][(a&3)+2]=v&cartmask[slot];
	refresh_cb(slot);
}

static void mapinit_btl80(int slot)
{
	int i;
	
	/* 4 pages of 8KB at $4000-$bfff, init to 0 */
	for (i=2;i<6;i++) cartbank[slot][i]=0;
	
	mapsetcustom_write(slot,2,2,mapwrite_btl80);
}





/* --- Bootleg 90-in-1 (single game) */
/* (without having been hardware reverse engineered, and only 1 game to test it with, I'm not sure if this implementation is accurate) */
static int btl90_init_done[2]={0,0};

static __inline__ void btl90_bankswitch(int slot,u8 v)
{
	if (v&0x80) {
		/* 32KB mode */
		int bank=(v&0xfe)<<1&cartmask[slot];
		cartbank[slot][2]=bank++;
		cartbank[slot][3]=bank++;
		cartbank[slot][4]=bank++;
		cartbank[slot][5]=bank;
	}
	else {
		/* 16KB mode */
		cartbank[slot][2]=cartbank[slot][4]=v<<1&cartmask[slot];
		cartbank[slot][3]=cartbank[slot][5]=(v<<1&cartmask[slot])|1;
	}
	
	refresh_cb(slot);
}

static void __fastcall mapwrite_btl90(u8 v)
{
	if (btl90_init_done[0]) btl90_bankswitch(0,v);
	if (btl90_init_done[1]) btl90_bankswitch(1,v);
}

static void mapclean_btl90(int slot)
{
	btl90_init_done[slot]=FALSE;
	if (!btl90_init_done[slot^1]) io_setwriteport(0x77,NULL);
}

static void mapinit_btl90(int slot)
{
	mapclean[slot]=mapclean_btl90;
	btl90_init_done[slot]=TRUE;
	
	/* I/O at port $77 instead of standard memory mapped I/O */
	io_setwriteport(0x77,mapwrite_btl90);
	
	/* init to 0 */
	cartbank[slot][2]=cartbank[slot][4]=0;
	cartbank[slot][3]=cartbank[slot][5]=1;
}





/* --- Bootleg 126-in-1 (single game) */
/* (without having been hardware reverse engineered, and only 1 game to test it with, I'm not sure if this implementation is accurate) */
static void __fastcall mapwrite_btl126(int slot,u16 a,u8 v)
{
	/* both pages switchable at $4000-$7fff */
	int page=(a<<1&2)+2;
	
	cartbank[slot][page++]=v<<1&cartmask[slot];
	cartbank[slot][page]=(v<<1&cartmask[slot])|1;
	refresh_cb(slot);
}

static void mapinit_btl126(int slot)
{
	/* 2 pages of 16KB at $4000-$bfff, init to 0 */
	cartbank[slot][2]=cartbank[slot][4]=0;
	cartbank[slot][3]=cartbank[slot][5]=1;
	
	mapsetcustom_write(slot,2,2,mapwrite_btl126);
}
