/* $OpenLDAP$ */
/*
 * Copyright 1998-2000 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/*  Portions
 *  Copyright (c) 1996 Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  LIBLDAP url.c -- LDAP URL (RFC 2255) related routines
 *
 *  LDAP URLs look like this:
 *    ldap[is]://host:port[/[dn[?[attributes][?[scope][?[filter][?exts]]]]]]
 *
 *  where:
 *   attributes is a comma separated list
 *   scope is one of these three strings:  base one sub (default=base)
 *   filter is an string-represented filter as in RFC 2254
 *
 *  e.g.,  ldap://host:port/dc=com?o,cn?base?o=openldap?extension
 *
 *  We also tolerate URLs that look like: <ldapurl> and <URL:ldapurl>
 */

#include "portable.h"

#include <stdio.h>

#include <ac/stdlib.h>

#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include "ldap-int.h"


/* local functions */
static const char* skip_url_prefix LDAP_P((
	const char *url,
	int *enclosedp,
	const char **scheme ));

int ldap_pvt_url_scheme2proto( const char *scheme )
{
	assert( scheme );

	if( scheme == NULL ) {
		return -1;
	}

	if( strcmp("ldap", scheme) == 0 ) {
		return LDAP_PROTO_TCP;
	}

	if( strcmp("ldapi", scheme) == 0 ) {
		return LDAP_PROTO_IPC;
	}

	if( strcmp("ldaps", scheme) == 0 ) {
		return LDAP_PROTO_TCP;
	}

	return -1;
}

int ldap_pvt_url_scheme2tls( const char *scheme )
{
	assert( scheme );

	if( scheme == NULL ) {
		return -1;
	}

	return strcmp("ldaps", scheme) == 0;
}

int
ldap_is_ldap_url( LDAP_CONST char *url )
{
	int	enclosed;
	const char * scheme;

	if( url == NULL ) {
		return 0;
	}

	if( skip_url_prefix( url, &enclosed, &scheme ) == NULL ) {
		return 0;
	}

	return 1;
}

int
ldap_is_ldaps_url( LDAP_CONST char *url )
{
	int	enclosed;
	const char * scheme;

	if( url == NULL ) {
		return 0;
	}

	if( skip_url_prefix( url, &enclosed, &scheme ) == NULL ) {
		return 0;
	}

	return strcmp(scheme, "ldaps") == 0;
}

int
ldap_is_ldapi_url( LDAP_CONST char *url )
{
	int	enclosed;
	const char * scheme;

	if( url == NULL ) {
		return 0;
	}

	if( skip_url_prefix( url, &enclosed, &scheme ) == NULL ) {
		return 0;
	}

	return strcmp(scheme, "ldapi") == 0;
}

static const char*
skip_url_prefix(
	const char *url,
	int *enclosedp,
	const char **scheme )
{
/*
 * return non-zero if this looks like a LDAP URL; zero if not
 * if non-zero returned, *urlp will be moved past "ldap://" part of URL
 */
	const char *p;

	if ( url == NULL ) {
		return( NULL );
	}

	p = url;

	/* skip leading '<' (if any) */
	if ( *p == '<' ) {
		*enclosedp = 1;
		++p;
	} else {
		*enclosedp = 0;
	}

	/* skip leading "URL:" (if any) */
	if ( strncasecmp( p, LDAP_URL_URLCOLON, LDAP_URL_URLCOLON_LEN ) == 0 )
	{
		p += LDAP_URL_URLCOLON_LEN;
	}

	/* check for "ldap://" prefix */
	if ( strncasecmp( p, LDAP_URL_PREFIX, LDAP_URL_PREFIX_LEN ) == 0 ) {
		/* skip over "ldap://" prefix and return success */
		p += LDAP_URL_PREFIX_LEN;
		*scheme = "ldap";
		return( p );
	}

	/* check for "ldaps://" prefix */
	if ( strncasecmp( p, LDAPS_URL_PREFIX, LDAPS_URL_PREFIX_LEN ) == 0 ) {
		/* skip over "ldaps://" prefix and return success */
		p += LDAPS_URL_PREFIX_LEN;
		*scheme = "ldaps";
		return( p );
	}

	/* check for "ldapi://" prefix */
	if ( strncasecmp( p, LDAPI_URL_PREFIX, LDAPI_URL_PREFIX_LEN ) == 0 ) {
		/* skip over "ldapi://" prefix and return success */
		p += LDAPI_URL_PREFIX_LEN;
		*scheme = "ldapi";
		return( p );
	}

	return( NULL );
}


