/*
   Copyright (C) 2003 Commonwealth Scientific and Industrial Research
   Organisation (CSIRO) Australia

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   - Neither the name of CSIRO Australia nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
   PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE ORGANISATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <errno.h>

#ifdef HAVE_INTTYPES_H
#  include <inttypes.h>
#else
#  define PRId64 "I64d"
#endif

#include <oggz/oggz.h>
#include "oggz_tools.h"

/* #define DEBUG */

typedef struct {
  OggzReadPacket read_packet;
  int clipcount;
  int pktssincekey;
  int granuleshift;
  int keyframes;
  int cmml;
  int html;
} OSData;

static char * progname;
static FILE * outfile = NULL;

#define HTML_HEAD "<html>\n<head>\n<title>OGGZ_SCAN OUTPUT</title>\n</head>\n\n<body>\n<h1>OGGZ_SCAN OUTPUT for %s</h1>\n\n"

#define HTML_END "<hr/>\n</body>\n</html>"

#define HTML_CLIP "<p>Clip No %i\tat t=%lf.</p>\n\n"


#define CMML_HEAD "<cmml>\n<stream>\n<import src=\"%s\"/>\n</stream>\n\n<head>\n<title>OGGZ_SCAN OUTPUT for %s</title>\n</head>\n\n"

#define CMML_END "</cmml>"

#define CMML_CLIP "<clip id=\"clip-%i\" start=\"%lf\">\n<desc>Enter description.</desc>\n</clip>\n\n"

static void
usage (char * progname)
{
  printf ("Usage: %s [options] filename\n", progname);
  printf ("Scans a ogg file and outputs specific characteristic landmarks.\n");
  printf ("E.g. -k calculates shot boundaries from theora keyframes.\n");
  printf ("\nOptions\n");
  printf ("  -o filename, --output filename\n");
  printf ("                         Specify output filename\n");
  printf ("  -c, --cmml             Outputs results as cmml\n");
  printf ("  -w, --web, --html      Outputs results as html page\n");
  printf ("  -k, --keyframe         Outputs shots using theora keyframes\n");
  printf ("  -h, --help             Display this help and exit\n");
  printf ("  -v, --version          Output version information and exit\n");
  printf ("\n");
  printf ("Please report bugs to <ogg-dev@xiph.org>\n");
}

static int
filter_page (OGGZ * oggz, const ogg_page * og, long serialno, void * user_data)
{
  OSData * osdata = (OSData *) user_data;
  const char * ident;

  /* set scanning callback for keyframe calculation on theora pages only */
  if (osdata->keyframes && ogg_page_bos ((ogg_page *)og)) {
    ident = ot_page_identify (og, NULL);
    if (strcasecmp ("theora", ident) == 0) {
       oggz_set_read_callback (oggz, serialno, osdata->read_packet, osdata);
    }
  }

  return 0;
}

/* FIXME: on Mac OS X, off_t is 64-bits.  Obviously we want a nicer
 * way to do it than this, but a quick fix is a good fix */
#ifdef __APPLE__
#  define PRI_off_t "q"
#else
#  define PRI_off_t "l"
#endif

static int
read_packet (OGGZ * oggz, ogg_packet * op, long serialno, void * user_data)
{
  OSData * osdata = (OSData *) user_data;
  double time_offset;

  /* calculate granuleshift for theora track */
  if (osdata->granuleshift == 0) {
    osdata->granuleshift = 1 << oggz_get_granuleshift (oggz, serialno);
    osdata->granuleshift--;
#ifdef DEBUG
    fprintf(outfile, "Granuleshift = %d\n", osdata->granuleshift);
#endif
  }

  /* don't do anything on bos page */
  if (op->b_o_s) {
    return 0;
  }

  /* calculate the keyframes if requested */
  if (osdata->keyframes) {
    /* increase number of packets seen since the last intra frame */
    osdata->pktssincekey++;

    /* does the current packet contain a keyframe? */
    if(!(op->packet[0] & 0x80) /* data packet */ &&
       !(op->packet[0] & 0x40) /* intra frame */ ) {
      ogg_int64_t units;

#ifdef DEBUG
      fprintf(outfile, "Keyframe found: packetno=%" PRId64 
              "\t pktssincekey=%d\n", op->packetno, osdata->pktssincekey);
#endif

      /* if the keyframe is on the granuleshift position, ignore it */
      if (osdata->pktssincekey >= osdata->granuleshift) {
        osdata->pktssincekey=0;
        return 0;
      }
      osdata->pktssincekey=0;

      /* new shot boundary found: calculate time */
      units = oggz_tell_units (oggz);
      if (units == -1) {
        time_offset = oggz_tell(oggz);
      } else {
        time_offset = (double)units / 1000.0;
      }

      /* output in requested format */
      if (osdata->html) {
        fprintf(outfile, HTML_CLIP, osdata->clipcount, time_offset);
      }
      if (osdata->cmml) {
        fprintf(outfile, CMML_CLIP, osdata->clipcount, time_offset);
      }
      osdata->clipcount++;
      if (!osdata->html && !osdata->cmml) {
        fprintf(outfile, "%lf\n", time_offset);
      }
    }
  }

#ifdef DEBUG
  fprintf (outfile, "%ld bytes pktno=%" PRId64 "\n", op->bytes, op->packetno);
#endif

  return 0;
}

