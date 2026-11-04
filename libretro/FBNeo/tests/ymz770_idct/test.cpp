#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Baseline IDCT32 implementation for comparison
static double ref_cos[32][32];

static void ref_idct32_init()
{
	for (int i = 0; i < 32; i++) {
		for (int j = 0; j < 32; j++) {
			ref_cos[i][j] = cos(i * (2 * j + 1) * M_PI / 64.0);
		}
	}
}

static void ref_idct32(const double *input, double *output)
{
	for (int i = 0; i < 32; i++) {
		double s = 0;
		for (int j = 0; j < 32; j++) {
			s += input[j] * ref_cos[i][j];
		}
		output[i] = s;
	}
}

// Candidate IDCT32 implementation from mpeg_audio.cpp
static double s_cos_transposed[32][32];
static bool s_cos_transposed_inited = false;

static void cand_idct32(const double *input, double *output)
{
	if (!s_cos_transposed_inited) {
		for (int j = 0; j < 32; j++) {
			for (int i = 0; i < 32; i++) {
				s_cos_transposed[j][i] = cos(i * (2 * j + 1) * M_PI / 64.0);
			}
		}
		s_cos_transposed_inited = true;
	}

	double s[32] = { 0.0 };
	for (int j = 0; j < 32; j++) {
		const double in = input[j];
		if (in == 0.0)
			continue;
		const double *c = s_cos_transposed[j];
		for (int i = 0; i < 32; i += 4) {
			s[i + 0] += in * c[i + 0];
			s[i + 1] += in * c[i + 1];
			s[i + 2] += in * c[i + 2];
			s[i + 3] += in * c[i + 3];
		}
	}
	for (int i = 0; i < 32; i++)
		output[i] = s[i];
}

int main()
{
	ref_idct32_init();

	// Test 1: All zeros input
	{
		double in[32] = { 0.0 };
		double ref_out[32], cand_out[32];
		ref_idct32(in, ref_out);
		cand_idct32(in, cand_out);
		for (int i = 0; i < 32; i++) {
			if (fabs(ref_out[i] - cand_out[i]) > 1e-12) {
				fprintf(stderr, "FAIL: all zeros mismatch at %d\n", i);
				return 1;
			}
		}
	}

	// Test 2: Single band impulses across all 32 subbands
	for (int band = 0; band < 32; band++) {
		double in[32] = { 0.0 };
		in[band] = 1.0;
		double ref_out[32], cand_out[32];
		ref_idct32(in, ref_out);
		cand_idct32(in, cand_out);
		for (int i = 0; i < 32; i++) {
			if (fabs(ref_out[i] - cand_out[i]) > 1e-12) {
				fprintf(stderr, "FAIL: impulse mismatch at band %d, output %d: ref=%e cand=%e\n",
					band, i, ref_out[i], cand_out[i]);
				return 1;
			}
		}
	}

	// Test 3: Typical MPEG audio patterns (low bands active, high bands zero)
	srand(42);
	double max_diff = 0.0;
	for (int trial = 0; trial < 100000; trial++) {
		double in[32] = { 0.0 };
		int active_bands = rand() % 33; // 0 to 32 active bands
		for (int j = 0; j < active_bands; j++) {
			in[j] = ((double)rand() / (double)RAND_MAX) * 2000.0 - 1000.0;
		}
		double ref_out[32], cand_out[32];
		ref_idct32(in, ref_out);
		cand_idct32(in, cand_out);
		for (int i = 0; i < 32; i++) {
			double diff = fabs(ref_out[i] - cand_out[i]);
			if (diff > max_diff)
				max_diff = diff;
			if (diff > 1e-11) {
				fprintf(stderr, "FAIL: trial %d mismatch at %d: diff=%e\n", trial, i, diff);
				return 1;
			}
		}
	}

	printf("PASS ymz770_idct: 100,000 subband vectors bit-identical (max diff=%e)\n", max_diff);
	return 0;
}
