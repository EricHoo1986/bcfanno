#ifndef BCFANNO_CONFIG_H
#define BCFANNO_CONFIG_H

#include <stdio.h>
#include <stdlib.h>

struct refgene_config {
    // if refgene_is_set == 1, genepred_fname and columns are mandatory.
    int refgene_is_set;
    char *genepred_fname;
    char *refseq_fname;
    char *trans_list_fname;
    char *gene_list_fname;
//    char *columns;
};

struct file_config {
    // file path
     char *fname;
    // columns string
     char *columns;
};
struct vcfs_config {
    // vcf files number
    int n_vcfs;
    struct file_config *files;
};
struct beds_config {
    // bed files number
    int n_beds;
    struct file_config *files;
};

// skip other keys except author, config_id and reference_version
struct bcfanno_config {
    char *author;
    char *config_id;
    char *reference_version;
    char *reference_path;
    struct vcfs_config vcfs;
    struct beds_config beds;
    struct refgene_config refgene;
};

extern struct bcfanno_config *bcfanno_config_init(void);
extern void bcfanno_config_destroy(struct bcfanno_config *);
extern int bcfanno_load_config(struct bcfanno_config *, const char *);
extern int bcfanno_config_debug(struct bcfanno_config *config);

#endif
