/* This file is part of GNU cflow
   Copyright (C) 1997,2005 Sergey Poznyakoff
 
   GNU cflow is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
 
   GNU cflow is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GNU cflow; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA  */

#include <cflow.h>
#include <parser.h>

char *level_mark;
/* Tree level information. level_mark[i] contains 1 if there are more
 * leafs on the level `i', otherwise it contains 0
 */
int level_mark_size=1000;
/* Arbitrary size of level mark. Seems to be more than enough */

int out_line = 1; /* Current output line number */
FILE *outfile;    /* Output file */


/* Low level output functions */

struct output_driver {
     char *name;
     void (*handler) (cflow_output_command cmd,
		      FILE *outfile, int line,
		      void *data, void *handler_data);
     void *handler_data;
};

static int driver_index;
static int driver_max=0;
struct output_driver output_driver[MAX_OUTPUT_DRIVERS];

int
register_output(const char *name,
		void (*handler) (cflow_output_command cmd,
				 FILE *outfile, int line,
				 void *data, void *handler_data),
		void *handler_data)
{
     if (driver_max == MAX_OUTPUT_DRIVERS-1)
	  abort ();
     output_driver[driver_max].name = strdup(name);
     output_driver[driver_max].handler = handler;
     output_driver[driver_max].handler_data = handler_data;
     return driver_max++;
}

int
select_output_driver(const char *name)
{
     int i;
     for (i = 0; i < driver_max; i++)
	  if (strcmp(output_driver[i].name, name) == 0) {
	       driver_index = i;
	       return 0;
	  }
     return -1;
}
	       
static void
newline()
{
     output_driver[driver_index].handler(cflow_output_newline,
					 outfile, out_line,
					 NULL,
				         output_driver[driver_index].handler_data);
     out_line++;
}

static void
begin()
{
     output_driver[driver_index].handler(cflow_output_begin,
					 outfile, out_line,
					 NULL,
					 output_driver[driver_index].handler_data);
}

static void
end()
{
     output_driver[driver_index].handler(cflow_output_end,
					 outfile, out_line,
					 NULL,
					 output_driver[driver_index].handler_data);
}

static void
separator()
{
     output_driver[driver_index].handler(cflow_output_separator,
					 outfile, out_line,
					 NULL,
					 output_driver[driver_index].handler_data);
}

static void
print_symbol (int direct, int level, int last, Symbol *sym)
{
     struct output_symbol output_symbol;
     output_symbol.direct = direct;
     output_symbol.level = level;
     output_symbol.last = last;
     output_symbol.sym = sym;
     output_driver[driver_index].handler(cflow_output_symbol,
					 outfile, out_line,
					 &output_symbol,
					 output_driver[driver_index].handler_data);
     
}

static void
header(char *str)
{
     output_driver[driver_index].handler(cflow_output_text,
					 outfile, out_line,
					 str,
					 output_driver[driver_index].handler_data);
     
}


static int
compare(const void *ap, const void *bp)
{
     Symbol * const *a = ap;
     Symbol * const *b = bp;
     return strcmp((*a)->name, (*b)->name);
}

static int
is_var(Symbol *symp)
{
     if (record_typedefs &&
	 symp->type == SymToken &&
	 symp->v.type.token_type == TYPE &&
	 symp->v.type.source)
	  return 1;
     return symp->type == SymFunction &&
	  (symp->v.func.storage == ExternStorage ||
	   symp->v.func.storage == StaticStorage);
}

static int
is_fun(Symbol *symp)
{
     return symp->type == SymFunction && symp->v.func.argc >= 0;
}

static void
clear_active(Symbol *sym)
{
    sym->active = 0;
}


/* Cross-reference output */
void
print_refs(char *name, Consptr cons)
{
    Ref *refptr;
    
    for ( ; cons; cons = CDR(cons)) {
	refptr = (Ref*)CAR(cons);
	fprintf(outfile, "%s   %s:%d\n",
		name,
		refptr->source,
		refptr->line);
    }
}

