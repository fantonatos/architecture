/* Copyright 2021 Fotis Antonatos. See LICENSE */

/* 
 * Direct mapped, set associative, and fully associative cache simulation.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

typedef struct {
	unsigned hits, accesses, kb;
	union { unsigned ways; bool pseudo_lru;};
	enum {OPTION_NONE, OPTION_WRITE_ON_MISS, OPTION_PREFETCH_ALWAYS, OPTION_PREFETCH_ON_MISS} options;
	pthread_t thread;
} ThreadInfo;

typedef struct {
	enum {LOAD, STORE} op;
	uint32_t addr;
} Trace;

struct set { 
	uint64_t tags[16];
	uint64_t lru[16];
	bool valid[16]; 
};

Trace     g_traces[15000000];
unsigned  g_traces_amt = 0;
const uint64_t block_id_offset = 5;

static const uint16_t
mylog2(unsigned n)
{
	unsigned exp = 0;

	while (n >>= 1)
		exp++;

	return exp;
}

static const uint64_t
bitmask(unsigned range)
{
	return ~(UINT32_MAX << mylog2(range));
}

static void
sim_set_associative_insert_tag(struct set *s, uint64_t tag, int ways)
{
	/* Find an empty block to insert the new tag.
	   If we cannot find one, overwrite the block with
	   the highest counter (least recently used). */
	unsigned lru_way = 0, lru_largest = 0;
	for (int w = 0; w < ways; w++) {
		if (s->lru[w] > lru_largest) {
			lru_largest = s->lru[w];
			lru_way = w;
		}

		if (s->valid[w] == false) {
			s->tags[w] = tag;
			s->lru[w] = 0;
			s->valid[w] = true;
			return;
		}
	}

	/* Failed to place tag in an empty block;
	   Overwrite the block at w. */
	s->tags[lru_way] = tag;
	s->lru[lru_way] = 0;
	s->valid[lru_way] = true;
}

/* Returns HIT (true) or MISS (false) */
static bool
sim_set_associative_do(struct set *s, uint64_t tag, int ways, bool no_modify)
{
	bool hit = false;
	/* Check all tags for a hit.
	   Increment LRU counters for all tags. */
	for (int w = 0; w < ways; w++) {
		s->lru[w]++;

		if (s->tags[w] == tag) {
			hit = true;
			s->lru[w] = 0;
		}
	}

	if (!hit && !no_modify)
		sim_set_associative_insert_tag(s, tag, ways);

	return hit;
}

static void *
sim_set_associative(void *arg)
{
	/* This algorithm will not scale ad infinitum due to how
	   the LRU tag is maintained. Although unlikely,
	   certain memory-access patterns, and a long enough trace,
	   may cause the counters overflow. */
	struct set cache[1024] = {{{0}, {0}, {false}}};

	ThreadInfo *i = arg;
	const uint64_t sets = (16 * 1024) / (32 * i->ways);
	const uint64_t log = mylog2(sets);
	const uint64_t mask = (i->ways == 1) ? bitmask(i->kb * 1024 / 32) : bitmask(sets);
	i->accesses = g_traces_amt;
	for (int ti = 0; ti < g_traces_amt; ti++) {
		uint64_t set, addr, tag;
		bool hit = false;

		set = ((addr = g_traces[ti].addr) >> block_id_offset) & mask;
		tag = addr >> (block_id_offset + log);

		if (i->ways == 1) { // Running in 1-way associative mode (aka direct mapping)
			tag = addr >> 10;

			if (cache[set].tags[0] == tag)
				i->hits++;
			else 
				cache[set].tags[0] = tag;

		} else {
			i->hits += (hit = sim_set_associative_do(
			            &cache[set], tag, i->ways,
			            i->options == OPTION_WRITE_ON_MISS &&
			            g_traces[ti].op == STORE));
			if (i->options == OPTION_PREFETCH_ALWAYS ||
			    (i->options == OPTION_PREFETCH_ON_MISS && !hit)) {
				
				set = ((addr + 32) >> block_id_offset) & mask;
				tag = (addr + 32) >> (block_id_offset + mylog2(sets));
				sim_set_associative_do(&cache[set], tag, i->ways, false);
			}
		}
		
	}

	return NULL;
}

