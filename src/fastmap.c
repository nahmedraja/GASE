#include <zlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>
#include "bwa.h"
#include "bwamem.h"
#include "kvec.h"
#include "utils.h"
#include "bntseq.h"
#include "kseq.h"
KSEQ_DECLARE(gzFile)

extern unsigned char nst_nt4_table[256];

void *kopen(const char *fn, int *_fd);
int kclose(void *a);
void kt_pipeline(int n_threads, void *(*func)(void*, int, void*), void *shared_data, int n_steps);

typedef struct {
	kseq_t *ks, *ks2;
	mem_opt_t *opt;
	mem_pestat_t *pes0;
	int64_t n_processed;
	int copy_comment, actual_chunk_size;
	bwaidx_t *idx;
} ktp_aux_t;

typedef struct {
	ktp_aux_t *aux;
	int n_seqs;
	bseq1_t *seqs;
} ktp_data_t;

static void *process(void *shared, int step, void *_data)
{
	ktp_aux_t *aux = (ktp_aux_t*)shared;
	ktp_data_t *data = (ktp_data_t*)_data;
	int i;
	if (step == 0) {
		ktp_data_t *ret;
		int64_t size = 0;
		ret = calloc(1, sizeof(ktp_data_t));
		ret->seqs = bseq_read(aux->actual_chunk_size, &ret->n_seqs, aux->ks, aux->ks2);
		if (ret->seqs == 0) {
			free(ret);
			return 0;
		}
		if (!aux->copy_comment)
			for (i = 0; i < ret->n_seqs; ++i) {
				free(ret->seqs[i].comment);
				ret->seqs[i].comment = 0;
			}
		for (i = 0; i < ret->n_seqs; ++i) size += ret->seqs[i].l_seq;
		if (bwa_verbose >= 3)
			fprintf(stderr, "[M::%s] read %d sequences (%ld bp)...\n", __func__, ret->n_seqs, (long)size);
		return ret;
	} else if (step == 1) {
		const mem_opt_t *opt = aux->opt;
		const bwaidx_t *idx = aux->idx;
		if (opt->flag & MEM_F_SMARTPE) {
			bseq1_t *sep[2];
			int n_sep[2];
			mem_opt_t tmp_opt = *opt;
			bseq_classify(data->n_seqs, data->seqs, n_sep, sep);
			if (bwa_verbose >= 3)
				fprintf(stderr, "[M::%s] %d single-end sequences; %d paired-end sequences\n", __func__, n_sep[0], n_sep[1]);
			if (n_sep[0]) {
				tmp_opt.flag &= ~MEM_F_PE;
				mem_process_seqs(&tmp_opt, idx->bwt, idx->bns, idx->pac, aux->n_processed, n_sep[0], sep[0], 0);
				for (i = 0; i < n_sep[0]; ++i)
					data->seqs[sep[0][i].id].sam = sep[0][i].sam;
			}
			if (n_sep[1]) {
				tmp_opt.flag |= MEM_F_PE;
				mem_process_seqs(&tmp_opt, idx->bwt, idx->bns, idx->pac, aux->n_processed + n_sep[0], n_sep[1], sep[1], aux->pes0);
				for (i = 0; i < n_sep[1]; ++i)
					data->seqs[sep[1][i].id].sam = sep[1][i].sam;
			}
			free(sep[0]); free(sep[1]);
		} else mem_process_seqs(opt, idx->bwt, idx->bns, idx->pac, aux->n_processed, data->n_seqs, data->seqs, aux->pes0);
		aux->n_processed += data->n_seqs;
		return data;
	} else if (step == 2) {
		for (i = 0; i < data->n_seqs; ++i) {
			if (data->seqs[i].sam) err_fputs(data->seqs[i].sam, stdout);
			free(data->seqs[i].name); free(data->seqs[i].comment);
			free(data->seqs[i].seq); free(data->seqs[i].qual); free(data->seqs[i].sam);
		}
		free(data->seqs); free(data);
		return 0;
	}
	return 0;
}

