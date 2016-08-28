#include "utils.h"
#include "anno.h"
#include "anno_bed.h"
#include <htslib/kseq.h>
#include <htslib/kstring.h>

struct anno_stack {
    int l, m;
    char **a;
};

struct anno_stack* anno_stack_init()
{
    struct anno_stack * stack = (struct anno_stack*)malloc(sizeof(struct anno_stack));
    stack->m = 0;
    stack->l = 0;
    stack->a = NULL;
    return stack;
}
static void anno_stack_push(struct anno_stack *stack, char *name)
{
    int i;
    for ( i = 0; i < stack->l; ++i ) {
	if ( strcmp(stack->a[i], name) == 0 )
	    return;
    }
    if ( stack->m == stack->l ) {
	stack->m = stack->m == 0 ? 2 : stack->m + 2;
	stack->a = (char**)realloc(stack->a, sizeof(char*)*stack->m);
    }
    stack->a[stack->l++] = (char*)strdup(name);
}
void anno_stack_destroy(struct anno_stack *stack)
{
    int i;
    for ( i = 0; i < stack->l; ++i )
	free(stack->a[i]);
    if ( stack->m )
	free(stack->a);
    free(stack);	
}
char * get_col_tsv(struct beds_anno_tsv *tsv, int icol)
{
    if (icol > tsv->nfields) {
	warnings("outof columns");
	return NULL;
    }
    return tsv->string.s + tsv->fields[icol];
}
static char *generate_funcreg_string (struct beds_anno_file *file, struct anno_col *col)
{
    if ( file->cached == 0 )
	return NULL;

    struct anno_stack *stack = anno_stack_init();
    int i;
    for ( i = 0; i < file->cached; ++i ) {
	struct beds_anno_tsv *tsv = file->buffer[i];
	anno_stack_push(stack, get_col_tsv(tsv, col->icol));
    }
    kstring_t string = KSTRING_INIT;
    for ( i = 0; i < stack->l; ++i ) {
	if ( i ) kputc(',', &string);
	kputs(stack->a[i], &string);	
    }
    anno_stack_destroy(stack);
    return string.s;
}
struct beds_anno_tsv *beds_anno_tsv_init()
{
    struct beds_anno_tsv *tsv = (struct beds_anno_tsv*)malloc(sizeof(struct beds_anno_tsv));
    tsv->nfields = 0;
    tsv->fields = NULL;
    tsv->string.l = tsv->string.m = 0;
    tsv->string.s = 0;
    return tsv;
}
void convert_string_tsv(struct beds_anno_tsv *tsv)
{
    tsv->fields = ksplit(&tsv->string, '\t', &tsv->nfields);
    assert(tsv->nfields > 3);
    tsv->start = atoi(tsv->string.s + tsv->fields[1]);
    tsv->end = atoi(tsv->string.s + tsv->fields[2]);
}
void beds_anno_tsv_destroy(struct beds_anno_tsv *tsv)
{
    if (tsv->nfields)
	free(tsv->fields);
    if (tsv->string.m)
	free(tsv->string.s);
    free(tsv);
}

