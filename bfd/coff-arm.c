/* BFD back-end for ARM COFF files.
   Copyright 1990, 91, 92, 93, 94, 95, 96, 1997 Free Software Foundation, Inc.
   Written by Cygnus Support.

This file is part of BFD, the Binary File Descriptor library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "bfd.h"
#include "sysdep.h"
#include "libbfd.h"

#include "coff/arm.h"

#include "coff/internal.h"

#ifdef COFF_WITH_PE
#include "coff/pe.h"
#endif

#include "libcoff.h"

#define APCS_26_FLAG(       abfd )	(coff_data (abfd)->flags & F_APCS_26)
#define APCS_FLOAT_FLAG(    abfd )	(coff_data (abfd)->flags & F_APCS_FLOAT)
#define PIC_FLAG(           abfd )	(coff_data (abfd)->flags & F_PIC)
#define APCS_SET(           abfd )	(coff_data (abfd)->flags & F_APCS_SET)
#define SET_APCS_FLAGS(     abfd, flgs)	(coff_data (abfd)->flags = \
					(coff_data (abfd)->flags & ~ (F_APCS_26 | F_APCS_FLOAT | F_PIC)) \
					 | (flgs | F_APCS_SET))
#define INTERWORK_FLAG(     abfd ) 	(coff_data (abfd)->flags & F_INTERWORK)
#define INTERWORK_SET(      abfd ) 	(coff_data (abfd)->flags & F_INTERWORK_SET)
#define SET_INTERWORK_FLAG( abfd, flg )	(coff_data (abfd)->flags = \
					(coff_data (abfd)->flags & ~ F_INTERWORK) \
					 | (flg | F_INTERWORK_SET))


static boolean coff_arm_relocate_section
  PARAMS ((bfd *, struct bfd_link_info *, bfd *, asection *, bfd_byte *,
           struct internal_reloc *, struct internal_syment *, asection **));

static bfd_reloc_status_type
aoutarm_fix_pcrel_26_done PARAMS ((bfd *, arelent *, asymbol *, PTR,
				  asection *, bfd *, char **));

static bfd_reloc_status_type
aoutarm_fix_pcrel_26 PARAMS ((bfd *, arelent *, asymbol *, PTR,
			     asection *, bfd *, char **));

static bfd_reloc_status_type
coff_thumb_pcrel_23 PARAMS ((bfd *, arelent *, asymbol *, PTR,
                             asection *, bfd *, char **));

static bfd_reloc_status_type
coff_thumb_pcrel_12 PARAMS ((bfd *, arelent *, asymbol *, PTR,
                             asection *, bfd *, char **));

static bfd_reloc_status_type
coff_thumb_pcrel_9 PARAMS ((bfd *, arelent *, asymbol *, PTR,
                            asection *, bfd *, char **));

static bfd_reloc_status_type
coff_arm_reloc PARAMS ((bfd *, arelent *, asymbol *, PTR, asection *, bfd *,
			char **));

static boolean
coff_arm_adjust_symndx PARAMS ((bfd *, struct bfd_link_info *, bfd *,
				asection *, struct internal_reloc *,
				boolean *));

/* The linker script knows the section names for placement.
   The entry_names are used to do simple name mangling on the stubs.
   Given a function name, and its type, the stub can be found. The
   name can be changed. The only requirement is the %s be present.
   */
   
#define THUMB2ARM_GLUE_SECTION_NAME ".glue_7t"
#define THUMB2ARM_GLUE_ENTRY_NAME   "__%s_from_thumb"

#define ARM2THUMB_GLUE_SECTION_NAME ".glue_7"
#define ARM2THUMB_GLUE_ENTRY_NAME   "__%s_from_arm"

/* Used by the assembler. */
static bfd_reloc_status_type
coff_arm_reloc (abfd, reloc_entry, symbol, data, input_section, output_bfd,
		 error_message)
     bfd *abfd;
     arelent *reloc_entry;
     asymbol *symbol;
     PTR data;
     asection *input_section;
     bfd *output_bfd;
     char **error_message;
{
  symvalue diff;
  if (output_bfd == (bfd *) NULL)
    return bfd_reloc_continue;

  diff = reloc_entry->addend;

#define DOIT(x) \
  x = ((x & ~howto->dst_mask) | (((x & howto->src_mask) + diff) & howto->dst_mask))

    if (diff != 0)
      {
	reloc_howto_type *howto = reloc_entry->howto;
	unsigned char *addr = (unsigned char *) data + reloc_entry->address;

	switch (howto->size)
	  {
	  case 0:
	    {
	      char x = bfd_get_8 (abfd, addr);
	      DOIT (x);
	      bfd_put_8 (abfd, x, addr);
	    }
	    break;

	  case 1:
	    {
	      short x = bfd_get_16 (abfd, addr);
	      DOIT (x);
	      bfd_put_16 (abfd, x, addr);
	    }
	    break;

	  case 2:
	    {
	      long x = bfd_get_32 (abfd, addr);
	      DOIT (x);
	      bfd_put_32 (abfd, x, addr);
	    }
	    break;

	  default:
	    abort ();
	  }
      }

  /* Now let bfd_perform_relocation finish everything up.  */
  return bfd_reloc_continue;
}

#define TARGET_UNDERSCORE '_'

#ifndef PCRELOFFSET
#define PCRELOFFSET true
#endif

/* These most certainly belong somewhere else. Just had to get rid of
   the manifest constants in the code. */

#define ARM_8        0
#define ARM_16       1
#define ARM_32       2
#define ARM_26       3
#define ARM_DISP8    4
#define ARM_DISP16   5
#define ARM_DISP32   6
#define ARM_26D      7
/* 8 is unused */
#define ARM_NEG16    9
#define ARM_NEG32   10
#define ARM_RVA32   11
#define ARM_THUMB9  12
#define ARM_THUMB12 13
#define ARM_THUMB23 14

static reloc_howto_type aoutarm_std_reloc_howto[] = 
{
  /* type              rs size bsz  pcrel bitpos ovrf                     sf name     part_inpl readmask  setmask    pcdone */
  HOWTO(ARM_8,			/* type */
	0,			/* rightshift */
	0,			/* size */
	8,			/* bitsize */
	false,			/* pc_relative */
	0,			/* bitpos */
	complain_overflow_bitfield, /* complain_on_overflow */
	coff_arm_reloc,		/* special_function */
	"ARM_8",		/* name */
        true,			/* partial_inplace */
	0x000000ff,		/* src_mask */
	0x000000ff,		/* dst_mask */
	PCRELOFFSET		/* pcrel_offset */),
  HOWTO(ARM_16,  
	0, 
	1, 
	16, 
	false,
	0,
	complain_overflow_bitfield,
	coff_arm_reloc,
	"ARM_16", 
	true,
	0x0000ffff,
	0x0000ffff, 
	PCRELOFFSET),
  HOWTO(ARM_32, 
	0,
	2, 
	32,
	false,
	0,
	complain_overflow_bitfield,
	coff_arm_reloc,
	"ARM_32",
        true,
	0xffffffff,
	0xffffffff,
	PCRELOFFSET),
  HOWTO(ARM_26,
	2,
	2,
	26,
	true,
	0,
	complain_overflow_signed,
	aoutarm_fix_pcrel_26 ,
	"ARM_26",
	false,
	0x00ffffff,
	0x00ffffff, 
	PCRELOFFSET),
  HOWTO(ARM_DISP8,        
	0,
	0,
	8, 
	true,
	0,
	complain_overflow_signed, 
	coff_arm_reloc,
	"ARM_DISP8",  
	true,
	0x000000ff,
	0x000000ff,
	true),
  HOWTO( ARM_DISP16, 
	0,
	1,
	16,
	true,
	0,
	complain_overflow_signed, 
	coff_arm_reloc,
	"ARM_DISP16",
	true,
	0x0000ffff,
	0x0000ffff,
	true),
  HOWTO( ARM_DISP32,
	0,
	2,
	32,
	true,
	0,
	complain_overflow_signed, 
 	coff_arm_reloc,
	"ARM_DISP32",
	true,
	0xffffffff,
	0xffffffff,
	true),
  HOWTO( ARM_26D,  
	2, 
	2,
	26,
	false,
	0,
	complain_overflow_signed,
	aoutarm_fix_pcrel_26_done, 
	"ARM_26D",
	true,
	0x00ffffff,
	0x0,
	false),
  /* 8 is unused */
  {-1},
  HOWTO( ARM_NEG16,
	0,
	-1,
	16,
	false,
	0, 
	complain_overflow_bitfield,
	coff_arm_reloc,
	"ARM_NEG16",
        true, 
	0x0000ffff,
	0x0000ffff, 
	false),
  HOWTO( ARM_NEG32, 
	0, 
	-2,
	32,
	false,
	0,
	complain_overflow_bitfield,
	coff_arm_reloc,
	"ARM_NEG32",
        true,
	0xffffffff,
	0xffffffff,
	false),
  HOWTO( ARM_RVA32, 
	0,
	2, 
	32,
	false,
	0,
	complain_overflow_bitfield,
	coff_arm_reloc,
	"ARM_RVA32",
        true,
	0xffffffff,
	0xffffffff,
	PCRELOFFSET),
  HOWTO( ARM_THUMB9,
	1,
	1,
	9,
	true,
	0,
	complain_overflow_signed,
	coff_thumb_pcrel_9 ,
	"ARM_THUMB9",
	false,
	0x000000ff,
	0x000000ff, 
	PCRELOFFSET),
  HOWTO( ARM_THUMB12,
	1,
	1,
	12,
	true,
	0,
	complain_overflow_signed,
	coff_thumb_pcrel_12 ,
	"ARM_THUMB12",
	false,
	0x000007ff,
	0x000007ff, 
	PCRELOFFSET),
  HOWTO( ARM_THUMB23,
	1,
	2,
	23,
	true,
	0,
	complain_overflow_signed,
	coff_thumb_pcrel_23 ,
	"ARM_THUMB23",
	false,
	0x07ff07ff,
	0x07ff07ff, 
	PCRELOFFSET),
};
#ifdef COFF_WITH_PE
/* Return true if this relocation should
   appear in the output .reloc section. */

static boolean in_reloc_p (abfd, howto)
     bfd * abfd;
     reloc_howto_type *howto;
{
  return !howto->pc_relative && howto->type != 11;
}     
#endif


#define RTYPE2HOWTO(cache_ptr, dst) \
	    (cache_ptr)->howto = aoutarm_std_reloc_howto + (dst)->r_type;

#define coff_rtype_to_howto coff_arm_rtype_to_howto