static void update_a(mem_opt_t *opt, const mem_opt_t *opt0)
{
	if (opt0->a) { // matching score is changed
		if (!opt0->b) opt->b *= opt->a;
		if (!opt0->T) opt->T *= opt->a;
		if (!opt0->o_del) opt->o_del *= opt->a;
		if (!opt0->e_del) opt->e_del *= opt->a;
		if (!opt0->o_ins) opt->o_ins *= opt->a;
		if (!opt0->e_ins) opt->e_ins *= opt->a;
		if (!opt0->zdrop) opt->zdrop *= opt->a;
		if (!opt0->pen_clip5) opt->pen_clip5 *= opt->a;
		if (!opt0->pen_clip3) opt->pen_clip3 *= opt->a;
		if (!opt0->pen_unpaired) opt->pen_unpaired *= opt->a;
	}
}
char run_exec_time[20];
FILE* f_exec_time;
int gase_aln(int argc, char *argv[])
{
	mem_opt_t *opt, opt0;
	int fd, fd2, i, c, ignore_alt = 0, no_mt_io = 0;
	int fixed_chunk_size = -1;
	gzFile fp, fp2 = 0;
	char *p, *rg_line = 0, *hdr_line = 0;
	const char *mode = 0;
	void *ko = 0, *ko2 = 0;
	mem_pestat_t pes[4];
	ktp_aux_t aux;
	//run_exec_time = "run_exec_time.txt";
	sprintf(run_exec_time, "f");
	memset(&aux, 0, sizeof(ktp_aux_t));
	memset(pes, 0, 4 * sizeof(mem_pestat_t));
	for (i = 0; i < 4; ++i) pes[i].failed = 1;

	aux.opt = opt = mem_opt_init();
	memset(&opt0, 0, sizeof(mem_opt_t));
	while ((c = getopt(argc, argv, "1paMCSPVYjgozFk:c:v:s:r:t:R:A:B:O:E:U:w:L:d:T:Q:D:m:I:N:W:x:G:h:y:K:X:H:u:b:J:e:l:f:")) >= 0) {
		if (c == 'k') opt->min_seed_len = atoi(optarg), opt0.min_seed_len = 1;
		else if (c == '1') no_mt_io = 1;
		else if (c == 'x') mode = optarg;
		else if (c == 'w') opt->w = atoi(optarg), opt0.w = 1;
		else if (c == 'A') opt->a = atoi(optarg), opt0.a = 1;
		else if (c == 'B') opt->b = atoi(optarg), opt0.b = 1;
		else if (c == 'T') opt->T = atoi(optarg), opt0.T = 1;
		else if (c == 'U') opt->pen_unpaired = atoi(optarg), opt0.pen_unpaired = 1;
		else if (c == 't') opt->n_threads = atoi(optarg), opt->n_threads = opt->n_threads > 1? opt->n_threads : 1;
		else if (c == 'P') opt->flag |= MEM_F_NOPAIRING;
		else if (c == 'a') opt->flag |= MEM_F_ALL;
		else if (c == 'p') opt->flag |= MEM_F_PE | MEM_F_SMARTPE;
		else if (c == 'M') opt->flag |= MEM_F_NO_MULTI;
		else if (c == 'S') opt->flag |= MEM_F_NO_RESCUE;
		else if (c == 'Y') opt->flag |= MEM_F_SOFTCLIP;
		else if (c == 'V') opt->flag |= MEM_F_REF_HDR;
		else if (c == 'c') opt->max_occ = atoi(optarg), opt0.max_occ = 1;
		else if (c == 'd') opt->zdrop = atoi(optarg), opt0.zdrop = 1;
		else if (c == 'v') bwa_verbose = atoi(optarg);
		else if (c == 'j') ignore_alt = 1;
		else if (c == 'r') opt->split_factor = atof(optarg), opt0.split_factor = 1.;
		else if (c == 'D') opt->drop_ratio = atof(optarg), opt0.drop_ratio = 1.;
		else if (c == 'm') opt->max_matesw = atoi(optarg), opt0.max_matesw = 1;
		else if (c == 's') opt->split_width = atoi(optarg), opt0.split_width = 1;
		else if (c == 'G') opt->max_chain_gap = atoi(optarg), opt0.max_chain_gap = 1;
		else if (c == 'N') opt->max_chain_extend = atoi(optarg), opt0.max_chain_extend = 1;
		else if (c == 'W') opt->min_chain_weight = atoi(optarg), opt0.min_chain_weight = 1;
		else if (c == 'y') opt->max_mem_intv = atol(optarg), opt0.max_mem_intv = 1;
		else if (c == 'C') aux.copy_comment = 1;
		else if (c == 'K') fixed_chunk_size = atoi(optarg);
		else if (c == 'X') opt->mask_level = atof(optarg);
		else if (c == 'u') opt->seed_type= atoi(optarg);
		else if (c == 'J') opt->seed_intv= atoi(optarg);
		else if (c == 'e') opt->dp_type = atoi(optarg);
		else if (c == 'o') opt->opt_ext = 1;
		else if (c == 'g') opt->re_seed = 1;
		else if (c == 'f') sprintf(run_exec_time, "%s", optarg);
		else if (c == 'F') opt->shd_filter = 1;
		else if (c == 'l') opt->read_len = atoi(optarg);
		else if (c == 'h') {
			opt0.max_XA_hits = opt0.max_XA_hits_alt = 1;
			opt->max_XA_hits = opt->max_XA_hits_alt = strtol(optarg, &p, 10);
			if (*p != 0 && ispunct(*p) && isdigit(p[1]))
				opt->max_XA_hits_alt = strtol(p+1, &p, 10);
		}
		else if (c == 'Q') {
			opt0.mapQ_coef_len = 1;
			opt->mapQ_coef_len = atoi(optarg);
			opt->mapQ_coef_fac = opt->mapQ_coef_len > 0? log(opt->mapQ_coef_len) : 0;
		} else if (c == 'O') {
			opt0.o_del = opt0.o_ins = 1;
			opt->o_del = opt->o_ins = strtol(optarg, &p, 10);
			if (*p != 0 && ispunct(*p) && isdigit(p[1]))
				opt->o_ins = strtol(p+1, &p, 10);
		} else if (c == 'E') {
			opt0.e_del = opt0.e_ins = 1;
			opt->e_del = opt->e_ins = strtol(optarg, &p, 10);
			if (*p != 0 && ispunct(*p) && isdigit(p[1]))
				opt->e_ins = strtol(p+1, &p, 10);
		} else if (c == 'L') {
			opt0.pen_clip5 = opt0.pen_clip3 = 1;
			opt->pen_clip5 = opt->pen_clip3 = strtol(optarg, &p, 10);
			if (*p != 0 && ispunct(*p) && isdigit(p[1]))
				opt->pen_clip3 = strtol(p+1, &p, 10);
		} else if (c == 'R') {
			if ((rg_line = bwa_set_rg(optarg)) == 0) return 1; // FIXME: memory leak
		} else if (c == 'H') {
			if (optarg[0] != '@') {
				FILE *fp;
				if ((fp = fopen(optarg, "r")) != 0) {
					char *buf;
					buf = calloc(1, 0x10000);
					while (fgets(buf, 0xffff, fp)) {
						i = strlen(buf);
						assert(buf[i-1] == '\n'); // a long line
						buf[i-1] = 0;
						hdr_line = bwa_insert_header(buf, hdr_line);
					}
					free(buf);
					fclose(fp);
				}
			} else hdr_line = bwa_insert_header(optarg, hdr_line);
		} else if (c == 'I') { // specify the insert size distribution
			aux.pes0 = pes;
			pes[1].failed = 0;
			pes[1].avg = strtod(optarg, &p);
			pes[1].std = pes[1].avg * .1;
			if (*p != 0 && ispunct(*p) && isdigit(p[1]))
				pes[1].std = strtod(p+1, &p);
			pes[1].high = (int)(pes[1].avg + 4. * pes[1].std + .499);
			pes[1].low  = (int)(pes[1].avg - 4. * pes[1].std + .499);
			if (pes[1].low < 1) pes[1].low = 1;
			if (*p != 0 && ispunct(*p) && isdigit(p[1]))
				pes[1].high = (int)(strtod(p+1, &p) + .499);
			if (*p != 0 && ispunct(*p) && isdigit(p[1]))
				pes[1].low  = (int)(strtod(p+1, &p) + .499);
			if (bwa_verbose >= 3)
				fprintf(stderr, "[M::%s] mean insert size: %.3f, stddev: %.3f, max: %d, min: %d\n",
						__func__, pes[1].avg, pes[1].std, pes[1].high, pes[1].low);
		}
		else return 1;
	}
	if (opt->read_len == 0){
	   fprintf(stderr, "Must specify a read length for reporting execution time\n");
	   return 1;
	}
	if (rg_line) {
		hdr_line = bwa_insert_header(rg_line, hdr_line);
		free(rg_line);
	}
	if (opt->n_threads < 1) opt->n_threads = 1;
	if (optind + 1 >= argc || optind + 3 < argc) {
		fprintf(stderr, "\n");
		fprintf(stderr, "Usage: gase gase_aln [options] <ref.fa> <reads.fq> \n\n");
		fprintf(stderr, "Algorithm options:\n\n");
		fprintf(stderr, "       -t INT        number of threads [%d]\n", opt->n_threads);
		fprintf(stderr, "       -k INT        minimum seed length. For fixed length seeds seed_length = INT [%d]\n", opt->min_seed_len);
		fprintf(stderr, "       -w INT        band width for banded alignment [%d]\n", opt->w);
		fprintf(stderr, "       -d INT        off-diagonal X-dropoff [%d]\n", opt->zdrop);
		fprintf(stderr, "       -r FLOAT      look for internal seeds inside a seed longer than {-k} * FLOAT [%g]\n", opt->split_factor);
		fprintf(stderr, "       -y INT        seed occurrence for the 3rd round seeding [%ld]\n", (long)opt->max_mem_intv);
		fprintf(stderr, "       -s INT        look for internal seeds inside a seed with less than INT occ [%d]\n", opt->split_width);
		fprintf(stderr, "       -c INT        skip seeds with more than INT occurrences [%d]\n", opt->max_occ);
		fprintf(stderr, "       -D FLOAT      drop chains shorter than FLOAT fraction of the longest overlapping chain [%.2f]\n", opt->drop_ratio);
		fprintf(stderr, "       -W INT        discard a chain if seeded bases shorter than INT [0]\n");
		fprintf(stderr, "       -m INT        perform at most INT rounds of mate rescues for each read [%d]\n", opt->max_matesw);
		fprintf(stderr, "       -S            skip mate rescue\n");
		fprintf(stderr, "       -P            skip pairing; mate rescue performed unless -S also in use\n");
		fprintf(stderr, "\nScoring options:\n\n");
		fprintf(stderr, "       -A INT        score for a sequence match, which scales options -TdBOELU unless overridden [%d]\n", opt->a);
		fprintf(stderr, "       -B INT        penalty for a mismatch [%d]\n", opt->b);
		fprintf(stderr, "       -O INT[,INT]  gap open penalties for deletions and insertions [%d,%d]\n", opt->o_del, opt->o_ins);
		fprintf(stderr, "       -E INT[,INT]  gap extension penalty; a gap of size k cost '{-O} + {-E}*k' [%d,%d]\n", opt->e_del, opt->e_ins);
		fprintf(stderr, "       -L INT[,INT]  penalty for 5'- and 3'-end clipping [%d,%d]\n", opt->pen_clip5, opt->pen_clip3);
		fprintf(stderr, "       -U INT        penalty for an unpaired read pair [%d]\n\n", opt->pen_unpaired);
		fprintf(stderr, "       -x STR        read type. Setting -x changes multiple parameters unless overriden [null]\n");
		fprintf(stderr, "                     pacbio: -k17 -W40 -r10 -A1 -B1 -O1 -E1 -L0  (PacBio reads to ref)\n");
		fprintf(stderr, "                     ont2d: -k14 -W20 -r10 -A1 -B1 -O1 -E1 -L0  (Oxford Nanopore 2D-reads to ref)\n");
		fprintf(stderr, "                     intractg: -B9 -O16 -L5  (intra-species contigs to ref)\n");
		fprintf(stderr, "\nInput/output options:\n\n");
		//fprintf(stderr, "       -p            smart pairing (ignoring in2.fq)\n");
		fprintf(stderr, "       -R STR        read group header line such as '@RG\\tID:foo\\tSM:bar' [null]\n");
		fprintf(stderr, "       -H STR/FILE   insert STR to header if it starts with @; or insert lines in FILE [null]\n");
		//fprintf(stderr, "       -j            treat ALT contigs as part of the primary assembly (i.e. ignore <idxbase>.alt file)\n");
		//fprintf(stderr, "\n");
		fprintf(stderr, "       -v INT        verbose level: 1=error, 2=warning, 3=message, 4+=debugging [%d]\n", bwa_verbose);
		fprintf(stderr, "       -D FLOAT      do not output a secondary alignment if its score is less than (max_score * FLOAT)  [%.2f]\n", opt->drop_ratio);
		fprintf(stderr, "       -T INT        minimum score to output [%d]\n", opt->T);
		//fprintf(stderr, "       -h INT[,INT]  if there are <INT hits with score >80%% of the max score, output all in XA [%d,%d]\n", opt->max_XA_hits, opt->max_XA_hits_alt);
		fprintf(stderr, "       -a            output all alignments for SE or unpaired PE\n");
		fprintf(stderr, "       -C            append FASTA/FASTQ comment to SAM output\n");
		fprintf(stderr, "       -V            output the reference FASTA header in the XR tag\n");
		fprintf(stderr, "       -Y            use soft clipping for supplementary alignments\n");
		fprintf(stderr, "       -u INT        Seed type. Possible options: 1(all-SMEM), 2(fixed length seeds with no mismatch),\n");
		fprintf(stderr, "                     3(nov-SMEM), 4(fixed length seeds with at most 1 mismatch) [%d],\n\n", opt->seed_type);
		fprintf(stderr, "       -J INT        Seed interval for fixed length seeds [%d]\n\n", opt->seed_intv);
		fprintf(stderr, "       -e INT        Algorithm in the extension stage. Possible options 0(BWA-MEM seed extension with all heuristics), 1(global alignment),\n");
		fprintf(stderr, "                     2(local alignment) [%d],\n\n", opt->dp_type);
		fprintf(stderr, "       -o            Use SSE2 optimized local alignment or banded BWA-MEM seed extension depending upon \"-e\" option.\n");
		fprintf(stderr, "                     Global alignment is not optimized.\n\n");
		fprintf(stderr, "       -g            Use BWA-MEM like reseeding with all-SMEM. For now reseeding is only available with all-SMEM\n");
		fprintf(stderr, "       -F            Use Shifted Hamming distance seed filter,\n");
		fprintf(stderr, "       -f STR        Use STR as the name of the file to append the execution time,[%s]\n", "run_exec_time.txt");
		fprintf(stderr, "       -l INT        Must specify a read length for reporting execution time[%d]\n", 0);
		fprintf(stderr, "       -M            mark shorter split hits as secondary\n\n");
		//fprintf(stderr, "       -I FLOAT[,FLOAT[,INT[,INT]]]\n");
		//fprintf(stderr, "                     specify the mean, standard deviation (10%% of the mean if absent), max\n");
		//fprintf(stderr, "                     (4 sigma from the mean if absent) and min of the insert size distribution.\n");
		//fprintf(stderr, "                     FR orientation only. [inferred]\n");
		fprintf(stderr, "\n");
		//fprintf(stderr, "Note: Please read the man page for detailed description of the command line and options.\n");
		fprintf(stderr, "\n");
		free(opt);
		return 1;
	}

	if (mode) {
		if (strcmp(mode, "intractg") == 0) {
			if (!opt0.o_del) opt->o_del = 16;
			if (!opt0.o_ins) opt->o_ins = 16;
			if (!opt0.b) opt->b = 9;
			if (!opt0.pen_clip5) opt->pen_clip5 = 5;
			if (!opt0.pen_clip3) opt->pen_clip3 = 5;
		} else if (strcmp(mode, "pacbio") == 0 || strcmp(mode, "pbref") == 0 || strcmp(mode, "ont2d") == 0) {
			if (!opt0.o_del) opt->o_del = 1;
			if (!opt0.e_del) opt->e_del = 1;
			if (!opt0.o_ins) opt->o_ins = 1;
			if (!opt0.e_ins) opt->e_ins = 1;
			if (!opt0.b) opt->b = 1;
			if (opt0.split_factor == 0.) opt->split_factor = 10.;
			if (strcmp(mode, "ont2d") == 0) {
				if (!opt0.min_chain_weight) opt->min_chain_weight = 20;
				if (!opt0.min_seed_len) opt->min_seed_len = 14;
				if (!opt0.pen_clip5) opt->pen_clip5 = 0;
				if (!opt0.pen_clip3) opt->pen_clip3 = 0;
			} else {
				if (!opt0.min_chain_weight) opt->min_chain_weight = 40;
				if (!opt0.min_seed_len) opt->min_seed_len = 17;
				if (!opt0.pen_clip5) opt->pen_clip5 = 0;
				if (!opt0.pen_clip3) opt->pen_clip3 = 0;
			}
		} else {
			fprintf(stderr, "[E::%s] unknown read type '%s'\n", __func__, mode);
			return 1; // FIXME memory leak
		}
	} else update_a(opt, &opt0);
	bwa_fill_scmat(opt->a, opt->b, opt->mat);

	aux.idx = bwa_idx_load_from_shm(argv[optind]);
	if (aux.idx == 0) {
		if ((aux.idx = bwa_idx_load(argv[optind], BWA_IDX_ALL)) == 0) return 1; // FIXME: memory leak
	} else if (bwa_verbose >= 3)
		fprintf(stderr, "[M::%s] load the bwa index from shared memory\n", __func__);
	if (ignore_alt)
		for (i = 0; i < aux.idx->bns->n_seqs; ++i)
			aux.idx->bns->anns[i].is_alt = 0;

	ko = kopen(argv[optind + 1], &fd);
	if (ko == 0) {
		if (bwa_verbose >= 1) fprintf(stderr, "[E::%s] fail to open file `%s'.\n", __func__, argv[optind + 1]);
		return 1;
	}
	fp = gzdopen(fd, "r");
	aux.ks = kseq_init(fp);
	if (optind + 2 < argc) {
		if (opt->flag&MEM_F_PE) {
			if (bwa_verbose >= 2)
				fprintf(stderr, "[W::%s] when '-p' is in use, the second query file is ignored.\n", __func__);
		} else {
			ko2 = kopen(argv[optind + 2], &fd2);
			if (ko2 == 0) {
				if (bwa_verbose >= 1) fprintf(stderr, "[E::%s] fail to open file `%s'.\n", __func__, argv[optind + 2]);
				return 1;
			}
			fp2 = gzdopen(fd2, "r");
			aux.ks2 = kseq_init(fp2);
			opt->flag |= MEM_F_PE;
		}
	}
	f_exec_time = fopen(run_exec_time, "a");
	//The columns in the tab seperated run_exec_time are:
	// 1- read length
	//	2- seed type
	//	3- min. seed length
	//	4- seed intv
	//	5- DP algo
	//	6- execution time in seconds
	//There is no new line at the end so more values may be printed on the same line, if desired.
	fprintf(f_exec_time,"%d\t%d\t%d\t%d\t%d\t",opt->read_len, opt->seed_type, opt->min_seed_len, opt->seed_intv, opt->dp_type);
	bwa_print_sam_hdr(aux.idx->bns, hdr_line);
	aux.actual_chunk_size = fixed_chunk_size > 0? fixed_chunk_size : opt->chunk_size * opt->n_threads;
	kt_pipeline(no_mt_io? 1 : 2, process, &aux, 3);
	free(hdr_line);
	free(opt);
	bwa_idx_destroy(aux.idx);
	kseq_destroy(aux.ks);
	err_gzclose(fp); kclose(ko);
	if (aux.ks2) {
		kseq_destroy(aux.ks2);
		err_gzclose(fp2); kclose(ko2);
	}
	return 0;
}

