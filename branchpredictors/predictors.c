/*
 * © 2021 Fotis Antonatos
 *
 * Copying and distribution of this software, in binary or source format,
 * and any accompanying files or documentation, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.  This software is offered as-is,
 * without any warranty.
 */

/*
 * This program simulates and measures the accuracy of branch prediction algorithms:
 *(sim_)always take
 *      always (not) take
 *      bimodal (with single-bit history)
 *      bimodal (two-bit history)
 *      gshare
 *      tournament
 * 
 * Table size for bimodal and GHR size for gshare vary.
 * Branch Target Buffer (using single-bit bimodal) is also tested.
 * 
 * main() reads the tracefile (provided via command-line args) and calls the predictors.
 * Predictors run on their own threads.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

#define STRONG_NO            0b00
#define WEAK_NO              0b01
#define WEAK_YES             0b10
#define STRONG_YES           0b11
#define PREFER_GSHARE        0b00
#define WEAK_PREFER_GSHARE   0b01
#define WEAK_PREFER_BIMODAL  0b10
#define PREFER_BIMODAL       0b11

/* correct       num of correctly predicted branches (passed as 0, set by the callee)
 * attempted     num of attempted branch target predictions
 * always_val    indicates to sim_always whether to always take the branch
 * table_size    specifies the branch prediction table size
 * history_size  specifies the global history register's number of bits
 */
typedef struct {
	unsigned correct, attempted;
	union
	{
		bool always_val;
		int table_size;
		int history_size;
	};
} TParams;

/* addr    the branch instruction's address
 * target  the target address
 * actual  whether the branch was actually taken
 */
struct pair {
	uint64_t addr, target;
	bool actual; 
};

struct pair *g_traces = NULL;
unsigned     g_traces_count = 0;

void *
sim_always(void *arg)
{
	TParams *p = arg;
	bool always_val = p->always_val;

	unsigned correct = 0;

	for (int i = 0; i < g_traces_count; i++) {
		struct pair branch = g_traces[i];
		if (branch.actual == always_val) correct++;
	}

	p->correct = correct;
	return NULL;
}

void *
sim_bimodal_one(void *arg) 
{
	TParams *p = arg;
	int table_size = p->table_size;
	bool hist[table_size];
	unsigned index, correct = 0;

	/* Only allowed on arrays of bytes! */
	(void) memset(&hist, true, table_size);

	for (int i = 0; i < g_traces_count; i++) {
		struct pair branch = g_traces[i];
		index = branch.addr % table_size;

		if (hist[index] == branch.actual) correct++;
		hist[index] = branch.actual;
	}

	p->correct = correct;
	return NULL;
}

void *
sim_bimodal_two(void *arg)
{
	TParams *p = arg;
	int table_size = p->table_size;
	unsigned char hist[table_size]; /* Initial configuration: Strong Yes */
	unsigned index, correct = 0;

	(void) memset(&hist, STRONG_YES, table_size);

	for (int i = 0; i < g_traces_count; i++) {
		struct pair branch = g_traces[i];
		index = branch.addr % table_size;

		if ((hist[index] >= WEAK_YES) == branch.actual) correct++;

		if (branch.actual && hist[index] <= WEAK_YES) hist[index]++;
		else if (!branch.actual && hist[index] >= WEAK_NO) hist[index]--;
	}

	p->correct = correct;
	return NULL;
}

void *
sim_gshare(void *arg)
{
	TParams *p = arg;
	int history_size = p->history_size;
	unsigned char hist[2048];
	unsigned index, correct = 0, ghr = 0;
	
	(void) memset(&hist, STRONG_YES, 2048);

	for (int i = 0; i < g_traces_count; i++) {
		struct pair branch = g_traces[i];
		index = (branch.addr % 2048) ^ ghr;

		if ((hist[index] >= WEAK_YES) == branch.actual) correct++;

		if (branch.actual && hist[index] <= WEAK_YES) hist[index]++;
		else if (!branch.actual && hist[index] >= WEAK_NO) hist[index]--;

		ghr = ((ghr << 1) + branch.actual) & ~(0xFFFF << history_size);
	}

	p->correct = correct;
	return NULL;
}

void *
sim_tournament(void *arg)
{
	TParams *p = arg;
	unsigned char gshare[2048], bimodal[2048], selector[2048];
	unsigned g, b, correct = 0, ghr = 0, history_size = 11;
	bool gshare_correct,    bimodal_correct,
	     gshare_prediction, bimodal_prediction;

	(void) memset(&gshare, STRONG_YES, 2048);
	(void) memset(&bimodal, STRONG_YES, 2048);
	(void) memset(&selector, PREFER_GSHARE, 2048);

	for (int i = 0; i < g_traces_count; i++) {
		struct pair branch = g_traces[i];
		g = (branch.addr % 2048) ^ ghr;
		b = (branch.addr % 2048);

		/* Predict gshare and bimodal */
		gshare_correct = (gshare_prediction = gshare[g] >= WEAK_YES) == branch.actual;
		bimodal_correct = (bimodal_prediction = bimodal[b] >= WEAK_YES) == branch.actual;

		/* Train gshare */
		if (branch.actual && gshare[g] <= WEAK_YES)
			gshare[g]++;
		else if (!branch.actual && gshare[g] >= WEAK_NO)
			gshare[g]--;

		ghr = ((ghr << 1) + branch.actual) & ~(0xFFFF << history_size);

		/* Train bimodal */
		if (branch.actual && bimodal[b] <= WEAK_YES)
			bimodal[b]++;
		else if (!branch.actual && bimodal[b] >= WEAK_NO)
			bimodal[b]--;

		/* Get our prediction via selector */
		if (selector[b] <= WEAK_PREFER_GSHARE && gshare_correct)
			correct++;
		else if (selector[b] >= WEAK_PREFER_BIMODAL && bimodal_correct)
			correct++;
		
		 if (bimodal_correct != gshare_correct) {
			if (bimodal_correct && selector[b] <= WEAK_PREFER_BIMODAL)
				selector[b]++;
			else if (gshare_correct && selector[b] >= WEAK_PREFER_GSHARE)
				selector[b]--;
		}
	}
	
	p->correct = correct;
	return NULL;
}