static reloc_howto_type *
coff_arm_rtype_to_howto (abfd, sec, rel, h, sym, addendp)
     bfd *abfd;
     asection *sec;
     struct internal_reloc *rel;
     struct coff_link_hash_entry *h;
     struct internal_syment *sym;
     bfd_vma *addendp;
{
  reloc_howto_type *howto;

  howto = aoutarm_std_reloc_howto + rel->r_type;

  if (rel->r_type == ARM_RVA32)
    {
      *addendp -= pe_data(sec->output_section->owner)->pe_opthdr.ImageBase;
    }

  /* The relocation_section function will skip pcrel_offset relocs
     when doing a relocateable link.  However, we want to convert
     ARM26 to ARM26D relocs if possible.  We return a fake howto in
     this case without pcrel_offset set, and adjust the addend to
     compensate.  */
  if (rel->r_type == ARM_26
      && h != NULL
      && (h->root.type == bfd_link_hash_defined
	  || h->root.type == bfd_link_hash_defweak)
      && h->root.u.def.section->output_section == sec->output_section)
    {
      static reloc_howto_type fake_arm26_reloc = 
	HOWTO (ARM_26,
	       2,
	       2,
	       26,
	       true,
	       0,
	       complain_overflow_signed,
	       aoutarm_fix_pcrel_26 ,
	       "ARM_26",
	       false,
	       0x00ffffff,
	       0x00ffffff, 
	       false);

      *addendp -= rel->r_vaddr - sec->vma;
      return & fake_arm26_reloc;
    }

  return howto;

}
/* Used by the assembler. */

static bfd_reloc_status_type
aoutarm_fix_pcrel_26_done (abfd, reloc_entry, symbol, data, input_section,
			  output_bfd, error_message)
     bfd *abfd;
     arelent *reloc_entry;
     asymbol *symbol;
     PTR data;
     asection *input_section;
     bfd *output_bfd;
     char **error_message;
{
  /* This is dead simple at present.  */
  return bfd_reloc_ok;
}

/* Used by the assembler. */

static bfd_reloc_status_type
aoutarm_fix_pcrel_26 (abfd, reloc_entry, symbol, data, input_section,
		     output_bfd, error_message)
     bfd *abfd;
     arelent *reloc_entry;
     asymbol *symbol;
     PTR data;
     asection *input_section;
     bfd *output_bfd;
     char **error_message;
{
  bfd_vma relocation;
  bfd_size_type addr = reloc_entry->address;
  long target = bfd_get_32 (abfd, (bfd_byte *) data + addr);
  bfd_reloc_status_type flag = bfd_reloc_ok;
  
  /* If this is an undefined symbol, return error */
  if (symbol->section == &bfd_und_section
      && (symbol->flags & BSF_WEAK) == 0)
    return output_bfd ? bfd_reloc_continue : bfd_reloc_undefined;

  /* If the sections are different, and we are doing a partial relocation,
     just ignore it for now.  */
  if (symbol->section->name != input_section->name
      && output_bfd != (bfd *)NULL)
    return bfd_reloc_continue;

  relocation = (target & 0x00ffffff) << 2;
  relocation = (relocation ^ 0x02000000) - 0x02000000; /* Sign extend */
  relocation += symbol->value;
  relocation += symbol->section->output_section->vma;
  relocation += symbol->section->output_offset;
  relocation += reloc_entry->addend;
  relocation -= input_section->output_section->vma;
  relocation -= input_section->output_offset;
  relocation -= addr;
  
  if (relocation & 3)
    return bfd_reloc_overflow;

  /* Check for overflow */
  if (relocation & 0x02000000)
    {
      if ((relocation & ~0x03ffffff) != ~0x03ffffff)
	flag = bfd_reloc_overflow;
    }
  else if (relocation & ~0x03ffffff)
    flag = bfd_reloc_overflow;

  target &= ~0x00ffffff;
  target |= (relocation >> 2) & 0x00ffffff;
  bfd_put_32 (abfd, target, (bfd_byte *) data + addr);

  /* Now the ARM magic... Change the reloc type so that it is marked as done.
     Strictly this is only necessary if we are doing a partial relocation.  */
  reloc_entry->howto = &aoutarm_std_reloc_howto[ARM_26D];

  return flag;
}

typedef enum {bunknown, b9, b12, b23} thumb_pcrel_branchtype;

static bfd_reloc_status_type
coff_thumb_pcrel_common (abfd, reloc_entry, symbol, data, input_section,
		     output_bfd, error_message, btype)
     bfd *abfd;
     arelent *reloc_entry;
     asymbol *symbol;
     PTR data;
     asection *input_section;
     bfd *output_bfd;
     char **error_message;
     thumb_pcrel_branchtype btype;
{
  bfd_vma relocation;
  bfd_size_type addr = reloc_entry->address;
  long target = bfd_get_32 (abfd, (bfd_byte *) data + addr);
  bfd_reloc_status_type flag = bfd_reloc_ok;
  bfd_vma dstmsk;
  bfd_vma offmsk;
  bfd_vma signbit;

  /* NOTE: This routine is currently used by GAS, but not by the link
     phase.  */

  switch (btype)
    {
    case b9:
      dstmsk  = 0x000000ff;
      offmsk  = 0x000001fe;
      signbit = 0x00000100;
      break;

    case b12:
      dstmsk  = 0x000007ff;
      offmsk  = 0x00000ffe;
      signbit = 0x00000800;
      break;

    case b23:
      dstmsk  = 0x07ff07ff;
      offmsk  = 0x007fffff;
      signbit = 0x00400000;
      break;

    default:
      abort ();
    }
  
  /* If this is an undefined symbol, return error */
  if (symbol->section == &bfd_und_section
      && (symbol->flags & BSF_WEAK) == 0)
    return output_bfd ? bfd_reloc_continue : bfd_reloc_undefined;

  /* If the sections are different, and we are doing a partial relocation,
     just ignore it for now.  */
  if (symbol->section->name != input_section->name
      && output_bfd != (bfd *)NULL)
    return bfd_reloc_continue;

  switch (btype)
    {
    case b9:
    case b12:
      relocation = ((target & dstmsk) << 1);
      break;

    case b23:
      if (bfd_big_endian (abfd))
	relocation = ((target & 0x7ff) << 1)  | ((target & 0x07ff0000) >> 4);
      else
	relocation = ((target & 0x7ff) << 12) | ((target & 0x07ff0000) >> 15);
      break;
    }

  relocation = (relocation ^ signbit) - signbit; /* Sign extend */
  relocation += symbol->value;
  relocation += symbol->section->output_section->vma;
  relocation += symbol->section->output_offset;
  relocation += reloc_entry->addend;
  relocation -= input_section->output_section->vma;
  relocation -= input_section->output_offset;
  relocation -= addr;

  if (relocation & 1)
    return bfd_reloc_overflow;

  /* Check for overflow */
  if (relocation & signbit)
    {
      if ((relocation & ~offmsk) != ~offmsk)
	flag = bfd_reloc_overflow;
    }
  else if (relocation & ~offmsk)
    flag = bfd_reloc_overflow;

  target &= ~dstmsk;
  switch (btype)
   {
   case b9:
   case b12:
     target |= (relocation >> 1);
     break;

   case b23:
     if (bfd_big_endian (abfd))
       target |= ((relocation & 0xfff) >> 1)  | ((relocation << 4)  & 0x07ff0000);
     else
       target |= ((relocation & 0xffe) << 15) | ((relocation >> 12) & 0x7ff);
     break;
   }

  bfd_put_32 (abfd, target, (bfd_byte *) data + addr);

  /* Now the ARM magic... Change the reloc type so that it is marked as done.
     Strictly this is only necessary if we are doing a partial relocation.  */
  reloc_entry->howto = & aoutarm_std_reloc_howto [ARM_26D];
  
  /* TODO: We should possibly have DONE entries for the THUMB PCREL relocations */
  return flag;
}

static bfd_reloc_status_type
coff_thumb_pcrel_23 (abfd, reloc_entry, symbol, data, input_section,
		     output_bfd, error_message)
     bfd *abfd;
     arelent *reloc_entry;
     asymbol *symbol;
     PTR data;
     asection *input_section;
     bfd *output_bfd;
     char **error_message;
{
  return coff_thumb_pcrel_common (abfd, reloc_entry, symbol, data,
                                  input_section, output_bfd, error_message, b23);
}

static bfd_reloc_status_type
coff_thumb_pcrel_12 (abfd, reloc_entry, symbol, data, input_section,
		     output_bfd, error_message)
     bfd *abfd;
     arelent *reloc_entry;
     asymbol *symbol;
     PTR data;
     asection *input_section;
     bfd *output_bfd;
     char **error_message;
{
  return coff_thumb_pcrel_common (abfd, reloc_entry, symbol, data,
                                  input_section, output_bfd, error_message, b12);
}

static bfd_reloc_status_type
coff_thumb_pcrel_9 (abfd, reloc_entry, symbol, data, input_section,
		     output_bfd, error_message)
     bfd *abfd;
     arelent *reloc_entry;
     asymbol *symbol;
     PTR data;
     asection *input_section;
     bfd *output_bfd;
     char **error_message;
{
  return coff_thumb_pcrel_common (abfd, reloc_entry, symbol, data,
                                  input_section, output_bfd, error_message, b9);
}


static CONST struct reloc_howto_struct *
arm_reloc_type_lookup(abfd,code)
      bfd *abfd;
      bfd_reloc_code_real_type code;
{
#define ASTD(i,j)       case i: return &aoutarm_std_reloc_howto[j]
  if (code == BFD_RELOC_CTOR)
    switch (bfd_get_arch_info (abfd)->bits_per_address)
      {
      case 32:
        code = BFD_RELOC_32;
        break;
      default: return (CONST struct reloc_howto_struct *) 0;
      }

  switch (code)
    {
      ASTD (BFD_RELOC_8,                    ARM_8);
      ASTD (BFD_RELOC_16,                   ARM_16);
      ASTD (BFD_RELOC_32,                   ARM_32);
      ASTD (BFD_RELOC_ARM_PCREL_BRANCH,     ARM_26);
      ASTD (BFD_RELOC_8_PCREL,              ARM_DISP8);
      ASTD (BFD_RELOC_16_PCREL,             ARM_DISP16);
      ASTD (BFD_RELOC_32_PCREL,             ARM_DISP32);
      ASTD (BFD_RELOC_RVA,                  ARM_RVA32);
      ASTD (BFD_RELOC_THUMB_PCREL_BRANCH9,  ARM_THUMB9);
      ASTD (BFD_RELOC_THUMB_PCREL_BRANCH12, ARM_THUMB12);
      ASTD (BFD_RELOC_THUMB_PCREL_BRANCH23, ARM_THUMB23);
    default: return (CONST struct reloc_howto_struct *) 0;
    }
}


#define coff_bfd_reloc_type_lookup arm_reloc_type_lookup

#define COFF_DEFAULT_SECTION_ALIGNMENT_POWER (2)
#define COFF_PAGE_SIZE 0x1000
/* Turn a howto into a reloc  nunmber */

#define SELECT_RELOC(x,howto) { x.r_type = howto->type; }
#define BADMAG(x) ARMBADMAG(x)
#define ARM 1			/* Customize coffcode.h */

/* The set of global variables that mark the total size of each kind
   of glue required. */
long int global_thumb_glue_size = 0;
long int global_arm_glue_size = 0;

bfd* bfd_of_glue_owner = 0;

/* some typedefs for holding instructions */
typedef unsigned long int insn32;
typedef unsigned short int insn16;


