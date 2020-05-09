﻿#include "decoder.h"
#include <stdio.h>
#include <time.h>

int main(int argc, char** argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s [*.mp3]\n", *argv);
		return 0;
	}

	struct decoder_handle* decoder = decoder_Init(argv[1], OUTPUT_AUDIO, "1.wav");

	if (!decoder) {
		LOG_E("decoder_Init", "failed!");
		return -1;
	}

	clock_t s = clock();
	unsigned frame_count = decoder_Run(decoder);
	decoder_Release(&decoder);
	if (frame_count) {
		printf("\ntime: %.3lfsecs", ((double)clock() - s) / CLOCKS_PER_SEC);
		printf("\nframe count: %u\n", frame_count);
	}

	getchar();

	return 0;
}