static int str2scope( const char *p )
{
	if ( strcasecmp( p, "one" ) == 0 ) {
		return LDAP_SCOPE_ONELEVEL;

	} else if ( strcasecmp( p, "onetree" ) == 0 ) {
		return LDAP_SCOPE_ONELEVEL;

	} else if ( strcasecmp( p, "base" ) == 0 ) {
		return LDAP_SCOPE_BASE;

	} else if ( strcasecmp( p, "sub" ) == 0 ) {
		return LDAP_SCOPE_SUBTREE;

	} else if ( strcasecmp( p, "subtree" ) == 0 ) {
		return LDAP_SCOPE_SUBTREE;
	}

	return( -1 );
}


int
ldap_url_parse( LDAP_CONST char *url_in, LDAPURLDesc **ludpp )
{
/*
 *  Pick apart the pieces of an LDAP URL.
 */

	LDAPURLDesc	*ludp;
	char	*p, *q, *r;
	int		i, enclosed;
	const char *scheme = NULL;
	const char *url_tmp;
	char *url;

	if( url_in == NULL && ludpp == NULL ) {
		return LDAP_URL_ERR_PARAM;
	}

#ifndef LDAP_INT_IN_KERNEL
	/* Global options may not be created yet
	 * We can't test if the global options are initialized
	 * because a call to LDAP_INT_GLOBAL_OPT() will try to allocate
	 * the options and cause infinite recursion
	 */
	Debug( LDAP_DEBUG_TRACE, "ldap_url_parse(%s)\n", url_in, 0, 0 );
#endif

	*ludpp = NULL;	/* pessimistic */

	url_tmp = skip_url_prefix( url_in, &enclosed, &scheme );

	if ( url_tmp == NULL ) {
		return LDAP_URL_ERR_BADSCHEME;
	}

	assert( scheme );

	/* make working copy of the remainder of the URL */
	url = LDAP_STRDUP( url_tmp );
	if ( url == NULL ) {
		return LDAP_URL_ERR_MEM;
	}

	if ( enclosed ) {
		p = &url[strlen(url)-1];

		if( *p != '>' ) {
			LDAP_FREE( url );
			return LDAP_URL_ERR_BADENCLOSURE;
		}

		*p = '\0';
	}

	/* allocate return struct */
	ludp = (LDAPURLDesc *)LDAP_CALLOC( 1, sizeof( LDAPURLDesc ));

	if ( ludp == NULL ) {
		LDAP_FREE( url );
		return LDAP_URL_ERR_MEM;
	}

	ludp->lud_next = NULL;
	ludp->lud_host = NULL;
	ludp->lud_port = LDAP_PORT;
	ludp->lud_dn = NULL;
	ludp->lud_attrs = NULL;
	ludp->lud_filter = NULL;
	ludp->lud_scope = LDAP_SCOPE_BASE;
	ludp->lud_filter = NULL;

	ludp->lud_scheme = LDAP_STRDUP( scheme );

	if ( ludp->lud_scheme == NULL ) {
		LDAP_FREE( url );
		ldap_free_urldesc( ludp );
		return LDAP_URL_ERR_MEM;
	}

	if( strcasecmp( ludp->lud_scheme, "ldaps" ) == 0 ) {
		ludp->lud_port = LDAPS_PORT;
	}

	/* scan forward for '/' that marks end of hostport and begin. of dn */
	p = strchr( url, '/' );

	if( p != NULL ) {
		/* terminate hostport; point to start of dn */
		*p++ = '\0';
	}

	/* IPv6 syntax with [ip address]:port */
	if ( *url == '[' ) {
		r = strchr( url, ']' );
		if ( r == NULL ) {
			LDAP_FREE( url );
			ldap_free_urldesc( ludp );
			return LDAP_URL_ERR_BADURL;
		}
		*r++ = '\0';
		q = strchr( r, ':' );
	} else {
		q = strchr( url, ':' );
	}

	if ( q != NULL ) {
		*q++ = '\0';
		ldap_pvt_hex_unescape( q );

		if( *q == '\0' ) {
			LDAP_FREE( url );
			ldap_free_urldesc( ludp );
			return LDAP_URL_ERR_BADURL;
		}

		ludp->lud_port = atoi( q );
	}

	ldap_pvt_hex_unescape( url );

	/* If [ip address]:port syntax, url is [ip and we skip the [ */
	ludp->lud_host = LDAP_STRDUP( url + ( *url == '[' ) );

	if( ludp->lud_host == NULL ) {
		LDAP_FREE( url );
		ldap_free_urldesc( ludp );
		return LDAP_URL_ERR_MEM;
	}

	/*
	 * Kludge.  ldap://111.222.333.444:389??cn=abc,o=company
	 *
	 * On early Novell releases, search references/referrals were returned
	 * in this format, i.e., the dn was kind of in the scope position,
	 * but the required slash is missing. The whole thing is illegal syntax,
	 * but we need to account for it. Fortunately it can't be confused with
	 * anything real.
	 */
	if( (p == NULL) && (q != NULL) && ((q = strchr( q, '?')) != NULL)) {
		q++;		
		/* ? immediately followed by question */
		if( *q == '?') {
			q++;
			if( *q != '\0' ) {
				/* parse dn part */
				ldap_pvt_hex_unescape( q );
				ludp->lud_dn = LDAP_STRDUP( q );
			} else {
				ludp->lud_dn = LDAP_STRDUP( "" );
			}

			if( ludp->lud_dn == NULL ) {
				LDAP_FREE( url );
				ldap_free_urldesc( ludp );
				return LDAP_URL_ERR_MEM;
			}
		}
	}

	if( p == NULL ) {
		LDAP_FREE( url );
		*ludpp = ludp;
		return LDAP_URL_SUCCESS;
	}

	/* scan forward for '?' that may marks end of dn */
	q = strchr( p, '?' );

	if( q != NULL ) {
		/* terminate dn part */
		*q++ = '\0';
	}

	if( *p != '\0' ) {
		/* parse dn part */
		ldap_pvt_hex_unescape( p );
		ludp->lud_dn = LDAP_STRDUP( p );
	} else {
		ludp->lud_dn = LDAP_STRDUP( "" );
	}

	if( ludp->lud_dn == NULL ) {
		LDAP_FREE( url );
		ldap_free_urldesc( ludp );
		return LDAP_URL_ERR_MEM;
	}

	if( q == NULL ) {
		/* no more */
		LDAP_FREE( url );
		*ludpp = ludp;
		return LDAP_URL_SUCCESS;
	}

	/* scan forward for '?' that may marks end of attributes */
	p = q;
	q = strchr( p, '?' );

	if( q != NULL ) {
		/* terminate attributes part */
		*q++ = '\0';
	}

	if( *p != '\0' ) {
		/* parse attributes */
		ldap_pvt_hex_unescape( p );
		ludp->lud_attrs = ldap_str2charray( p, "," );

		if( ludp->lud_attrs == NULL ) {
			LDAP_FREE( url );
			ldap_free_urldesc( ludp );
			return LDAP_URL_ERR_BADATTRS;
		}
	}

	if ( q == NULL ) {
		/* no more */
		LDAP_FREE( url );
		*ludpp = ludp;
		return LDAP_URL_SUCCESS;
	}

	/* scan forward for '?' that may marks end of scope */
	p = q;
	q = strchr( p, '?' );

	if( q != NULL ) {
		/* terminate the scope part */
		*q++ = '\0';
	}

	if( *p != '\0' ) {
		/* parse the scope */
		ldap_pvt_hex_unescape( p );
		ludp->lud_scope = str2scope( p );

		if( ludp->lud_scope == -1 ) {
			LDAP_FREE( url );
			ldap_free_urldesc( ludp );
			return LDAP_URL_ERR_BADSCOPE;
		}
	}

	if ( q == NULL ) {
		/* no more */
		LDAP_FREE( url );
		*ludpp = ludp;
		return LDAP_URL_SUCCESS;
	}

	/* scan forward for '?' that may marks end of filter */
	p = q;
	q = strchr( p, '?' );

	if( q != NULL ) {
		/* terminate the filter part */
		*q++ = '\0';
	}

	if( *p != '\0' ) {
		/* parse the filter */
		ldap_pvt_hex_unescape( p );

		if( ! *p ) {
			/* missing filter */
			LDAP_FREE( url );
			ldap_free_urldesc( ludp );
			return LDAP_URL_ERR_BADFILTER;
		}

		LDAP_FREE( ludp->lud_filter );
		ludp->lud_filter = LDAP_STRDUP( p );

		if( ludp->lud_filter == NULL ) {
			LDAP_FREE( url );
			ldap_free_urldesc( ludp );
			return LDAP_URL_ERR_MEM;
		}
	}

	if ( q == NULL ) {
		/* no more */
		LDAP_FREE( url );
		*ludpp = ludp;
		return LDAP_URL_SUCCESS;
	}

	/* scan forward for '?' that may marks end of extensions */
	p = q;
	q = strchr( p, '?' );

	if( q != NULL ) {
		/* extra '?' */
		LDAP_FREE( url );
		ldap_free_urldesc( ludp );
		return LDAP_URL_ERR_BADURL;
	}

	/* parse the extensions */
	ludp->lud_exts = ldap_str2charray( p, "," );

	if( ludp->lud_exts == NULL ) {
		LDAP_FREE( url );
		ldap_free_urldesc( ludp );
		return LDAP_URL_ERR_BADEXTS;
	}

	for( i=0; ludp->lud_exts[i] != NULL; i++ ) {
		ldap_pvt_hex_unescape( ludp->lud_exts[i] );
	}

	if( i == 0 ) {
		/* must have 1 or more */
		ldap_charray_free( ludp->lud_exts );
		LDAP_FREE( url );
		ldap_free_urldesc( ludp );
		return LDAP_URL_ERR_BADEXTS;
	}

	/* no more */
	*ludpp = ludp;
	LDAP_FREE( url );
	return LDAP_URL_SUCCESS;
}