/* The thumb form of a long branch is a bit finicky, because the offset
   encoding is split over two fields, each in it's own instruction. They
   can occur in any order. So given a thumb form of long branch, and an 
   offset, insert the offset into the thumb branch and return finished
   instruction. 

   It takes two thumb instructions to encode the target address. Each has 
   11 bits to invest. The upper 11 bits are stored in one (identifed by
   H-0.. see below), the lower 11 bits are stored in the other (identified 
   by H-1). 

   Combine together and shifted left by 1 (it's a half word address) and 
   there you have it.

     Op: 1111 = F,
     H-0, upper address-0 = 000
     Op: 1111 = F,
     H-1, lower address-0 = 800

   They can be ordered either way, but the arm tools I've seen always put 
   the lower one first. It probably doesn't matter. krk@cygnus.com

   XXX:  Actually the order does matter.  The second instruction (H-1)
   moves the computed address into the PC, so it must be the second one
   in the sequence.  The problem, however is that whilst little endian code
   stores the instructions in HI then LOW order, big endian code does the
   reverse.  nickc@cygnus.com  */

#define LOW_HI_ORDER 0xF800F000
#define HI_LOW_ORDER 0xF000F800

static insn32
insert_thumb_branch (br_insn, rel_off)
     insn32 br_insn;
     int rel_off;
{
  unsigned int low_bits;
  unsigned int high_bits;


  BFD_ASSERT((rel_off & 1) != 1);

  rel_off >>= 1;                              /* half word aligned address */
  low_bits = rel_off & 0x000007FF;            /* the bottom 11 bits */
  high_bits = ( rel_off >> 11 ) & 0x000007FF; /* the top 11 bits */

  if ((br_insn & LOW_HI_ORDER) == LOW_HI_ORDER)
    br_insn = LOW_HI_ORDER | (low_bits << 16) | high_bits;
  else if ((br_insn & HI_LOW_ORDER) == HI_LOW_ORDER)
    br_insn = HI_LOW_ORDER | (high_bits << 16) | low_bits;
  else
    abort(); /* error - not a valid branch instruction form */

  /* FIXME: abort is probably not the right call. krk@cygnus.com */

  return br_insn;
}


static struct coff_link_hash_entry *
find_thumb_glue (info, name, input_bfd)
     struct bfd_link_info * info;
     char *                 name;
     bfd *                  input_bfd;
{
  char *tmp_name = 0;

  struct coff_link_hash_entry *myh = 0;

  tmp_name = ((char *)
	 bfd_malloc (strlen (name) + strlen (THUMB2ARM_GLUE_ENTRY_NAME)));

  BFD_ASSERT (tmp_name);

  sprintf (tmp_name, THUMB2ARM_GLUE_ENTRY_NAME, name);
  
  myh = coff_link_hash_lookup (coff_hash_table (info),
			       tmp_name,
			       false, false, true);
  if (myh == NULL)
    {
      _bfd_error_handler ("%s: unable to find THUMB glue '%s' for `%s'",
			  bfd_get_filename (input_bfd), tmp_name, name);
    }
  
  free (tmp_name);

  return myh;
}

static struct coff_link_hash_entry *
find_arm_glue (info, name, input_bfd)
     struct bfd_link_info * info;
     char *                 name;
     bfd *                  input_bfd;
{
  char *tmp_name = 0;
  struct coff_link_hash_entry *myh = 0;

  tmp_name = ((char *)
	      bfd_malloc (strlen (name) + strlen (ARM2THUMB_GLUE_ENTRY_NAME)));

  BFD_ASSERT (tmp_name);

  sprintf (tmp_name, ARM2THUMB_GLUE_ENTRY_NAME, name);
  
  myh = coff_link_hash_lookup (coff_hash_table (info),
			       tmp_name,
			       false, false, true);

  if (myh == NULL)
    {
      _bfd_error_handler ("%s: unable to find ARM glue '%s' for `%s'",
			  bfd_get_filename (input_bfd), tmp_name, name);
    }
  
  free (tmp_name);

  return myh;
}

/*
  ARM->Thumb glue:

       .arm
       __func_from_arm:
	     ldr r12, __func_addr
	     bx  r12
       __func_addr:
            .word func    @ behave as if you saw a ARM_32 reloc

   ldr ip,8 <__func_addr> e59fc000
   bx  ip                 e12fff1c
   .word func             00000001

*/

#define ARM2THUMB_GLUE_SIZE 12
static insn32 
        a2t1_ldr_insn = 0xe59fc000,
	a2t2_bx_r12_insn = 0xe12fff1c,
	a2t3_func_addr_insn = 0x00000001;

/*
   Thumb->ARM:

   .thumb
   .align 2
      __func_from_thumb:
	   bx pc
	   nop
   .arm
      __func_change_to_arm:
           b func


   bx  pc    4778
   nop       0000
   b   func  eafffffe

*/

#define THUMB2ARM_GLUE_SIZE 8
static insn16
        t2a1_bx_pc_insn    = 0x4778,
        t2a2_noop_insn     = 0x46c0;
static insn32
        t2a3_b_insn        = 0xea000000;

/* TODO:
     We should really create new local (static) symbols in destination
     object for each stub we create.  We should also create local
     (static) symbols within the stubs when switching between ARM and
     Thumb code.  This will ensure that the debugger and disassembler
     can present a better view of stubs.

     We can treat stubs like literal sections, and for the THUMB9 ones
     (short addressing range) we should be able to insert the stubs
     between sections. i.e. the simplest approach (since relocations
     are done on a section basis) is to dump the stubs at the end of
     processing a section. That way we can always try and minimise the
     offset to and from a stub. However, this does not map well onto
     the way that the linker/BFD does its work: mapping all input
     sections to output sections via the linker script before doing
     all the processing.

     Unfortunately it may be easier to just to disallow short range
     Thumb->ARM stubs (i.e. no conditional inter-working branches,
     only branch-and-link (BL) calls.  This will simplify the processing
     since we can then put all of the stubs into their own section.

  TODO:
     On a different subject, rather than complaining when a
     branch cannot fit in the number of bits available for the
     instruction we should generate a trampoline stub (needed to
     address the complete 32bit address space).  */

/* The standard COFF backend linker does not cope with the special 
   Thumb BRANCH23 relocation.  The alternative would be to split the
   BRANCH23 into seperate HI23 and LO23 relocations. However, it is a
   bit simpler simply providing our own relocation driver. */

/* The reloc processing routine for the ARM/Thumb COFF linker.  NOTE:
   This code is a very slightly modified copy of
   _bfd_coff_generic_relocate_section.  It would be a much more
   maintainable solution to have a MACRO that could be expanded within
   _bfd_coff_generic_relocate_section that would only be provided for
   ARM/Thumb builds.  It is only the code marked THUMBEXTENSION that
   is different from the original.  */

