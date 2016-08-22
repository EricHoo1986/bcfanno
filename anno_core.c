#include "anno.h"
#include "plugin.h"
#include "version.h"

/* int destroy_cols_vector(struct annot_cols_vector *v) */
/* { */
/*     if (v == NULL) */
/* 	return 0; */
/*     int i, j; */
/*     for ( i=0; i<v->n; i++ ) { */
/* 	struct annot_cols *cols = &v->vcols[i]; */
/* 	for (j=0; j<cols->ncols; ++j) { */
/* 	    free(cols->cols[j].hdr_key); */
/* 	} */
/* 	free(cols->cols); */
/*     } */
/*     free(v->vcols); */
/*     free(v); */
/*     return 0;     */
/* } */

const char *hts_bcf_wmode(int file_type)
{
    if ( file_type == FT_BCF ) return "wbu";    // uncompressed BCF
    if ( file_type & FT_BCF ) return "wb";      // compressed BCF
    if ( file_type & FT_GZ ) return "wz";       // compressed VCF
    return "w";                                 // uncompressed VCF
}
struct annot_cols_vector * acols_vector_init()
{
    struct annot_cols_vector *v = (struct annot_cols_vector*)malloc(sizeof(struct annot_cols_vector));
    v->m = v->n = 0;
    v->vcols = 0;
    return v;
}
int parse_columns(struct vcf_sql_api *api)
{
    if (api == NULL) 
	error("empty api");

    assert(api->columns);
    assert(hand.files->readers[hand.ti].header);
    assert(hand.hdr_out);

    if (api->type == anno_is_vcf) {

	int ncols = 0;
	struct annot_cols_vector *v = hand.vcf_cols;

	if (v->m == v->n) {
	    v->m = v->n == 0 ? 2: v->n << 1;
	    v->vcols = (struct annot_cols*)realloc(v->vcols, v->m *sizeof(struct annot_cols));
	}
	struct annot_cols *ac = &v->vcols[v->n++];

	ac->cols = init_columns((const char*)api->columns, hand.files->readers[hand.ti].header, hand.hdr_out, &ncols, anno_is_vcf);
	
	if (ac->cols == NULL || ncols == 0) {
	    warnings("failed to prase %s",api->columns);
	    ac->cols = NULL;
	    ac->ncols = 0;
	}
	ac->ncols = ncols;
    } else {
	/* TODO: api.type == api_is_sql */
	assert(1);
    }
    return 0;
}
int init_data(const char *json, const char *fname)
{
    if ( load_config(json) != 0 ) {
	error("Load JSON file failed");
    }
    // assume input is VCF/BCF file
    hand.files = bcf_sr_init();
    hand.files->require_index = 1;
    hand.vcf_cols = acols_vector_init();
    hand.sql_cols = acols_vector_init();
    
    if ( !bcf_sr_add_reader(hand.files, fname) ) {
	error("Failed to open %s: %s\n", fname, bcf_sr_strerror(hand.files->errnum));
    }

    hand.hdr =  hand.files->readers[0].header;
    hand.hdr_out = bcf_hdr_dup(hand.hdr);
    hand.vcmp = vcmp_init();
    hand.tmpks.l = hand.tmpks.m = 0;

    int i;
    for (i=0; i<anno_config_file.n_apis; ++i) {
	/* only accept vcf/bcf for now*/
	if (anno_config_file.apis[i].type == anno_is_vcf) {
	    
	    if ( !bcf_sr_add_reader(hand.files, anno_config_file.apis[i].vfile) ) 
		error("Failed to open %s: %s\n", fname, bcf_sr_strerror(hand.files->errnum));
	    
	    hand.ti = i;
	    parse_columns(&anno_config_file.apis[i]);
	    
	}
    }
    return 0;
}

