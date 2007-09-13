
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_mail.h>


static ngx_int_t ngx_mail_pop3_user(ngx_mail_session_t *s, ngx_connection_t *c);
static ngx_int_t ngx_mail_pop3_pass(ngx_mail_session_t *s, ngx_connection_t *c);
static ngx_int_t ngx_mail_pop3_capa(ngx_mail_session_t *s, ngx_connection_t *c,
    ngx_int_t stls);
static ngx_int_t ngx_mail_pop3_stls(ngx_mail_session_t *s, ngx_connection_t *c);
static ngx_int_t ngx_mail_pop3_apop(ngx_mail_session_t *s, ngx_connection_t *c);
static ngx_int_t ngx_mail_pop3_auth(ngx_mail_session_t *s, ngx_connection_t *c);


static u_char  pop3_greeting[] = "+OK POP3 ready" CRLF;
static u_char  pop3_ok[] = "+OK" CRLF;
static u_char  pop3_next[] = "+ " CRLF;
static u_char  pop3_username[] = "+ VXNlcm5hbWU6" CRLF;
static u_char  pop3_password[] = "+ UGFzc3dvcmQ6" CRLF;
static u_char  pop3_invalid_command[] = "-ERR invalid command" CRLF;


void
ngx_mail_pop3_init_session(ngx_mail_session_t *s, ngx_connection_t *c)
{
    u_char                    *p;
    ngx_mail_core_srv_conf_t  *cscf;

    cscf = ngx_mail_get_module_srv_conf(s, ngx_mail_core_module);

    if (cscf->pop3_auth_methods
        & (NGX_MAIL_AUTH_APOP_ENABLED|NGX_MAIL_AUTH_CRAM_MD5_ENABLED))
    {
        if (ngx_mail_salt(s, c, cscf) != NGX_OK) {
            ngx_mail_session_internal_server_error(s);
            return;
        }

        s->out.data = ngx_palloc(c->pool, sizeof(pop3_greeting) + s->salt.len);
        if (s->out.data == NULL) {
            ngx_mail_session_internal_server_error(s);
            return;
        }

        p = ngx_cpymem(s->out.data, pop3_greeting, sizeof(pop3_greeting) - 3);
        *p++ = ' ';
        p = ngx_cpymem(p, s->salt.data, s->salt.len);

        s->out.len = p - s->out.data;

    } else {
        s->out.len = sizeof(pop3_greeting) - 1;
        s->out.data = pop3_greeting;
    }

    c->read->handler = ngx_mail_pop3_init_protocol;

    ngx_mail_send(c->write);
}


void
ngx_mail_pop3_init_protocol(ngx_event_t *rev)
{
    ngx_connection_t    *c;
    ngx_mail_session_t  *s;

    c = rev->data;

    c->log->action = "in auth state";

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        c->timedout = 1;
        ngx_mail_close_connection(c);
        return;
    }

    s = c->data;

    if (s->buffer == NULL) {
        if (ngx_array_init(&s->args, c->pool, 2, sizeof(ngx_str_t))
            == NGX_ERROR)
        {
            ngx_mail_session_internal_server_error(s);
            return;
        }

        s->buffer = ngx_create_temp_buf(c->pool, 128);
        if (s->buffer == NULL) {
            ngx_mail_session_internal_server_error(s);
            return;
        }
    }

    s->mail_state = ngx_pop3_start;
    c->read->handler = ngx_pop3_auth_state;

    ngx_pop3_auth_state(rev);
}