static boolean
coff_arm_relocate_section (output_bfd, info, input_bfd, input_section,
                           contents, relocs, syms, sections)
     bfd *output_bfd;
     struct bfd_link_info *info;
     bfd *input_bfd;
     asection *input_section;
     bfd_byte *contents;
     struct internal_reloc *relocs;
     struct internal_syment *syms;
     asection **sections;
{
  struct internal_reloc *rel;
  struct internal_reloc *relend;

  rel = relocs;
  relend = rel + input_section->reloc_count;

  for (; rel < relend; rel++)
    {
      int done = 0;
      long symndx;
      struct coff_link_hash_entry *h;
      struct internal_syment *sym;
      bfd_vma addend;
      bfd_vma val;
      reloc_howto_type *howto;
      bfd_reloc_status_type rstat;
      bfd_vma h_val;

      symndx = rel->r_symndx;

      if (symndx == -1)
	{
	  h = NULL;
	  sym = NULL;
	}
      else
	{    
	  h = obj_coff_sym_hashes (input_bfd)[symndx];
	  sym = syms + symndx;
	}

      /* COFF treats common symbols in one of two ways.  Either the
         size of the symbol is included in the section contents, or it
         is not.  We assume that the size is not included, and force
         the rtype_to_howto function to adjust the addend as needed.  */

      if (sym != NULL && sym->n_scnum != 0)
	addend = - sym->n_value;
      else
	addend = 0;


      howto = bfd_coff_rtype_to_howto (input_bfd, input_section, rel, h,
				       sym, &addend);
      if (howto == NULL)
	return false;

      /* If we are doing a relocateable link, then we can just ignore
         a PC relative reloc that is pcrel_offset.  It will already
         have the correct value.  If this is not a relocateable link,
         then we should ignore the symbol value.  */
      if (howto->pc_relative && howto->pcrel_offset)
        {
          if (info->relocateable)
            continue;
          if (sym != NULL && sym->n_scnum != 0)
            addend += sym->n_value;
        }

      /* If we are doing a relocateable link, then we can just ignore
         a PC relative reloc that is pcrel_offset.  It will already
         have the correct value.  */
      if (info->relocateable
          && howto->pc_relative
          && howto->pcrel_offset)
        continue;

      val = 0;

      if (h == NULL)
	{
	  asection *sec;

	  if (symndx == -1)
	    {
	      sec = bfd_abs_section_ptr;
	      val = 0;
	    }
	  else
	    {
	      sec = sections[symndx];
              val = (sec->output_section->vma
		     + sec->output_offset
		     + sym->n_value
		     - sec->vma);
	    }
	}
      else
	{
#if 1 /* THUMBEXTENSION */
          /* We don't output the stubs if we are generating a
             relocatable output file, since we may as well leave the
             stub generation to the final linker pass. If we fail to
	     verify that the name is defined, we'll try to build stubs
	     for an undefined name... */
          if (! info->relocateable
	      && (   h->root.type == bfd_link_hash_defined
		  || h->root.type == bfd_link_hash_defweak))
            {
	      asection *   sec;
	      asection *   h_sec = h->root.u.def.section;
	      const char * name  = h->root.root.string;
	      
	      /* h locates the symbol referenced in the reloc.  */
	      h_val = (h->root.u.def.value
		       + h_sec->output_section->vma
		       + h_sec->output_offset);

              if (howto->type == ARM_26)
                {
                  if (   h->class == C_THUMBSTATFUNC
		      || h->class == C_THUMBEXTFUNC)
		    {
		      /* Arm code calling a Thumb function */
		      signed long int                final_disp;
		      unsigned long int              tmp;
		      long int                       my_offset;
		      long int                       offset;
		      asection *                     s = 0;
		      unsigned long int              return_address;
		      long int                       ret_offset;
		      long int                       disp;
		      struct coff_link_hash_entry *  myh; 

		      myh = find_arm_glue (info, name, input_bfd);
		      if (myh == NULL)
			return false;

		      my_offset = myh->root.u.def.value;

		      s = bfd_get_section_by_name (bfd_of_glue_owner, 
						  ARM2THUMB_GLUE_SECTION_NAME);
		      BFD_ASSERT (s != NULL);
		      BFD_ASSERT (s->contents != NULL);

		      if ((my_offset & 0x01) == 0x01)
			{
			  if (INTERWORK_SET (h_sec->owner) && ! INTERWORK_FLAG (h_sec->owner))
			    _bfd_error_handler ("%s: warning: interworking not enabled.",
						bfd_get_filename (h_sec->owner));
			  
			  --my_offset;
			  myh->root.u.def.value = my_offset;

			  bfd_put_32 (output_bfd, a2t1_ldr_insn, s->contents + my_offset);
			  
			  bfd_put_32 (output_bfd, a2t2_bx_r12_insn, s->contents + my_offset + 4);
			  
			  /* It's a thumb address.  Add the low order bit.  */
			  bfd_put_32 (output_bfd, h_val | a2t3_func_addr_insn, s->contents + my_offset + 8);
			}

		      BFD_ASSERT (my_offset <= global_arm_glue_size);

		      tmp = bfd_get_32 (input_bfd, contents + rel->r_vaddr - input_section->vma);
		      
		      tmp = tmp & 0xFF000000;
		      
		      /* Somehow these are both 4 too far, so subtract 8. */
		      ret_offset =
			s->output_offset
			+ my_offset 
			+ s->output_section->vma
			- (input_section->output_offset
			   + input_section->output_section->vma 
			   + rel->r_vaddr)
			- 8;

		      tmp = tmp | ((ret_offset >> 2) & 0x00FFFFFF);
		      
		      bfd_put_32 (output_bfd, tmp, contents + rel->r_vaddr - input_section->vma);
		      
		      done = 1;
		    }
                }
	      
	      /* Note: We used to check for ARM_THUMB9 and ARM_THUMB12 */
              else if (howto->type == ARM_THUMB23)
                {
                  if (   h->class == C_EXT 
		      || h->class == C_STAT
		      || h->class == C_LABEL)
		    {
		      /* Thumb code calling an ARM function */
		      unsigned long int              return_address;
		      signed long int                final_disp;
		      asection *                     s = 0;
		      long int                       my_offset;
		      unsigned long int              tmp;
		      long int                       ret_offset;
		      struct coff_link_hash_entry *  myh;

		      myh = find_thumb_glue (info, name, input_bfd);
		      if (myh == NULL)
			return false;

		      my_offset = myh->root.u.def.value;
		      
		      s = bfd_get_section_by_name (bfd_of_glue_owner, 
						   THUMB2ARM_GLUE_SECTION_NAME);
		      
		      BFD_ASSERT (s != NULL);
		      BFD_ASSERT (s->contents != NULL);
		      
		      if ((my_offset & 0x01) == 0x01)
			{
			  if (INTERWORK_SET (h_sec->owner) && ! INTERWORK_FLAG (h_sec->owner))
			    _bfd_error_handler ("%s: warning: interworking not enabled.",
						bfd_get_filename (h_sec->owner));
			  
			  -- my_offset;
			  myh->root.u.def.value = my_offset;

			  bfd_put_16 (output_bfd, t2a1_bx_pc_insn,
				     s->contents + my_offset);
		      
			  bfd_put_16 (output_bfd, t2a2_noop_insn,
				     s->contents + my_offset + 2);
		      
			  ret_offset =
			    ((signed)h_val) - 
			    ((signed)(s->output_offset
				      + my_offset 
				      + s->output_section->vma))
			    - 12;
		      
			  t2a3_b_insn |= ((ret_offset >> 2) & 0x00FFFFFF);
			  
			  bfd_put_32 (output_bfd, t2a3_b_insn, s->contents + my_offset + 4);
			}

		      BFD_ASSERT (my_offset <= global_thumb_glue_size);

		      /* Now go back and fix up the original bl insn to point here.  */
		      ret_offset =
			s->output_offset
			+ my_offset
			- (input_section->output_offset
			   + rel->r_vaddr)
			-4;
		      
		      tmp = bfd_get_32 (input_bfd, contents + rel->r_vaddr - input_section->vma);
		      
		      bfd_put_32 (output_bfd,
				  insert_thumb_branch (tmp, ret_offset),
				  contents + rel->r_vaddr - input_section->vma);
		      
		      done = 1;
                    }
                }
            }
	  
          /* If the relocation type and destination symbol does not
             fall into one of the above categories, then we can just
             perform a direct link. */

	  if (done)
	    rstat = bfd_reloc_ok;
	  else 
#endif /* THUMBEXTENSION */
	    if (   h->root.type == bfd_link_hash_defined
		|| h->root.type == bfd_link_hash_defweak)
	    {
	      asection *sec;

	      sec = h->root.u.def.section;
	      val = (h->root.u.def.value
		     + sec->output_section->vma
		     + sec->output_offset);
	      }

	  else if (! info->relocateable)
	    {
	      if (! ((*info->callbacks->undefined_symbol)
		     (info, h->root.root.string, input_bfd, input_section,
		      rel->r_vaddr - input_section->vma)))
		return false;
	    }
	}

      if (info->base_file)
	{
	  /* Emit a reloc if the backend thinks it needs it. */
	  if (sym && pe_data(output_bfd)->in_reloc_p(output_bfd, howto))
	    {
	      /* relocation to a symbol in a section which
		 isn't absolute - we output the address here 
		 to a file */
	      bfd_vma addr = rel->r_vaddr 
		- input_section->vma 
		+ input_section->output_offset 
		  + input_section->output_section->vma;
	      if (coff_data(output_bfd)->pe)
		addr -= pe_data(output_bfd)->pe_opthdr.ImageBase;
	      /* FIXME: Shouldn't 4 be sizeof (addr)?  */
	      fwrite (&addr, 1,4, (FILE *) info->base_file);
	    }
	}
  
#if 1 /* THUMBEXTENSION */
      if (done)
	;
      /* Only perform this fix during the final link, not a relocatable link.  nickc@cygnus.com  */
      else if (! info->relocateable
	       && howto->type == ARM_THUMB23)
        {
          /* This is pretty much a copy of what the default
             _bfd_final_link_relocate and _bfd_relocate_contents
             routines do to perform a relocation, with special
             processing for the split addressing of the Thumb BL
             instruction.  Again, it would probably be simpler adding a
             ThumbBRANCH23 specific macro expansion into the default
             code.  */
	  
          bfd_vma address = rel->r_vaddr - input_section->vma;
	  
          if (address > input_section->_raw_size)
            rstat = bfd_reloc_outofrange;
          else
            {
              bfd_vma         relocation       = val + addend;
	      int             size             = bfd_get_reloc_size (howto);
	      boolean         overflow         = false;
	      bfd_byte *      location         = contents + address;
	      bfd_vma         x                = bfd_get_32 (input_bfd, location);
	      bfd_vma         src_mask         = 0x007FFFFE;
	      bfd_signed_vma  reloc_signed_max = (1 << (howto->bitsize - 1)) - 1;
	      bfd_signed_vma  reloc_signed_min = ~reloc_signed_max;
	      bfd_vma         check;
	      bfd_signed_vma  signed_check;
	      bfd_vma         add;
	      bfd_signed_vma  signed_add;

	      BFD_ASSERT (size == 4);
	      
              /* howto->pc_relative should be TRUE for type 14 BRANCH23 */
              relocation -= (input_section->output_section->vma
                             + input_section->output_offset);
	      
              /* howto->pcrel_offset should be TRUE for type 14 BRANCH23 */
              relocation -= address;
	      
	      /* No need to negate the relocation with BRANCH23. */
	      /* howto->complain_on_overflow == complain_overflow_signed for BRANCH23.  */
	      /* howto->rightshift == 1 */
	      /* Drop unwanted bits from the value we are relocating to.  */
	      
	      check = relocation >> howto->rightshift;
		
	      /* If this is a signed value, the rightshift just dropped
		 leading 1 bits (assuming twos complement).  */
	      if ((bfd_signed_vma) relocation >= 0)
		signed_check = check;
	      else
		signed_check = (check
				| ((bfd_vma) - 1
				   & ~((bfd_vma) - 1 >> howto->rightshift)));
	      
	      /* Get the value from the object file.  */
	      if (bfd_big_endian (input_bfd))
		{
		  add = (((x) & 0x07ff0000) >> 4) | (((x) & 0x7ff) << 1);
		}
	      else
		{
		  add = ((((x) & 0x7ff) << 12) | (((x) & 0x07ff0000) >> 15));
		}

	      /* Get the value from the object file with an appropriate sign.
		 The expression involving howto->src_mask isolates the upper
		 bit of src_mask.  If that bit is set in the value we are
		 adding, it is negative, and we subtract out that number times
		 two.  If src_mask includes the highest possible bit, then we
		 can not get the upper bit, but that does not matter since
		 signed_add needs no adjustment to become negative in that
		 case.  */
	      
	      signed_add = add;
	      
	      if ((add & (((~ src_mask) >> 1) & src_mask)) != 0)
		signed_add -= (((~ src_mask) >> 1) & src_mask) << 1;
	      
	      /* Add the value from the object file, shifted so that it is a
		 straight number.  */
	      /* howto->bitpos == 0 */
	      
	      signed_check += signed_add;
	      relocation += signed_add;

	      BFD_ASSERT (howto->complain_on_overflow == complain_overflow_signed);

	      /* Assumes two's complement.  */
	      if (   signed_check > reloc_signed_max
		  || signed_check < reloc_signed_min)
		overflow = true;
	      
	      /* Put RELOCATION into the correct bits:  */
	      
	      if (bfd_big_endian (input_bfd))
		{
		  relocation = (((relocation & 0xffe) >> 1)  | ((relocation << 4) & 0x07ff0000));
		}
	      else
		{
		  relocation = (((relocation & 0xffe) << 15) | ((relocation >> 12) & 0x7ff));
		}
	      
	      /* Add RELOCATION to the correct bits of X:  */
	      x = ((x & ~howto->dst_mask) | relocation);

	      /* Put the relocated value back in the object file:  */
	      bfd_put_32 (input_bfd, x, location);

	      rstat = overflow ? bfd_reloc_overflow : bfd_reloc_ok;
            }
        }
      else
#endif /* THUMBEXTENSION */
        rstat = _bfd_final_link_relocate (howto, input_bfd, input_section,
                                          contents,
                                          rel->r_vaddr - input_section->vma,
                                          val, addend);
#if 1 /* THUMBEXTENSION */
      /* FIXME: 
	 Is this the best way to fix up thumb addresses? krk@cygnus.com
	 Probably not, but it works, and if it works it don't need fixing!  nickc@cygnus.com */
      /* Only perform this fix during the final link, not a relocatable link.  nickc@cygnus.com  */
      if (! info->relocateable
	  && rel->r_type == ARM_32)
	{
	  /* Determine if we need to set the bottom bit of a relocated address
	     because the address is the address of a Thumb code symbol.  */
	     
	  int patchit = false;
	  
	  if (h != NULL
	      && (   h->class == C_THUMBSTATFUNC
		  || h->class == C_THUMBEXTFUNC))
	    {
	      patchit = true;
	    }
	  else if (sym != NULL
		   && sym->n_scnum > N_UNDEF)
	    {
	      /* No hash entry - use the symbol instead.  */

	      if (   sym->n_sclass == C_THUMBSTATFUNC
		  || sym->n_sclass == C_THUMBEXTFUNC)
		patchit = true;
	    }
			
	  if (patchit)
	    {
	      bfd_byte * location = contents + rel->r_vaddr - input_section->vma;
	      bfd_vma    x        = bfd_get_32 (input_bfd, location);

	      bfd_put_32 (input_bfd, x | 1, location);
	    }
	}
#endif /* THUMBEXTENSION */      
      
      switch (rstat)
	{
	default:
	  abort ();
	case bfd_reloc_ok:
	  break;
	case bfd_reloc_outofrange:
	  (*_bfd_error_handler)
	    ("%s: bad reloc address 0x%lx in section `%s'",
	     bfd_get_filename (input_bfd),
	     (unsigned long) rel->r_vaddr,
	     bfd_get_section_name (input_bfd, input_section));
	  return false;
	case bfd_reloc_overflow:
	  {
	    const char *name;
	    char buf[SYMNMLEN + 1];

	    if (symndx == -1)
	      name = "*ABS*";
	    else if (h != NULL)
	      name = h->root.root.string;
	    else
	      {
		name = _bfd_coff_internal_syment_name (input_bfd, sym, buf);
		if (name == NULL)
		  return false;
	      }

	    if (! ((*info->callbacks->reloc_overflow)
		   (info, name, howto->name, (bfd_vma) 0, input_bfd,
		    input_section, rel->r_vaddr - input_section->vma)))
	      return false;
	  }
	}
    }

  return true;
}