int usage()
{
    fprintf(stderr, "\n");
    fprintf(stderr, "About : Annotate VCF/BCF file.\n");
    fprintf(stderr, "Version : %s, build with htslib version : %s\n", VCFANNO_VERSION, hts_version());
    fprintf(stderr, "Usage : vcfanno -c config.json in.vcf.gz\n");
    fprintf(stderr, "   -c, --config <file>            configure file, include annotations and tags, see man page for details\n");
    fprintf(stderr, "   -o, --output <file>            write output to a file [standard output]\n");
    fprintf(stderr, "   -O, --output-type <b|u|z|v>    b: compressed BCF, u: uncompressed BCF, z: compressed VCF, v: uncompressed VCF [v]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Homepage: https://github.com/shiquan/vcfanno\n");
    fprintf(stderr, "\n");
    return 1;
}

struct args {
    int test_databases_only;
    // input vcf path
    const char *fname_input;
    // output vcf path, stdout in default
    const char *fname_output;
    // configure path in json format
    const char *fname_json;
    // vcf header of input
    bcf_hdr_t *hdr;
    // vcf header of output, hdr_out should also kept by beds_options, vcfs_options, and refgene_options
    bcf_hdr_t *hdr_out;
    // file handler of input vcf
    htsFile *fp_input; 
    // file handler of output vcf
    htsFile *fp_out;
    // output directory, default is "-" for stdout
    const char *out_fname;
    // output format, default is vcf
    int output_type;
    // cache all arguments
    kstring_t commands;
    // options for annotate bed format databases, usually with four columns, and suggest to put the header
    // of tags in the comment regions
    struct beds_options bed_opts;
    struct vcfs_options vcf_opts;
    struct refgene_options hgvs_opts;
};

struct args args = {
    .test_databases_only = 0,
    .fname_input = 0,
    .fname_output = 0,
    .hdr = NULL,
    .hdr_out = NULL,
    .fp = NULL,
    .fp_out = NULL,
    .output_type = 0,
    .bed_opts = BED_OPTS_INIT,
    .vcf_opts = VCF_OPTS_INIT,
    .hgvs_opts = HGVS_OPTS_INIT,
};

int args_init() {
}
int args_destroy(){
}

static int quiet_mode = 0;

