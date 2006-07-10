/* $Id$ $Revision$ */
/* vim:set shiftwidth=4 ts=8: */

/**********************************************************
*      This software is part of the graphviz package      *
*                http://www.graphviz.org/                 *
*                                                         *
*            Copyright (c) 1994-2004 AT&T Corp.           *
*                and is licensed under the                *
*            Common Public License, Version 1.0           *
*                      by AT&T Corp.                      *
*                                                         *
*        Information and Software Systems Research        *
*              AT&T Research, Florham Park NJ             *
**********************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBZ
#include <zlib.h>
#endif
#ifdef MSWIN32
#include <io.h>
#endif

#include "macros.h"
#include "const.h"

#include "gvplugin_render.h"
#include "graph.h"

typedef enum { FORMAT_SVG, FORMAT_SVGZ, } format_type;

extern char *xml_string(char *str);

/* SVG dash array */
static char *sdarray = "5,2";
/* SVG dot array */
static char *sdotarray = "1,5";

void svggen_fputs(GVJ_t * job, char *s)
{
    int len;

    len = strlen(s);
    switch (job->render.id) {
    case FORMAT_SVGZ:
#ifdef HAVE_LIBZ
	gzwrite((gzFile *) (job->output_file), s, (unsigned) len);
#endif
	break;
    case FORMAT_SVG:
	fwrite(s, sizeof(char), (unsigned) len, job->output_file);
	break;
    }
}

/* svggen_printf:
 * Note that this function is unsafe due to the fixed buffer size.
 * It should only be used when the caller is sure the input will not
 * overflow the buffer. In particular, it should be avoided for
 * input coming from users. Also, if vsnprintf is available, the
 * code should check for return values to use it safely.
 */
void svggen_printf(GVJ_t * job, const char *format, ...)
{
    char buf[BUFSIZ];
    va_list argp;

    va_start(argp, format);
#ifdef HAVE_VSNPRINTF
    (void) vsnprintf(buf, sizeof(buf), format, argp);
#else
    (void) vsprintf(buf, format, argp);
#endif
    va_end(argp);

    svggen_fputs(job, buf);
}

static void svggen_bzptarray(GVJ_t * job, pointf * A, int n)
{
    int i;
    char c;

    c = 'M';			/* first point */
    for (i = 0; i < n; i++) {
	svggen_printf(job, "%c%g,%g", c, A[i].x, -A[i].y);
	if (i == 0)
	    c = 'C';		/* second point */
	else
	    c = ' ';		/* remaining points */
    }
}

static void svggen_print_color(GVJ_t * job, gvcolor_t color)
{
    static char buf[SMALLBUF];

    switch (color.type) {
    case COLOR_STRING:
	svggen_fputs(job, color.u.string);
	break;
    case RGBA_BYTE:
	sprintf(buf, "#%02x%02x%02x",
		color.u.rgba[0], color.u.rgba[1], color.u.rgba[2]);
	svggen_fputs(job, buf);
	break;
    default:
	assert(0);		/* internal error */
    }
}

static void svggen_font(GVJ_t * job)
{
    gvstyle_t *style = job->style;
    char buf[BUFSIZ];
    int needstyle = 0;

    strcpy(buf, " style=\"");
    if (strcasecmp(style->fontfam, DEFAULT_FONTNAME)) {
	sprintf(buf + strlen(buf), "font-family:%s;", style->fontfam);
	needstyle++;
    }
    if (style->fontsz != DEFAULT_FONTSIZE) {
	sprintf(buf + strlen(buf), "font-size:%.2f;", (style->fontsz));
	needstyle++;
    }
    switch (style->pencolor.type) {
    case COLOR_STRING:
	if (strcasecmp(style->pencolor.u.string, "black")) {
	    sprintf(buf + strlen(buf), "fill:%s;",
		    style->pencolor.u.string);
	    needstyle++;
	}
	break;
    case RGBA_BYTE:
	sprintf(buf + strlen(buf), "fill:#%02x%02x%02x;",
		style->pencolor.u.rgba[0],
		style->pencolor.u.rgba[1], style->pencolor.u.rgba[2]);
	needstyle++;
	break;
    default:
	assert(0);		/* internal error */
    }
    if (needstyle) {
	strcat(buf, "\"");
	svggen_fputs(job, buf);
    }
}


