/*
 * Query post-processor
 *
 * By Jason Schlesinger 11/14/2012
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sysexits.h>
#include <errno.h>

#include "growbuf.h"
#include "csvformat.h"
#include "util.h"
#include "postprocess.h"
#include "csvsel.h"

//TODO: FROM can be either full path or relative path.  Try to determine this, and convert to fullpath.

int querypostprocess(growbuf* selectors,growbuf* froms,growbuf* columnnames,FILE* input,FILE* header){
  int i,j;
  bool columnfound;
  headerstruct * hed;
  growbuf* headers;
  headers = growbuf_create(1);
  if (NULL == headers) {
      fprintf(stderr, "malloc failed\n");
      return 4;
  }
 
  //first determine the input(s)
  //then try to find the files/selectors of columnnames
  //1. if there is a file, only check the headers for that file
  //2. if not, check the headers of all files to find the column/file combo
  if(input!=stdin && froms->size != 0){
    fprintf(stderr,"Cannot combine FROM with option -f\n");
    return 1;
  }
  if(froms->size > 1){
    fprintf(stderr,"Currently unable to support multiple files.\n");
    return 2;
  }
  
  //first and foremost, handle the stdin case
  if(input==stdin || froms->size == 1){
    for(i=0;i<selectors->size;i++){
      (((selector**)selectors->buf)[i])->table="default";
    }
    for(i=0;i<columnnames->size;i++){
      columnname * c = ((columnname**)columnnames->buf)[i];
      c->table = "default";
    }
  } 
  if(header == NULL){
    for(i=0;i<froms->size;i++){
      fromval* f = ((fromval**)froms->buf)[i];
      FILE * hi = fopen(f->table,"r");
      headertype htype = FIRSTLINE;
      char* file = f->table;
      eval_args args = {htype, headers, file};
      if(NULL == hi || 0!=read_csv(header, (row_evaluator)&eval_and_save_header, (void*)&args)){
       fprintf(stderr,"Unable to read headers from %s\n",f->table);
        return 5;
      }
      fclose(hi);
    }
  } else {
    headertype htype = HEADERFILE;
    eval_args args = {htype, headers,""};
    if(0!= read_csv(header, (row_evaluator)&eval_and_save_header, (void*)&args)){
      fprintf(stderr,"Unable to read header file\n");
      return 5;
    }
  }

  for(i=0;i<columnnames->size;i++){
    columnfound = false;
    columnname * c = ((columnname**)columnnames->buf)[i];
    for(int j=0;j<headers->size;j++){
      hed = ((headerstruct**)headers->buf)[j];
      if(strcmp(hed->table,c->table)==0 && strcmp(hed->name,c->name) == 0){
        c->col = hed->col;
        selector* s = (selector*)malloc(sizeof(selector));
        s->un1.column = c->col;
        s->table = c->table;
        growbuf_append(selectors, &s, sizeof(size_t));
        columnfound = true;
        break;
      }
    }
    if(!columnfound){
      fprintf(stderr,"Column %s not found in table",c->name);
      return 3;
    }
  }
  //TODO: Also use what we learned from processing headers to modify our base_condition values of type 
  return 0;
}


void eval_and_save_header(growbuf* fields, size_t rownum, eval_args* args){
  char* table;
  switch( args->htype ){
    case FIRSTLINE:
      if(rownum > 0){return;}
      for(int i=0;i<fields->size;i++){
        growbuf* field = ((growbuf**)fields->buf)[i];
        headerstruct* h = (headerstruct*)malloc(sizeof(headerstruct));
        h->table = args->file;
        h->name  = (char*)field->buf;
        h->col   = i;
        growbuf_append(args->headers, &h, sizeof(size_t));
      }
      break;
    case HEADERFILE:
      for(int i=0;i<fields->size;i++){
        growbuf* field = ((growbuf**)fields->buf)[i];
        if(i==0){
          table = (char*)field->buf;
        } else {
          headerstruct* h = (headerstruct*)malloc(sizeof(headerstruct));
          h->table = table;
          h->name  = (char*)field->buf;
          h->col   = i;
          growbuf_append(args->headers, &h, sizeof(size_t));
        }
      }
  }
  return;
}