void
ngx_pop3_auth_state(ngx_event_t *rev)
{
    ngx_int_t            rc;
    ngx_connection_t    *c;
    ngx_mail_session_t  *s;

    c = rev->data;
    s = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_MAIL, c->log, 0, "pop3 auth state");

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        c->timedout = 1;
        ngx_mail_close_connection(c);
        return;
    }

    if (s->out.len) {
        ngx_log_debug0(NGX_LOG_DEBUG_MAIL, c->log, 0, "pop3 send handler busy");
        s->blocked = 1;
        return;
    }

    s->blocked = 0;

    rc = ngx_mail_read_command(s);

    if (rc == NGX_AGAIN || rc == NGX_ERROR) {
        return;
    }

    s->out.len = sizeof(pop3_ok) - 1;
    s->out.data = pop3_ok;

    if (rc == NGX_OK) {
        switch (s->mail_state) {

        case ngx_pop3_start:

            switch (s->command) {

            case NGX_POP3_USER:
                rc = ngx_mail_pop3_user(s, c);
                break;

            case NGX_POP3_CAPA:
                rc = ngx_mail_pop3_capa(s, c, 1);
                break;

            case NGX_POP3_APOP:
                rc = ngx_mail_pop3_apop(s, c);
                break;

            case NGX_POP3_AUTH:
                rc = ngx_mail_pop3_auth(s, c);
                break;

            case NGX_POP3_QUIT:
                s->quit = 1;
                break;

            case NGX_POP3_NOOP:
                break;

            case NGX_POP3_STLS:
                rc = ngx_mail_pop3_stls(s, c);
                break;

            default:
                rc = NGX_MAIL_PARSE_INVALID_COMMAND;
                s->mail_state = ngx_pop3_start;
                break;
            }

            break;

        case ngx_pop3_user:

            switch (s->command) {

            case NGX_POP3_PASS:
                rc = ngx_mail_pop3_pass(s, c);
                break;

            case NGX_POP3_CAPA:
                rc = ngx_mail_pop3_capa(s, c, 0);
                break;

            case NGX_POP3_QUIT:
                s->quit = 1;
                break;

            case NGX_POP3_NOOP:
                break;

            default:
                rc = NGX_MAIL_PARSE_INVALID_COMMAND;
                s->mail_state = ngx_pop3_start;
                break;
            }

            break;

        /* suppress warinings */
        case ngx_pop3_passwd:
            break;

        case ngx_pop3_auth_login_username:
            rc = ngx_mail_auth_login_username(s, c);

            s->out.len = sizeof(pop3_password) - 1;
            s->out.data = pop3_password;
            s->mail_state = ngx_pop3_auth_login_password;
            break;

        case ngx_pop3_auth_login_password:
            rc = ngx_mail_auth_login_password(s, c);
            break;

        case ngx_pop3_auth_plain:
            rc = ngx_mail_auth_plain(s, c, 0);
            break;

        case ngx_pop3_auth_cram_md5:
            rc = ngx_mail_auth_cram_md5(s, c);
            break;
        }
    }

    switch (rc) {

    case NGX_DONE:
        ngx_mail_auth(s);
        return;

    case NGX_ERROR:
        ngx_mail_session_internal_server_error(s);
        return;

    case NGX_MAIL_PARSE_INVALID_COMMAND:
        s->mail_state = ngx_pop3_start;
        s->state = 0;

        s->out.len = sizeof(pop3_invalid_command) - 1;
        s->out.data = pop3_invalid_command;

        /* fall through */

    case NGX_OK:

        s->args.nelts = 0;
        s->buffer->pos = s->buffer->start;
        s->buffer->last = s->buffer->start;

        if (s->state) {
            s->arg_start = s->buffer->start;
        }

        ngx_mail_send(c->write);
    }
}