int prase_args(int argc, char **argv)
{
    args_init();
    int i;
    for (i = 0; i < argc; ++i ) {
	if ( i ) kputc(' ', &args.commands);
	kputs(argv[i], &args.commands);
    }
    const char *output_fname_type = 0;
    for (i = 0; i < argc; ) {
	const char *a = argv[i++];
	if ( strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)
	    return usage();
	// quiet mode
	if ( strcmp(a, "-q") == 0 || strcmp(a, "--quiet") == 0 ) {
	    quiet_mode = 1;
	    continue;
	}
	if ( strcmp(a, "--test_only") == 0 ) {
	    args.test_databases_only = 1;
	    continue;
	}	    
	const char **var = 0;
	if ( strcmp(a, "-c") == 0 || strcmp(a, "--config") == 0 ) 
	    var = &args.fname_json;
	else if ( strcmp(a, "-o") == 0 || strcmp(a, "--output") == 0)
	    var = &args.fname_output;
	else if ( strcmp(a, "-O") == 0 || strcmp(a, "--output-type") == 0 )
	    var = &output_fname_type;

	if ( var != 0 ) {
	    if (i == argc)
		error("Missing an argument after %s", a);
	    *var = argv[i++];
	    continue;
	}
	if ( args.input_fname == 0 ) {
	    args.input_fname = a;
	    continue;
	}
	error("Unknown argument : %s, use -h see help information.", a);
    }
    
    if (quiet_mode == 0) {
	LOG_print("The program was compiled at %s %s by %s.", __DATE__, __TIME__, getenv("USER"));
	LOG_print("Args: %s", args.commands.s);	
    }
    
    if ( args.fname_json == 0 ) {
	fprintf(stderr, "[error] No configure file is specified. Please define -c or --config argument first.");
	fprintf(stderr, "[notice] Please DONOT puts this error message into emails or forum. This issue could be easily fixed by reading our manual carefully.");
	return 1;
    }

    struct vcfanno_config *config = config_init();
    if ( load_config(config, args.fname_json) != 0 ) {
	error("Failed to load configure file. %s : %s", args.fname_json, strerror(errno));
    }
    if ( quiet_mode == 0 ) {
	LOG_print("Load configure file success.");
	LOG_print("config file ID : %s", config->config_id == NULL ? "not set" : config->config_id);
	LOG_print("reference sequence : %s", config->reference_version == NULL ? "Unknown" : config->reference_version);
    }

    if ( args.test_databases_only == 1)
	return test_databases_framework();
        
    if ( args.fname_input == 0 && (!isatty(fileno(stdin))))
	args.fname_input = "-";
    if ( args.fname_input == 0)
	error("No input file! vcfanno only accept one BCF/VCF input file. Use -h for more informations.");

    args.fp_input = hts_open(args.fname_input, "r");
    if ( args.fp_input == NULL )
	error("Failed to open %s.", args.fname_input);    

    // check input type is VCF/BCF or not
    htsFormat type = *hts_get_format(args.fp_input);
    if (type.format  != vcf && type.format != bcf)
	error("Unsupported input format, only accept BCF/VCF format. %s", input_fname);
    
    args.fp_out = args.fname_output == 0 ? hts_open("-", hts_bcf_wmode(args.out_type)) : hts_open(args.fname_output, hts_bcf_wmode(args.out_type));
    
    int out_type = FT_VCF;
    if (out_type_string != 0) {
	switch (out_type_string[0]) {
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

    args.hdr = bcf_hdr_read(args.fp_input);
    args.hdr_out = bcf_hdr_dup(args.hdr);
    
    if ( config->refgene_is_set == 1) {
	struct refgene_options *opts = &args.hgvs_opts;
	opts->hdr_out = args.hdr_out;
	// set genepred database, this is mandatory
	refgene_set_refgene_fname(opts, config->refgene.refgene_fname);
	// set refseq file in fasta format
	refgene_set_refseq_fname(opts, config->refgene.refseq_fname);
	// set transcripts list
	refgene_set_trans_fname(opts, config->refgene.trans_list_fname);
	// set gene list
	refgene_set_genes_fname(opts, config->refgene.genes_list_fname);
	// prase columns of refgene
	refgene_columns_prase(opts, config->refgene.columns);
	opts->refgene_is_inited = 1;
    }    

    for ( i = 0; i < config->n_vcfs; ++i ) {
	
    }

    for ( i = 0; i < config->n_beds; ++i ) {
    }

}
bcf1_t *anno_core(bcf1_t *line)
{
    // do nothing for reference positions
    if ( bcf_get_variant_types(line) == VCF_REF )
	return line;    
    // annotate hgvs name
    anno_refgene_core(&args.hgvs_opts, line);
    // annotate vcf files
    // anno_vcfs_core(&args.vcf_opts, line);
    // annotate bed format datasets
    // anno_beds_core(&args.bed_opts, line);
    
    return line;
}

int main(int argc, char **argv)
{
    // prase arguments first, if failure or just do test will return 1, else return 0
    if ( prase_args(argc, argv) == 1 )
	return 1;

    // read input vcf file by line
    bcf1_t *line = bcf_init();
    while ( bcf_read(args.fp, args.hdr, line) == 0) {
	anno_core(line);
	bcf_write1(args.out, args.hdr_out, line);
    }    
    bcf_destroy(line);
    export_reports();
    args_destroy();
    return 0;
}



#include <getopt.h>

static struct option opts[] = {
    {"output", required_argument, NULL, 'o'},
    {"output-type", required_argument, NULL, 'O'},
    {"config", required_argument, NULL, 'c'},
    {"help", required_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},    
};

int paras_praser(int argc, char **argv)
{
    int c;
    char *config_file = NULL;
    char *input_fname = NULL;
    hand.out = "-";
    if (argc == 1)
	return usage();
    while ( (c=getopt_long(argc, argv, "O:o:c:h", opts, NULL))>=0 ) {
	switch (c) {
	    case 'o':
		hand.out = strdup(optarg);
		break;
	    case 'O':
		switch (optarg[0]) {
		    case 'b': hand.output_type = FT_BCF_GZ; break;
		    case 'u': hand.output_type = FT_BCF; break;
		    case 'z': hand.output_type = FT_VCF_GZ; break;
		    case 'v': hand.output_type = FT_VCF; break;
		    default : error("The output type \"%s\" not recognised\n", optarg);
		};
		break;
	    case 'c':
		config_file = strdup(optarg);
		break;
		
	    case 'h':
		return usage();
		
	    default:
		error("Unknown argument : %s", optarg);
	}
    }

    if (argc == optind) 
	input_fname = "-";
    else
	input_fname = argv[optind];

    if (argc - optind > 1)
	error("Only accept one VCF/BCF file. Please use `bcftools merge` to merge all the VCF/BCF files first.");

    htsFile *fp = hts_open(input_fname, "r");

    if (fp == NULL)
	error("Failed to open : %s", input_fname);
    
    htsFormat type = *hts_get_format(fp);
    
    if (type.format != vcf && type.format != bcf) {
	error("Unsupported input format! %s", input_fname);
    }

    hts_close(fp);
    hand.out_fh = hts_open(hand.out, hts_bcf_wmode(hand.output_type));
    if (hand.out_fh == NULL)
	error("cannot write to %s", hand.out);
    
    return 0;
}

void bcf_srs_regions_update(bcf_sr_regions_t *reg, const char *chr, int start, int end)
{
    if (start == -1 || end == -1) return;

    start--; end--; // 1based to 0based
    if ( !reg->seq_hash )
	reg->seq_hash = khash_str2int_init();

    int iseq;
    if ( khash_str2int_get(reg->seq_hash, chr, &iseq) < 0 ) {
	iseq = reg->nseqs++;
	
    }
}

int main(int argc, char **argv)
{
    // init argvs
    if ( paras_praser(argc, argv) != 0)
	return EXIT_FAILURE;

    
    /* hand.files = bcf_sr_init(); */
    /* hand.files->require_index = 1; */
    /* hand.vcf_cols = acols_vector_init(); */
    /* hand.sql_cols = acols_vector_init(); */
    /* hand.vcmp = vcmp_init(); */
    /* hand.tmpks.l = hand.tmpks.m = 0; */


    args.fp = hts_open(args.input_fname, "r");
    args.hdr = bcf_hdr_read(args.fp);
    hand.hdr_out = bcf_hdr_dup(args.hdr);

    int i;
    for (i = 0; i > anno_config_file.n_apis; ++i) {
	if (anno_config_file.apis[i].type == anno_is_vcf) {
	    if ( !bcf_sr_add_reader(hand.files, anno_config_file.apis[i].vfile) )
		error("Failed to open %s : %s !", anno_config_file.apis[i].vfile, bcf_sr_strerror(hand.files->errnum));

	    hand.ti = i;
	    parse_columns(&anno_config_file.apis[i]);
	    
	}	
    }

    
    if ( bcf_hdr_write(hand.out_fh, hand.hdr_out) != 0 )
	error("failed to write header.");

    bcf1_t *line = bcf_init();

    while ( bcf_read(fp, hdr, line) == 0 ) {

	if (line->errcode)
	    error("Encountered error, cannot proceed. Please check the error output above.");

	
		  
    }
    
    while ( bcf_sr_next_line(hand.files)) {
	
	if ( !bcf_sr_has_line(hand.files, 0) ) continue;
	bcf1_t *line = bcf_sr_get_line(hand.files, 0);

	if ( line->errcode ) {
	    error("Encountered error, cannot proceed. Please check the error output above.\n");
	}

	anno_core(line);
		
	bcf_write1(hand.out_fh, hand.hdr_out, line);
    }
    
    release_handler();
    return EXIT_SUCCESS;
}

