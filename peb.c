/*
 	Copyright (c) 2005-2010, York Liu <sadly@phpx.com>.
 	All rights reserved.
 	
 	Redistribution and use in source and binary forms, with or without
 	modification, are permitted provided that the following conditions are
 	met:
 	
 	    * Redistributions of source code must retain the above copyright
 	      notice, this list of conditions and the following disclaimer.
 	    * Redistributions in binary form must reproduce the above
 	      copyright notice, this list of conditions and the following
 	      disclaimer in the documentation and/or other materials provided
 	      with the distribution.
 	    * Neither the name Message Systems, Inc. nor the names
 	      of its contributors may be used to endorse or promote products
 	      derived from this software without specific prior written
 	      permission.
 	
 	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 	"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 	A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 	OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 	SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 	LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 	DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 	THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* $Id: header,v 1.16.2.1.2.1 2007/01/01 19:32:09 iliaa Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_peb.h"


/****************************************
  constant define
****************************************/
#define PEB_VERSION "0.10b"
#define PEB_ERRORNO_INIT 1
#define PEB_ERROR_INIT    "ei_connect_init error"
#define PEB_ERRORNO_CONN 2
#define PEB_ERROR_CONN    "ei_connect error"
#define PEB_ERRORNO_SEND 3
#define PEB_ERROR_SEND    "ei_send error"
#define PEB_ERRORNO_RECV 4
#define PEB_ERROR_RECV    "ei_receive error"
#define PEB_ERRORNO_NOTMINE 5
#define PEB_ERROR_NOTMINE    "ei_receive got a message but not mine"
#define PEB_ERRORNO_DECODE 6
#define PEB_ERROR_DECODE    "ei_decode error, unsupported data type"

#define PEB_RESOURCENAME    "Php-Erlang Bridge"
#define PEB_TERMRESOURCE    "Erlang Term"
#define PEB_SERVERPID    "Erlang Pid"

/****************************************
  macros define
****************************************/


ZEND_DECLARE_MODULE_GLOBALS(peb)

/* True global resources - no need for thread safety here */
static int le_link,le_plink,le_msgbuff,le_serverpid;
static int fd;

typedef struct _peb_link{
ei_cnode * ec;
char * node;
char * secret;
int fd;
int is_persistent;
} peb_link;


/* {{{ peb_functions[]
 *
 * Every user visible function must have an entry in peb_functions[].
 */
zend_function_entry peb_functions[] = {
	PHP_FE(peb_connect,	NULL)
	PHP_FE(peb_pconnect,	NULL)
	PHP_FE(peb_close,	NULL)
	PHP_FE(peb_send_byname,	NULL)
	PHP_FE(peb_send_bypid,	NULL)	
	PHP_FE(peb_receive,	NULL)
	PHP_FE(peb_encode,	NULL)
	PHP_FE(peb_decode,	NULL)
	PHP_FE(peb_error,	NULL)
	PHP_FE(peb_errorno,	NULL)
	PHP_FE(peb_linkinfo,	NULL)
	PHP_FE(peb_status,	NULL)
	{NULL, NULL, NULL}	/* Must be the last line in peb_functions[] */
};
/* }}} */

/* {{{ peb_module_entry
 */
zend_module_entry peb_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"peb",
	peb_functions,
	PHP_MINIT(peb),
	PHP_MSHUTDOWN(peb),
	PHP_RINIT(peb),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(peb),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(peb),
#if ZEND_MODULE_API_NO >= 20010901
	PEB_VERSION, /* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_PEB
ZEND_GET_MODULE(peb)
#endif

/* {{{ PHP_INI
 */

PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("peb.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_peb_globals, peb_globals)
PHP_INI_END()

/* }}} */


/* {{{ PHP_MINIT_FUNCTION
 */
static ZEND_RSRC_DTOR_FUNC(le_msgbuff_dtor)
{
	if(rsrc->ptr) {
		ei_x_buff * tmp = (ei_x_buff *) rsrc->ptr;;
		ei_x_free(tmp);
		efree(tmp);
		rsrc->ptr = NULL;
	}
}

static ZEND_RSRC_DTOR_FUNC(le_serverpid_dtor)
{
	if(rsrc->ptr){
		erlang_pid * tmp= rsrc->ptr;
		efree(tmp);
		rsrc->ptr = NULL;
	}
}

static ZEND_RSRC_DTOR_FUNC(le_link_dtor)
{
    if (rsrc->ptr) {
        peb_link * tmp = (peb_link *) rsrc->ptr;
        int p = tmp->is_persistent;

		/*close fd first?*/
		
        pefree(tmp->ec, p);
        efree(tmp->node);
        efree(tmp->secret);
        pefree(tmp, p);
        if(p)
            PEB_G(num_persistent)--;
        else
            PEB_G(num_link)--;
        rsrc->ptr = NULL;
    }
}

PHP_MINIT_FUNCTION(peb)
{
    PEB_G(default_link) = -1;
    PEB_G(num_link) = 0;
    PEB_G(num_persistent) = 0;
    PEB_G(error) = NULL;
    PEB_G(errorno) = 0;

    PEB_G(instanceid) = 0;
#ifdef ZTS
#else
#endif

    le_link = zend_register_list_destructors_ex(le_link_dtor,NULL,PEB_RESOURCENAME,module_number);
    le_plink = zend_register_list_destructors_ex(NULL,le_link_dtor,PEB_RESOURCENAME,module_number);

    le_msgbuff = zend_register_list_destructors_ex(le_msgbuff_dtor,NULL,PEB_TERMRESOURCE,module_number);
    le_serverpid = zend_register_list_destructors_ex(le_serverpid_dtor,NULL,PEB_SERVERPID,module_number);
        
	REGISTER_INI_ENTRIES();
	return SUCCESS;
}
/* }}} */


/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(peb)
{

#ifdef ZTS
#else
#endif
	UNREGISTER_INI_ENTRIES();

   /* release all link resource here */

	if (PEB_G(error)!=NULL) {
		efree(PEB_G(error));
	}

	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(peb)
{
    PEB_G(default_link) = -1;
    PEB_G(num_link) = PEB_G(num_persistent);
    PEB_G(error) = NULL;
    PEB_G(errorno) = 0;

	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(peb)
{
	if (PEB_G(error)!=NULL) {
		efree(PEB_G(error));
	}

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(peb)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "PEB (Php-Erlang Bridge) support", "enabled");
    php_info_print_table_row(2, "version", PEB_VERSION);
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */


/* Remove the following function when you have succesfully modified config.m4
   so that your module can be compiled into PHP, it exists only for testing
   purposes. */

/* Every user-visible function in PHP should document itself in the source */
/* {{{ proto string confirm_peb_compiled(string arg)
   Return a string to confirm that the module is compiled in */
PHP_FUNCTION(peb_status)
{
    php_printf("\r\n<br>default link: %d",PEB_G(default_link));
    php_printf("\r\n<br>num link: %d",PEB_G(num_link));
    php_printf("\r\n<br>num persistent: %d",PEB_G(num_persistent));
	return;
}

   
PHP_FUNCTION(peb_linkinfo)
{
	peb_link *m;
	zval * tmp;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|r", &tmp) ==FAILURE) {
        RETURN_FALSE;
    }
	if (ZEND_NUM_ARGS()==0) {
    ALLOC_INIT_ZVAL(tmp);
    ZVAL_RESOURCE(tmp,PEB_G(default_link));
    }

    ZEND_FETCH_RESOURCE2(m, peb_link*, &tmp TSRMLS_CC,-1 , PEB_RESOURCENAME ,le_link, le_plink);

	array_init(return_value);
	add_assoc_string(return_value, "thishostname", m->ec->thishostname,1);
	add_assoc_string(return_value, "thisnodename", m->ec->thisnodename,1);	
	add_assoc_string(return_value, "thisalivename", m->ec->thisalivename,1);		
	add_assoc_string(return_value, "connectcookie", m->ec->ei_connect_cookie,1);
	add_assoc_long(return_value, "creation", m->ec->creation);	
	add_assoc_long(return_value, "is_persistent", m->is_persistent);		
	
}


static void php_peb_connect_impl(INTERNAL_FUNCTION_PARAMETERS, int persistent)
{
	char *node=NULL, *secret=NULL;
    char * thisnode = NULL;
    char * key = NULL;
	int  node_len, secret_len, key_len, this_len;

    peb_link * alink = NULL;
    ei_cnode * ec = NULL;
    list_entry * le ;
    list_entry * newle;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"ss",&node,&node_len,&secret,&secret_len)==FAILURE) {
        RETURN_FALSE;
    }

    key_len = spprintf(&key, 0, "peb_%s_%s",node,secret);

    if (persistent) {
        if (SUCCESS==zend_hash_find(&EG(persistent_list), key , key_len+1, (void*)&le)) {
            if (Z_TYPE_P(le)==le_plink) {
                alink = (peb_link *) le->ptr;

                ZEND_REGISTER_RESOURCE(return_value, alink, le_plink);
                PEB_G(default_link) = Z_LVAL_P(return_value TSRMLS_CC);
                /* php_printf("<br>found a exist link(persistent) \r\n"); */
                efree(key);
                return;
            }
            else {
                php_error(E_WARNING, "Hash key confilict! Given name associate with non-peb resource!");
                efree(key);
                RETURN_FALSE;
            }
        }
    }

    ec = pemalloc(sizeof(ei_cnode),persistent);

    int instance = persistent?0:PEB_G(instanceid)++;
    this_len = spprintf(&thisnode, 0, "peb_client_%d_%d", getpid(), instance);

    /* php_printf("node name:%s",thisnode); */

    if (ei_connect_init(ec, thisnode, secret, instance) < 0) {
        PEB_G(errorno) = PEB_ERRORNO_INIT;
		PEB_G(error)=estrdup(PEB_ERROR_INIT);
		efree(key);
		efree(thisnode);
		pefree(ec, persistent);
		RETURN_FALSE
    }

	efree(thisnode);

    if ((fd = ei_connect(ec,node)) < 0) {
        PEB_G(errorno) = PEB_ERRORNO_CONN;
		PEB_G(error)=estrdup(PEB_ERROR_CONN);
		efree(key);
		pefree(ec, persistent);
		RETURN_FALSE
    }

    alink = pemalloc(sizeof(peb_link),persistent);
    alink->ec = ec;
    alink->node = estrndup(node,node_len);
    alink->secret = estrndup(secret,secret_len);
    alink->fd = fd;
    alink->is_persistent = persistent;

    if (persistent) {
        PEB_G(num_link)++;
        PEB_G(num_persistent)++;
        newle = pemalloc(sizeof(list_entry),persistent);
        newle->ptr = alink;
        newle->type = le_plink;
        zend_hash_update(&EG(persistent_list), key, key_len+1, newle,sizeof(list_entry), NULL );
        PEB_G(default_link) = Z_LVAL_P(return_value TSRMLS_CC);
        ZEND_REGISTER_RESOURCE(return_value, alink, le_plink);
    }
    else {
        PEB_G(num_link)++;
        ZEND_REGISTER_RESOURCE(return_value, alink, le_link);
    }

    efree(key);

	/*
	if (PEB_G(default_link) != -1) {
		zend_list_delete(PEB_G(default_link));
	}
	PEB_G(default_link) = fd;
	zend_list_addref(fd);
	*/
}