kstring_t * kstring_init()
{
    kstring_t *str = (kstring_t *)malloc(sizeof(kstring_t));
    str->l = 0;
    str->m = 0;
    str->s = 0;
    return str;
}
void kstring_destroy(kstring_t *str)
{
    if (str->m)
	free(str->s);
    free(str);
}
int beds_fill_buffer(struct beds_anno_file *file, bcf_hdr_t *hdr_out, bcf1_t *line)
{
    assert(file->idx);
    int tid = tbx_name2id(file->idx, bcf_seqname(hdr_out, line));
    if ( tid == -1 ) {
	warnings("no chromosome %s found in databases %s.", bcf_seqname(hdr_out, line), file->fname);
	return 1;
    }
    // if cached this region already, just skip refill. this is different from vcfs_fill_buffer()
    if ( tid == file->last_id && file->last_start <= line->pos + 1 && file->last_end > line->pos)
	return -1;
    // empty cache
    file->cached = 0;
    int i;
    hts_itr_t *itr = tbx_itr_queryi(file->idx, tid, line->pos, line->pos + line->rlen);
    if ( itr == NULL )
	return 1;
    // if buffer refilled, init last start and end
    file->last_id = tid;
    file->last_start = -1;
    file->last_end = -1;    
    while (1) {
	if ( file->cached == file->max ) {
	    file->max += 8;
	    file->buffer = (struct beds_anno_tsv**)realloc(file->buffer, sizeof(struct beds_anno_tsv*)*file->max);
	    for (i = 8; i > 0; --i)
		file->buffer[file->max - i] = beds_anno_tsv_init();
	}

	if ( tbx_itr_next(file->fp, file->idx, itr, &file->buffer[file->cached]->string) < 0)
	    break;
	struct beds_anno_tsv *tsv = file->buffer[file->cached++];
	convert_string_tsv(tsv);
	
	if ( file->last_end == -1 ) {
	    file->last_end = tsv->end;
	    file->last_start = tsv->start;
	    continue;
	} 
	if ( file->last_end < tsv->end )
	    file->last_end = tsv->end;
	if ( file->last_start > tsv->start )
	    file->last_start = tsv->start;	
    }
    // if buffer is filled return 0, else return 1
    return file->cached ? 0 : 1;    
}

int beds_options_init(struct beds_options *opts)
{
    memset(opts, 0, sizeof(struct beds_options));
    opts->beds_is_inited = 1;
    return 0;
}
int beds_file_destroy(struct beds_anno_file *file)
{
    int i;
    hts_close(file->fp);
    tbx_destroy(file->idx);
    for ( i = 0; i < file->n_cols; ++i ) 
	free(file->cols[i].hdr_key);
    free(file->cols);
    for ( i = 0; i < file->max; ++i ) 
	beds_anno_tsv_destroy(file->buffer[i]);
    
    if ( file->max )
	free(file->buffer);
    return 0;
}
int beds_options_destroy(struct beds_options *opts)
{
    if (opts->beds_is_inited == 0)
	return 1;
    int i;
    for ( i = 0; i < opts->n_files; ++i )
	beds_file_destroy(&opts->files[i]);
    if ( opts->m_files )
	free(opts->files);
    return 0;
}
int beds_setter_info_string(struct beds_options *opts, bcf1_t *line, struct anno_col *col)
{
    struct beds_anno_file *file = &opts->files[col->ifile];
    beds_fill_buffer(file, opts->hdr_out, line);   
    char *string = generate_funcreg_string(file, col);
    if ( string == NULL )
	return 1;
    // only support string for bed function regions
    bcf_update_info_string(opts->hdr_out, line, col->hdr_key, string);
    return 0;
}

