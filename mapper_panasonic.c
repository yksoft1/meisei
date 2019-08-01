/* Panasonic mappers */

/* --- Panasoft PAC, mapper chip: MEI MB674175U, board: JCI-C1H DFUP0204ZAJ
	8KB SRAM cart supported by Hydlide 3, Psychic War, ..?
*/
static const char* pac_fn="sram.pac";

/* 8KB SRAM in $4000-$5fff if $5ffe=$4d and $5fff=$69 */
static u8 __fastcall mapread_pac(int slot,u16 a)
{
	if ((cartsram[slot][0x1ffe]|cartsram[slot][0x1fff]<<8)==0x694d) return cartsram[slot][a&0x1fff];
	else return UNMAPPED;
}

static void __fastcall mapwrite_pac(int slot,u16 a,u8 v)
{
	a&=0x1fff;
	
	if (a>0x1ffd) cartsram[slot][a]=v;
	else if ((cartsram[slot][0x1ffe]|cartsram[slot][0x1fff]<<8)==0x694d) cartsram[slot][a]=v;
}

/* init/clean */
static void mapclean_pac(int slot)
{
	if (cartsram[slot]) {
		/* SRAM is saved in MSX compatible FM-PAC util format:
		16 bytes text header + 8192-2 bytes SRAM data */
		memmove(cartsram[slot]+16,cartsram[slot],0x1ffe);
		memcpy(cartsram[slot],"PAC2 BACKUP DATA",16);
	}
	
	sram_save(slot,cartsram[slot],8206,pac_fn);
}

static void mapinit_pac(int slot)
{
	if (sram_load(slot,&cartsram[slot],8206,pac_fn,0xff)) {
		memmove(cartsram[slot],cartsram[slot]+16,0x1ffe);
		cartsram[slot][0x1ffe]=cartsram[slot][0x1fff]=0xff;
	}
	cartsramsize[slot]=0x2000; /* mem size */
	mapclean[slot]=mapclean_pac;
	
	/* not mirrored */
	mapsetcustom_read(slot,2,1,mapread_pac);
	mapsetcustom_write(slot,2,1,mapwrite_pac);
}