boolean
arm_allocate_interworking_sections (info) 
     struct bfd_link_info *info;
{
  asection *s;
  bfd_byte *foo;
  static char test_char = '1';

  if ( global_arm_glue_size != 0 )
    {
      BFD_ASSERT (bfd_of_glue_owner != NULL);
      
      s = bfd_get_section_by_name (bfd_of_glue_owner, ARM2THUMB_GLUE_SECTION_NAME);

      BFD_ASSERT (s != NULL);
      
      foo = (bfd_byte *) bfd_alloc(bfd_of_glue_owner, global_arm_glue_size);
      memset(foo, test_char, global_arm_glue_size);
      
      s->_raw_size = s->_cooked_size = global_arm_glue_size;
      s->contents = foo;
    }

  if (global_thumb_glue_size != 0)
    {
      BFD_ASSERT (bfd_of_glue_owner != NULL);
      
      s = bfd_get_section_by_name (bfd_of_glue_owner, THUMB2ARM_GLUE_SECTION_NAME);

      BFD_ASSERT (s != NULL);
      
      foo = (bfd_byte *) bfd_alloc(bfd_of_glue_owner, global_thumb_glue_size);
      memset(foo, test_char, global_thumb_glue_size);
      
      s->_raw_size = s->_cooked_size = global_thumb_glue_size;
      s->contents = foo;
    }

  return true;
}

static void
record_arm_to_thumb_glue (info, h)
     struct bfd_link_info *        info;
     struct coff_link_hash_entry * h;
{
  const char *                   name = h->root.root.string;
  register asection *            s;
  char *                         tmp_name;
  struct coff_link_hash_entry *  myh;

  
  BFD_ASSERT (bfd_of_glue_owner != NULL);

  s = bfd_get_section_by_name (bfd_of_glue_owner, 
			       ARM2THUMB_GLUE_SECTION_NAME);

  BFD_ASSERT (s != NULL);

  tmp_name = ((char *)
	      bfd_malloc (strlen (name) + strlen (ARM2THUMB_GLUE_ENTRY_NAME)));

  BFD_ASSERT (tmp_name);

  sprintf (tmp_name, ARM2THUMB_GLUE_ENTRY_NAME, name);
  
  myh = coff_link_hash_lookup (coff_hash_table (info),
			       tmp_name,
			       false, false, true);
  
  if (myh != NULL)
    {
      free (tmp_name);
      return; /* we've already seen this guy */
    }


  /* The only trick here is using global_arm_glue_size as the value. Even
     though the section isn't allocated yet, this is where we will be putting
     it.  */

  bfd_coff_link_add_one_symbol (info, bfd_of_glue_owner, tmp_name,
				BSF_EXPORT | BSF_GLOBAL, 
				s, global_arm_glue_size + 1,
				NULL, true, false, 
				(struct bfd_link_hash_entry **) & myh);
  
  global_arm_glue_size += ARM2THUMB_GLUE_SIZE;

  free (tmp_name);

  return;
}

static void
record_thumb_to_arm_glue (info, h)
     struct bfd_link_info *        info;
     struct coff_link_hash_entry * h;
{
  const char *                   name = h->root.root.string;
  register asection *            s;
  char *                         tmp_name;
  struct coff_link_hash_entry *  myh;

  
  BFD_ASSERT (bfd_of_glue_owner != NULL);

  s = bfd_get_section_by_name (bfd_of_glue_owner, THUMB2ARM_GLUE_SECTION_NAME);

  BFD_ASSERT (s != NULL);

  tmp_name = (char *) bfd_malloc (strlen (name) + strlen (THUMB2ARM_GLUE_ENTRY_NAME));

  BFD_ASSERT (tmp_name);

  sprintf (tmp_name, THUMB2ARM_GLUE_ENTRY_NAME, name);

  myh = coff_link_hash_lookup (coff_hash_table (info),
			       tmp_name,
			       false, false, true);
  
  if (myh != NULL)
    {
      free (tmp_name);
      return; /* we've already seen this guy */
    }

  bfd_coff_link_add_one_symbol (info, bfd_of_glue_owner, tmp_name,
				BSF_LOCAL, s, global_thumb_glue_size + 1,
				NULL, true, false, 
				(struct bfd_link_hash_entry **) & myh);
  
  /* if we mark it 'thumb', the disassembler will do a better job */
  myh->class = C_THUMBEXTFUNC;

  free (tmp_name);

#define CHANGE_TO_ARM "__%s_change_to_arm"
  
  tmp_name = ((char *)
	      bfd_malloc (strlen (name) + strlen (CHANGE_TO_ARM)));

  BFD_ASSERT (tmp_name);

  sprintf (tmp_name, CHANGE_TO_ARM, name);

  myh = NULL;

  /* now allocate another symbol to switch back to arm mode */
  bfd_coff_link_add_one_symbol (info, bfd_of_glue_owner, tmp_name,
				BSF_LOCAL, s, global_thumb_glue_size + 4,
				NULL, true, false, 
				(struct bfd_link_hash_entry **) & myh);

  free (tmp_name);  

  global_thumb_glue_size += THUMB2ARM_GLUE_SIZE;

  return;
}

boolean
arm_process_before_allocation (abfd, info)
     bfd *                   abfd;
     struct bfd_link_info *  info;
{
  asection * sec;

  /* If we are only performing a partial link do not bother
     to construct any glue.  */
  if (info->relocateable)
    return true;
  
  /* Here we have a bfd that is to be included on the link.  We have a hook
     to do reloc rummaging, before section sizes are nailed down.  */

  _bfd_coff_get_external_symbols (abfd);

  /* Rummage around all the relocs and map the glue vectors.  */
  sec = abfd->sections;

  if (sec == NULL)
    return true;

  for (; sec != NULL; sec = sec->next)
    {
      struct internal_reloc * i;
      struct internal_reloc * rel;
      
      if (sec->reloc_count == 0) 
	continue;

      /* Load the relocs.  */
      /* FIXME: there may be a storage leak here. */
      
      i = _bfd_coff_read_internal_relocs (abfd, sec, 1, 0, 0, 0);
    
      BFD_ASSERT (i != 0);

      for (rel = i; rel < i + sec->reloc_count; ++rel) 
	{
	  unsigned short                 r_type  = rel->r_type;
	  long                           symndx;
	  struct coff_link_hash_entry *  h;

	  symndx = rel->r_symndx;

	  /* If the relocation is not against a symbol it cannot concern us.  */
	  if (symndx == -1)
	    continue;

	  h = obj_coff_sym_hashes (abfd)[symndx];

	  /* If the relocation is against a static symbol it must be within
	     the current section and so canot be a cross ARM/Thumb relocation.  */
	  if (h == NULL)
	    continue;

	  switch (r_type)
	    {
	    case ARM_26:
	      /* This one is a call from arm code.  We need to look up
		 the target of the call. If it is a thumb target, we
		 insert glue.  */
	      
	      if (h->class == C_THUMBEXTFUNC)
		{
		  record_arm_to_thumb_glue (info, h);
		}
	      break;
	      
	    case ARM_THUMB23:
	      /* This one is a call from thumb code.  We used to look
		 for ARM_THUMB9 and ARM_THUMB12 as well.  We need to look
		 up the target of the call. If it is an arm target, we
		 insert glue.  */
	      
	      switch (h->class)
		{
		case C_EXT:
		case C_STAT:
		case C_LABEL:
		  record_thumb_to_arm_glue (info, h);
		  break;
		default:
		  ;
		}
	      break;
	      
	    default:
	      break;
	    }
	}
    }

  return true;
}

#define coff_relocate_section coff_arm_relocate_section

/* When doing a relocateable link, we want to convert ARM26 relocs
   into ARM26D relocs.  */

static boolean
coff_arm_adjust_symndx (obfd, info, ibfd, sec, irel, adjustedp)
     bfd *obfd;
     struct bfd_link_info *info;
     bfd *ibfd;
     asection *sec;
     struct internal_reloc *irel;
     boolean *adjustedp;
{
  if (irel->r_type == 3)
    {
      struct coff_link_hash_entry *h;

      h = obj_coff_sym_hashes (ibfd)[irel->r_symndx];
      if (h != NULL
	  && (h->root.type == bfd_link_hash_defined
	      || h->root.type == bfd_link_hash_defweak)
	  && h->root.u.def.section->output_section == sec->output_section)
	irel->r_type = 7;
    }
  *adjustedp = false;
  return true;
}


/* Called when merging the private data areas of two BFDs.
   This is important as it allows us to detect if we are
   attempting to merge binaries compiled for different ARM
   targets, eg different CPUs or differents APCS's.     */

static boolean
coff_arm_bfd_merge_private_bfd_data (ibfd, obfd)
     bfd *   ibfd;
     bfd *   obfd;
{
  BFD_ASSERT (ibfd != NULL && obfd != NULL);

  if (ibfd == obfd)
    return true;

  /* If the two formats are different we cannot merge anything.  */
  if (ibfd->xvec != obfd->xvec)
    {
      bfd_set_error (bfd_error_wrong_format);
      return false;
    }

  /* Verify that the APCS is the same for the two BFDs */
  if (APCS_SET (ibfd))
    {
      if (APCS_SET (obfd))
	{
	  /* If the src and dest have different APCS flag bits set, fail.  */
	  if (APCS_26_FLAG (obfd) != APCS_26_FLAG (ibfd))
	    {
	      _bfd_error_handler
		("%s: ERROR: compiled for APCS-%d whereas target %s uses APCS-%d",
		 bfd_get_filename (ibfd), APCS_26_FLAG (ibfd) ? 26 : 32,
		 bfd_get_filename (obfd), APCS_26_FLAG (obfd) ? 26 : 32
		 );

	      bfd_set_error (bfd_error_wrong_format);
	      return false;
	    }
	  
	  if (APCS_FLOAT_FLAG (obfd) != APCS_FLOAT_FLAG (ibfd))
	    {
	      _bfd_error_handler
		("%s: ERROR: passes floats in %s registers whereas target %s uses %s registers",
		 bfd_get_filename (ibfd), APCS_FLOAT_FLAG (ibfd) ? "float" : "integer",
		 bfd_get_filename (obfd), APCS_FLOAT_FLAG (obfd) ? "float" : "integer"
		 );

	      bfd_set_error (bfd_error_wrong_format);
	      return false;
	    }
	  
	  if (PIC_FLAG (obfd) != PIC_FLAG (ibfd))
	    {
	      _bfd_error_handler
		("%s: ERROR: compiled as %s code, whereas target %s is %s",
		 bfd_get_filename (ibfd), PIC_FLAG (ibfd) ? "position independent" : "absoluste position",
		 bfd_get_filename (obfd), PIC_FLAG (obfd) ? "position independent" : "absoluste position"
		 );

	      bfd_set_error (bfd_error_wrong_format);
	      return false;
	    }
	}
      else
	{
	  SET_APCS_FLAGS (obfd, APCS_26_FLAG (ibfd) | APCS_FLOAT_FLAG (ibfd) | PIC_FLAG (ibfd));
	  
	  /* Set up the arch and fields as well as these are probably wrong.  */
	  bfd_set_arch_mach (obfd, bfd_get_arch (ibfd), bfd_get_mach (ibfd));
	}
    }
  
  /* Check the interworking support.  */
  if (INTERWORK_SET (ibfd))
    {
      if (INTERWORK_SET (obfd))
	{
	  /* If the src and dest differ in their interworking issue a warning.  */
	  if (INTERWORK_FLAG (obfd) != INTERWORK_FLAG (ibfd))
	    {
	      _bfd_error_handler
		("Warning: input file %s %s interworking, whereas %s does%s",
		 bfd_get_filename (ibfd),
		 INTERWORK_FLAG (ibfd) ? "supports" : "does not support",
		 bfd_get_filename (obfd),
		 INTERWORK_FLAG (obfd) ? "." : " not."
		 );
	    }
	}
      else
	{
	  SET_INTERWORK_FLAG (obfd, INTERWORK_FLAG (ibfd));
	}
    }

  return true;
}


