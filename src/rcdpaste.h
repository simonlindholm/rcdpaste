#pragma once
#include "rcd.h"

typedef struct {
    int128_t paste_id;
    fstr_t name, shortname, content;
    fstr_t language;
    int128_t cdate;
} paste_t;

paste_t* get_paste(fstr_t name);
fstr_t upload_paste(fstr_t name, fstr_t content, fstr_t language);
