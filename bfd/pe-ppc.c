/* BFD back-end for PowerPC PECOFF files.
   Copyright (C) 1995-2014 Free Software Foundation, Inc.

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"

#define E_FILENMLEN     18

#define PPC_PE

#define TARGET_LITTLE_SYM   bfd_powerpcle_pe_vec
#define TARGET_LITTLE_NAME "pe-powerpcle"

#define TARGET_BIG_SYM      bfd_powerpc_pe_vec
#define TARGET_BIG_NAME    "pe-powerpc"

#define COFF_WITH_PE

#define COFF_LONG_SECTION_NAMES

/* FIXME: verify PCRELOFFSET is always false */

/* FIXME: This target no longer works.  Search for POWERPC_LE_PE in
   coff-ppc.c and peigen.c.  */

#ifndef bfd_pe_print_pdata
#define bfd_pe_print_pdata	NULL
#endif

#include "coff-ppc.c"