int
main (int argc, char ** argv)
{
  int show_version = 0;
  int show_help = 0;
  int output_cmml = 0;
  int output_html = 0;
  int scan_keyframes = 0;

  OSData * osdata = NULL;
  OGGZ * oggz;
  char * infilename = NULL, * outfilename = NULL;
  int i;
  long n;

  progname = argv[0];

  if (argc < 2) {
    usage (progname);
    return (1);
  }

  while (1) {
    char * optstring = "cwkhvo:";

#ifdef HAVE_GETOPT_LONG
    static struct option long_options[] = {
      {"output",   required_argument, 0, 'o'},
      {"cmml",     no_argument, 0, 'c'},
      {"web",      no_argument, 0, 'w'},
      {"html",     no_argument, 0, 'w'},
      {"keyframe", no_argument, 0, 'k'},
      {"help",     no_argument, 0, 'h'},
      {"version",  no_argument, 0, 'v'},
      {0,0,0,0}
    };

    i = getopt_long(argc, argv, optstring, long_options, NULL);
#else
    i = getopt (argc, argv, optstring);
#endif
    if (i == -1) break;
    if (i == ':') {
      usage (progname);
      goto exit_err;
    }

    switch (i) {
    case 'c': /* cmml */
      output_cmml = 1;
      break;
    case 'w': /* html */
      output_html = 1;
      break;
    case 'k': /* keyframe */
      scan_keyframes = 1;
      break;
    case 'h': /* help */
      show_help = 1;
      break;
    case 'v': /* version */
      show_version = 1;
      break;
    case 'o': /* output */
      outfilename = optarg;
      break;
    default:
      break;
    }
  }

  if (show_version) {
    printf ("%s version " VERSION "\n", progname);
  }

  if (show_help) {
    usage (progname);
  }

  if (show_version || show_help) {
    goto exit_ok;
  }

  if (optind >= argc) {
    usage (progname);
    goto exit_err;
  }

  infilename = argv[optind++];

  if (outfilename == NULL) {
    outfile = stdout;
  } else {
    outfile = fopen (outfilename, "wb");
    if (outfile == NULL) {
      fprintf (stderr, "%s: unable to open output file %s\n",
	       progname, outfilename);
      goto exit_err;
    }
  }

  errno = 0;

  if (strcmp (infilename, "-") == 0) {
    oggz = oggz_open_stdio (stdin, OGGZ_READ|OGGZ_AUTO);
  } else {
    oggz = oggz_open (infilename, OGGZ_READ|OGGZ_AUTO);
  }

  if (oggz == NULL) {
    if (errno == 0) {
      fprintf (stderr, "%s: %s: error opening input file\n",
	      progname, infilename);
    } else {
      fprintf (stderr, "%s: %s: %s\n",
	       progname, infilename, strerror (errno));
    }
    goto exit_err;
  }

  /* init osdata */
  osdata = malloc (sizeof (OSData));
  memset (osdata, 0, sizeof (OSData));
  osdata->read_packet = read_packet;
  if (scan_keyframes) osdata->keyframes = 1;
  if (output_cmml)    osdata->cmml = 1;
  if (output_html)    osdata->html = 1;

  /* set up the right filters on the tracks */
  oggz_set_read_page (oggz, -1, filter_page, osdata);

  /* correct output format */
  if (output_html) {
    fprintf(outfile, HTML_HEAD, infilename);
  }
  if (output_cmml) {
    fprintf(outfile, CMML_HEAD, infilename, infilename);
  }
 
  while ((n = oggz_read (oggz, 1024)) > 0);

  /* finish output */
  if (output_html) {
    fprintf(outfile, HTML_END);
  }
  if (output_cmml) {
    fprintf(outfile, CMML_END);
  }

  oggz_close (oggz);

exit_ok:
  free(osdata);
  exit(0);

exit_err:
  free(osdata);
  exit(1);
}