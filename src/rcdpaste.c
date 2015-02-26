#include "rcd.h"
#include "jpq.h"
#include "wsr.h"
#include "wsr-mime.h"
#include "wsr-tpl.h"
#include "rcdpaste.h"

#pragma librcd

typedef enum { RAW, DOWNLOAD, VIEW } display_t;

typedef struct {
    fstr_t asset_path;
    fstr_t secret_token;
    wsr_tpl_ctx_t* tpl_ctx;
} ctx_t;

static fstr_mem_t* highlight_pygments(fstr_t content, fstr_t language) { sub_heap {
    fstr_t python_lines[] = {
"from pygments import highlight",
"from pygments.lexers import get_lexer_by_name",
"from pygments.formatters import HtmlFormatter",
"import sys",
"",
concs("language = '", language, "'"),
"code = sys.stdin.read()",
"print(highlight(code, get_lexer_by_name(language), HtmlFormatter(linenos='inline', noclasses=True, style='monokai')))"
    };
    fstr_t python_code = fss(fstr_concat(python_lines, LENGTHOF(python_lines), "\n"));

    list(fstr_t)* cmd_args = new_list(fstr_t, "-c", python_code);
    rio_proc_t* proc_h;
    rio_t* pipe_h;
    rio_proc_execute_and_pipe(fss(rio_which("python")), cmd_args, true, &proc_h, &pipe_h);
    rio_write(pipe_h, content);
    rio_pipe_close_end(pipe_h, false);
    fstr_t out_buf = fss(fstr_alloc_buffer(50 << 20));
    fstr_t output = rio_read_to_end(pipe_h, out_buf);
    int32_t ret = rio_proc_wait(proc_h);
    if (ret != 0)
        return 0;
    return escape(fstr_cpy(output));
}}

static fstr_mem_t* markdown(fstr_t content) {
    // TODO "<div class='markdown-body'>" + md2html(content) + "</div>"
    // new PegDownProcessor(Extensions.ABBREVIATIONS | Extensions.AUTOLINKS | Extensions.DEFINITIONS | Extensions.FENCED_CODE_BLOCKS | Extensions.TABLES | Extensions.WIKILINKS)
    return 0;
}

static fstr_mem_t* highlight(fstr_t content, fstr_t language) {
    fstr_t lang = language;
#pragma re2c(lang): \
    ^(llvm|asm|c|cpp|css|json|yaml|csharp|go|haskell|html|xml|js|java|scala|make| \
    latex|matlab|php|prolog|python|python3|ruby|rust|lua|bash|text|sql|vim)$ \
        {@return highlight_pygments(content, language)} | \
    ^publish:html$ {@return fstr_cpy(content)} | \
    ^publish:md$ {@return markdown(content)}
    return 0;
}

static wsr_rsp_t* response_json(wsr_status_t http_status, fstr_t status, fstr_t content) {
    fstr_t data = fss(json_stringify(jobj_new(
        {"status", jstr(status)},
        {"content", jstr(content)}
    )));
    return wsr_response_static(http_status, data, wsr_mime_json);
}

static wsr_rsp_t* handle_upload(ctx_t* ctx, wsr_req_t* req) {
    fstr_t token, name, content, language;
    try {
        json_value_t json = json_parse(req->post_body)->value;
        token = jstrv(JSON_REF(json, "token"));
        name = jstrv(JSON_REF(json, "name"));
        content = jstrv(JSON_REF(json, "content"));
        json_value_t language_jv = JSON_LREF(json, "language");
        language = (json_is_null(language_jv)? "": jstrv(language_jv));
    } catch(exception_io, e) {
        return response_json(HTTP_BAD_REQUEST, "fail", e->message);
    }

    if (!fstr_equal(token, ctx->secret_token))
        return response_json(HTTP_FORBIDDEN, "fail", "Not allowed to add pastes");
    try {
        fstr_t short_name = upload_paste(name, content, language);
        return response_json(HTTP_OK, "success", short_name);
    } catch(exception_io, e) {
        return response_json(HTTP_INTERNAL_SERVER_ERROR, "error", "Could not save paste");
    }
}

static wsr_rsp_t* response_download(fstr_t content, fstr_t name, fstr_t disposition_type) {
    wsr_rsp_t* rsp = wsr_response_static(HTTP_OK, content, wsr_mime_bin);
    (void) dict_insert(rsp->headers, fstr_t, fstr("content-disposition"),
            concs(disposition_type, "; filename=", name));
    return rsp;
}