static void svggen_grstyle(GVJ_t * job, int filled)
{
    gvstyle_t *style = job->style;

    svggen_fputs(job, " style=\"fill:");
    if (filled && ! (style->fillcolor.type == RGBA_BYTE
		  && style->fillcolor.u.RGBA[3] < 128))
	svggen_print_color(job, style->fillcolor);
    else
	svggen_fputs(job, "none");
    svggen_fputs(job, ";stroke:");
    if (! (style->pencolor.type == RGBA_BYTE
		  && style->pencolor.u.RGBA[3] < 128))
	svggen_print_color(job, style->pencolor);
    else
	svggen_fputs(job, "none");
    if (style->penwidth != PENWIDTH_NORMAL)
	svggen_printf(job, ";stroke-width:%g", style->penwidth);
    if (style->pen == PEN_DASHED) {
	svggen_printf(job, ";stroke-dasharray:%s", sdarray);
    } else if (style->pen == PEN_DOTTED) {
	svggen_printf(job, ";stroke-dasharray:%s", sdotarray);
    }
    svggen_fputs(job, ";\"");
}

static void svggen_comment(GVJ_t * job, char *str)
{
    svggen_fputs(job, "<!-- ");
    svggen_fputs(job, xml_string(str));
    svggen_fputs(job, " -->\n");
}

static void svggen_begin_job(GVJ_t * job)
{
#if HAVE_LIBZ
    int fd;
#endif

    switch (job->render.id) {
    case FORMAT_SVGZ:
#if HAVE_LIBZ
	/* open dup so can gzclose independent of FILE close */
	fd = dup(fileno(job->output_file));
#ifdef HAVE_SETMODE
#ifdef O_BINARY
	/*
	 * Windows will do \n -> \r\n  translations on
	 * stdout unless told otherwise.
	 */
	setmode(fd, O_BINARY);
#endif
#endif

	job->output_file = (FILE *) (gzdopen(fd, "wb"));
	if (!job->output_file) {
	    (job->common->errorfn) ("Error opening compressed output file\n");
	    exit(1);
	}
	break;
#else
	(job->common->errorfn) ("No libz support.\n");
	exit(1);
#endif
    case FORMAT_SVG:
	break;
    }

    svggen_fputs(job,
		 "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n");
    svggen_fputs(job,
		 "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.0//EN\"\n");
    svggen_fputs(job,
		 " \"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\"");

    /* This is to work around a bug in the SVG 1.0 DTD */
    svggen_fputs(job,
		 " [\n <!ATTLIST svg xmlns:xlink CDATA #FIXED \"http://www.w3.org/1999/xlink\">\n]");

    svggen_fputs(job, ">\n<!-- Generated by ");
    svggen_fputs(job, xml_string(job->common->info[0]));
    svggen_fputs(job, " version ");
    svggen_fputs(job, xml_string(job->common->info[1]));
    svggen_fputs(job, " (");
    svggen_fputs(job, xml_string(job->common->info[2]));
    svggen_fputs(job, ")\n     For user: ");
    svggen_fputs(job, xml_string(job->common->user));
    svggen_fputs(job, " -->\n");
}