int beds_databases_add(struct beds_options *opts, const char *fname, char *columns)
{
    if ( opts->n_files == opts->m_files ) {
	opts->m_files = opts->m_files == 0 ? 2 : opts->m_files +2;
	opts->files = (struct beds_anno_file*)realloc(opts->files, opts->m_files*sizeof(struct beds_anno_file));	
    }
    struct beds_anno_file *file = &opts->files[opts->n_files];
    file->id = opts->n_files;
    file->fp = hts_open(fname, "r");
    if (file->fp == NULL)
	error("Failed to open %s : %s", fname, strerror(errno));
    int n;
    file->idx = tbx_index_load(fname);
    if ( file->idx == NULL)
	error("Failed to load index of %s.", fname);
    opts->n_files++;
    file->n_cols = 0;
    file->cols = 0;
    file->cached = 0;
    file->max = 0;
    file->buffer = 0;

    file->last_id = -1;
    file->last_start = -1;
    file->last_end = -1;
    kstring_t string = KSTRING_INIT;
    int no_columns = 0;
    int i;
    if ( columns == NULL ) {
	warnings("No columns string specified for %s. Will annotate all tags in this data.", fname);
	no_columns = 1;
    } else {
	int *splits = NULL;
	kputs(columns, &string);
	int nfields;
	splits = ksplit(&string, ',', &nfields);
	file->m_cols = nfields;
	file->cols = (struct anno_col*)malloc(sizeof(struct anno_col) * file->m_cols);

	for ( i = 0; i < nfields; ++i ) {
	    char *ss = string.s + splits[i];
	    struct anno_col *col = &file->cols[file->n_cols];
	    col->icol = i;
	    col->replace = REPLACE_MISSING;
	    if (*ss == '+') {
		col->replace = REPLACE_MISSING;
		ss++;
	    } else if ( *ss == '-' ) {
		col->replace = REPLACE_EXISTING;
		ss++;
	    }
	    if (ss[0] == '\0')
		continue;
	    col->hdr_key = strdup(ss);	    
	    col->icol = -1;
	    // debug_print("%s, %d", col->hdr_key, file->n_cols);
	    file->n_cols++;	    
	}
	string.l = 0;	    
    }

    int n_hdr = 0;
    int m_hdr = 0;
    struct hdr_string *hdrs = NULL;
    while (1) {
	string.l =0;
	if ( hts_getline(file->fp, KS_SEP_LINE, &string) < 0 )
	    break;
	// only accept header line in the beginning for file
	if ( string.s[0] != '#' )
	    break;
	if ( strncmp(string.s, "##INFO=", 7) == 0) {
	    char *ss = string.s + 11;
	    char *se = ss;
	    while (se && *se != ',') se++;
	    struct anno_col *col = NULL;
	    // if no column string specified, init all header lines
	    if ( no_columns ) {
		if ( file->n_cols == file->m_cols ) {
		    file->m_cols = file->m_cols == 0 ? 2 : file->m_cols + 2;
		    file->cols = (struct anno_col *) realloc(file->cols, file->m_cols*sizeof(struct anno_col));
		}
		col = &file->cols[file->n_cols++];
		col->icol = -1;
		col->hdr_key = strndup(ss, se-ss+1);
		col->hdr_key[se-ss] = '\0';
	    } else {
		for ( i = 0; i < file->n_cols; ++i ) {		    
		    if ( strncmp(file->cols[i].hdr_key, ss, se-ss) == 0)
			break;
		}
		// if header line is not set in the column string, skip
		if ( i == file->n_cols )
		    continue;
		col = &file->cols[i];
	    }
	    debug_print("key : %s", col->hdr_key);
	    // specify setter functions here
	    col->setter.bed = beds_setter_info_string;
	    
	    bcf_hdr_append(opts->hdr_out, string.s);
	    bcf_hdr_sync(opts->hdr_out);
	    int hdr_id = bcf_hdr_id2int(opts->hdr_out, BCF_DT_ID,col->hdr_key);
	    assert ( bcf_hdr_idinfo_exists(opts->hdr_out, BCF_HL_INFO, hdr_id) );
	}
	string.l = 0;
	// set column number for each col
	if ( strncasecmp(string.s, "#chr", 4) == 0) {
	    int nfields;	    
	    int *splits = ksplit(&string, '\t', &nfields);

	    if (nfields < 4)
		error("Bad header of bed database : %s. n_fields : %d, %s", fname, nfields, string.s);
	    int k;
	    for ( k = 3; k < nfields; ++k ) {
		char *ss = string.s + splits[k];
		for (i = 0; i < file->n_cols; ++i ) {
		    struct anno_col *col = &file->cols[i];
		    if ( strcmp(col->hdr_key, ss) == 0)
			break;
		}
		// if name line specify more names than column string or header, skip
		if ( i == file->n_cols )
		    continue;

		struct anno_col *col = &file->cols[i];
		col->icol = k;
	    }
	}
    }
    for ( i = 0; i < file->n_cols; ++i ) {
	struct anno_col *col = &file->cols[i];
	if ( col->hdr_key && col->icol == -1 )
	    error("No column %s found in bed database : %s", col->hdr_key, fname);

	int hdr_id = bcf_hdr_id2int(opts->hdr_out, BCF_DT_ID, col->hdr_key);
	col->number = bcf_hdr_id2length(opts->hdr_out, BCF_HL_INFO, hdr_id);
	if ( col->number == BCF_VL_A || col->number == BCF_VL_R || col->number == BCF_VL_G)
	    error("Only support fixed INFO number for bed database. %s", col->hdr_key);
	col->ifile = file->id;
    }
    if ( string.m )
	free(string.s);
    return 0;
}