LDAPURLDesc *
ldap_url_dup ( LDAPURLDesc *ludp )
{
	LDAPURLDesc *dest;

	if ( ludp == NULL ) {
		return NULL;
	}

	dest = LDAP_MALLOC( sizeof(LDAPURLDesc) );
	if (dest == NULL)
		return NULL;
	
	*dest = *ludp;
	dest->lud_scheme = NULL;
	dest->lud_host = NULL;
	dest->lud_dn = NULL;
	dest->lud_filter = NULL;
	dest->lud_attrs = NULL;
	dest->lud_exts = NULL;
	dest->lud_next = NULL;

	if ( ludp->lud_scheme != NULL ) {
		dest->lud_scheme = LDAP_STRDUP( ludp->lud_scheme );
		if (dest->lud_scheme == NULL) {
			ldap_free_urldesc(dest);
			return NULL;
		}
	}

	if ( ludp->lud_host != NULL ) {
		dest->lud_host = LDAP_STRDUP( ludp->lud_host );
		if (dest->lud_host == NULL) {
			ldap_free_urldesc(dest);
			return NULL;
		}
	}

	if ( ludp->lud_dn != NULL ) {
		dest->lud_dn = LDAP_STRDUP( ludp->lud_dn );
		if (dest->lud_dn == NULL) {
			ldap_free_urldesc(dest);
			return NULL;
		}
	}

	if ( ludp->lud_filter != NULL ) {
		dest->lud_filter = LDAP_STRDUP( ludp->lud_filter );
		if (dest->lud_filter == NULL) {
			ldap_free_urldesc(dest);
			return NULL;
		}
	}

	if ( ludp->lud_attrs != NULL ) {
		dest->lud_attrs = ldap_charray_dup( ludp->lud_attrs );
		if (dest->lud_attrs == NULL) {
			ldap_free_urldesc(dest);
			return NULL;
		}
	}

	if ( ludp->lud_exts != NULL ) {
		dest->lud_exts = ldap_charray_dup( ludp->lud_exts );
		if (dest->lud_exts == NULL) {
			ldap_free_urldesc(dest);
			return NULL;
		}
	}

	return dest;
}