static ngx_int_t
ngx_mail_pop3_user(ngx_mail_session_t *s, ngx_connection_t *c)
{
    ngx_str_t            *arg;
#if (NGX_MAIL_SSL)
    ngx_mail_ssl_conf_t  *sslcf;

    if (c->ssl == NULL) {
        sslcf = ngx_mail_get_module_srv_conf(s, ngx_mail_ssl_module);

        if (sslcf->starttls == NGX_MAIL_STARTTLS_ONLY) {
            return NGX_MAIL_PARSE_INVALID_COMMAND;
        }
    }

#endif

    if (s->args.nelts != 1) {
        return NGX_MAIL_PARSE_INVALID_COMMAND;
    }

    arg = s->args.elts;
    s->login.len = arg[0].len;
    s->login.data = ngx_palloc(c->pool, s->login.len);
    if (s->login.data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(s->login.data, arg[0].data, s->login.len);

    ngx_log_debug1(NGX_LOG_DEBUG_MAIL, c->log, 0,
                   "pop3 login: \"%V\"", &s->login);

    s->mail_state = ngx_pop3_user;

    return NGX_OK;
}


static ngx_int_t
ngx_mail_pop3_pass(ngx_mail_session_t *s, ngx_connection_t *c)
{
    ngx_str_t  *arg;

    if (s->args.nelts != 1) {
        return NGX_MAIL_PARSE_INVALID_COMMAND;
    }

    arg = s->args.elts;
    s->passwd.len = arg[0].len;
    s->passwd.data = ngx_palloc(c->pool, s->passwd.len);
    if (s->passwd.data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(s->passwd.data, arg[0].data, s->passwd.len);

#if (NGX_DEBUG_MAIL_PASSWD)
    ngx_log_debug1(NGX_LOG_DEBUG_MAIL, c->log, 0,
                   "pop3 passwd: \"%V\"", &s->passwd);
#endif

    return NGX_DONE;
}


static ngx_int_t
ngx_mail_pop3_capa(ngx_mail_session_t *s, ngx_connection_t *c, ngx_int_t stls)
{
    ngx_mail_core_srv_conf_t  *cscf;
#if (NGX_MAIL_SSL)
    ngx_mail_ssl_conf_t       *sslcf;
#endif

    cscf = ngx_mail_get_module_srv_conf(s, ngx_mail_core_module);

#if (NGX_MAIL_SSL)

    if (stls && c->ssl == NULL) {
        sslcf = ngx_mail_get_module_srv_conf(s, ngx_mail_ssl_module);

        if (sslcf->starttls == NGX_MAIL_STARTTLS_ON) {
            s->out = cscf->pop3_starttls_capability;
            return NGX_OK;
        }

        if (sslcf->starttls == NGX_MAIL_STARTTLS_ONLY) {
            s->out = cscf->pop3_starttls_only_capability;
            return NGX_OK;
        }
    }

#endif

    s->out = cscf->pop3_capability;
    return NGX_OK;
}


static ngx_int_t
ngx_mail_pop3_stls(ngx_mail_session_t *s, ngx_connection_t *c)
{
#if (NGX_MAIL_SSL)
    ngx_mail_ssl_conf_t  *sslcf;

    if (c->ssl == NULL) {
        sslcf = ngx_mail_get_module_srv_conf(s, ngx_mail_ssl_module);
        if (sslcf->starttls) {
            c->read->handler = ngx_mail_starttls_handler;
            return NGX_OK;
        }
    }

#endif

    return NGX_MAIL_PARSE_INVALID_COMMAND;
}


static ngx_int_t
ngx_mail_pop3_apop(ngx_mail_session_t *s, ngx_connection_t *c)
{
    ngx_str_t                 *arg;
    ngx_mail_core_srv_conf_t  *cscf;
#if (NGX_MAIL_SSL)
    ngx_mail_ssl_conf_t       *sslcf;

    if (c->ssl == NULL) {
        sslcf = ngx_mail_get_module_srv_conf(s, ngx_mail_ssl_module);

        if (sslcf->starttls == NGX_MAIL_STARTTLS_ONLY) {
            return NGX_MAIL_PARSE_INVALID_COMMAND;
        }
    }

#endif

    if (s->args.nelts != 2) {
        return NGX_MAIL_PARSE_INVALID_COMMAND;
    }

    cscf = ngx_mail_get_module_srv_conf(s, ngx_mail_core_module);

    if (!(cscf->pop3_auth_methods & NGX_MAIL_AUTH_APOP_ENABLED)) {
        return NGX_MAIL_PARSE_INVALID_COMMAND;
    }

    arg = s->args.elts;

    s->login.len = arg[0].len;
    s->login.data = ngx_palloc(c->pool, s->login.len);
    if (s->login.data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(s->login.data, arg[0].data, s->login.len);

    s->passwd.len = arg[1].len;
    s->passwd.data = ngx_palloc(c->pool, s->passwd.len);
    if (s->passwd.data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(s->passwd.data, arg[1].data, s->passwd.len);

    ngx_log_debug2(NGX_LOG_DEBUG_MAIL, c->log, 0,
                   "pop3 apop: \"%V\" \"%V\"", &s->login, &s->passwd);

    s->auth_method = NGX_MAIL_AUTH_APOP;

    return NGX_DONE;
}


static ngx_int_t
ngx_mail_pop3_auth(ngx_mail_session_t *s, ngx_connection_t *c)
{
    size_t                     n;
    u_char                    *p;
    ngx_str_t                 *arg, salt;
    ngx_mail_core_srv_conf_t  *cscf;
#if (NGX_MAIL_SSL)
    ngx_mail_ssl_conf_t       *sslcf;

    if (c->ssl == NULL) {
        sslcf = ngx_mail_get_module_srv_conf(s, ngx_mail_ssl_module);

        if (sslcf->starttls == NGX_MAIL_STARTTLS_ONLY) {
            return NGX_MAIL_PARSE_INVALID_COMMAND;
        }
    }

#endif

    cscf = ngx_mail_get_module_srv_conf(s, ngx_mail_core_module);

    if (s->args.nelts == 0) {
        s->out = cscf->pop3_auth_capability;
        s->state = 0;

        return NGX_OK;
    }

    arg = s->args.elts;

    if (arg[0].len == 5) {

        if (ngx_strncasecmp(arg[0].data, (u_char *) "LOGIN", 5) == 0) {

            if (s->args.nelts != 1) {
                return NGX_MAIL_PARSE_INVALID_COMMAND;
            }

            s->out.len = sizeof(pop3_username) - 1;
            s->out.data = pop3_username;
            s->mail_state = ngx_pop3_auth_login_username;

            return NGX_OK;

        } else if (ngx_strncasecmp(arg[0].data, (u_char *) "PLAIN", 5) == 0) {

            if (s->args.nelts == 1) {

                s->out.len = sizeof(pop3_next) - 1;
                s->out.data = pop3_next;
                s->mail_state = ngx_pop3_auth_plain;

                return NGX_OK;
            }

            if (s->args.nelts == 2) {

                /*
                 * workaround for Eudora for Mac: it sends
                 *    AUTH PLAIN [base64 encoded]
                 */

                return ngx_mail_auth_plain(s, c, 1);
            }

            return NGX_MAIL_PARSE_INVALID_COMMAND;
        }

    } else if (arg[0].len == 8
               && ngx_strncasecmp(arg[0].data, (u_char *) "CRAM-MD5", 8) == 0)
    {
        if (s->args.nelts != 1) {
            return NGX_MAIL_PARSE_INVALID_COMMAND;
        }

        if (!(cscf->pop3_auth_methods & NGX_MAIL_AUTH_CRAM_MD5_ENABLED)) {
            return NGX_MAIL_PARSE_INVALID_COMMAND;
        }

        p = ngx_palloc(c->pool,
                       sizeof("+ " CRLF) - 1
                       + ngx_base64_encoded_length(s->salt.len));
        if (p == NULL) {
            return NGX_ERROR;
        }

        p[0] = '+'; p[1]= ' ';
        salt.data = &p[2];
        s->salt.len -= 2;

        ngx_encode_base64(&salt, &s->salt);

        s->salt.len += 2;
        n = 2 + salt.len;
        p[n++] = CR; p[n++] = LF;

        s->out.len = n;
        s->out.data = p;
        s->mail_state = ngx_pop3_auth_cram_md5;

        return NGX_OK;
    }

    return NGX_MAIL_PARSE_INVALID_COMMAND;
}