static void
print_function(Symbol *symp)
{
    if (symp->v.func.source) {
	 fprintf(outfile, "%s * %s:%d %s\n",
		 symp->name,
		 symp->v.func.source,
		 symp->v.func.def_line,
		 symp->v.func.type);
    }
    print_refs(symp->name, symp->v.func.ref_line);
}

static void
print_type(Symbol *symp)
{
     fprintf(outfile, "%s t %s:%d\n",
	     symp->name,
	     symp->v.type.source,
	     symp->v.type.def_line,
	     symp->v.func.type);
}
   
void
xref_output()
{
    Symbol **symbols, *symp;
    int i, num;
    
    num = collect_symbols(&symbols, is_var);
    qsort(symbols, num, sizeof(*symbols), compare);

    /* produce xref output */
    for (i = 0; i < num; i++) {
	symp = symbols[i];
	switch (symp->type) {
	case SymFunction:
	    print_function(symp);
	    break;
	case SymToken:
	    print_type(symp);
	    break;
	}
    }
    free(symbols);
}



/* Tree output */

/* Scan call tree. Mark the recursive calls
 */
static void
scan_tree(int lev, Symbol *sym)
{
    Consptr cons;

    if (sym->active) {
	sym->v.func.recursive = 1;
	return;
    }
    sym->active = 1;
    for (cons = sym->v.func.callee; cons; cons = CDR(cons)) {
	scan_tree(lev+1, (Symbol*)CAR(cons));
    }
    sym->active = 0;
}

static void
set_active(Symbol *sym)
{
    sym->active = out_line;
}

/* Produce direct call tree output
 */
static void
direct_tree(int lev, int last, Symbol *sym)
{
    Consptr cons;
    
    print_symbol(1, lev, last, sym);
    newline();
    if (sym->active)
	return;
    set_active(sym);
    for (cons = sym->v.func.callee; cons; cons = CDR(cons)) {
	level_mark[lev+1] = CDR(cons) != NULL;
	direct_tree(lev+1, CDR(cons) == NULL, (Symbol*)CAR(cons));
    }
    clear_active(sym);
}

/* Produce reverse call tree output
 */
static void
inverted_tree(int lev, int last, Symbol *sym)
{
    Consptr cons;

    print_symbol(0, lev, last, sym);
    newline();
    if (sym->active)
	return;
    set_active(sym);
    for (cons = sym->v.func.caller; cons; cons = CDR(cons)) {
	level_mark[lev+1] = CDR(cons) != NULL;
	inverted_tree(lev+1, CDR(cons) == NULL, (Symbol*)CAR(cons));
    }
    clear_active(sym);
}

static void
tree_output()
{
    Symbol **symbols, *main;
    int i, num;

    /* Collect and sort symbols */
    num = collect_symbols(&symbols, is_fun);
    qsort(symbols, num, sizeof(*symbols), compare);
    /* Scan and mark the recursive ones */
    for (i = 0; i < num; i++) {
	if (symbols[i]->v.func.callee)
            scan_tree(0, symbols[i]);
    }

    /* Produce output */
    begin();
    
    header("Direct Tree");
    main = lookup(start_name);
    if (main) {
	direct_tree(0, 0, main);
	separator();
    } else {
	for (i = 0; i < num; i++) {
	    if (symbols[i]->v.func.callee == NULL)
		continue;
	    direct_tree(0, 0, symbols[i]);
	    separator();
	}
    }

    if (!reverse_tree)
	return;
    
    header("Reverse Tree");
    for (i = 0; i < num; i++) {
	inverted_tree(0, 0, symbols[i]);
	separator();
    }

    end();
    
    free(symbols);
}

void
output()
{
    if (strcmp(outname, "-") == 0) {
	outfile = stdout;
    } else {
	outfile = fopen(outname, "w");
	if (!outfile)
	    error(SYSTEM_ERROR|FATAL(2), "cannot open file `%s'", outname);
    } 
	
    level_mark = emalloc(level_mark_size);
    level_mark[0] = 0;
    if (print_option & PRINT_XREF) {
	xref_output();
    }
    if (print_option & PRINT_TREE) {
	tree_output();
    }
    fclose(outfile);
}