PHP_FUNCTION(peb_connect)
{
    PEB_G(error) = NULL;
    PEB_G(errorno) = 0;

    php_peb_connect_impl(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}

PHP_FUNCTION(peb_pconnect)
{
    PEB_G(error) = NULL;
    PEB_G(errorno) = 0;
    
    php_peb_connect_impl(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}


PHP_FUNCTION(peb_close)
{
    peb_link *m;
	zval *tmp=NULL;

    PEB_G(error) = NULL;
    PEB_G(errorno) = 0;

    if (ZEND_NUM_ARGS()==0) {
        if(PEB_G(default_link)>0){
            zend_list_delete(PEB_G(default_link));
            PEB_G(default_link) = -1;
        }
        RETURN_TRUE;
    }
    else
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &tmp) ==FAILURE) {
        RETURN_FALSE;
    }

	ZEND_FETCH_RESOURCE2(m, peb_link*, &tmp TSRMLS_CC, -1, PEB_RESOURCENAME, le_link, le_plink);
    zend_list_delete(Z_RESVAL_P(tmp));

    if(Z_RESVAL_P(tmp)==PEB_G(default_link)) PEB_G(default_link) = -1;
	RETURN_TRUE;
}

PHP_FUNCTION(peb_send_byname)
{
    char * model_name;
    int m_len, ret;
   
    peb_link *m;
	zval * tmp=NULL;
	zval * msg=NULL;
	ei_x_buff * newbuff;

    PEB_G(error) = NULL;
    PEB_G(errorno) = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sr|r", &model_name, &m_len, &msg, &tmp) ==FAILURE) {
        RETURN_FALSE;
    }

    if(ZEND_NUM_ARGS()==2){
        ALLOC_INIT_ZVAL(tmp);
        ZVAL_RESOURCE(tmp,PEB_G(default_link));
    }

    ZEND_FETCH_RESOURCE2(m, peb_link*, &tmp TSRMLS_CC,-1 , PEB_RESOURCENAME ,le_link, le_plink);

    ZEND_FETCH_RESOURCE(newbuff, ei_x_buff *, &msg TSRMLS_CC,-1 , PEB_TERMRESOURCE ,le_msgbuff);
	
    ret = ei_reg_send(m->ec, m->fd, model_name, newbuff->buff, newbuff->index);

    if (ret<0){
    	/* process peb_error here */
        PEB_G(errorno) = PEB_ERRORNO_SEND;
		PEB_G(error)=estrdup(PEB_ERROR_SEND);
	    RETURN_FALSE;
    }

	RETURN_TRUE;
}