LDAPURLDesc *
ldap_url_duplist (LDAPURLDesc *ludlist)
{
	LDAPURLDesc *dest, *tail, *ludp, *newludp;

	dest = NULL;
	tail = NULL;
	for (ludp = ludlist; ludp != NULL; ludp = ludp->lud_next) {
		newludp = ldap_url_dup(ludp);
		if (newludp == NULL) {
			ldap_free_urllist(dest);
			return NULL;
		}
		if (tail == NULL)
			dest = newludp;
		else
			tail->lud_next = newludp;
		tail = newludp;
	}
	return dest;
}

int
ldap_url_parselist (LDAPURLDesc **ludlist, const char *url )
{
	int i, rc;
	LDAPURLDesc *ludp;
	char **urls;

	*ludlist = NULL;

	if (url == NULL)
		return LDAP_PARAM_ERROR;

	urls = ldap_str2charray((char *)url, ", ");
	if (urls == NULL)
		return LDAP_NO_MEMORY;

	/* count the URLs... */
	for (i = 0; urls[i] != NULL; i++) ;
	/* ...and put them in the "stack" backward */
	while (--i >= 0) {
		rc = ldap_url_parse( urls[i], &ludp );
		if ( rc != 0 ) {
			ldap_charray_free(urls);
			ldap_free_urllist(*ludlist);
			*ludlist = NULL;
			return rc;
		}
		ludp->lud_next = *ludlist;
		*ludlist = ludp;
	}
	ldap_charray_free(urls);
	return LDAP_SUCCESS;
}

