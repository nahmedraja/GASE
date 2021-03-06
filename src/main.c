#include <stdio.h>
#include <string.h>
#include "kstring.h"
#include "utils.h"

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "0.1"
#endif

int bwa_fa2pac(int argc, char *argv[]);
int bwa_pac2bwt(int argc, char *argv[]);
int bwa_bwtupdate(int argc, char *argv[]);
int bwa_bwt2sa(int argc, char *argv[]);
int bwa_index(int argc, char *argv[]);
int bwt_bwtgen_main(int argc, char *argv[]);

int bwa_aln(int argc, char *argv[]);
int bwa_sai2sam_se(int argc, char *argv[]);
int bwa_sai2sam_pe(int argc, char *argv[]);

int bwa_bwtsw2(int argc, char *argv[]);

int main_fastmap(int argc, char *argv[]);
int gase_aln(int argc, char *argv[]);
int main_shm(int argc, char *argv[]);

int main_pemerge(int argc, char *argv[]);
int main_maxk(int argc, char *argv[]);
	
static int usage()
{
	fprintf(stderr, "\n");
	fprintf(stderr, "Program: GASE (Generic Aligner for Seed-and-Extend)\n");
	fprintf(stderr, "Version: %s\n", PACKAGE_VERSION);
	fprintf(stderr, "GASE is an extension of BWA (version 0.7.13) developed by Heng Li.\n");
	fprintf(stderr, "Contact: Nauman Ahmed <n.ahmed@tudelft.nl>\n\n");
	fprintf(stderr, "Usage:   gase <command> [options]\n\n");
	fprintf(stderr, "Command: index         index sequences in the FASTA format\n");
	fprintf(stderr, "         gase_aln           GASE algorithm\n");
	fprintf(stderr, "         fastmap       identify seeds\n");
	/*fprintf(stderr, "         pemerge       merge overlapping paired ends (EXPERIMENTAL)\n");
	fprintf(stderr, "         aln           gapped/ungapped alignment\n");
	fprintf(stderr, "         samse         generate alignment (single ended)\n");
	fprintf(stderr, "         sampe         generate alignment (paired ended)\n");
	fprintf(stderr, "         bwasw         BWA-SW for long queries\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "         shm           manage indices in shared memory\n");
	fprintf(stderr, "         fa2pac        convert FASTA to PAC format\n");
	fprintf(stderr, "         pac2bwt       generate BWT from PAC\n");
	fprintf(stderr, "         pac2bwtgen    alternative algorithm for generating BWT\n");
	fprintf(stderr, "         bwtupdate     update .bwt to the new format\n");
	fprintf(stderr, "         bwt2sa        generate SA from BWT and Occ\n");*/
	fprintf(stderr, "\n");
	fprintf(stderr, "Note: To use GASE, you need to first index the genome with `gase index'.\n\n");
	return 1;
}

int main(int argc, char *argv[])
{
	extern char *bwa_pg;
	int i, ret;
	double t_real;
	extern FILE* f_exec_time;
	kstring_t pg = {0,0,0};
	t_real = realtime();
	ksprintf(&pg, "@PG\tID:bwa\tPN:bwa\tVN:%s\tCL:%s", PACKAGE_VERSION, argv[0]);
	for (i = 1; i < argc; ++i) ksprintf(&pg, " %s", argv[i]);
	bwa_pg = pg.s;
	if (argc < 2) return usage();
	/*if (strcmp(argv[1], "fa2pac") == 0) ret = bwa_fa2pac(argc-1, argv+1);
	else if (strcmp(argv[1], "pac2bwt") == 0) ret = bwa_pac2bwt(argc-1, argv+1);
	else if (strcmp(argv[1], "pac2bwtgen") == 0) ret = bwt_bwtgen_main(argc-1, argv+1);
	else if (strcmp(argv[1], "bwtupdate") == 0) ret = bwa_bwtupdate(argc-1, argv+1);
	else if (strcmp(argv[1], "bwt2sa") == 0) ret = bwa_bwt2sa(argc-1, argv+1);*/
	if (strcmp(argv[1], "index") == 0) ret = bwa_index(argc-1, argv+1);
	/*else if (strcmp(argv[1], "aln") == 0) ret = bwa_aln(argc-1, argv+1);
	else if (strcmp(argv[1], "samse") == 0) ret = bwa_sai2sam_se(argc-1, argv+1);
	else if (strcmp(argv[1], "sampe") == 0) ret = bwa_sai2sam_pe(argc-1, argv+1);
	else if (strcmp(argv[1], "bwtsw2") == 0) ret = bwa_bwtsw2(argc-1, argv+1);
	else if (strcmp(argv[1], "dbwtsw") == 0) ret = bwa_bwtsw2(argc-1, argv+1);
	else if (strcmp(argv[1], "bwasw") == 0) ret = bwa_bwtsw2(argc-1, argv+1);*/
	else if (strcmp(argv[1], "fastmap") == 0) ret = main_fastmap(argc-1, argv+1);
	/*else if (strcmp(argv[1], "mem") == 0) ret = main_mem(argc-2, argv+2, fresult);
	else if (strcmp(argv[1], "shm") == 0) ret = main_shm(argc-1, argv+1);
	else if (strcmp(argv[1], "pemerge") == 0) ret = main_pemerge(argc-1, argv+1);
	else if (strcmp(argv[1], "maxk") == 0) ret = main_maxk(argc-1, argv+1);*/
	else if (strcmp(argv[1], "gase_aln") == 0) ret = gase_aln(argc-1, argv+1);
	else {
		fprintf(stderr, "[main] unrecognized command '%s'\n", argv[1]);
		return 1;
	}
	err_fflush(stdout);
	err_fclose(stdout);
	if (ret == 0) {
		fprintf(stderr, "[%s] Version: %s\n", __func__, PACKAGE_VERSION);
		fprintf(stderr, "[%s] CMD:", __func__);
		for (i = 0; i < argc; ++i)
			fprintf(stderr, " %s", argv[i]);
		fprintf(f_exec_time,"%.3f\t", realtime() - t_real);
		fprintf(stderr, "\n[%s] Real time: %.3f sec; CPU: %.3f sec\n", __func__, realtime() - t_real, cputime());

	}
	free(bwa_pg);
	return ret;
}