PHP_FUNCTION(peb_send_bypid)
{
    int ret;
    peb_link *m;
	zval * tmp=NULL;
	zval * msg=NULL;
	zval * pid=NULL;
	erlang_pid * serverpid;
	ei_x_buff * newbuff;

    PEB_G(error) = NULL;
    PEB_G(errorno) = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rr|r",&pid,&msg, &tmp) ==FAILURE) {
        RETURN_FALSE;
    }

    if(ZEND_NUM_ARGS()==1){
        ALLOC_INIT_ZVAL(tmp);
        ZVAL_RESOURCE(tmp,PEB_G(default_link));
    }

    ZEND_FETCH_RESOURCE2(m, peb_link*, &tmp TSRMLS_CC,-1 , PEB_RESOURCENAME ,le_link, le_plink);
    ZEND_FETCH_RESOURCE(newbuff, ei_x_buff *, &msg TSRMLS_CC,-1 , PEB_TERMRESOURCE ,le_msgbuff);
    ZEND_FETCH_RESOURCE(serverpid, erlang_pid *, &pid TSRMLS_CC,-1 , PEB_SERVERPID ,le_serverpid);    

    ret = ei_send( m->fd, serverpid, newbuff->buff, newbuff->index);
    if (ret<0){
    	/* process peb_error here */
        PEB_G(errorno) = PEB_ERRORNO_SEND;
		PEB_G(error)=estrdup(PEB_ERROR_SEND);    	
	    RETURN_FALSE;
    }

	RETURN_TRUE;
}

PHP_FUNCTION(peb_receive)
{
    int ret;
    peb_link * m;
    zval * tmp;
	ei_x_buff * newbuff;
    erlang_msg msg;

    PEB_G(error) = NULL;
    PEB_G(errorno) = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|r", &tmp) ==FAILURE) {
        RETURN_FALSE;
    }

    if(ZEND_NUM_ARGS()==0){
        ALLOC_INIT_ZVAL(tmp);
        ZVAL_RESOURCE(tmp,PEB_G(default_link));
    }

    ZEND_FETCH_RESOURCE2(m, peb_link*, &tmp TSRMLS_CC,-1 , PEB_RESOURCENAME ,le_link, le_plink);
	
	newbuff =emalloc(sizeof(ei_x_buff));
	ei_x_new(newbuff);
	
	while(1){
		ret = ei_xreceive_msg_tmo(m->fd, &msg, newbuff,1000);
		switch(ret){
			case ERL_TICK: /* idle */
    	        break;
    	    case ERL_MSG:
    	        if( msg.msgtype == ERL_SEND ) {
    	            ZEND_REGISTER_RESOURCE(return_value, newbuff, le_msgbuff);
    	            return;
    	        } else {
    	        	/* php_printf("error: not erl_send\r\n"); */
			        PEB_G(errorno) = PEB_ERRORNO_NOTMINE;
					PEB_G(error)=estrdup(PEB_ERROR_NOTMINE);    	        	
    	            ei_x_free(newbuff);
    	            efree(newbuff);
    	            RETURN_FALSE;
    	        }
    	        break;
    	    default:
   	        	/* php_printf("error: unknown ret %d\r\n",ret); */
		        PEB_G(errorno) = PEB_ERRORNO_RECV;
				PEB_G(error)=estrdup(PEB_ERROR_RECV);   	        	
    	        ei_x_free(newbuff);
    	        efree(newbuff);
    	        RETURN_FALSE;
			}
	}
	
    RETURN_TRUE;
}