/* Display the flags field */

static boolean
coff_arm_bfd_print_private_bfd_data (abfd, ptr)
     bfd *   abfd;
     PTR     ptr;
{
  FILE * file = (FILE *) ptr;
  
  BFD_ASSERT (abfd != NULL && ptr != NULL)
  
  fprintf (file, "private flags = %x", coff_data( abfd )->flags);
  
  if (APCS_SET (abfd))
    fprintf (file, ": [APCS-%d] [floats passed in %s registers] [%s]",
	     APCS_26_FLAG (abfd) ? 26 : 32,
	     APCS_FLOAT_FLAG (abfd) ? "float" : "integer",
	     PIC_FLAG (abfd) ? "position independent" : "absolute position"
	     );
  
  if (INTERWORK_SET (abfd))
    fprintf (file, ": [interworking %ssupported]",
	     INTERWORK_FLAG (abfd) ? "" : "not " );
  
  fputc ('\n', file);
  
  return true;
}


/* Copies the given flags into the coff_tdata.flags field.
   Typically these flags come from the f_flags[] field of
   the COFF filehdr structure, which contains important,
   target specific information.  */

static boolean
coff_arm_bfd_set_private_flags (abfd, flags)
	bfd *	   abfd;
	flagword   flags;
{
  int flag;

  BFD_ASSERT (abfd != NULL);

  flag = (flags & F_APCS26) ? F_APCS_26 : 0;
  
  /* Make sure that the APCS field has not been initialised to the opposite value.  */
  if (APCS_SET (abfd)
      && (   (APCS_26_FLAG    (abfd) != flag)
	  || (APCS_FLOAT_FLAG (abfd) != (flags & F_APCS_FLOAT))
	  || (PIC_FLAG        (abfd) != (flags & F_PIC))
	  ))
    return false;

  flag |= (flags & (F_APCS_FLOAT | F_PIC));
  
  SET_APCS_FLAGS (abfd, flag);

  flag = (flags & F_INTERWORK);
  
  /* If either the flags or the BFD do support interworking then do not set the interworking flag.  */
  if (INTERWORK_SET (abfd) && (INTERWORK_FLAG (abfd) != flag))
    flag = 0;

  SET_INTERWORK_FLAG (abfd, flag);

  return true;
}


/* Copy the important parts of the target specific data
   from one instance of a BFD to another.  */

static boolean
coff_arm_bfd_copy_private_bfd_data (src, dest)
     bfd *  src;
     bfd *  dest;
{
  BFD_ASSERT (src != NULL && dest != NULL);

  if (src == dest)
    return true;

  /* If the destination is not in the same format as the source, do not do the copy.  */
  if (src->xvec != dest->xvec)
    return true;

  /* copy the flags field */
  if (APCS_SET (src))
    {
      if (APCS_SET (dest))
	{
	  /* If the src and dest have different APCS flag bits set, fail.  */
	  if (APCS_26_FLAG (dest) != APCS_26_FLAG (src))
	    return false;
	  
	  if (APCS_FLOAT_FLAG (dest) != APCS_FLOAT_FLAG (src))
	    return false;
	  
	  if (PIC_FLAG (dest) != PIC_FLAG (src))
	    return false;
	}
      else
	SET_APCS_FLAGS (dest, APCS_26_FLAG (src) | APCS_FLOAT_FLAG (src) | PIC_FLAG (src));
    }

  if (INTERWORK_SET (src))
    {
      if (INTERWORK_SET (dest))
	{
	  /* If the src and dest have different interworking flags then turn off the interworking bit.  */
	  if (INTERWORK_FLAG (dest) != INTERWORK_FLAG (src))
	    SET_INTERWORK_FLAG (dest, 0);
	}
      else
	{
	  SET_INTERWORK_FLAG (dest, INTERWORK_FLAG (src));
	}
    }

  return true;
}

/* Note:  the definitions here of LOCAL_LABEL_PREFIX and USER_LABEL_PREIFX
 *must* match the definitions on gcc/config/arm/semi.h.  */
#define LOCAL_LABEL_PREFIX "."
#define USER_LABEL_PREFIX "_"

static boolean
coff_arm_is_local_label_name (abfd, name)
     bfd *        abfd;
     const char * name;
{
#ifdef LOCAL_LABEL_PREFIX
  /* If there is a prefix for local labels then look for this.
     If the prefix exists, but it is empty, then ignore the test. */
  
  if (LOCAL_LABEL_PREFIX[0] != 0)
    {
      if (strncmp (name, LOCAL_LABEL_PREFIX, strlen (LOCAL_LABEL_PREFIX)) == 0)
	return true;
    }
#endif
#ifdef USER_LABEL_PREFIX
  if (USER_LABEL_PREFIX[0] != 0)
    {
      if (strncmp (name, USER_LABEL_PREFIX, strlen (USER_LABEL_PREFIX)) == 0)
	return false;
    }
#endif
  
  /* devo/gcc/config/dbxcoff.h defines ASM_OUTPUT_SOURCE_LINE to generate local line numbers as .LM<number>, so treat these as local.  */
  
  switch (name[0])
    {
    case 'L': return true;
    case '.': return (name[1] == 'L' && name[2] == 'M') ? true : false;
    default:  return false;     /* Cannot make our minds up - default to false so that it will not be stripped by accident.  */ 
    }
}

#define coff_bfd_is_local_label_name 		coff_arm_is_local_label_name
#define coff_adjust_symndx			coff_arm_adjust_symndx
#define coff_bfd_final_link          		coff_arm_bfd_final_link 
#define coff_bfd_merge_private_bfd_data		coff_arm_bfd_merge_private_bfd_data
#define coff_bfd_print_private_bfd_data		coff_arm_bfd_print_private_bfd_data
#define coff_bfd_set_private_flags              coff_arm_bfd_set_private_flags
#define coff_bfd_copy_private_bfd_data          coff_arm_bfd_copy_private_bfd_data

#include "coffcode.h"

static boolean
arm_do_last (abfd)
     bfd * abfd;
{
  if (abfd == bfd_of_glue_owner)
    return true;
  else
    return false;
}

static bfd *
arm_get_last()
{
  return bfd_of_glue_owner;
}

/* This piece of machinery exists only to guarantee that the bfd that holds
   the glue section is written last. 

   This does depend on bfd_make_section attaching a new section to the
   end of the section list for the bfd. 

   This is otherwise intended to be functionally the same as 
   cofflink.c:_bfd_coff_final_link(). It is specifically different only 
   where the ARM_HACKS macro modifies the code. It is left in as a 
   precise form of comment. krk@cygnus.com  */

#define ARM_HACKS


/* Do the final link step.  */

boolean
coff_arm_bfd_final_link (abfd, info)
     bfd *abfd;
     struct bfd_link_info *info;
{
  bfd_size_type symesz;
  struct coff_final_link_info finfo;
  boolean debug_merge_allocated;
  asection *o;
  struct bfd_link_order *p;
  size_t max_sym_count;
  size_t max_lineno_count;
  size_t max_reloc_count;
  size_t max_output_reloc_count;
  size_t max_contents_size;
  file_ptr rel_filepos;
  unsigned int relsz;
  file_ptr line_filepos;
  unsigned int linesz;
  bfd *sub;
  bfd_byte *external_relocs = NULL;
  char strbuf[STRING_SIZE_SIZE];

  symesz = bfd_coff_symesz (abfd);

  finfo.info = info;
  finfo.output_bfd = abfd;
  finfo.strtab = NULL;
  finfo.section_info = NULL;
  finfo.last_file_index = -1;
  finfo.last_bf_index = -1;
  finfo.internal_syms = NULL;
  finfo.sec_ptrs = NULL;
  finfo.sym_indices = NULL;
  finfo.outsyms = NULL;
  finfo.linenos = NULL;
  finfo.contents = NULL;
  finfo.external_relocs = NULL;
  finfo.internal_relocs = NULL;
  debug_merge_allocated = false;

  coff_data (abfd)->link_info = info;

  finfo.strtab = _bfd_stringtab_init ();
  if (finfo.strtab == NULL)
    goto error_return;

  if (! coff_debug_merge_hash_table_init (&finfo.debug_merge))
    goto error_return;
  debug_merge_allocated = true;

  /* Compute the file positions for all the sections.  */
  if (! abfd->output_has_begun)
    bfd_coff_compute_section_file_positions (abfd);

  /* Count the line numbers and relocation entries required for the
     output file.  Set the file positions for the relocs.  */
  rel_filepos = obj_relocbase (abfd);
  relsz = bfd_coff_relsz (abfd);
  max_contents_size = 0;
  max_lineno_count = 0;
  max_reloc_count = 0;

  for (o = abfd->sections; o != NULL; o = o->next)
    {
      o->reloc_count = 0;
      o->lineno_count = 0;
      for (p = o->link_order_head; p != NULL; p = p->next)
	{

	  if (p->type == bfd_indirect_link_order)
	    {
	      asection *sec;

	      sec = p->u.indirect.section;

	      /* Mark all sections which are to be included in the
		 link.  This will normally be every section.  We need
		 to do this so that we can identify any sections which
		 the linker has decided to not include.  */
	      sec->linker_mark = true;

	      if (info->strip == strip_none
		  || info->strip == strip_some)
		o->lineno_count += sec->lineno_count;

	      if (info->relocateable)
		o->reloc_count += sec->reloc_count;

	      if (sec->_raw_size > max_contents_size)
		max_contents_size = sec->_raw_size;
	      if (sec->lineno_count > max_lineno_count)
		max_lineno_count = sec->lineno_count;
	      if (sec->reloc_count > max_reloc_count)
		max_reloc_count = sec->reloc_count;
	    }
	  else if (info->relocateable
		   && (p->type == bfd_section_reloc_link_order
		       || p->type == bfd_symbol_reloc_link_order))
	    ++o->reloc_count;
	}
      if (o->reloc_count == 0)
	o->rel_filepos = 0;
      else
	{
	  o->flags |= SEC_RELOC;
	  o->rel_filepos = rel_filepos;
	  rel_filepos += o->reloc_count * relsz;
	}
    }