void *
sim_btb(void *arg)
{
	TParams *p = arg;
	/* Using singe bit bimodal with initial configuration 'Take' */
	bool     hist[512];
	uint64_t btb[512] = {0ULL};
	unsigned index, correct = 0, attempted = 0;

	(void) memset(&hist, true, sizeof(hist));

	for (int i = 0; i < g_traces_count; i++) {
		struct pair branch = g_traces[i];
		index = branch.addr % sizeof(hist);

		if (hist[index]) {
			attempted++;

			if (branch.target == btb[index])
				correct++;
		}

		hist[index] = branch.actual;

		if (branch.actual)
			btb[index] = branch.target;
	}

	p->attempted = attempted;
	p->correct = correct;
	return NULL;
}

int
main(int argc, char *argv[])
{
	if (argc != 3)
		fprintf(stderr, "Usage: predictors input_trace.txt output.txt\n"), exit(1);

	unsigned long long addr, target;
	char behavior[10];

	FILE *input  = fopen(argv[1], "r"),
	     *output = fopen(argv[2], "w");

	if (!input || !output)
		fprintf(stderr, "Failed to open files.\n"), exit(1);

	/* Enough memory for 25mil lines of branch trace */
	g_traces = malloc(25000100 * sizeof(struct pair));

	while(fscanf(input, "%llx %10s %llx\n", &addr, behavior, &target) != EOF)
		g_traces[g_traces_count++] = (struct pair) {addr, target, (bool) !strncmp(behavior, "T", 2)};

	/* Arbitrarily picked 10 to prevent overflows... */
	pthread_t  t[7][10] = {0};
	TParams    p[7][10] = {0};

	for (int x = 0; x < 10; x++) {
		switch (x) {
		case 0: /* FALLTHROUGH */
		case 1:
			p[x][0] = (TParams) {.correct = 0, .always_val = !x};
			pthread_create(&t[x][0], NULL, &sim_always, (void *) &p[x][0]);
			break;
		case 2: /* FALLTHROUGH */
		case 3:
			for (int table_size = 16, i = 0; table_size <= 2048; table_size *= 2, i++) {
				if (table_size != 64) {
					p[x][i] = (TParams) {.correct = 0, .table_size = table_size};
					pthread_create(&t[x][i], NULL, x == 2 ? &sim_bimodal_one : &sim_bimodal_two, (void *) &p[x][i]); 
				}
			}
			break;
		case 4:
			for (int ghr_size = 3, i = 0; ghr_size <= 11; ghr_size++, i++) {
				p[4][i] = (TParams) {.correct = 0, .history_size = ghr_size};
				pthread_create(&t[4][i], NULL, &sim_gshare, (void *) &p[4][i]);
			}
			break;
		case 5: /* FALLTHROUGH */
		case 6:
			p[x][0] = (TParams) {.correct = 0, .attempted = 0};
			pthread_create(&t[x][0], NULL, x == 5 ? &sim_tournament : &sim_btb, (void *) &p[x][0]);
			break;
		default: /* DO NOTHING CASE */
			break;
		}
	}

	/****** JOIN THREADS ******/
	pthread_join(t[0][0], NULL);
	fprintf(output, "%d,%d;\n", p[0][0].correct, g_traces_count);

	pthread_join(t[1][0], NULL);
	fprintf(output, "%d,%d;\n", p[1][0].correct, g_traces_count);
	
	for (int table_size = 16, i = 0; table_size <= 2048; table_size *= 2, i++) {
		if (table_size != 64) {
			pthread_join(t[2][i], NULL);
			fprintf(output, "%d,%d; ", p[2][i].correct, g_traces_count);
		}
	}
	fprintf(output, "\n");
	
	for (int table_size = 16, i = 0; table_size <= 2048; table_size *= 2, i++) {
		if (table_size != 64) {
			pthread_join(t[3][i], NULL);
			fprintf(output, "%d,%d; ", p[3][i].correct, g_traces_count);
		}
	}
	fprintf(output, "\n");

	for (int ghr_size = 3, i = 0; ghr_size <= 11; ghr_size++, i++) {
		pthread_join(t[4][i], NULL);
		fprintf(output, "%d,%d; ", p[4][i].correct, g_traces_count);
	}

	pthread_join(t[5][0], NULL);
	fprintf(output, "\n%d,%d;", p[5][0].correct, g_traces_count);

	pthread_join(t[6][0], NULL);
	fprintf(output, "\n%d,%d;\n", p[6][0].correct, p[6][0].attempted);
	
	free(g_traces);
	fclose(input);
	fclose(output);
	return 0;
}