static void svggen_begin_graph(GVJ_t * job)
{
    obj_state_t *obj = job->obj;

    svggen_fputs(job, "<!--");
    if (obj->g->name[0]) {
        svggen_fputs(job, " Title: ");
	svggen_fputs(job, xml_string(obj->g->name));
    }
    svggen_printf(job, " Pages: %d -->\n", job->pagesArraySize.x * job->pagesArraySize.y);

    svggen_printf(job, "<svg width=\"%dpx\" height=\"%dpx\"\n",
	job->width, job->height);
    /* namespace of svg */
    svggen_fputs(job, " xmlns=\"http://www.w3.org/2000/svg\"");
    /* namespace of xlink */
    svggen_fputs(job, " xmlns:xlink=\"http://www.w3.org/1999/xlink\"");
    svggen_fputs(job, ">\n");
}

static void svggen_end_graph(GVJ_t * job)
{
    svggen_fputs(job, "</svg>\n");
    switch (job->render.id) {
    case FORMAT_SVGZ:
#ifdef HAVE_LIBZ
	gzclose((gzFile *) (job->output_file));
	break;
#else
	(job->common->errorfn) ("No libz support\n");
	exit(1);
#endif
    case FORMAT_SVG:
	break;
    }
}

static void svggen_begin_layer(GVJ_t * job, char *layername, int layerNum, int numLayers)
{
    svggen_fputs(job, "<g id=\"");
    svggen_fputs(job, xml_string(layername));
    svggen_fputs(job, "\" class=\"layer\">\n");
}

static void svggen_end_layer(GVJ_t * job)
{
    svggen_fputs(job, "</g>\n");
}

static void svggen_begin_page(GVJ_t * job)
{
    obj_state_t *obj = job->obj;

    /* its really just a page of the graph, but its still a graph,
     * and it is the entire graph if we're not currently paging */
    svggen_printf(job, "<g id=\"graph%d\" class=\"graph\"",
	    job->common->viewNum);
    svggen_printf(job,
	    " transform=\"scale(%g %g) rotate(%d) translate(%g %g)\"",
	    job->scale.x, job->scale.y, job->rotation,
	    job->translation.x, job->translation.y);
    /* default style */
    svggen_fputs(job, " style=\"font-family:");
    svggen_fputs(job, job->style->fontfam);
    svggen_printf(job, ";font-size:%.2f;\">\n", job->style->fontsz);
    if (obj->g->name[0]) {
        svggen_fputs(job, "<title>");
        svggen_fputs(job, xml_string(obj->g->name));
        svggen_fputs(job, "</title>\n");
    }
}

static void svggen_end_page(GVJ_t * job)
{
    svggen_fputs(job, "</g>\n");
}

static void svggen_begin_cluster(GVJ_t * job)
{
    obj_state_t *obj = job->obj;

    svggen_printf(job, "<g id=\"cluster%ld\" class=\"cluster\">",
	    obj->sg->meta_node->id);
    svggen_fputs(job, "<title>");
    svggen_fputs(job, xml_string(obj->sg->name));
    svggen_fputs(job, "</title>\n");
}

static void svggen_end_cluster(GVJ_t * job)
{
    svggen_fputs(job, "</g>\n");
}

static void svggen_begin_node(GVJ_t * job)
{
    obj_state_t *obj = job->obj;

    svggen_printf(job, "<g id=\"node%ld\" class=\"node\">", obj->n->id);
    svggen_fputs(job, "<title>");
    svggen_fputs(job, xml_string(obj->n->name));
    svggen_fputs(job, "</title>\n");
}

static void svggen_end_node(GVJ_t * job)
{
    svggen_fputs(job, "</g>\n");
}

static void
svggen_begin_edge(GVJ_t * job)
{
    obj_state_t *obj = job->obj;
    char *edgeop;

    svggen_printf(job, "<g id=\"edge%ld\" class=\"edge\">", obj->e->id);
    if (obj->e->tail->graph->root->kind & AGFLAG_DIRECTED)
	edgeop = "&#45;&gt;";
    else
	edgeop = "&#45;&#45;";
    svggen_fputs(job, "<title>");
    svggen_fputs(job, xml_string(obj->e->tail->name));
    svggen_fputs(job, edgeop);
    /* can't do this in single svggen_printf because
     * xml_string's buffer gets reused. */
    svggen_fputs(job, xml_string(obj->e->head->name));
    svggen_fputs(job, "</title>\n");
}