  /* If doing a relocateable link, allocate space for the pointers we
     need to keep.  */
  if (info->relocateable)
    {
      unsigned int i;

      /* We use section_count + 1, rather than section_count, because
         the target_index fields are 1 based.  */
      finfo.section_info =
	((struct coff_link_section_info *)
	 bfd_malloc ((abfd->section_count + 1)
		     * sizeof (struct coff_link_section_info)));
      if (finfo.section_info == NULL)
	goto error_return;
      for (i = 0; i <= abfd->section_count; i++)
	{
	  finfo.section_info[i].relocs = NULL;
	  finfo.section_info[i].rel_hashes = NULL;
	}
    }

  /* We now know the size of the relocs, so we can determine the file
     positions of the line numbers.  */
  line_filepos = rel_filepos;
  linesz = bfd_coff_linesz (abfd);
  max_output_reloc_count = 0;
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      if (o->lineno_count == 0)
	o->line_filepos = 0;
      else
	{
	  o->line_filepos = line_filepos;
	  line_filepos += o->lineno_count * linesz;
	}

      if (o->reloc_count != 0)
	{
	  /* We don't know the indices of global symbols until we have
             written out all the local symbols.  For each section in
             the output file, we keep an array of pointers to hash
             table entries.  Each entry in the array corresponds to a
             reloc.  When we find a reloc against a global symbol, we
             set the corresponding entry in this array so that we can
             fix up the symbol index after we have written out all the
             local symbols.

	     Because of this problem, we also keep the relocs in
	     memory until the end of the link.  This wastes memory,
	     but only when doing a relocateable link, which is not the
	     common case.  */
	  BFD_ASSERT (info->relocateable);
	  finfo.section_info[o->target_index].relocs =
	    ((struct internal_reloc *)
	     bfd_malloc (o->reloc_count * sizeof (struct internal_reloc)));
	  finfo.section_info[o->target_index].rel_hashes =
	    ((struct coff_link_hash_entry **)
	     bfd_malloc (o->reloc_count
		     * sizeof (struct coff_link_hash_entry *)));
	  if (finfo.section_info[o->target_index].relocs == NULL
	      || finfo.section_info[o->target_index].rel_hashes == NULL)
	    goto error_return;

	  if (o->reloc_count > max_output_reloc_count)
	    max_output_reloc_count = o->reloc_count;
	}

      /* Reset the reloc and lineno counts, so that we can use them to
	 count the number of entries we have output so far.  */
      o->reloc_count = 0;
      o->lineno_count = 0;
    }

  obj_sym_filepos (abfd) = line_filepos;

  /* Figure out the largest number of symbols in an input BFD.  Take
     the opportunity to clear the output_has_begun fields of all the
     input BFD's.  */
  max_sym_count = 0;
  for (sub = info->input_bfds; sub != NULL; sub = sub->link_next)
    {
      size_t sz;

      sub->output_has_begun = false;
      sz = obj_raw_syment_count (sub);
      if (sz > max_sym_count)
	max_sym_count = sz;
    }

  /* Allocate some buffers used while linking.  */
  finfo.internal_syms = ((struct internal_syment *)
			 bfd_malloc (max_sym_count
				     * sizeof (struct internal_syment)));
  finfo.sec_ptrs = (asection **) bfd_malloc (max_sym_count
					     * sizeof (asection *));
  finfo.sym_indices = (long *) bfd_malloc (max_sym_count * sizeof (long));
  finfo.outsyms = ((bfd_byte *)
		   bfd_malloc ((size_t) ((max_sym_count + 1) * symesz)));
  finfo.linenos = (bfd_byte *) bfd_malloc (max_lineno_count
				       * bfd_coff_linesz (abfd));
  finfo.contents = (bfd_byte *) bfd_malloc (max_contents_size);
  finfo.external_relocs = (bfd_byte *) bfd_malloc (max_reloc_count * relsz);
  if (! info->relocateable)
    finfo.internal_relocs = ((struct internal_reloc *)
			     bfd_malloc (max_reloc_count
					 * sizeof (struct internal_reloc)));
  if ((finfo.internal_syms == NULL && max_sym_count > 0)
      || (finfo.sec_ptrs == NULL && max_sym_count > 0)
      || (finfo.sym_indices == NULL && max_sym_count > 0)
      || finfo.outsyms == NULL
      || (finfo.linenos == NULL && max_lineno_count > 0)
      || (finfo.contents == NULL && max_contents_size > 0)
      || (finfo.external_relocs == NULL && max_reloc_count > 0)
      || (! info->relocateable
	  && finfo.internal_relocs == NULL
	  && max_reloc_count > 0))
    goto error_return;

  /* We now know the position of everything in the file, except that
     we don't know the size of the symbol table and therefore we don't
     know where the string table starts.  We just build the string
     table in memory as we go along.  We process all the relocations
     for a single input file at once.  */
  obj_raw_syment_count (abfd) = 0;

  if (coff_backend_info (abfd)->_bfd_coff_start_final_link)
    {
      if (! bfd_coff_start_final_link (abfd, info))
	goto error_return;
    }

  for (o = abfd->sections; o != NULL; o = o->next)
    {
      for (p = o->link_order_head; p != NULL; p = p->next)
	{
	  if (p->type == bfd_indirect_link_order
	      && (bfd_get_flavour (p->u.indirect.section->owner)
		  == bfd_target_coff_flavour))
	    {
	      sub = p->u.indirect.section->owner;
#ifdef ARM_HACKS
	      if (! sub->output_has_begun && !arm_do_last(sub))
#else
	      if (! sub->output_has_begun)
#endif
		{
		  if (! _bfd_coff_link_input_bfd (&finfo, sub))
		    goto error_return;
		  sub->output_has_begun = true;
		}
	    }
	  else if (p->type == bfd_section_reloc_link_order
		   || p->type == bfd_symbol_reloc_link_order)
	    {
	      if (! _bfd_coff_reloc_link_order (abfd, &finfo, o, p))
		goto error_return;
	    }
	  else
	    {
	      if (! _bfd_default_link_order (abfd, info, o, p))
		goto error_return;
	    }
	}
    }

#ifdef ARM_HACKS
  {
    extern bfd* arm_get_last();
    bfd* last_one = arm_get_last();
    if (last_one)
      {
	if (! _bfd_coff_link_input_bfd (&finfo, last_one))
	  goto error_return;
      }
    last_one->output_has_begun = true;
  }
#endif

  /* Free up the buffers used by _bfd_coff_link_input_bfd.  */

  coff_debug_merge_hash_table_free (&finfo.debug_merge);
  debug_merge_allocated = false;

  if (finfo.internal_syms != NULL)
    {
      free (finfo.internal_syms);
      finfo.internal_syms = NULL;
    }
  if (finfo.sec_ptrs != NULL)
    {
      free (finfo.sec_ptrs);
      finfo.sec_ptrs = NULL;
    }
  if (finfo.sym_indices != NULL)
    {
      free (finfo.sym_indices);
      finfo.sym_indices = NULL;
    }
  if (finfo.linenos != NULL)
    {
      free (finfo.linenos);
      finfo.linenos = NULL;
    }
  if (finfo.contents != NULL)
    {
      free (finfo.contents);
      finfo.contents = NULL;
    }
  if (finfo.external_relocs != NULL)
    {
      free (finfo.external_relocs);
      finfo.external_relocs = NULL;
    }
  if (finfo.internal_relocs != NULL)
    {
      free (finfo.internal_relocs);
      finfo.internal_relocs = NULL;
    }

  /* The value of the last C_FILE symbol is supposed to be the symbol
     index of the first external symbol.  Write it out again if
     necessary.  */
  if (finfo.last_file_index != -1
      && (unsigned int) finfo.last_file.n_value != obj_raw_syment_count (abfd))
    {
      finfo.last_file.n_value = obj_raw_syment_count (abfd);
      bfd_coff_swap_sym_out (abfd, (PTR) &finfo.last_file,
			     (PTR) finfo.outsyms);
      if (bfd_seek (abfd,
		    (obj_sym_filepos (abfd)
		     + finfo.last_file_index * symesz),
		    SEEK_SET) != 0
	  || bfd_write (finfo.outsyms, symesz, 1, abfd) != symesz)
	return false;
    }

  /* Write out the global symbols.  */
  finfo.failed = false;
  coff_link_hash_traverse (coff_hash_table (info), _bfd_coff_write_global_sym,
			   (PTR) &finfo);
  if (finfo.failed)
    goto error_return;

  /* The outsyms buffer is used by _bfd_coff_write_global_sym.  */
  if (finfo.outsyms != NULL)
    {
      free (finfo.outsyms);
      finfo.outsyms = NULL;
    }

  if (info->relocateable)
    {
      /* Now that we have written out all the global symbols, we know
	 the symbol indices to use for relocs against them, and we can
	 finally write out the relocs.  */
      external_relocs = ((bfd_byte *)
			 bfd_malloc (max_output_reloc_count * relsz));
      if (external_relocs == NULL)
	goto error_return;

      for (o = abfd->sections; o != NULL; o = o->next)
	{
	  struct internal_reloc *irel;
	  struct internal_reloc *irelend;
	  struct coff_link_hash_entry **rel_hash;
	  bfd_byte *erel;

	  if (o->reloc_count == 0)
	    continue;

	  irel = finfo.section_info[o->target_index].relocs;
	  irelend = irel + o->reloc_count;
	  rel_hash = finfo.section_info[o->target_index].rel_hashes;
	  erel = external_relocs;
	  for (; irel < irelend; irel++, rel_hash++, erel += relsz)
	    {
	      if (*rel_hash != NULL)
		{
		  BFD_ASSERT ((*rel_hash)->indx >= 0);
		  irel->r_symndx = (*rel_hash)->indx;
		}
	      bfd_coff_swap_reloc_out (abfd, (PTR) irel, (PTR) erel);
	    }

	  if (bfd_seek (abfd, o->rel_filepos, SEEK_SET) != 0
	      || bfd_write ((PTR) external_relocs, relsz, o->reloc_count,
			    abfd) != relsz * o->reloc_count)
	    goto error_return;
	}

      free (external_relocs);
      external_relocs = NULL;
    }

  /* Free up the section information.  */
  if (finfo.section_info != NULL)
    {
      unsigned int i;

      for (i = 0; i < abfd->section_count; i++)
	{
	  if (finfo.section_info[i].relocs != NULL)
	    free (finfo.section_info[i].relocs);
	  if (finfo.section_info[i].rel_hashes != NULL)
	    free (finfo.section_info[i].rel_hashes);
	}
      free (finfo.section_info);
      finfo.section_info = NULL;
    }

  /* If we have optimized stabs strings, output them.  */
  if (coff_hash_table (info)->stab_info != NULL)
    {
      if (! _bfd_write_stab_strings (abfd, &coff_hash_table (info)->stab_info))
	return false;
    }

  /* Write out the string table.  */
  if (obj_raw_syment_count (abfd) != 0)
    {
      if (bfd_seek (abfd,
		    (obj_sym_filepos (abfd)
		     + obj_raw_syment_count (abfd) * symesz),
		    SEEK_SET) != 0)
	return false;

#if STRING_SIZE_SIZE == 4
      bfd_h_put_32 (abfd,
		    _bfd_stringtab_size (finfo.strtab) + STRING_SIZE_SIZE,
		    (bfd_byte *) strbuf);
#else
 #error Change bfd_h_put_32
#endif

      if (bfd_write (strbuf, 1, STRING_SIZE_SIZE, abfd) != STRING_SIZE_SIZE)
	return false;

      if (! _bfd_stringtab_emit (abfd, finfo.strtab))
	return false;
    }

  _bfd_stringtab_free (finfo.strtab);

  /* Setting bfd_get_symcount to 0 will cause write_object_contents to
     not try to write out the symbols.  */
  bfd_get_symcount (abfd) = 0;

  return true;

 error_return:
  if (debug_merge_allocated)
    coff_debug_merge_hash_table_free (&finfo.debug_merge);
  if (finfo.strtab != NULL)
    _bfd_stringtab_free (finfo.strtab);
  if (finfo.section_info != NULL)
    {
      unsigned int i;

      for (i = 0; i < abfd->section_count; i++)
	{
	  if (finfo.section_info[i].relocs != NULL)
	    free (finfo.section_info[i].relocs);
	  if (finfo.section_info[i].rel_hashes != NULL)
	    free (finfo.section_info[i].rel_hashes);
	}
      free (finfo.section_info);
    }
  if (finfo.internal_syms != NULL)
    free (finfo.internal_syms);
  if (finfo.sec_ptrs != NULL)
    free (finfo.sec_ptrs);
  if (finfo.sym_indices != NULL)
    free (finfo.sym_indices);
  if (finfo.outsyms != NULL)
    free (finfo.outsyms);
  if (finfo.linenos != NULL)
    free (finfo.linenos);
  if (finfo.contents != NULL)
    free (finfo.contents);
  if (finfo.external_relocs != NULL)
    free (finfo.external_relocs);
  if (finfo.internal_relocs != NULL)
    free (finfo.internal_relocs);
  if (external_relocs != NULL)
    free (external_relocs);
  return false;
}