bcf1_t *anno_beds_core(struct beds_options *opts, bcf1_t *line)
{
    if ( opts->beds_is_inited == 0 )
	return line;
    assert(opts->hdr_out);
    int i, j;
    for ( i = 0; i < opts->n_files; ++i ) {
	struct beds_anno_file *file = &opts->files[i];
	for ( j = 0; j < file->n_cols; ++j ) {
	    struct anno_col *col = &file->cols[j];
	    col->setter.bed(opts, line, col);
	}
    }
    return line;
}



#ifdef _BED_ANNOS_MAIN

#include "config.h"
static const char *hts_bcf_wmode(int file_type)
{
    if ( file_type == FT_BCF )
	return "wbu";    // uncompressed BCF
    if ( file_type & FT_BCF )
	return "wb";      // compressed BCF
    if ( file_type & FT_GZ )
	return "wz";       // compressed VCF
    return "w";                                 // uncompressed VCF
}

const char *json_fname = 0;
const char *input_fname = 0;
const char *output_fname_type = 0;
const char *output_fname = 0;

int main(int argc, char **argv)
{
    if ( argc == 1 )
	error("Usage : bed_annos -c config.json -O z -o output.vcf.gz input.vcf.gz");
    int i;
    for ( i = 1; i < argc; ) {
	const char *a = argv[i++];
	const char **var = 0;
	if ( strcmp(a, "-c") == 0 )
	    var = &json_fname;
	else if ( strcmp(a, "-O") == 0 )
	    var = &output_fname_type;
	else if ( strcmp(a, "-o") == 0 )
	    var = &output_fname;

	if ( var != 0 ) {
	    if ( i == argc )
		error("Missing an argument after %s", a);
	    *var = argv[i++];
	    continue;
	}

	if ( input_fname == 0 ) {
	    input_fname = a;
	    continue;
	}

	error("Unknown argument : %s.", a);
    }

    struct vcfanno_config *con = vcfanno_config_init();
    if ( vcfanno_load_config(con, json_fname) != 0 )
	error("Failed to load configure file. %s : %s", json_fname, strerror(errno));
    vcfanno_config_debug(con);
    if ( con->beds.n_beds == 0)
	error("No bed database specified.");
    if ( input_fname == 0 && (!isatty(fileno(stdin))) )
	input_fname = "-";
    if ( input_fname == 0 )
	error("No input file.");

    int out_type = FT_VCF;
    if ( output_fname_type != 0 ) {
	switch (output_fname_type[0]) {
	    case 'b':
		out_type = FT_BCF_GZ; break;
	    case 'u':
		out_type = FT_BCF; break;
	    case 'z':
		out_type = FT_VCF_GZ; break;
	    case 'v':
		out_type = FT_VCF; break;
	    default :
		error("The output type \"%d\" not recognised\n", out_type);
	};
    }

    htsFile *fp = hts_open(input_fname, "r");
    if ( fp == NULL )
	error("Failed to open %s : %s.", input_fname, strerror(errno));
    htsFormat type = *hts_get_format(fp);
    if ( type.format != vcf && type.format != bcf )
	error("Unsupported input format. %s", input_fname);
    
    bcf_hdr_t *hdr = bcf_hdr_read(fp);
    if ( hdr == NULL )
	error("Failed to parse header.");	
    bcf_hdr_t *hdr_out = bcf_hdr_dup(hdr);    
    htsFile *fout = output_fname == 0 ? hts_open("-", hts_bcf_wmode(out_type)) : hts_open(output_fname, hts_bcf_wmode(out_type));
    struct beds_options opts = { .beds_is_inited = 0,};
    beds_options_init(&opts);
    opts.hdr_out = hdr_out;

    for ( i = 0; i < con->beds.n_beds; ++i ) {
	beds_databases_add(&opts, con->beds.files[i].fname, con->beds.files[i].columns);
    }

    bcf_hdr_write(fout, hdr_out);
    bcf1_t *line = bcf_init();
    while ( bcf_read(fp, hdr, line) == 0 ) {
	anno_beds_core(&opts, line);
	bcf_write(fout, hdr_out, line);
    }
    bcf_destroy(line);
    bcf_hdr_destroy(hdr);
    bcf_hdr_destroy(hdr_out);
    beds_options_destroy(&opts);
    hts_close(fp);
    hts_close(fout);
    return 0;

}
#endif