static void svggen_end_edge(GVJ_t * job)
{
    svggen_fputs(job, "</g>\n");
}

static void
svggen_begin_anchor(GVJ_t * job, char *href, char *tooltip, char *target)
{
    svggen_fputs(job, "<a xlink:href=\"");
    svggen_fputs(job, xml_string(href));
    if (tooltip && tooltip[0]) {
	svggen_fputs(job, "\" xlink:title=\"");
	svggen_fputs(job, xml_string(tooltip));
    }
    if (target && target[0]) {
	svggen_fputs(job, "\" target=\"");
	svggen_fputs(job, xml_string(target));
    }
    svggen_fputs(job, "\">\n");
}

static void svggen_end_anchor(GVJ_t * job)
{
    svggen_fputs(job, "</a>\n");
}

static void svggen_textpara(GVJ_t * job, pointf p, textpara_t * para)
{
    char *anchor;

    switch (para->just) {
    case 'l':
	anchor = "start";
	break;
    case 'r':
	anchor = "end";
	break;
    default:
    case 'n':
	anchor = "middle";
	break;
    }

    svggen_printf(job, "<text text-anchor=\"%s\"", anchor);
    svggen_printf(job, " x=\"%g\" y=\"%g\"", p.x, -p.y);
    svggen_font(job);
    svggen_fputs(job, ">");
    svggen_fputs(job, xml_string(para->str));
    svggen_fputs(job, "</text>\n");
}

static void svggen_ellipse(GVJ_t * job, pointf * A, int filled)
{
    /* A[] contains 2 points: the center and corner. */
    svggen_fputs(job, "<ellipse");
    svggen_grstyle(job, filled);
    svggen_printf(job, " cx=\"%g\" cy=\"%g\"", A[0].x, -A[0].y);
    svggen_printf(job, " rx=\"%g\" ry=\"%g\"",
	    A[1].x - A[0].x, A[1].y - A[0].y);
    svggen_fputs(job, "/>\n");
}

static void
svggen_bezier(GVJ_t * job, pointf * A, int n, int arrow_at_start,
	      int arrow_at_end, int filled)
{
    svggen_fputs(job, "<path");
    svggen_grstyle(job, filled);
    svggen_fputs(job, " d=\"");
    svggen_bzptarray(job, A, n);
    svggen_fputs(job, "\"/>\n");
}

static void svggen_polygon(GVJ_t * job, pointf * A, int n, int filled)
{
    int i;

    svggen_fputs(job, "<polygon");
    svggen_grstyle(job, filled);
    svggen_fputs(job, " points=\"");
    for (i = 0; i < n; i++)
	svggen_printf(job, "%g,%g ", A[i].x, -A[i].y);
    svggen_printf(job, "%g,%g", A[0].x, -A[0].y);	/* because Adobe SVG is broken */
    svggen_fputs(job, "\"/>\n");
}

static void svggen_polyline(GVJ_t * job, pointf * A, int n)
{
    int i;

    svggen_fputs(job, "<polyline");
    svggen_grstyle(job, 0);
    svggen_fputs(job, " points=\"");
    for (i = 0; i < n; i++)
	svggen_printf(job, "%g,%g ", A[i].x, -A[i].y);
    svggen_fputs(job, "\"/>\n");
}