static wsr_rsp_t* response_paste_html(ctx_t* ctx, paste_t* paste, fstr_t markup) {
    int width = (fstr_prefixes(paste->language, "publish:")? 800: 1024);
    html_t* tpl = wsr_tpl_start();
    dict(html_t*)* pt = new_dict(html_t*,
        {"title", H("rcdpaste")},
        {"content", HRAW(markup)});
    json_value_t jdata = jobj_new(
        {"name", jstr(paste->name)},
        {"width", jnum(width)});
    wsr_tpl_render_jd(ctx->tpl_ctx, "/paste.html", pt, jdata, tpl);
    return wsr_response_html(HTTP_OK, tpl);
}

static wsr_rsp_t* response_paste(ctx_t* ctx, wsr_req_t* req, fstr_t path, display_t type) {
    paste_t* paste = get_paste(path);
    if (!paste)
        return wsr_response_static(HTTP_NOT_FOUND, "No such paste", wsr_mime_txt);
    if (type == RAW)
        return wsr_response_static(HTTP_OK, paste->content, wsr_mime_txt);
    if (type == DOWNLOAD)
        return response_download(paste->content, paste->name, "attachment");
    fstr_mem_t* markup = highlight(paste->content, paste->language);
    if (markup == 0)
        return response_download(paste->content, paste->name, "inline");
    return response_paste_html(ctx, paste, fss(markup));
}

static wsr_rsp_t* response_asset(ctx_t* ctx, wsr_req_t* req, fstr_t path) {
    req->path = concs("/", path);
    return wsr_response_file(req, ctx->asset_path);
}

static wsr_rsp_t* http_request_cb(wsr_req_t* req, void* cb_arg) {
    ctx_t* ctx = cb_arg;
    if (req->method == METHOD_POST)
        return handle_upload(ctx, req);
    fstr_t path;
#pragma re2c(req->path): \
    ^/$ {@return wsr_response_static(HTTP_OK, "rcdpaste", wsr_mime_txt)} | \
    ^/assets/(.*){path}$ {@return response_asset(ctx, req, path)} | \
    ^/raw/(.*){path}$ {@return response_paste(ctx, req, path, RAW)} | \
    ^/download/(.*){path}$ {@return response_paste(ctx, req, path, DOWNLOAD)} | \
    ^/(.*){path}$ {@return response_paste(ctx, req, path, VIEW)}
    return wsr_response(HTTP_NOT_FOUND);
}

void rcd_main(list(fstr_t)* main_args, list(fstr_t)* main_env) {
    dict(fstr_t)* params = new_dict(fstr_t,
        {"db.default.dbname", "paste"},
        {"db.default.user", "postgres"},
        {"db.default.password", "postgres"},
        {"http.port", "80"}
    );
    list_foreach(main_args, fstr_t, arg) {
        fstr_t param, value;
#pragma re2c(arg): \
        ^-D([^=]*){param}="?(.*){value}"?$ {@has_arg}
        continue;
has_arg:
        dict_replace(params, fstr_t, param, value);
    }

    fstr_t* secret_token_n = dict_read(params, fstr_t, "token");
    if (secret_token_n == 0) {
        rio_debug("missing secret token - pass -Dtoken=secret on the command line.\n");
        lwt_exit(1);
    }

    ctx_t ctx = {
        .asset_path = fss(rio_file_real_path("./public")),
        .secret_token = fsc(*secret_token_n),
        .tpl_ctx = wsr_tpl_init(fss(rio_file_real_path("./views")), true),
    };

    fstr_t dbname = *dict_read(params, fstr_t, "db.default.dbname");
    fstr_t dbuser = *dict_read(params, fstr_t, "db.default.user");
    fstr_t dbpassword = *dict_read(params, fstr_t, "db.default.password");
    fstr_t pg_conn_str = concs("host='127.0.0.1' port='5432' user='", dbuser, "' dbname='", dbname, "' password='", dbpassword, "'");
    jpq_initialize(pg_conn_str, 10);

    uint32_t port = fs2ui(*dict_read(params, fstr_t, "http.port"));
    wsr_cfg_t cfg = wsr_default_cfg();
    cfg.bind = new_list(rio_in_addr4_t, {.port = port});
    cfg.req_cb = http_request_cb;
    cfg.cb_arg = &ctx;
    wsr_start(cfg);
}