static void *
sim_fully_associative(void *arg)
{
	/* We are running this with 32 bytes line size
	   and 16 KB total cache size.
	   512 blocks * 32 bytes = 16KB cache */
	struct block {
		uint64_t tag;
		uint64_t lru;
	} cache[512] = {{0}, {0}};

	ThreadInfo *i = arg;

	i->accesses = g_traces_amt;
	for (int ti = 0; ti < g_traces_amt; ti++) {
		uint64_t addr, tag;
		bool hit = false;

		tag = (addr = g_traces[ti].addr) >> block_id_offset;

		/* Search for tag in cache */
		for (int b = 0; b < 512; b += 8) {
			#define SIMFA_UNROLL1(N)                      \
			cache[((b) + (N))].lru++;                    \
			if (!hit && cache[((b) + (N))].tag == tag) { \
				hit = true;                      \
				cache[((b) + (N))].lru = 0;          \
			}

			/* We are sacrificing everything here
			   for the sole purpose of speed... */
			SIMFA_UNROLL1(0);SIMFA_UNROLL1(1);
			SIMFA_UNROLL1(2);SIMFA_UNROLL1(3);
			SIMFA_UNROLL1(4);SIMFA_UNROLL1(5);
			SIMFA_UNROLL1(6);SIMFA_UNROLL1(7);
		}

		if (hit) {
			i->hits++;
		} else {
			bool hasempty = false;
			unsigned lru_block = 0, lru_largest = 0;
			for (int w = 0; w < 512; w += 8) {
				#define SIMFA_UNROLL2(N) \
				if (cache[w + N].lru > lru_largest) { \
					lru_largest = cache[w + N].lru; \
					lru_block = w + N; \
				} \
				if (cache[w + N].tag == 0) { \
					cache[w + N].tag = tag; \
					cache[w + N].lru = 0; \
					hasempty = true; \
					break; \
				}

				SIMFA_UNROLL2(0);SIMFA_UNROLL2(1);
				SIMFA_UNROLL2(2);SIMFA_UNROLL2(3);
				SIMFA_UNROLL2(4);SIMFA_UNROLL2(5);
				SIMFA_UNROLL2(6);SIMFA_UNROLL2(7);
			}

			if (!hasempty) {
				/* Failed to place tag in an empty block;
				   Overwrite the least recently used block. */
				cache[lru_block].tag = tag;
				cache[lru_block].lru = 0;
			}
		}

	}
	
	return NULL;
}

static void *
sim_fully_associative_pseudo(void *arg)
{
	/* We are running this with 32 bytes line size
	   and 16 KB total cache size.
	   512 blocks * 32 bytes = 16KB cache

	   Combining the LRU bits and the cache will allow us
	   to easily translate between a cache block, and it's
	   corresponding LRU bit.

	   It is possible that this approach worsens spatial locality.

	   511 table bytes  indexes 0 to 510     for LRU calculation
	   512 cache bytes  indexes 511 to 1023 for holding cached tags */
	uint64_t lru_cache[1024] = {0};

	ThreadInfo *i = arg;
	i->accesses = g_traces_amt;
	for (int ti = 0; ti < g_traces_amt; ti++) {
		uint64_t addr, tag;
		bool hit = false;

		tag = (addr = g_traces[ti].addr) >> block_id_offset;

		/* If we find a hit, update the path to the least recently used tag. */
		for (int block = 511; block < 1023; block++) {
			if (lru_cache[block] == tag) {
				hit = (i->hits++ - i->hits);

				do
					lru_cache[(block - 1) / 2] = (block % 2) ? 0 : 1;
				while ((block = (block - 1) / 2));
				break;
			}
		}

		/* Start at first LRU bit, 0.
		   Follow the 'coldest' path to find the least recently used tag.
		   tmp will be set the index of the least recently used tag. */
		int tmp = 0;
		if(!hit) {
			for (tmp; tmp < 511; tmp = 2 * tmp + (!(lru_cache[tmp] = !lru_cache[tmp]) ? 1 : 2));
			lru_cache[tmp] = tag;
		}
	}
	return NULL;
}

