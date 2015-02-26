#include "rcd.h"
#include "jpq.h"
#include "rcdpaste.h"

#pragma librcd

static fstr_mem_t* gen_shortname() {
    fstr_t alphanumeric = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    fstr_mem_t* ret = fstr_alloc(10);
    for (int i = 0; i < 10; i++)
        ret->str[i] = alphanumeric.str[prng_rand() % alphanumeric.len];
    return ret;
}

static paste_t* fetch_paste(jpq_session_t* s, fstr_t name, bool allow_shortname) {
    jpq_res_t* res = jpq(s, (select paste_id, name, shortname, encode(content, 'base64'), language, date_created
            from pastes where name = $1), name);
    if (res->n_rows == 0 && allow_shortname) {
        res = jpq(s, (select paste_id, name, shortname, encode(content, 'base64'), language, date_created
            from pastes where shortname = $1), name);
    }
    if (res->n_rows == 0)
        return 0;

    paste_t* ret = new(paste_t);
    JPQ_READ(res, 0, &ret->paste_id, &ret->name, &ret->shortname, &ret->content, &ret->language, &ret->cdate);
    ret->content = fss(fstr_base64_decode(ret->content));
    ret->name = fsc(ret->name);
    ret->shortname = fsc(ret->shortname);
    ret->language = fsc(ret->language);
    return ret;
}

paste_t* get_paste(fstr_t name) {
    paste_t* ret;
    JPQ_TXN_PUSHS_LEAK {
        ret = fetch_paste(s, name, true);
    } JPQ_TXN_POP;
    return ret;
}

fstr_t upload_paste(fstr_t name, fstr_t content, fstr_t language) {
    fstr_t ret;
    JPQ_TXN_PUSHS_LEAK {
        paste_t* existing = fetch_paste(s, name, false);
        fstr_t enc = fss(fstr_base64_encode(content));
        if (existing != 0) {
            jpq_put1(jpq(s,
                (update pastes set content = decode($1, 'base64'), date_created = now(),
                 language = $2 where paste_id = $3),
                content, jpq_wnull(language), existing->paste_id));
            ret = existing->shortname;
        }
        else {
            fstr_t shortname = fss(gen_shortname());
            jpq_res_t* res = jpq(s,
                (insert into pastes (name, shortname, content, language, date_created)
                 values ($1, $2, decode($3, 'base64'), $4, now())),
                name, shortname, content, jpq_wnull(language));
            ret = shortname;
        }
    } JPQ_TXN_POP;
    return ret;
}