static void
arm_bfd_coff_swap_sym_in (abfd, ext1, in1)
     bfd            *abfd;
     PTR ext1;
     PTR in1;
{
  SYMENT *ext = (SYMENT *)ext1;
  struct internal_syment      *in = (struct internal_syment *)in1;
  flagword flags;
  register asection *s;

  if( ext->e.e_name[0] == 0) {
    in->_n._n_n._n_zeroes = 0;
    in->_n._n_n._n_offset = bfd_h_get_32(abfd, (bfd_byte *) ext->e.e.e_offset);
  }
  else {
#if SYMNMLEN != E_SYMNMLEN
   -> Error, we need to cope with truncating or extending SYMNMLEN!;
#else
    memcpy(in->_n._n_name, ext->e.e_name, SYMNMLEN);
#endif
  }
  in->n_value = bfd_h_get_32(abfd, (bfd_byte *) ext->e_value); 
  in->n_scnum = bfd_h_get_16(abfd, (bfd_byte *) ext->e_scnum);
  if (sizeof(ext->e_type) == 2){
    in->n_type = bfd_h_get_16(abfd, (bfd_byte *) ext->e_type);
  }
  else {
    in->n_type = bfd_h_get_32(abfd, (bfd_byte *) ext->e_type);
  }
  in->n_sclass = bfd_h_get_8(abfd, ext->e_sclass);
  in->n_numaux = bfd_h_get_8(abfd, ext->e_numaux);

  if (bfd_of_glue_owner != 0) /* we already have a toc, so go home */
    return;

  /* save the bfd for later allocation */
  bfd_of_glue_owner = abfd;

  s = bfd_get_section_by_name (bfd_of_glue_owner , 
			       ARM2THUMB_GLUE_SECTION_NAME);

  if (s == NULL) 
    {
      flags = SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY ;
      
      s = bfd_make_section (bfd_of_glue_owner , ARM2THUMB_GLUE_SECTION_NAME);

      if (s == NULL
	  || !bfd_set_section_flags (bfd_of_glue_owner, s, flags)
	  || !bfd_set_section_alignment (bfd_of_glue_owner, s, 2))
	{
	  /* FIXME: set appropriate bfd error */
	  abort();
	}
    }

  s = bfd_get_section_by_name ( bfd_of_glue_owner , THUMB2ARM_GLUE_SECTION_NAME);

  if (s == NULL) 
    {
      flags = SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY ;
      
      s = bfd_make_section (bfd_of_glue_owner , THUMB2ARM_GLUE_SECTION_NAME);
      
      if (s == NULL
	  || !bfd_set_section_flags (bfd_of_glue_owner, s, flags)
	  || !bfd_set_section_alignment (bfd_of_glue_owner, s, 2))
	{
	  /* FIXME: set appropriate bfd error krk@cygnus.com */
	  abort();
	}
    }
  
  return;
}

static bfd_coff_backend_data arm_bfd_coff_std_swap_table =
{
  coff_swap_aux_in, arm_bfd_coff_swap_sym_in, coff_swap_lineno_in,
  coff_swap_aux_out, coff_swap_sym_out,
  coff_swap_lineno_out, coff_swap_reloc_out,
  coff_swap_filehdr_out, coff_swap_aouthdr_out,
  coff_swap_scnhdr_out,
  FILHSZ, AOUTSZ, SCNHSZ, SYMESZ, AUXESZ, RELSZ, LINESZ,
#ifdef COFF_LONG_FILENAMES
  true,
#else
  false,
#endif
#ifdef COFF_LONG_SECTION_NAMES
  true,
#else
  false,
#endif
  COFF_DEFAULT_SECTION_ALIGNMENT_POWER,
  coff_swap_filehdr_in, coff_swap_aouthdr_in, coff_swap_scnhdr_in,
  coff_swap_reloc_in, coff_bad_format_hook, coff_set_arch_mach_hook,
  coff_mkobject_hook, styp_to_sec_flags, coff_set_alignment_hook,
  coff_slurp_symbol_table, symname_in_debug_hook, coff_pointerize_aux_hook,
  coff_print_aux, coff_reloc16_extra_cases, coff_reloc16_estimate,
  coff_sym_is_global, coff_compute_section_file_positions,
  coff_start_final_link, coff_relocate_section, coff_rtype_to_howto,
  coff_adjust_symndx, coff_link_add_one_symbol
};

const bfd_target
#ifdef TARGET_LITTLE_SYM
TARGET_LITTLE_SYM =
#else
armcoff_little_vec =
#endif
{
#ifdef TARGET_LITTLE_NAME
  TARGET_LITTLE_NAME,
#else
  "coff-arm-little",
#endif
  bfd_target_coff_flavour,
  BFD_ENDIAN_LITTLE,		/* data byte order is little */
  BFD_ENDIAN_LITTLE,		/* header byte order is little */

  (HAS_RELOC | EXEC_P |		/* object flags */
   HAS_LINENO | HAS_DEBUG |
   HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),

#ifndef COFF_WITH_PE
  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC), /* section flags */
#else
  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC /* section flags */
   | SEC_LINK_ONCE | SEC_LINK_DUPLICATES),
#endif

#ifdef TARGET_UNDERSCORE
  TARGET_UNDERSCORE,		/* leading underscore */
#else
  0,				/* leading underscore */
#endif
  '/',				/* ar_pad_char */
  15,				/* ar_max_namelen */

  bfd_getl64, bfd_getl_signed_64, bfd_putl64,
     bfd_getl32, bfd_getl_signed_32, bfd_putl32,
     bfd_getl16, bfd_getl_signed_16, bfd_putl16, /* data */
  bfd_getl64, bfd_getl_signed_64, bfd_putl64,
     bfd_getl32, bfd_getl_signed_32, bfd_putl32,
     bfd_getl16, bfd_getl_signed_16, bfd_putl16, /* hdrs */

/* Note that we allow an object file to be treated as a core file as well. */
    {_bfd_dummy_target, coff_object_p, /* bfd_check_format */
       bfd_generic_archive_p, coff_object_p},
    {bfd_false, coff_mkobject, _bfd_generic_mkarchive, /* bfd_set_format */
       bfd_false},
    {bfd_false, coff_write_object_contents, /* bfd_write_contents */
       _bfd_write_archive_contents, bfd_false},

     BFD_JUMP_TABLE_GENERIC (coff),
     BFD_JUMP_TABLE_COPY (coff),
     BFD_JUMP_TABLE_CORE (_bfd_nocore),
     BFD_JUMP_TABLE_ARCHIVE (_bfd_archive_coff),
     BFD_JUMP_TABLE_SYMBOLS (coff),
     BFD_JUMP_TABLE_RELOCS (coff),
     BFD_JUMP_TABLE_WRITE (coff),
     BFD_JUMP_TABLE_LINK (coff),
     BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

  & arm_bfd_coff_std_swap_table,
};

const bfd_target
#ifdef TARGET_BIG_SYM
TARGET_BIG_SYM =
#else
armcoff_big_vec =
#endif
{
#ifdef TARGET_BIG_NAME
  TARGET_BIG_NAME,
#else
  "coff-arm-big",
#endif
  bfd_target_coff_flavour,
  BFD_ENDIAN_BIG,		/* data byte order is big */
  BFD_ENDIAN_BIG,		/* header byte order is big */

  (HAS_RELOC | EXEC_P |		/* object flags */
   HAS_LINENO | HAS_DEBUG |
   HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),

#ifndef COFF_WITH_PE
  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC), /* section flags */
#else
  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC /* section flags */
   | SEC_LINK_ONCE | SEC_LINK_DUPLICATES),
#endif

#ifdef TARGET_UNDERSCORE
  TARGET_UNDERSCORE,		/* leading underscore */
#else
  0,				/* leading underscore */
#endif
  '/',				/* ar_pad_char */
  15,				/* ar_max_namelen */

  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
     bfd_getb32, bfd_getb_signed_32, bfd_putb32,
     bfd_getb16, bfd_getb_signed_16, bfd_putb16, /* data */
  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
     bfd_getb32, bfd_getb_signed_32, bfd_putb32,
     bfd_getb16, bfd_getb_signed_16, bfd_putb16, /* hdrs */

/* Note that we allow an object file to be treated as a core file as well. */
    {_bfd_dummy_target, coff_object_p, /* bfd_check_format */
       bfd_generic_archive_p, coff_object_p},
    {bfd_false, coff_mkobject, _bfd_generic_mkarchive, /* bfd_set_format */
       bfd_false},
    {bfd_false, coff_write_object_contents, /* bfd_write_contents */
       _bfd_write_archive_contents, bfd_false},

     BFD_JUMP_TABLE_GENERIC (coff),
     BFD_JUMP_TABLE_COPY (coff),
     BFD_JUMP_TABLE_CORE (_bfd_nocore),
     BFD_JUMP_TABLE_ARCHIVE (_bfd_archive_coff),
     BFD_JUMP_TABLE_SYMBOLS (coff),
     BFD_JUMP_TABLE_RELOCS (coff),
     BFD_JUMP_TABLE_WRITE (coff),
     BFD_JUMP_TABLE_LINK (coff),
     BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

  & arm_bfd_coff_std_swap_table,
};
