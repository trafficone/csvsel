#ifndef POSTPROCESS_H
#define POSTPROCESS_H

#include <stdio.h>

#include "growbuf.h"
#include "csvsel.h"

typedef enum { FIRSTLINE, HEADERFILE } headertype;

typedef struct {
  headertype htype;
  growbuf* headers;
  char* file;
} eval_args;

typedef struct _header {
  char* name;
  char* table;
  size_t col;
} headerstruct;

void eval_and_save_header(growbuf* fields, size_t rownum, eval_args* args);

int querypostprocess(growbuf* selectors,growbuf* froms,growbuf* columnnames,FILE* input,FILE* header);
 

#endif // POSTPROCESS_H