int
ldap_url_parsehosts(
	LDAPURLDesc **ludlist,
	const char *hosts,
	int port )
{
	int i;
	LDAPURLDesc *ludp;
	char **specs, *p;

	*ludlist = NULL;

	if (hosts == NULL)
		return LDAP_PARAM_ERROR;

	specs = ldap_str2charray((char *)hosts, ", ");
	if (specs == NULL)
		return LDAP_NO_MEMORY;

	/* count the URLs... */
	for (i = 0; specs[i] != NULL; i++) /* EMPTY */;

	/* ...and put them in the "stack" backward */
	while (--i >= 0) {
		ludp = LDAP_CALLOC( 1, sizeof(LDAPURLDesc) );
		if (ludp == NULL) {
			ldap_charray_free(specs);
			ldap_free_urllist(*ludlist);
			*ludlist = NULL;
			return LDAP_NO_MEMORY;
		}
		ludp->lud_port = port;
		ludp->lud_host = specs[i];
		specs[i] = NULL;
		p = strchr(ludp->lud_host, ':');
		if (p != NULL) {
			/* more than one :, IPv6 address */
			if ( strchr(p+1, ':') != NULL ) {
				/* allow [address] and [address]:port */
				if ( *ludp->lud_host == '[' ) {
					p = LDAP_STRDUP(ludp->lud_host+1);
					/* copied, make sure we free source later */
					specs[i] = ludp->lud_host;
					ludp->lud_host = p;
					p = strchr( ludp->lud_host, ']' );
					if ( p == NULL )
						return LDAP_PARAM_ERROR;
					*p++ = '\0';
					if ( *p != ':' ) {
						if ( *p != '\0' )
							return LDAP_PARAM_ERROR;
						p = NULL;
					}
				} else {
					p = NULL;
				}
			}
			if (p != NULL) {
				*p++ = 0;
				ldap_pvt_hex_unescape(p);
				ludp->lud_port = atoi(p);
			}
		}
		ldap_pvt_hex_unescape(ludp->lud_host);
		ludp->lud_scheme = LDAP_STRDUP("ldap");
		ludp->lud_next = *ludlist;
		*ludlist = ludp;
	}

	/* this should be an array of NULLs now */
	/* except entries starting with [ */
	ldap_charray_free(specs);
	return LDAP_SUCCESS;
}