int _peb_encode_term(ei_x_buff* x,char **fmt,int * fmtpos, HashTable *arr, unsigned long * arridx)
{
    char* p = *fmt + *fmtpos;
    int i,v;
    zval ** pdata;
    ei_x_buff * newbuff, decoded;
	peb_link *m;
	

    ++p;
    (*fmtpos)++;

    while (*p==' ')
    {
        ++p;
        (*fmtpos)++;
    }

    switch(*p)
    {
    case 'a':
        if (zend_hash_index_find(arr,*arridx,(void**) &pdata)==SUCCESS) { 
    	    newbuff = emalloc(sizeof(ei_x_buff));
	        ei_x_new(newbuff);
	        ei_x_encode_atom(newbuff, Z_STRVAL_PP((zval**)pdata));
	        ei_x_append(x,newbuff);
	        ei_x_free(newbuff);
	        efree(newbuff);
        }
        ++(*arridx);
        break;

    case 's':
        if (zend_hash_index_find(arr,*arridx,(void**) &pdata)==SUCCESS) { 
    	    newbuff = emalloc(sizeof(ei_x_buff));
	        ei_x_new(newbuff);
	        ei_x_encode_string_len(newbuff, Z_STRVAL_PP((zval**)pdata),Z_STRLEN_PP((zval**)pdata));
	        ei_x_append(x,newbuff);
	        ei_x_free(newbuff);
	        efree(newbuff);
        }
        ++(*arridx);
        break;

    case 'b':
        if (zend_hash_index_find(arr,*arridx,(void**) &pdata)==SUCCESS) { 
    	    newbuff = emalloc(sizeof(ei_x_buff));
	        ei_x_new(newbuff);
	        ei_x_encode_binary(newbuff, Z_STRVAL_PP((zval**)pdata),Z_STRLEN_PP((zval**)pdata));
	        ei_x_append(x,newbuff);
	        ei_x_free(newbuff);
	        efree(newbuff);
        }
        ++(*arridx);
        break;
        
    case 'i':
    case 'l':    
    case 'u':
        if (zend_hash_index_find(arr,*arridx,(void**) &pdata)==SUCCESS) { 
    	    newbuff = emalloc(sizeof(ei_x_buff));
	        ei_x_new(newbuff);
	        ei_x_encode_long(newbuff, Z_LVAL_PP((zval**)pdata));
	        ei_x_append(x,newbuff);
	        ei_x_free(newbuff);
	        efree(newbuff);
        }
        ++(*arridx);
        break;
    
    case 'f':
    case 'd':
        if (zend_hash_index_find(arr,*arridx,(void**) &pdata)==SUCCESS) { 
    	    newbuff = emalloc(sizeof(ei_x_buff));
	        ei_x_new(newbuff);
	        ei_x_encode_double(newbuff, Z_DVAL_PP((zval**)pdata));
	        ei_x_append(x,newbuff);
	        ei_x_free(newbuff);
	        efree(newbuff);
        }
        ++(*arridx);
        break;

	case 'p':
        if (zend_hash_index_find(arr,*arridx,(void**) &pdata)==SUCCESS) { 
			m = (peb_link*) zend_fetch_resource(pdata TSRMLS_CC,-1 , PEB_RESOURCENAME , NULL, 2, le_link, le_plink);
	        if (m) { 
	    	    newbuff = emalloc(sizeof(ei_x_buff));
		        ei_x_new(newbuff);
		        ei_x_encode_pid(newbuff, &(m->ec->self));
		        ei_x_append(x,newbuff);
		        ei_x_free(newbuff);
		        efree(newbuff);
	        }
	    }
        ++(*arridx);
		break;

    case ',':
    case '~':
        break;
    default:
        return;
        break;
    }

    _peb_encode_term(x,fmt,fmtpos,arr,arridx);

}