/* color names from http://www.w3.org/TR/SVG/types.html */
/* NB.  List must be LANG_C sorted */
static char *svggen_knowncolors[] = {
    "aliceblue", "antiquewhite", "aqua", "aquamarine", "azure",
    "beige", "bisque", "black", "blanchedalmond", "blue",
    "blueviolet", "brown", "burlywood",
    "cadetblue", "chartreuse", "chocolate", "coral",
    "cornflowerblue", "cornsilk", "crimson", "cyan",
    "darkblue", "darkcyan", "darkgoldenrod", "darkgray",
    "darkgreen", "darkgrey", "darkkhaki", "darkmagenta",
    "darkolivegreen", "darkorange", "darkorchid", "darkred",
    "darksalmon", "darkseagreen", "darkslateblue", "darkslategray",
    "darkslategrey", "darkturquoise", "darkviolet", "deeppink",
    "deepskyblue", "dimgray", "dimgrey", "dodgerblue",
    "firebrick", "floralwhite", "forestgreen", "fuchsia",
    "gainsboro", "ghostwhite", "gold", "goldenrod", "gray",
    "green", "greenyellow", "grey",
    "honeydew", "hotpink", "indianred",
    "indigo", "ivory", "khaki",
    "lavender", "lavenderblush", "lawngreen", "lemonchiffon",
    "lightblue", "lightcoral", "lightcyan", "lightgoldenrodyellow",
    "lightgray", "lightgreen", "lightgrey", "lightpink",
    "lightsalmon", "lightseagreen", "lightskyblue",
    "lightslategray", "lightslategrey", "lightsteelblue",
    "lightyellow", "lime", "limegreen", "linen",
    "magenta", "maroon", "mediumaquamarine", "mediumblue",
    "mediumorchid", "mediumpurple", "mediumseagreen",
    "mediumslateblue", "mediumspringgreen", "mediumturquoise",
    "mediumvioletred", "midnightblue", "mintcream",
    "mistyrose", "moccasin",
    "navajowhite", "navy", "oldlace",
    "olive", "olivedrab", "orange", "orangered", "orchid",
    "palegoldenrod", "palegreen", "paleturquoise",
    "palevioletred", "papayawhip", "peachpuff", "peru", "pink",
    "plum", "powderblue", "purple",
    "red", "rosybrown", "royalblue",
    "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell",
    "sienna", "silver", "skyblue", "slateblue", "slategray",
    "slategrey", "snow", "springgreen", "steelblue",
    "tan", "teal", "thistle", "tomato", "turquoise",
    "violet",
    "wheat", "white", "whitesmoke",
    "yellow", "yellowgreen"
};

gvrender_engine_t svggen_engine = {
    svggen_begin_job,
    0,				/* svggen_end_job */
    svggen_begin_graph,
    svggen_end_graph,
    svggen_begin_layer,
    svggen_end_layer,
    svggen_begin_page,
    svggen_end_page,
    svggen_begin_cluster,
    svggen_end_cluster,
    0,				/* svggen_begin_nodes */
    0,				/* svggen_end_nodes */
    0,				/* svggen_begin_edges */
    0,				/* svggen_end_edges */
    svggen_begin_node,
    svggen_end_node,
    svggen_begin_edge,
    svggen_end_edge,
    svggen_begin_anchor,
    svggen_end_anchor,
    svggen_textpara,
    0,				/* svggen_resolve_color */
    svggen_ellipse,
    svggen_polygon,
    svggen_bezier,
    svggen_polyline,
    svggen_comment,
};

gvrender_features_t svggen_features = {
    GVRENDER_DOES_TRUECOLOR
	| GVRENDER_Y_GOES_DOWN
        | GVRENDER_DOES_TRANSFORM, /* flags*/
    DEFAULT_EMBED_MARGIN,	/* default margin - points */
    {96.,96.},			/* default dpi */
    svggen_knowncolors,		/* knowncolors */
    sizeof(svggen_knowncolors) / sizeof(char *),	/* sizeof knowncolors */
    RGBA_BYTE,			/* color_type */
    NULL,                       /* device */
    "svg",                      /* gvloadimage target for usershapes */
};

gvplugin_installed_t gvrender_core_svg_types[] = {
    {FORMAT_SVG, "svg", 1, &svggen_engine, &svggen_features},
#if HAVE_LIBZ
    {FORMAT_SVGZ, "svgz", 1, &svggen_engine, &svggen_features},
#endif
    {0, NULL, 0, NULL, NULL}
};