char *
ldap_url_list2hosts (LDAPURLDesc *ludlist)
{
	LDAPURLDesc *ludp;
	int size;
	char *s, *p, buf[32];	/* big enough to hold a long decimal # (overkill) */

	if (ludlist == NULL)
		return NULL;

	/* figure out how big the string is */
	size = 1;	/* nul-term */
	for (ludp = ludlist; ludp != NULL; ludp = ludp->lud_next) {
		size += strlen(ludp->lud_host) + 1;		/* host and space */
		if (strchr(ludp->lud_host, ':'))        /* will add [ ] below */
			size += 2;
		if (ludp->lud_port != 0)
			size += sprintf(buf, ":%d", ludp->lud_port);
	}
	s = LDAP_MALLOC(size);
	if (s == NULL)
		return NULL;

	p = s;
	for (ludp = ludlist; ludp != NULL; ludp = ludp->lud_next) {
		if (strchr(ludp->lud_host, ':')) {
			p += sprintf(p, "[%s]", ludp->lud_host);
		} else {
			strcpy(p, ludp->lud_host);
			p += strlen(ludp->lud_host);
		}
		if (ludp->lud_port != 0)
			p += sprintf(p, ":%d", ludp->lud_port);
		*p++ = ' ';
	}
	if (p != s)
		p--;	/* nuke that extra space */
	*p = 0;
	return s;
}

char *
ldap_url_list2urls(
	LDAPURLDesc *ludlist )
{
	LDAPURLDesc *ludp;
	int size;
	char *s, *p, buf[32];	/* big enough to hold a long decimal # (overkill) */

	if (ludlist == NULL)
		return NULL;

	/* figure out how big the string is */
	size = 1;	/* nul-term */
	for (ludp = ludlist; ludp != NULL; ludp = ludp->lud_next) {
		size += strlen(ludp->lud_scheme) + strlen(ludp->lud_host);
		if (strchr(ludp->lud_host, ':'))        /* will add [ ] below */
			size += 2;
		size += sizeof(":/// ");

		if (ludp->lud_port != 0) {
			size += sprintf(buf, ":%d", ludp->lud_port);
		}
	}

	s = LDAP_MALLOC(size);
	if (s == NULL) {
		return NULL;
	}

	p = s;
	for (ludp = ludlist; ludp != NULL; ludp = ludp->lud_next) {
		p += sprintf(p,
			     strchr(ludp->lud_host, ':') ? "%s://[%s]" : "%s://%s",
			     ludp->lud_scheme, ludp->lud_host);
		if (ludp->lud_port != 0)
			p += sprintf(p, ":%d", ludp->lud_port);
		*p++ = '/';
		*p++ = ' ';
	}
	if (p != s)
		p--;	/* nuke that extra space */
	*p = 0;
	return s;
}

void
ldap_free_urllist( LDAPURLDesc *ludlist )
{
	LDAPURLDesc *ludp, *next;

	for (ludp = ludlist; ludp != NULL; ludp = next) {
		next = ludp->lud_next;
		ldap_free_urldesc(ludp);
	}
}