int _peb_encode(ei_x_buff* x, char** fmt, int * fmtpos, HashTable *arr, unsigned long * arridx)
{
    /*
    ~a - an atom, char*
    ~s - a string, char*
    ~b - a binary, char*    
    ~i - an integer, int
    ~l - a long integer, long int
    ~u - an unsigned long integer, unsigned long int
    ~f - a float, float
    ~d - a double float, double float
    ~p - an erlang pid
    */

    char* p = *fmt + *fmtpos;
    int res;
    unsigned long  newidx=0;
    
    HashTable * newarr;
    zval * tmp;
	ei_x_buff * newbuff;

/*     php_printf("<br>enter for fmtpos %d fmtstr: %c </br>\r\n\r\n", *fmtpos, *p ); */
	        
    while (*p==' ')
    {
        ++p;
        (*fmtpos)++;
    }

    switch (*p) {
    case '~':
            _peb_encode_term(x,fmt,fmtpos,arr, arridx);
        break;

    case '[':
        if (zend_hash_index_find(arr,*arridx,(void**)&tmp)==SUCCESS) {
	        newarr= Z_ARRVAL_PP((zval**)tmp);
    	    ++p;
    	    (*fmtpos)++;
	        newbuff = emalloc(sizeof(ei_x_buff));
    	    ei_x_new(newbuff);

    	    _peb_encode(newbuff, fmt, fmtpos, newarr,&newidx);
       
    	    if(newidx!=0) {
/*		        php_printf("newidx:%d",newidx); */
		        ei_x_encode_list_header(x,newidx);
		        ei_x_append(x,newbuff);
		        ei_x_encode_empty_list(x);
    		    ei_x_free(newbuff);        
		        efree(newbuff);
    	    }
	        else {
    		    ei_x_free(newbuff);        
		        efree(newbuff);        
	        }
        }
        (*arridx)++;
        break;

    case ']':
        ++p;
        (*fmtpos)++;
        return;
        break;

    case '{':
        if (zend_hash_index_find(arr,*arridx,(void**)&tmp)==SUCCESS) {
    	    newarr= Z_ARRVAL_PP((zval**)tmp);
    	    ++p;
    	    (*fmtpos)++;
    	    newbuff = emalloc(sizeof(ei_x_buff));
    	    ei_x_new(newbuff);

    	    _peb_encode(newbuff, fmt, fmtpos, newarr,&newidx);
       
	        if(newidx!=0) {
/*		        php_printf("newidx:%d",newidx); */
		        ei_x_encode_tuple_header(x,newidx);
		        ei_x_append(x,newbuff);
    		    ei_x_free(newbuff);        
		        efree(newbuff);
	        }
    	    else {
	    	    ei_x_free(newbuff);        
		        efree(newbuff);        
	        }
        }
        (*arridx)++;
        break;

    case '}':
        ++p;
        (*fmtpos)++;
        return;        
        break;

    case ',':
        ++p;
        (*fmtpos)++;
        break;

    default:
        return;
        break;
    }

    _peb_encode(x,fmt,fmtpos,arr,arridx);
}

PHP_FUNCTION(peb_encode)
{
    char * fmt;
    int fmt_len;
    int fmtpos=0;
    int res;
    unsigned long arridx=0;

    zval * tmp;
    ei_x_buff * x;
    HashTable * htable;

    PEB_G(error) = NULL;
    PEB_G(errorno) = 0;

    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sa", &fmt, &fmt_len, &tmp)==FAILURE) {
        RETURN_FALSE;
    }

    htable = Z_ARRVAL_P(tmp); /* find hashtable for array */

    x=emalloc(sizeof(ei_x_buff));
    ei_x_new_with_version(x);
    _peb_encode(x, &fmt, &fmtpos, htable,&arridx);
	ZEND_REGISTER_RESOURCE(return_value,x,le_msgbuff);
}