int
main(int argc, char *argv[])
{
	FILE *input, *output;
	uint32_t addr;
	char behavior;

	if (argc != 3)
		fprintf(stderr, "Usage: predictors input.txt output.txt\n"), exit(1);

	if (!(input = fopen(argv[1], "r")) || !(output = fopen(argv[2], "w")))
		fprintf(stderr, "Failed to open files.\n"), exit(1);

	/* Read trace into g_traces */
	while(fscanf(input, "%c %x\n", &behavior, &addr) != EOF)
		g_traces[g_traces_amt++] = (Trace) {behavior == 'L' ? LOAD : STORE, addr};
	fclose(input);
	
	/**
	 * threads[0] - direct
	 * threads[1] - set associative
	 * threads[2] - fully associative
	 */
	ThreadInfo threads[6][4];

	/* Run fully-associative caches asynchronously */
	threads[2][0] = (ThreadInfo) {0, 0};
	(void) pthread_create(&threads[2][0].thread, NULL,
	                      sim_fully_associative, (void *) &threads[2][0]);
	threads[2][1] = (ThreadInfo) {0, 0};
	(void) pthread_create(&threads[2][1].thread, NULL,
	                      sim_fully_associative_pseudo, (void *) &threads[2][1]);

	/* Simulate direct cache */
	for (int kb = 1; kb <= 32; kb *= 2) {
		if (kb == 2 || kb == 8)
			continue;

		ThreadInfo info = {0, 0, .kb = kb, .ways = 1};
		sim_set_associative(&info);
		fprintf(output, "%d,%d; ", info.hits, info.accesses);
	}
	fprintf(output, "\n");
	
	
	/* Set associative */
	for (int asc = 2; asc <= 16; asc *= 2) {
		ThreadInfo info = {0, 0, .ways = asc};
		sim_set_associative(&info);
		fprintf(output, "%d,%d; ", info.hits, info.accesses);
	}
	fprintf(output, "\n");

	/* Join the fully-associative threads */
	(void) pthread_join(threads[2][0].thread, NULL);
	(void) pthread_join(threads[2][1].thread, NULL);
	fprintf(output, "%d,%d;\n", threads[2][0].hits, threads[2][0].accesses);
	fprintf(output, "%d,%d;\n", threads[2][1].hits, threads[2][1].accesses);

	/* Set associative with Write on Miss */
	for (int asc = 2, i = 0; asc <= 16; asc *= 2, i++) {
		threads[3][i] = (ThreadInfo) {0, 0, .kb = 16, .ways = asc, .options = OPTION_WRITE_ON_MISS};
		(void) pthread_create(&threads[3][i].thread, NULL,
	                      sim_set_associative, (void *) &threads[3][i]);
	}

	/* Set associative with always prefetch */
	for (int asc = 2, i = 0; asc <= 16; asc *= 2, i++) {
		threads[4][i] = (ThreadInfo) {0, 0, .ways = asc, .options = OPTION_PREFETCH_ALWAYS};
		(void) pthread_create(&threads[4][i].thread, NULL,
	                      sim_set_associative, (void *) &threads[4][i]);
	}


	for (int i = 0; i < 4; i++) {
		(void) pthread_join(threads[3][i].thread, NULL);
		fprintf(output, "%d,%d; ", threads[3][i].hits, threads[3][i].accesses);
	}
	fprintf(output, "\n");

	for (int i = 0; i < 4; i++) {
		(void) pthread_join(threads[4][i].thread, NULL);
		fprintf(output, "%d,%d; ", threads[4][i].hits, threads[4][i].accesses);
	}
	fprintf(output, "\n");

	/* Set associative with prefetch on miss */
	for (int asc = 2; asc <= 16; asc *= 2) {
		ThreadInfo info = {0, 0, .ways = asc, .options = OPTION_PREFETCH_ON_MISS};
		sim_set_associative(&info);
		fprintf(output, "%d,%d; ", info.hits, info.accesses);
	}
	fprintf(output, "\n");

	fclose(output);
	return 0;
}