void
ldap_free_urldesc( LDAPURLDesc *ludp )
{
	if ( ludp == NULL ) {
		return;
	}
	
	if ( ludp->lud_scheme != NULL ) {
		LDAP_FREE( ludp->lud_scheme );
	}

	if ( ludp->lud_host != NULL ) {
		LDAP_FREE( ludp->lud_host );
	}

	if ( ludp->lud_dn != NULL ) {
		LDAP_FREE( ludp->lud_dn );
	}

	if ( ludp->lud_filter != NULL ) {
		LDAP_FREE( ludp->lud_filter);
	}

	if ( ludp->lud_attrs != NULL ) {
		LDAP_VFREE( ludp->lud_attrs );
	}

	if ( ludp->lud_exts != NULL ) {
		LDAP_VFREE( ludp->lud_exts );
	}

	LDAP_FREE( ludp );
}



int
ldap_url_search( LDAP *ld, LDAP_CONST char *url, int attrsonly )
{
	int		err;
	LDAPURLDesc	*ludp;
	BerElement	*ber;
	LDAPreqinfo  bind;

	if ( ldap_url_parse( url, &ludp ) != 0 ) {
		ld->ld_errno = LDAP_PARAM_ERROR;
		return( -1 );
	}

	ber = ldap_build_search_req( ld, ludp->lud_dn, ludp->lud_scope,
	    ludp->lud_filter, ludp->lud_attrs, attrsonly, NULL, NULL,
		-1, -1 );

	if ( ber == NULL ) {
		err = -1;
	} else {
		bind.ri_request = LDAP_REQ_SEARCH;
		bind.ri_msgid = ld->ld_msgid;
		bind.ri_url = (char *)url;
		err = ldap_send_server_request(
					ld, ber, ld->ld_msgid, NULL,
					(ludp->lud_host != NULL || ludp->lud_port != 0)
						? ludp : NULL,
					NULL, &bind );
	}

	ldap_free_urldesc( ludp );
	return( err );
}


int
ldap_url_search_st( LDAP *ld, LDAP_CONST char *url, int attrsonly,
	struct timeval *timeout, LDAPMessage **res )
{
	int	msgid;

	if (( msgid = ldap_url_search( ld, url, attrsonly )) == -1 ) {
		return( ld->ld_errno );
	}

	if ( ldap_result( ld, msgid, 1, timeout, res ) == -1 ) {
		return( ld->ld_errno );
	}

	if ( ld->ld_errno == LDAP_TIMEOUT ) {
		(void) ldap_abandon( ld, msgid );
		ld->ld_errno = LDAP_TIMEOUT;
		return( ld->ld_errno );
	}

	return( ldap_result2error( ld, *res, 0 ));
}


int
ldap_url_search_s(
	LDAP *ld, LDAP_CONST char *url, int attrsonly, LDAPMessage **res )
{
	int	msgid;

	if (( msgid = ldap_url_search( ld, url, attrsonly )) == -1 ) {
		return( ld->ld_errno );
	}

	if ( ldap_result( ld, msgid, 1, (struct timeval *)NULL, res ) == -1 ) {
		return( ld->ld_errno );
	}

	return( ldap_result2error( ld, *res, 0 ));
}


void
ldap_pvt_hex_unescape( char *s )
{
/*
* Remove URL hex escapes from s... done in place.  The basic concept for
* this routine is borrowed from the WWW library HTUnEscape() routine.
*/
	char	*p;

	for ( p = s; *s != '\0'; ++s ) {
		if ( *s == '%' ) {
			if ( *++s != '\0' ) {
				*p = ldap_pvt_unhex( *s ) << 4;
			}
			if ( *++s != '\0' ) {
				*p++ += ldap_pvt_unhex( *s );
			}
		} else {
			*p++ = *s;
		}
	}

	*p = '\0';
}


int
ldap_pvt_unhex( int c )
{
	return( c >= '0' && c <= '9' ? c - '0'
	    : c >= 'A' && c <= 'F' ? c - 'A' + 10
	    : c - 'a' + 10 );
}