int _peb_decode( ei_x_buff * x ,zval * htable) {
    zval * z;
    int type;
    int size;
    char * buff;
    long len;
    long long_value;
    double double_value;
    int i;
    
    ALLOC_INIT_ZVAL(z);
    ei_get_type( x->buff, & x->index, & type, & size );

    switch( type ) {
    case ERL_ATOM_EXT:
        buff = emalloc( size + 1 );
        ei_decode_atom( x->buff, & x->index, buff );
        buff[ size ] = '\0';
        ZVAL_STRING(z, buff, 0);
        add_next_index_zval( htable, z);
        break;

    case ERL_STRING_EXT:
        buff = emalloc( size + 1 );
        ei_decode_string( x->buff, & x->index, buff );
        buff[ size ] = '\0';
        ZVAL_STRING(z, buff, 0);
        add_next_index_zval( htable, z);                
        break;

    case ERL_BINARY_EXT:
        buff = emalloc( size );
        ei_decode_binary( x->buff, & x->index, buff, & len );
        ZVAL_STRINGL(z, buff, size, 0);
        add_next_index_zval( htable, z);                
        break;

    case ERL_PID_EXT:
        buff = emalloc( sizeof( erlang_pid ) );
        ei_decode_pid( x->buff, & x->index, (erlang_pid *) buff );
        ZEND_REGISTER_RESOURCE(z, buff, le_serverpid);
        add_next_index_zval( htable, z);                        
        break;

    case ERL_SMALL_INTEGER_EXT:    
    case ERL_INTEGER_EXT:
        ei_decode_long( x->buff, & x->index, & long_value );
        ZVAL_LONG(z, long_value);
        add_next_index_zval( htable, z);                
        break;

    case ERL_FLOAT_EXT:
        ei_decode_double( x->buff, & x->index, & double_value );
        ZVAL_DOUBLE(z, double_value);
        add_next_index_zval( htable, z);                
        break;

    case ERL_SMALL_TUPLE_EXT:
    case ERL_LARGE_TUPLE_EXT:
        array_init( z );
        ei_decode_tuple_header( x->buff, & x->index, & size );
        for( i = 1; i <= size; i++ ) {
            if(_peb_decode(x,z)!=SUCCESS) { 
            return FAILURE; 
            }
        }
        add_next_index_zval( htable, z );
        break;

    case ERL_NIL_EXT:
    case ERL_LIST_EXT:
        array_init( z );
        ei_decode_list_header( x->buff, & x->index, & size );
        while( size > 0 ) {
            for( i = 1; i <= size; i++ ) {
	            if(_peb_decode(x,z)!=SUCCESS) { 
	            return FAILURE; 
	            }
            }
            ei_decode_list_header( x->buff, & x->index, & size );
        }
        add_next_index_zval( htable, z );        
        break;

    default:
        php_error( E_ERROR, "unsupported data type %d", type );
        PEB_G(errorno) = PEB_ERRORNO_DECODE;
		PEB_G(error)=estrdup(PEB_ERROR_DECODE);        
        return FAILURE;
    }

    return SUCCESS;
}

PHP_FUNCTION(peb_decode)
{
    int v, ret;
    char atom[ MAXATOMLEN ];
    zval * tmp, * htable;
    ei_x_buff * x;

    PEB_G(error) = NULL;
    PEB_G(errorno) = 0;

    if( zend_parse_parameters( ZEND_NUM_ARGS() TSRMLS_CC, "r", & tmp ) == FAILURE ) {
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(x, ei_x_buff *, & tmp, -1, PEB_TERMRESOURCE, le_msgbuff);

    x->index = 0;
    ei_decode_version( x->buff, & x->index, & v );

    ALLOC_INIT_ZVAL(htable);
    array_init(htable);
    ret = _peb_decode( x ,htable);
    
    if( ret==SUCCESS) {
        * return_value = * htable;    
    } else {
        RETURN_FALSE;
    }
}

PHP_FUNCTION(peb_error)
{
    if (PEB_G(error)!=NULL) {
        RETURN_STRING(PEB_G(error),1);
    }
}

PHP_FUNCTION(peb_errorno)
{
        RETURN_LONG(PEB_G(errorno));
}

/* }}} */
/* The previous line is meant for vim and emacs, so it can correctly fold and
   unfold functions in source code. See the corresponding marks just before
   function definition, where the functions purpose is also documented. Please
   follow this convention for the convenience of others editing your code.
*/


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
