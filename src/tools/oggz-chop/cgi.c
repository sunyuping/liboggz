#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "oggz-chop.h"
#include "header.h"
#include "httpdate.h"
#include "timespec.h"

/* Customization: for servers that do not set PATH_TRANSLATED, specify the
 * DocumentRoot here and it will be prepended to PATH_INFO */
//#define DOCUMENT_ROOT "/var/www"
#define DOCUMENT_ROOT NULL

static void
set_param (OCState * state, char * key, char * val)
{
  char * sep;

  if (!strncmp ("s", key, 2)) state->start = parse_timespec (val);
  if (!strncmp ("start", key, 6)) state->start = parse_timespec (val);

  if (!strncmp ("e", key, 2)) state->end = parse_timespec (val);
  if (!strncmp ("end", key, 6)) state->end = parse_timespec (val);

  if (!strncmp ("t", key, 2)) {
    if (val && (sep = strchr (val, '/')) != NULL) {
      *sep++ = '\0';
      state->end = parse_timespec (sep);
    } else {
      state->end = -1.0;
    }
    state->start = parse_timespec (val);
  }
}

/**
 * Parse the name=value pairs in the query string and set parameters
 * @param start,end The range parameters to set
 * @param query The query string
 */
static void
parse_query (OCState * state, char * query)
{
  char * key, * val, * end;

  if (!query) return;

  key = query;

  do {
    val = strchr (key, '=');
    end = strchr (key, '&');

    if (end) {
      if (val) {
        if (val < end) {
          *val++ = '\0';
        } else {
          val = NULL;
        }
      }
      *end++ = '\0';
    } else {
      if (val) *val++ = '\0';
    }

    /* fprintf (stderr, "%s = %s\n", key, val);*/
    set_param (state, key, val);

    key = end;

  } while (end != NULL);

  return;
}

int
cgi_test (void)
{
  char * gateway_interface;

  gateway_interface = getenv ("GATEWAY_INTERFACE");
  if (gateway_interface == NULL) {
    return 0;
  }

  return 1;
}

#if 0
static int
cgi_send_photo (photo_t * photo)
{
  /*header_content_length (photo->size);*/
  header_end();

  photo_put (photo);

  return 0;
}

static int
cgi_send (OCState * state)
{
  header_last_modified (state->in.mtime);

  if (params->nochange) {
    cgi_send_photo (&params->in);
  } else if (params->out.name) {
    cgi_send_photo (&params->out);
  } else {
    header_content_length ((off_t)params->data_size);
    header_end();

    memory_send (params);
  }

  return 0;
}
#endif

static char *
prepend_document_root (char * path_info)
{
  char * dr = DOCUMENT_ROOT;
  char * path_translated;
  int dr_len, pt_len;

  if (path_info == NULL) return NULL;

  if (dr == NULL || *dr == '\0') return strdup (path_info);

  dr_len = strlen (dr);

  pt_len = dr_len + strlen(path_info) + 1;
  path_translated = malloc (pt_len);
  snprintf (path_translated, pt_len , "%s%s", dr, path_info);

  return path_translated;
}

static int
path_undefined (char * vars)
{
  fprintf (stderr, "oggz-chop: Cannot determine real filename due to CGI configuration error: %s undefined\n", vars);
  return -1;
}

int
cgi_main (OCState * state)
{
  int err = 0;
  char * path_info;
  char * path_translated;
  char * query_string;
  char * if_modified_since;
  time_t since_time, last_time;
  struct stat statbuf;
  int built_path_translated=0;

  httpdate_init ();

  path_info = getenv ("PATH_INFO");
  path_translated = getenv ("PATH_TRANSLATED");
  query_string = getenv ("QUERY_STRING");
  if_modified_since = getenv ("HTTP_IF_MODIFIED_SINCE");

  memset (state, 0, sizeof(*state));
  state->end = -1.0;
  state->do_skeleton = 1;

  if (path_translated == NULL) {
    if (path_info == NULL)
      return path_undefined ("PATH_TRANSLATED and PATH_INFO");

    path_translated = prepend_document_root (path_info);
    if (path_translated == NULL)
      return path_undefined ("PATH_TRANSLATED");

    built_path_translated = 1;
  }

  state->infilename = path_translated;

  /* Get Last-Modified time */
  if (stat (path_translated, &statbuf) == -1) {
    switch (errno) {
    case ENOENT:
      return 0;
    default:
      fprintf (stderr, "oggz-chop: %s: %s\n", path_translated, strerror(errno));
      return -1;
    }
  }

  last_time = statbuf.st_mtime;

  if (if_modified_since != NULL) {
    int len;

    fprintf (stderr, "If-Modified-Since: %s\n", if_modified_since);

    len = strlen (if_modified_since) + 1;
    since_time = httpdate_parse (if_modified_since, len);

    if (last_time <= since_time) {
      header_not_modified();
      header_end();
      return 1;
    }
  }

  header_content_type_ogg ();

  header_last_modified (last_time);

  header_accept_timeuri_ogg ();

  /*config_init (params);*/

  parse_query (state, query_string);

#if 0
  if (params->x || params->y || params->scale || params->gray ||
      params->quality) {
    cache_init (params, path_info);
  } else {
    params->nochange = 1;
  }
#endif

  header_end();

  err = 0;
#if 0
  if (!(params->nochange || params->cached)) {
    err = chop (state);
  }
#else
  err = chop (state);
#endif

  if (built_path_translated && path_translated != NULL)
    free (path_translated);
  
  return err;
}