int main_fastmap(int argc, char *argv[])
{
	int c, i, print_seq = 0;
	uint64_t max_intv = 0;
	kseq_t *seq;
	bwtint_t k;
	gzFile fp;
	smem_aux_t *a;
	bwaidx_t *idx;
	mem_opt_t *opt;
	opt = mem_opt_init();

	while ((c = getopt(argc, argv, "k:r:y:s:c:u:J:gp")) >= 0) {
		switch (c) {
			case 'k': opt->min_seed_len = atoi(optarg); break;
			case 'r': opt->split_factor = atoi(optarg); break;
			case 'y': opt->max_mem_intv = atoi(optarg); break;
			case 's': opt->split_width = atoi(optarg); break;
			case 'c': opt->max_occ = atoi(optarg); break;
			case 'u': opt->seed_type = atoi(optarg); break;
			case 'J': opt->seed_intv = atoi(optarg); break;
			case 'g': opt->re_seed = 1; break;
			case 'p': print_seq = 1; break;
		   default: return 1;
		}
	}
	if (optind + 1 >= argc || optind + 3 < argc) {
	      fprintf(stderr, "\n");
	      fprintf(stderr, "Usage: gase fastmap [options] <ref.fasta> <reads.fq> \n\n");
	      fprintf(stderr, "Algorithm options:\n\n");
	      fprintf(stderr, "       -k INT        minimum seed length. For fixed length seeds seed_length = INT [%d]\n", opt->min_seed_len);
	      fprintf(stderr, "       -r FLOAT      look for internal seeds inside a seed longer than {-k} * FLOAT (ONLY FOR ALL SMEM RE-SEEDING) [%g]\n", opt->split_factor);
	      fprintf(stderr, "       -y INT        seed occurrence for the 3rd round seeding (ONLY FOR ALL SMEM RE-SEEDING) [%ld]\n", (long)opt->max_mem_intv);
	      fprintf(stderr, "       -s INT        look for internal seeds inside a seed with less than INT occ (ONLY FOR ALL SMEM RE-SEEDING)[%d]\n", opt->split_width);
	      fprintf(stderr, "       -c INT        skip seeds with more than INT occurrences [%d]\n", opt->max_occ);
	      fprintf(stderr, "       -u INT        Seed type. Possible options: 1(all-SMEM), 2(fixed length seeds with no mismatch),\n");
	      fprintf(stderr, "                     3(nov-SMEM), 4(fixed length seeds with at most 1 mismatch) [%d],\n\n", opt->seed_type);
	      fprintf(stderr, "       -J INT        Seed interval for fixed length seeds [%d]\n\n", opt->seed_intv);
	      fprintf(stderr, "       -g            Use BWA-MEM like reseeding with all-SMEM. For now reseeding is only available with all-SMEM\n");
	      fprintf(stderr, "       -p            Print the read ");
	      //fprintf(stderr, "       -I FLOAT[,FLOAT[,INT[,INT]]]\n");
	      //fprintf(stderr, "                     specify the mean, standard deviation (10%% of the mean if absent), max\n");
	      //fprintf(stderr, "                     (4 sigma from the mean if absent) and min of the insert size distribution.\n");
	      //fprintf(stderr, "                     FR orientation only. [inferred]\n");
	      fprintf(stderr, "\n");
	      //fprintf(stderr, "Note: Please read the man page for detailed description of the command line and options.\n");
	      fprintf(stderr, "\n");
	      free(opt);
	      return 1;
	   }

	fp = xzopen(argv[optind + 1], "r");
	seq = kseq_init(fp);
	if ((idx = bwa_idx_load(argv[optind], BWA_IDX_BWT|BWA_IDX_BNS)) == 0) return 1;
	a = smem_aux_init();
	//smem_config(itr, min_intv, max_len, max_intv);
	while (kseq_read(seq) >= 0) {
	   err_printf("SQ\t%s\t%ld", seq->name.s, seq->seq.l);
	   if (print_seq) {
	      err_putchar('\t');
	      err_puts(seq->seq.s);
	   } else err_putchar('\n');
	   for (i = 0; i < seq->seq.l; ++i)
	      seq->seq.s[i] = nst_nt4_table[(int)seq->seq.s[i]];
	   //smem_set_query(itr, seq->seq.l, (uint8_t*)seq->seq.s);
	   mem_collect_intv(opt, idx->bwt, seq->seq.l, (uint8_t*)seq->seq.s, a);
	   for (i = 0; i < a->mem.n; ++i) {
	      bwtintv_t *p = &a->mem.a[i];
	      if ((uint32_t)p->info - (p->info>>32) < opt->min_seed_len) continue;
	      err_printf("EM\t%d\t%d\t%ld", (uint32_t)(p->info>>32), (uint32_t)p->info, (long)p->x[2]);
	      if (p->x[2] <= opt->max_occ) {
	         for (k = 0; k < p->x[2]; ++k) {
	            bwtint_t pos;
	            int len, is_rev, ref_id;
	            len  = (uint32_t)p->info - (p->info>>32);
	            pos = bns_depos(idx->bns, bwt_sa(idx->bwt, p->x[0] + k), &is_rev);
	            if (is_rev) pos -= len - 1;
	            bns_cnt_ambi(idx->bns, pos, len, &ref_id);
	            err_printf("\t%s:%c%ld", idx->bns->anns[ref_id].name, "+-"[is_rev], (long)(pos - idx->bns->anns[ref_id].offset) + 1);
	         }
	      } else err_puts("\t*");
	      err_putchar('\n');
	   }
	   err_puts("//");
	}

	smem_aux_destroy(a);
	bwa_idx_destroy(idx);
	kseq_destroy(seq);
	err_gzclose(fp);
	return 0;
}
