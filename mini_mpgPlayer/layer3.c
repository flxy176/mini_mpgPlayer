#include "layer3.h"
#include "huffman.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define	SBLIMIT	32
#define SSLIMIT	18

#define	PI		3.14159265358979323846

// scalefactor bit lengths
static const unsigned char sflen_table[2][16] = {
	{ 0, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4 },
	{ 0, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 2, 3 }
};

// MPEG-1.0 scalefactor bands index
// __sfb_index_long[sampling_frequency]
static const unsigned short __sfb_index_long[3][23] = {
	{ 0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 52, 62, 74, 90, 110, 134, 162, 196, 238, 288, 342, 418, 576 },	// 44.1kHz
	{ 0, 4, 8, 12, 16, 20, 24, 30, 36, 42, 50, 60, 72, 88, 106, 128, 156, 190, 230, 276, 330, 384, 576 },	// 48kHz
	{ 0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 54, 66, 82, 102, 126, 156, 194, 240, 296, 364, 448, 550, 576 }	// 32kHz
};
// __sfb_index_short[sampling_frequency]
static const unsigned short __sfb_index_short[3][14] = {
	{ 0, 4, 8, 12, 16, 22, 30, 40, 52, 66, 84, 106, 136, 192 },	// 44.1kHz
	{ 0, 4, 8, 12, 16, 22, 28, 38, 50, 64, 80, 100, 126, 192 },	// 48kHz
	{ 0, 4, 8, 12, 16, 22, 30, 42, 58, 78, 104, 138, 180, 192 }	// 32kHz
};

// MPEG-1.0 scalefactor band widths
// __sfb_width_long[sampling_frequency]
static const unsigned short __sfb_width_long[3][22] = {
	{ 4, 4, 4, 4, 4, 4, 6, 6, 8, 8, 10, 12, 16, 20, 24, 28, 34, 42, 50, 54, 76, 158 },	// 44.1kHz
	{ 4, 4, 4, 4, 4, 4, 6, 6, 6, 8, 10, 12, 16, 18, 22, 28, 34, 40, 46, 54, 54, 192 },	// 48kHz
	{ 4, 4, 4, 4, 4, 4, 6, 6, 8, 10, 12, 16, 20, 24, 30, 38, 46, 56, 68, 84, 102, 26 }	// 32kHz
};
// __sfb_width_short[sampling_frequency]
static const unsigned short __sfb_width_short[3][13] = {
	{ 4, 4, 4, 4, 6, 8, 10, 12, 14, 18, 22, 30, 56 },	// 44.1kHz
	{ 4, 4, 4, 4, 6, 6, 10, 12, 14, 16, 20, 26, 66 },	// 48kHz
	{ 4, 4, 4, 4, 6, 8, 12, 16, 20, 26, 34, 42, 12 }	// 32kHz
};

static struct {
	const unsigned short* index_long;
	const unsigned short* index_short;
	const unsigned short* width_long;
	const unsigned short* width_short;
} cur_sfb_table;

// scalefactor band preemphasis (used only when preflag is set)
static const unsigned char pretab[2][21] = {
	{ 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 2 }
};

static struct {
	unsigned char band_short[3];
	unsigned char band_long;
} rzero;

// gain_powreq[i] = i^(4/3)
static float gain_powreq[8207];

// gain_pow2_is[i] = 2^i
static float gain_pow2_is[256 + 118 + 4];

static float pre_block[2][32 * 18];
static float samples[2][32];

/*
coefficients for aliasing reduction

c[] = { -0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037}
cs[i] = 1 / sqrt(1 + c[i]^2)
ca[i] = c[i] / sqrt(1 + c[i]^2)
*/
static const float __c[] = { -0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037 };
static float cs[8], ca[8];

/*
IMDCT coefficients for short blocks and long blocks

imdct_s[i][k] = cos(PI * (2 * i + 7) * (2 * k + 1) / 24) = cos(PI * (i + 3.5) * (k + 0.5) / 12)
imdct_l[i][k] = cos(PI * (2 * i + 19) * (2 * k + 1) / 72) = cos(PI * (i + 9.5) * (k + 0.5) / 36)
*/
static float imdct_s[12][6];
static float imdct_l[36][18];

/*
windowing coefficients for long blocks

window_l[i] = sin(PI * (i + 1/2) / 36) = sin(PI * (2 * i + 1) / 72)
*/
static float window_l[36];

/*
windowing coefficients for short blocks

window_s[i] = sin(PI * (i + 1/2) / 12) = sin(PI * (2 * i + 1) / 24)
*/
static float window_s[12];

/*
coefficients for intensity stereo processing

is_ratio[i] = tan(i * (PI / 12))
is_table[i] = is_ratio[i] / (1 + is_ratio[i])
*/
static float is_table[7];

/*
coefficients Di for the synthesis window
*/
static float _D[] =
{
	0.000000000, -0.000015259, -0.000015259, -0.000015259,
	-0.000015259, -0.000015259, -0.000015259, -0.000030518,
	-0.000030518, -0.000030518, -0.000030518, -0.000045776,
	-0.000045776, -0.000061035, -0.000061035, -0.000076294,
	-0.000076294, -0.000091553, -0.000106812, -0.000106812,
	-0.000122070, -0.000137329, -0.000152588, -0.000167847,
	-0.000198364, -0.000213623, -0.000244141, -0.000259399,
	-0.000289917, -0.000320435, -0.000366211, -0.000396729,
	-0.000442505, -0.000473022, -0.000534058, -0.000579834,
	-0.000625610, -0.000686646, -0.000747681, -0.000808716,
	-0.000885010, -0.000961304, -0.001037598, -0.001113892,
	-0.001205444, -0.001296997, -0.001388550, -0.001480103,
	-0.001586914, -0.001693726, -0.001785278, -0.001907349,
	-0.002014160, -0.002120972, -0.002243042, -0.002349854,
	-0.002456665, -0.002578735, -0.002685547, -0.002792358,
	-0.002899170, -0.002990723, -0.003082275, -0.003173828,
	0.003250122, 0.003326416, 0.003387451, 0.003433228,
	0.003463745, 0.003479004, 0.003479004, 0.003463745,
	0.003417969, 0.003372192, 0.003280640, 0.003173828,
	0.003051758, 0.002883911, 0.002700806, 0.002487183,
	0.002227783, 0.001937866, 0.001617432, 0.001266479,
	0.000869751, 0.000442505, -0.000030518, -0.000549316,
	-0.001098633, -0.001693726, -0.002334595, -0.003005981,
	-0.003723145, -0.004486084, -0.005294800, -0.006118774,
	-0.007003784, -0.007919312, -0.008865356, -0.009841919,
	-0.010848999, -0.011886597, -0.012939453, -0.014022827,
	-0.015121460, -0.016235352, -0.017349243, -0.018463135,
	-0.019577026, -0.020690918, -0.021789551, -0.022857666,
	-0.023910522, -0.024932861, -0.025909424, -0.026840210,
	-0.027725220, -0.028533936, -0.029281616, -0.029937744,
	-0.030532837, -0.031005859, -0.031387329, -0.031661987,
	-0.031814575, -0.031845093, -0.031738281, -0.031478882,
	0.031082153, 0.030517578, 0.029785156, 0.028884888,
	0.027801514, 0.026535034, 0.025085449, 0.023422241,
	0.021575928, 0.019531250, 0.017257690, 0.014801025,
	0.012115479, 0.009231567, 0.006134033, 0.002822876,
	-0.000686646, -0.004394531, -0.008316040, -0.012420654,
	-0.016708374, -0.021179199, -0.025817871, -0.030609131,
	-0.035552979, -0.040634155, -0.045837402, -0.051132202,
	-0.056533813, -0.061996460, -0.067520142, -0.073059082,
	-0.078628540, -0.084182739, -0.089706421, -0.095169067,
	-0.100540161, -0.105819702, -0.110946655, -0.115921021,
	-0.120697021, -0.125259399, -0.129562378, -0.133590698,
	-0.137298584, -0.140670776, -0.143676758, -0.146255493,
	-0.148422241, -0.150115967, -0.151306152, -0.151962280,
	-0.152069092, -0.151596069, -0.150497437, -0.148773193,
	-0.146362305, -0.143264771, -0.139450073, -0.134887695,
	-0.129577637, -0.123474121, -0.116577148, -0.108856201,
	0.100311279, 0.090927124, 0.080688477, 0.069595337,
	0.057617187, 0.044784546, 0.031082153, 0.016510010,
	0.001068115, -0.015228271, -0.032379150, -0.050354004,
	-0.069168091, -0.088775635, -0.109161377, -0.130310059,
	-0.152206421, -0.174789429, -0.198059082, -0.221984863,
	-0.246505737, -0.271591187, -0.297210693, -0.323318481,
	-0.349868774, -0.376800537, -0.404083252, -0.431655884,
	-0.459472656, -0.487472534, -0.515609741, -0.543823242,
	-0.572036743, -0.600219727, -0.628295898, -0.656219482,
	-0.683914185, -0.711318970, -0.738372803, -0.765029907,
	-0.791213989, -0.816864014, -0.841949463, -0.866363525,
	-0.890090942, -0.913055420, -0.935195923, -0.956481934,
	-0.976852417, -0.996246338, -1.014617920, -1.031936646,
	-1.048156738, -1.063217163, -1.077117920, -1.089782715,
	-1.101211548, -1.111373901, -1.120223999, -1.127746582,
	-1.133926392, -1.138763428, -1.142211914, -1.144287109,
	1.144989014, 1.144287109, 1.142211914, 1.138763428,
	1.133926392, 1.127746582, 1.120223999, 1.111373901,
	1.101211548, 1.089782715, 1.077117920, 1.063217163,
	1.048156738, 1.031936646, 1.014617920, 0.996246338,
	0.976852417, 0.956481934, 0.935195923, 0.913055420,
	0.890090942, 0.866363525, 0.841949463, 0.816864014,
	0.791213989, 0.765029907, 0.738372803, 0.711318970,
	0.683914185, 0.656219482, 0.628295898, 0.600219727,
	0.572036743, 0.543823242, 0.515609741, 0.487472534,
	0.459472656, 0.431655884, 0.404083252, 0.376800537,
	0.349868774, 0.323318481, 0.297210693, 0.271591187,
	0.246505737, 0.221984863, 0.198059082, 0.174789429,
	0.152206421, 0.130310059, 0.109161377, 0.088775635,
	0.069168091, 0.050354004, 0.032379150, 0.015228271,
	-0.001068115, -0.016510010, -0.031082153, -0.044784546,
	-0.057617187, -0.069595337, -0.080688477, -0.090927124,
	0.100311279, 0.108856201, 0.116577148, 0.123474121,
	0.129577637, 0.134887695, 0.139450073, 0.143264771,
	0.146362305, 0.148773193, 0.150497437, 0.151596069,
	0.152069092, 0.151962280, 0.151306152, 0.150115967,
	0.148422241, 0.146255493, 0.143676758, 0.140670776,
	0.137298584, 0.133590698, 0.129562378, 0.125259399,
	0.120697021, 0.115921021, 0.110946655, 0.105819702,
	0.100540161, 0.095169067, 0.089706421, 0.084182739,
	0.078628540, 0.073059082, 0.067520142, 0.061996460,
	0.056533813, 0.051132202, 0.045837402, 0.040634155,
	0.035552979, 0.030609131, 0.025817871, 0.021179199,
	0.016708374, 0.012420654, 0.008316040, 0.004394531,
	0.000686646, -0.002822876, -0.006134033, -0.009231567,
	-0.012115479, -0.014801025, -0.017257690, -0.019531250,
	-0.021575928, -0.023422241, -0.025085449, -0.026535034,
	-0.027801514, -0.028884888, -0.029785156, -0.030517578,
	0.031082153, 0.031478882, 0.031738281, 0.031845093,
	0.031814575, 0.031661987, 0.031387329, 0.031005859,
	0.030532837, 0.029937744, 0.029281616, 0.028533936,
	0.027725220, 0.026840210, 0.025909424, 0.024932861,
	0.023910522, 0.022857666, 0.021789551, 0.020690918,
	0.019577026, 0.018463135, 0.017349243, 0.016235352,
	0.015121460, 0.014022827, 0.012939453, 0.011886597,
	0.010848999, 0.009841919, 0.008865356, 0.007919312,
	0.007003784, 0.006118774, 0.005294800, 0.004486084,
	0.003723145, 0.003005981, 0.002334595, 0.001693726,
	0.001098633, 0.000549316, 0.000030518, -0.000442505,
	-0.000869751, -0.001266479, -0.001617432, -0.001937866,
	-0.002227783, -0.002487183, -0.002700806, -0.002883911,
	-0.003051758, -0.003173828, -0.003280640, -0.003372192,
	-0.003417969, -0.003463745, -0.003479004, -0.003479004,
	-0.003463745, -0.003433228, -0.003387451, -0.003326416,
	0.003250122, 0.003173828, 0.003082275, 0.002990723,
	0.002899170, 0.002792358, 0.002685547, 0.002578735,
	0.002456665, 0.002349854, 0.002243042, 0.002120972,
	0.002014160, 0.001907349, 0.001785278, 0.001693726,
	0.001586914, 0.001480103, 0.001388550, 0.001296997,
	0.001205444, 0.001113892, 0.001037598, 0.000961304,
	0.000885010, 0.000808716, 0.000747681, 0.000686646,
	0.000625610, 0.000579834, 0.000534058, 0.000473022,
	0.000442505, 0.000396729, 0.000366211, 0.000320435,
	0.000289917, 0.000259399, 0.000244141, 0.000213623,
	0.000198364, 0.000167847, 0.000152588, 0.000137329,
	0.000122070, 0.000106812, 0.000106812, 0.000091553,
	0.000076294, 0.000076294, 0.000061035, 0.000061035,
	0.000045776, 0.000045776, 0.000030518, 0.000030518,
	0.000030518, 0.000030518, 0.000015259, 0.000015259,
	0.000015259, 0.000015259, 0.000015259, 0.000015259
};	// need init ( _D[i] *= 32768 )

/*
coefficients Nik for the synthesis window
*/
static float _N[64][32]; // need init ( _N[i][k] = cos((16+i)*(2*k+1)*PI/64) )
// static float _N[32][32]; // need init ( _N[i][k] = cos(i*(2*k+1)*PI/64) )

static float _V[2][1024];
static int _V_index[2];

float buf[2][32 * 18];

static int l3_decode_sideinfo(struct bitstream* decode_stream, struct l3_sideinfo* si, const int nch)
{
	int gr, ch;

	si->main_data_begin = bs_readBits(decode_stream, 9);
	si->private_bits = bs_readBits(decode_stream, nch == 1 ? 5 : 3);	// private_bits

	for (ch = 0; ch < nch; ++ch)
		si->scfsi[ch] = bs_readBits(decode_stream, 4);

	for (gr = 0; gr < 2; ++gr) {
		for (ch = 0; ch < nch; ++ch) {
			struct ch_info* cur_ch = &si->info_ch[ch][gr];

			cur_ch->part2_3_len = bs_readBits(decode_stream, 12);

			cur_ch->big_values = bs_readBits(decode_stream, 9);
			if (cur_ch->big_values > 288) {
				fprintf(stderr, "[W] big_values==%u too large!\n", cur_ch->big_values);
				cur_ch->big_values = 288;
			}

			cur_ch->global_gain = bs_readBits(decode_stream, 8);

			cur_ch->scalefac_compress = bs_readBits(decode_stream, 4);
			if (cur_ch->part2_3_len == 0) {
				if (cur_ch->scalefac_compress) {
					fprintf(stderr, "[W] scalefac_compress==%u when part2_3_len==0!\n", cur_ch->scalefac_compress);
					cur_ch->scalefac_compress = 0;
				}
			}

			cur_ch->win_switch_flag = bs_readBit(decode_stream);
			if (cur_ch->win_switch_flag == 1) {
				cur_ch->block_type = bs_readBits(decode_stream, 2);

				if (cur_ch->block_type == 0) {
					fprintf(stderr, "[E] block_type==0 when blocksplit_flag==1!\n");
					return -1;
				} else if (cur_ch->block_type == 2 && si->scfsi[ch]) {
					fprintf(stderr, "[E] block_type==2 when scfsi!=0!\n");
					return -1;
				}

				cur_ch->mixed_block_flag = bs_readBit(decode_stream);

				for (int region = 0; region < 2; ++region)
					cur_ch->table_select[region] = bs_readBits(decode_stream, 5);
				cur_ch->table_select[2] = 0;

				for (int window = 0; window < 3; ++window)
					cur_ch->subblock_gain[window] = bs_readBits(decode_stream, 3);

				if (cur_ch->block_type == 2 && cur_ch->mixed_block_flag == 0)
					cur_ch->region0_count = 8;
				else cur_ch->region0_count = 7;
				cur_ch->region1_count = 20 - cur_ch->region0_count;

				//cur_ch->region0_count = 36 >> 1;
				//cur_ch->region1_count = 576 >> 1;
			} else {
				for (int region = 0; region < 3; ++region)
					cur_ch->table_select[region] = bs_readBits(decode_stream, 5);

				//int r1 = bs_readBits(decode_stream, 4) + 1, r2 = bs_readBits(decode_stream, 3) + 1;
				//cur_ch->region0_count = cur_sfb_table.index_long[r1] >> 1;
				//if (r1 + r2 > 22)
				//	cur_ch->region1_count = 576 >> 1;
				//else cur_ch->region1_count = cur_sfb_table.index_long[r1 + r2] >> 1;
				cur_ch->region0_count = bs_readBits(decode_stream, 4);
				cur_ch->region1_count = bs_readBits(decode_stream, 3);

				cur_ch->block_type = 0;
				cur_ch->mixed_block_flag = 0;
			}
			cur_ch->preflag = bs_readBit(decode_stream);
			cur_ch->scalefac_scale = bs_readBit(decode_stream);
			cur_ch->count1table_select = bs_readBit(decode_stream);
		}
	}

	return 0;
}

static void l3_decode_scalefactors(struct bitstream* stream, struct l3_sideinfo* si, const int gr, const int ch)
{
	struct ch_info* cur_ch = &si->info_ch[ch][gr];
	const unsigned char slen0 = sflen_table[0][cur_ch->scalefac_compress];
	const unsigned char slen1 = sflen_table[1][cur_ch->scalefac_compress];
	int sfi;

	if (cur_ch->part2_3_len == 0) {
		for (sfi = 0; sfi < 21; ++sfi) {
			cur_ch->scalefac_l[sfi] = 0;
			cur_ch->scalefac_s[sfi] = 0;
		}
		for (sfi = 23; sfi < 36; ++sfi)
			cur_ch->scalefac_s[sfi];
		return;
	}

	if (cur_ch->win_switch_flag == 1 && cur_ch->block_type == 2) {
		if (cur_ch->mixed_block_flag == 1) {
			// MIXED block
			// cur_ch->part2_len = 17 * slen1 + 18 * slen0;
			for (sfi = 0; sfi < 8; ++sfi)
				cur_ch->scalefac_l[sfi] = bs_readBits(stream, slen0);

			for (sfi = 3; sfi < 12; ++sfi) {
				unsigned nbits = sfi < 6 ? slen0 : slen1;
				for (int win = 0; win < 3; ++win)
					cur_ch->scalefac_s[sfi * 3 + win] = bs_readBits(stream, nbits);
			}
		} else {
			// pure SHORT block
			// cur_ch->part2_len = 18 * (slen0 + slen1);
			for (sfi = 0; sfi < 12; ++sfi) {
				unsigned nbits = sfi < 6 ? slen0 : slen1;
				for (int win = 0; win < 3; ++win)
					cur_ch->scalefac_s[sfi * 3 + win] = bs_readBits(stream, nbits);
			}
		}
	} else {
		// LONG types 0,1,3

		/* Scale factor bands 0-5 */
		if (!(si->scfsi[ch] & 0x8) || !gr) {
			for (sfi = 0; sfi < 6; ++sfi)
				cur_ch->scalefac_l[sfi] = bs_readBits(stream, slen0);
		} else if ((si->scfsi[ch] & 0x8) && gr) {
			/* Copy scalefactors from granule 0 to granule 1 */
			for (sfi = 0; sfi < 6; ++sfi)
				cur_ch->scalefac_l[sfi] = si->info_ch[ch][0].scalefac_l[sfi];
		}

		/* Scale factor bands 6-10 */
		if (!(si->scfsi[ch] & 0x4) || !gr) {
			for (sfi = 6; sfi < 11; ++sfi) {
				cur_ch->scalefac_l[sfi] = bs_readBits(stream, slen0);
			}
		} else if ((si->scfsi[ch] & 0x4) && gr) {
			/* Copy scalefactors from granule 0 to granule 1 */
			for (sfi = 6; sfi < 11; ++sfi) {
				cur_ch->scalefac_l[sfi] = si->info_ch[ch][0].scalefac_l[sfi];
			}
		}

		/* Scale factor bands 11-15 */
		if (!(si->scfsi[ch] & 0x2) || !gr) {
			for (sfi = 11; sfi < 16; ++sfi) {
				cur_ch->scalefac_l[sfi] = bs_readBits(stream, slen1);
			}
		} else if ((si->scfsi[ch] & 0x2) && gr) {
			/* Copy scalefactors from granule 0 to granule 1 */
			for (sfi = 11; sfi < 16; ++sfi) {
				cur_ch->scalefac_l[sfi] = si->info_ch[ch][0].scalefac_l[sfi];
			}
		}

		/* Scale factor bands 16-20 */
		if (!(si->scfsi[ch] & 0x1) || !gr) {
			for (sfi = 16; sfi < 21; ++sfi) {
				cur_ch->scalefac_l[sfi] = bs_readBits(stream, slen1);
			}
		} else if ((si->scfsi[ch] & 0x1) && gr) {
			/* Copy scalefactors from granule 0 to granule 1 */
			for (sfi = 16; sfi < 21; ++sfi) {
				cur_ch->scalefac_l[sfi] = si->info_ch[ch][0].scalefac_l[sfi];
			}
		}
	}
}

static int _l3_huff_val[SBLIMIT * SSLIMIT];
static void l3_huffman_decode(struct bs* maindata_stream, struct ch_info *cur_ch, int gr, int ch, int bit_pos_end, float is[576])
{
	int region1_start, region2_start, is_pos, bv = cur_ch->big_values * 2;
	const struct huff_tab* htab;
	unsigned mask;
	int x, y, v, i, off = 0, tmp, idx = 0;

	if (cur_ch->part2_3_len == 0) {
		LOG_W("check_len", "part2_3_len == 0");
		for (is_pos = 0; is_pos < 576; ++is_pos)
			_l3_huff_val[is_pos] = 0;
		cur_ch->nonzero_len = 0;
		return;
	}

	//int num = (8 - maindata_stream->bit_pos) & 7;
	//mask = bs_readBits(maindata_stream, num) << 24;
	//mask <<= 8 - num;

	if (cur_ch->win_switch_flag && cur_ch->block_type == 2) {
		region1_start = 36;
		region2_start = 576;
	} else {
		region1_start = cur_sfb_table.index_long[cur_ch->region0_count + 1];
		region2_start = cur_sfb_table.index_long[cur_ch->region0_count + region1_start + 2];
	}

	// 解码大值区
	for (is_pos = 0; is_pos < bv; ++is_pos) {
		if (is_pos < region1_start) {
			htab = ht + cur_ch->table_select[0];
		} else if (is_pos < region2_start) {
			htab = ht + cur_ch->table_select[1];
		} else {
			htab = ht + cur_ch->table_select[2];
		}

		while ((y = htab->table[off++]) < 0) {
			if (bs_readBit(maindata_stream))
				off -= y;
		}

		x = y >> 4;
		y &= 0xf;

		if (x == 15 && htab->linbits) {
			x += bs_readBits(maindata_stream, htab->linbits);
			if (x > 0 && bs_readBit(maindata_stream))
				x = -x;
		} else if (x) {
			if (x > 0 && bs_readBit(maindata_stream))
				x = -x;
		}
		_l3_huff_val[is_pos++] = x;

		if (y == 15 && htab->linbits) {
			y += bs_readBits(maindata_stream, htab->linbits);
			if (y > 0 && bs_readBit(maindata_stream))
				y = -y;
		} else if (y) {
			if (y > 0 && bs_readBit(maindata_stream))
				y = -y;
		}
		_l3_huff_val[is_pos] = y;
	}

	htab = htc + cur_ch->count1table_select;

	for (off = 0; is_pos <= 572 && (maindata_stream->byte_ptr - maindata_stream->bit_buf) * 8 + maindata_stream->bit_pos <= bit_pos_end; ++is_pos) {
		while ((v = htab->table[off++]) < 0) {
			if (bs_readBit(maindata_stream))
				off -= v;
		}

		if (v & 0x8 && bs_readBit(maindata_stream))
			_l3_huff_val[is_pos++] = -1;
		else _l3_huff_val[is_pos++] = 0;

		if (is_pos >= 576)
			break;

		if (v & 0x4 && bs_readBit(maindata_stream))
			_l3_huff_val[is_pos++] = -1;
		else _l3_huff_val[is_pos++] = 0;

		if (is_pos >= 576)
			break;

		if (v & 0x2 && bs_readBit(maindata_stream))
			_l3_huff_val[is_pos++] = -1;
		else _l3_huff_val[is_pos++] = 0;

		if (is_pos >= 576)
			break;

		if (v & 0x1 && bs_readBit(maindata_stream))
			_l3_huff_val[is_pos] = -1;
		else _l3_huff_val[is_pos] = 0;
	}

	if ((maindata_stream->byte_ptr - maindata_stream->bit_buf) * 8 + maindata_stream->bit_pos > bit_pos_end + 1) {
		is_pos -= 4;
	}

	cur_ch->nonzero_len = is_pos;

	for (; is_pos < 576; ++is_pos) {
		_l3_huff_val[is_pos] = 0;
	}

	maindata_stream->bit_pos = bit_pos_end & 0x7;
	maindata_stream->byte_ptr = maindata_stream->bit_buf + (bit_pos_end >> 3);
}

//static int _l3_huff_val[SBLIMIT * SSLIMIT];
//static int l3_huffman_decode(struct bitstream* stream, struct l3_sideinfo* si, const int gr, const int ch)
//{
//	struct ch_info* cur_ch = &si->info_ch[ch][gr];
//	int region[4] = { 0 };
//	int part3_len = cur_ch->part2_3_len - cur_ch->part2_len;
//	int idx = 0;
//
//	if (cur_ch->part2_3_len == 0) {
//		for (; idx < 576; ++idx)
//			_l3_huff_val[idx] = 0;
//		return 0;
//	}
//
//	unsigned int mask;
//	int num = (8 - stream->bit_pos) & 7;
//	mask = bs_readBits(stream, num) << 24;
//	mask <<= 8 - num;
//	part3_len -= num;
//
//	{
//		int r1 = cur_ch->region0_count, r2 = cur_ch->region1_count, bv = cur_ch->big_values;
//		if (cur_ch->blocksplit_flag) {
//			r1 = 36;
//			r2 = 576;
//		} else {
//			r1 = cur_ch->region0_count + 1;
//			r2 = cur_ch->region1_count + 1 + r1;
//			if (r2 > 22) r2 = 22;
//			r1 = cur_sfb_table.index_long[r1];
//			r2 = cur_sfb_table.index_long[r2];
//		}
//
//		if (bv <= r1)
//			region[0] = region[1] = region[2] = bv;
//		else {
//			region[0] = r1;
//			if (bv <= r2)
//				region[1] = region[2] = bv;
//			else {
//				region[1] = r2;
//				region[2] = bv;
//			}
//		}
//	}
//
//	//int tmp, maxidx, idx = 0;
//	//const struct huff_tab* htab;
//
//	///*
//	// * 1. 初始化num,mask,part3len
//	// * mask: 暂存位流缓冲区不超过32比特数据,位流2级缓冲
//	// * num: mask剩余的比特数
//	// * part3len: 哈夫曼编码的主数据(main_data)的比特数
//	// */
//	//int part3_len = cur_ch->part2_3_len - cur_ch->part2_len;
//	//int num = (8 - stream->bit_pos) & 7;
//	//int mask = 0;
//	//short x, y;
//	//int i;
//
//	///*
//	// * 2. 使位流缓冲区按字节对齐
//	// */
//	//if (num > 0) {
//	//	mask = bs_readBits(stream, num);
//	//	mask <<= 32 - num;
//	//	part3_len -= num;
//	//}
//
//	///*
//	// * 3. 解码大值区
//	// */
//	//for (i = 0; i < 3; i++) {
//	//	maxidx = region[i];
//	//	htab = ht_bv + cur_ch->table_select[i];
//	//	while (idx < maxidx) {
//	//		if (part3_len + num <= 0) { //检测位流是否有错误
//	//			num -= part3_len + num;
//	//			break;
//	//		}
//
//	//		while (num < 24) { // refresh mask
//	//			mask |= bs_readBits(stream, 8) << (24 - num);
//	//			num += 8;
//	//			part3_len -= 8;
//	//		}
//	//		tmp = mask;
//	//		y = htab->tab[(tmp >> 30) & 3];
//	//		while (y < 0) {
//	//			tmp <<= 2;
//	//			y = htab->tab[((tmp >> 30) & 3) - y];
//	//		}
//	//		x = (y >> 8) & 0xff; // x暂存hlen
//	//		num -= x;
//	//		mask <<= x;
//
//	//		x = (y >> 4) & 0xf; // 解得x,y
//	//		y &= 0xf;
//
//	//		if (x != 0) {
//	//			if (x == 15 && htab->linebits != 0) {
//	//				while (num < 24) { // refresh mask
//	//					mask |= bs_readBits(stream, 8) << (24 - num);
//	//					num += 8;
//	//					part3_len -= 8;
//	//				}
//	//				//x += (mask >> (32 - htab->linebits))&; // 循环右移
//	//				x += (mask >> (32 - htab->linebits)) & (0xffffffffU >> (32 - htab->linebits));	// ????? 
//	//				num -= htab->linebits;
//	//				mask <<= htab->linebits;
//	//			}
//	//			_l3_huff_val[idx++] = (mask < 0) ? -x : x;
//	//			--num;
//	//			mask <<= 1;
//	//		} else
//	//			_l3_huff_val[idx++] = 0;
//
//	//		if (y != 0) {
//	//			if (y == 15 && htab->linebits != 0) {
//	//				while (num < 24) { // refresh mask
//	//					mask |= bs_readBits(stream, 8) << (24 - num);
//	//					num += 8;
//	//					part3_len -= 8;
//	//				}
//	//				// y += mask >> (32 - htab->linebits);
//	//				y += (mask >> (32 - htab->linebits)) & (0xffffffffU >> (32 - htab->linebits));	// ??????
//	//				num -= htab->linebits;
//	//				mask <<= htab->linebits;
//	//			}
//	//			_l3_huff_val[idx++] = (mask < 0) ? -y : y;
//	//			--num;
//	//			mask <<= 1;
//	//		} else
//	//			_l3_huff_val[idx++] = 0;
//	//	}
//	//}
//
//	///*
//	// * 4. 解码count1区
//	// */
//	//htab = ht_c + cur_ch->count1table_select;
//	//while (idx < 572) {
//	//	while (num < 10) { // 6(max hlen) + 4(signed bit)
//	//		mask |= bs_readBits(stream, 8) << (24 - num);
//	//		num += 8;
//	//		part3_len -= 8;
//	//	}
//	//	tmp = mask;
//	//	y = htab->tab[(tmp >> 28) & 0xf];
//	//	while (y < 0) {
//	//		tmp <<= 4;
//	//		y = htab->tab[((tmp >> 28) & 0xf) - y];
//	//	}
//
//	//	x = (y >> 8) & 0xff; // hlen
//	//	mask <<= x;
//	//	num -= x;
//
//	//	// 修改num后立即检测(当前粒度内的当前声道的)主数据是否处理完，使数据损坏的文件也能继续解码。
//	//	if (part3_len + num <= 0) {
//	//		num -= part3_len + num;
//	//		break;
//	//	}
//
//	//	// 一个码字(hcod)解码得到4个值
//	//	if (/*(y <<= 28) < 0*/ y & 0x8) {
//	//		_l3_huff_val[idx++] = mask < 0 ? -1 : 1;
//	//		num--;
//	//		mask <<= 1;
//	//	} else
//	//		_l3_huff_val[idx++] = 0;
//	//	if (/*(y <<= 1) < 0*/ y & 0x4) {
//	//		_l3_huff_val[idx++] = mask < 0 ? -1 : 1;
//	//		num--;
//	//		mask <<= 1;
//	//	} else
//	//		_l3_huff_val[idx++] = 0;
//	//	if (/*(y <<= 1) < 0*/ y & 0x2) {
//	//		_l3_huff_val[idx++] = mask < 0 ? -1 : 1;
//	//		num--;
//	//		mask <<= 1;
//	//	} else
//	//		_l3_huff_val[idx++] = 0;
//	//	if (/*(y <<= 1) < 0*/ y & 0x1) {
//	//		_l3_huff_val[idx++] = mask < 0 ? -1 : 1;
//	//		num--;
//	//		mask <<= 1;
//	//	} else
//	//		_l3_huff_val[idx++] = 0;
//	//}
//
//	//if (num > 0) // num位归还到位流缓冲区
//	//	bs_backBits(stream, num);
//
//	///*
//	// * 5. rzone区直接置0,即hv[nonzero..575]=0
//	// */
//	//cur_ch->nonzero_len = idx;
//	//for (; idx < 576; ++idx)
//	//	_l3_huff_val[idx] = 0;
//
//	///*
//	// * 6. 丢弃附属位(ancillary_bit)。附属位不超过多少位？
//	// */
//	//part3_len += num;
//	//if (part3_len > 0)		// 这还不一定是附属位，码流有错误也有可能出现这种情况
//	//	bs_skipBits(stream, part3_len);
//
//	//return cur_ch->nonzero_len;
//}

static void l3_requantize_samples(const struct mpeg_frame* frame, struct l3_sideinfo* si, const int gr, const int ch)
{
	struct ch_info* cur_ch = &si->info_ch[ch][gr];
	int shift = cur_ch->scalefac_scale + 1, sfb_idx = 0, hvidx = 0, sb_idx, val, xriStart = 0, xri, scf_idx = 0, pow2gain_idx = 255 - cur_ch->global_gain, width;
	float requVal;

	if (cur_ch->part2_3_len == 0) {
		for (; hvidx < 576; ++hvidx)
			cur_ch->buf[hvidx] = 0.0;
		return;
	}

	if (frame->is_MS)
		pow2gain_idx += 2;

	// pure SHORT blocks:
	// window_switching_flag=1, block_type=2, mixed_block_flag=0

	if (cur_ch->blocksplit_flag && cur_ch->block_type == 2) {
		rzero.band_short[0] = rzero.band_short[1] = rzero.band_short[2] = -1;
		if (cur_ch->mixed_block_flag) {
			/*
			 * 混合块:
			 * 混合块的前8个频带是长块。 前8块各用一个增益因子逆量化，这8个增益因子的频带总和为36，
			 * 这36条频率线用长块公式逆量化。
			 */
			rzero.band_long = -1;
			for (; sfb_idx < 8; ++sfb_idx) {
				requVal = gain_pow2_is[pow2gain_idx + ((si->scalefac_l[ch][sfb_idx] + pretab[cur_ch->preflag][sfb_idx]) << shift)];
				for (sb_idx = 0, width = cur_sfb_table.width_long[sfb_idx]; sb_idx < width; ++sb_idx, ++hvidx) {
					val = _l3_huff_val[hvidx]; // 哈夫曼值
					if (val < 0) {
						cur_ch->buf[hvidx] = -requVal * gain_powreq[-val];
						rzero.band_long = sfb_idx;
					} else if (val > 0) {
						cur_ch->buf[hvidx] = requVal * gain_powreq[val];
						rzero.band_long = sfb_idx;
					} else
						cur_ch->buf[hvidx] = 0;
				}
			}

			/*
			 * 混合块的后9个频带是被加窗的短块，其每一块同一窗口内3个值的增益因子频带相同。
			 * 后9块增益因子对应的频率子带值为widthShort[3..11]
			 */
			rzero.band_short[0] = rzero.band_short[1] = rzero.band_short[2] = 2;
			++rzero.band_long;
			scf_idx = 9;
			sfb_idx = 3;
			xriStart = 36; // 为短块重排序准备好下标
		}

		// 短块(混合块中的短块和纯短块)
		for (; hvidx < cur_ch->nonzero_len; ++sfb_idx) {
			width = cur_sfb_table.width_short[sfb_idx];
			for (int win = 0; win < 3; ++win) {
				requVal = gain_pow2_is[pow2gain_idx + cur_ch->subblock_gain[win] + (si->scalefac_s[ch][scf_idx++] << shift)];
				xri = xriStart + win;
				for (sb_idx = 0; sb_idx < width; ++sb_idx, ++hvidx, xri += 3) {
					val = _l3_huff_val[hvidx];
					if (val < 0) {
						cur_ch->buf[xri] = -requVal * gain_powreq[-val];
						rzero.band_short[win] = sfb_idx;
					} else if (val > 0) {
						cur_ch->buf[xri] = requVal * gain_powreq[val];
						rzero.band_short[win] = sfb_idx;
					} else
						cur_ch->buf[xri] = 0;
				}
			}
			xriStart = xri - 2;
		}
		++rzero.band_short[0];
		++rzero.band_short[1];
		++rzero.band_short[2];
		++rzero.band_long;
	} else {
		// 长块
		xri = -1;
		for (; hvidx < cur_ch->nonzero_len; ++sfb_idx) {
			requVal = gain_powreq[pow2gain_idx + ((si->scalefac_l[ch][sfb_idx] + pretab[cur_ch->preflag][sfb_idx]) << shift)];
			for (sb_idx = hvidx + cur_sfb_table.width_long[sfb_idx]; hvidx < sb_idx; ++hvidx) {
				val = _l3_huff_val[hvidx];
				if (val < 0) {
					cur_ch->buf[hvidx] = -requVal * gain_pow2_is[-val];
					xri = sfb_idx;
				} else if (val > 0) {
					cur_ch->buf[hvidx] = requVal * gain_pow2_is[val];
					xri = sfb_idx;
				} else
					cur_ch->buf[hvidx] = 0;
			}
		}
		rzero.band_long = xri + 1;
	}

	// 不逆量化0值区,置0.
	for (; hvidx < 576; ++hvidx)
		cur_ch->buf[hvidx] = 0;
}

static void l3_do_ms_stereo(struct l3_sideinfo* si, const int gr)
{
	int max_len = max(si->info_ch[0][gr].nonzero_len, si->info_ch[1][gr].nonzero_len);
	si->info_ch[0][gr].nonzero_len = si->info_ch[1][gr].nonzero_len = max_len;

	for (--max_len; max_len >= 0; --max_len) {
		const float v0 = si->info_ch[0][gr].buf[max_len];
		const float v1 = si->info_ch[1][gr].buf[max_len];
		si->info_ch[0][gr].buf[max_len] = v0 + v1;
		si->info_ch[1][gr].buf[max_len] = v0 - v1;
	}
}

static void l3_do_intesity_stereo(struct l3_sideinfo* si, const int gr)
{
	struct ch_info* right_ch = &si->info_ch[1][gr];
	int sfb, is_possb, idx, width;
	float v;

	if (right_ch->block_type == 2) {
		// MPEG-1, short block/mixed block
		for (int win = 0; win < 3; ++win) {
			for (sfb = rzero.band_short[win]; sfb < 12; ++sfb) { // 混合块sfb最小为3
				if ((is_possb = si->scalefac_s[1][sfb * 3 + win]) >= 7)
					continue;
				for (width = cur_sfb_table.width_short[sfb], idx = cur_sfb_table.index_short[sfb] * 3 + win; width > 0; --width, idx += 3) {
					v = si->info_ch[0][gr].buf[idx];
					si->info_ch[0][gr].buf[idx] = is_table[is_possb] * v;
					right_ch->buf[idx] = is_table[6 - is_possb] * v;
				}
			}
		}

		// TODO: need check l-part?
	} else {
		// MPEG-1, long block
		for (sfb = rzero.band_long; sfb < 22; ++sfb) {
			if ((is_possb = si->scalefac_l[1][sfb]) >= 7)
				continue;
			for (width = cur_sfb_table.width_long[sfb], idx = cur_sfb_table.index_long[sfb]; width > 0; --width, ++idx) {
				v = si->info_ch[0][gr].buf[idx];
				si->info_ch[0][gr].buf[idx] = is_table[is_possb] * v;
				right_ch->buf[idx] = is_table[6 - is_possb] * v;
			}
		}
	}
}

static void l3_do_stereo(const struct mpeg_frame* frame, struct l3_sideinfo* si, const int gr)
{
	if (frame->is_MS)
		l3_do_ms_stereo(si, gr);

	if (si->info_ch[0][gr].mixed_block_flag != si->info_ch[1][gr].mixed_block_flag || si->info_ch[0][gr].block_type != si->info_ch[1][gr].block_type) {
		fprintf(stderr, "[W] bad stereo!\n");
	} else if (frame->is_Intensity)
		l3_do_intesity_stereo(si, gr);
}

static void l3_antialias(struct ch_info* cur_ch)
{
	//int maxi;
	//float lo, hi;

	//if (cur_ch->block_type == 2) {
	//	if (cur_ch->mixed_block_flag == 0)
	//		return;
	//	maxi = 36;
	//} else
	//	maxi = 576;

	//for (int i = 18; i < maxi; i += 18) {
	//	for (int j = 0; j < 8; ++j) {
	//		lo = cur_ch->buf[i - 1 - j];
	//		hi = cur_ch->buf[i + j];
	//		cur_ch->buf[i - 1 - j] = lo * cs[j] - hi * ca[j];
	//		cur_ch->buf[i + j] = lo * ca[j] + hi * cs[j];
	//	}
	//}

	int i, maxi;
	float bu, bd;
	float* recv = cur_ch->buf;

	if (cur_ch->block_type == 2) {
		if (cur_ch->mixed_block_flag == 0)
			return;
		maxi = 18;
	} else
		maxi = cur_ch->nonzero_len - 18;

	for (i = 0; i < maxi; i += 18) {
		bu = recv[i + 17];
		bd = recv[i + 18];
		recv[i + 17] = bu * 0.85749293f + bd * 0.51449576f;
		recv[i + 18] = bd * 0.85749293f - bu * 0.51449576f;
		bu = recv[i + 16];
		bd = recv[i + 19];
		recv[i + 16] = bu * 0.8817420f + bd * 0.47173197f;
		recv[i + 19] = bd * 0.8817420f - bu * 0.47173197f;
		bu = recv[i + 15];
		bd = recv[i + 20];
		recv[i + 15] = bu * 0.94962865f + bd * 0.31337745f;
		recv[i + 20] = bd * 0.94962865f - bu * 0.31337745f;
		bu = recv[i + 14];
		bd = recv[i + 21];
		recv[i + 14] = bu * 0.98331459f + bd * 0.18191320f;
		recv[i + 21] = bd * 0.98331459f - bu * 0.18191320f;
		bu = recv[i + 13];
		bd = recv[i + 22];
		recv[i + 13] = bu * 0.99551782f + bd * 0.09457419f;
		recv[i + 22] = bd * 0.99551782f - bu * 0.09457419f;
		bu = recv[i + 12];
		bd = recv[i + 23];
		recv[i + 12] = bu * 0.99916056f + bd * 0.04096558f;
		recv[i + 23] = bd * 0.99916056f - bu * 0.04096558f;
		bu = recv[i + 11];
		bd = recv[i + 24];
		recv[i + 11] = bu * 0.99989920f + bd * 0.0141986f;
		recv[i + 24] = bd * 0.99989920f - bu * 0.0141986f;
		bu = recv[i + 10];
		bd = recv[i + 25];
		recv[i + 10] = bu * 0.99999316f + bd * 3.69997467e-3f;
		recv[i + 25] = bd * 0.99999316f - bu * 3.69997467e-3f;
	}
}

static void imdct12(float pre_block[SBLIMIT * SSLIMIT], float recv[SBLIMIT * SSLIMIT], int off)
{
	float* io = recv;
	float* pre = pre_block;
	int i, j;
	float in1, in2, in3, in4;
	float out0, out1, out2, out3, out4, out5, tmp;
	float out6 = 0, out7 = 0, out8 = 0, out9 = 0, out10 = 0, out11 = 0;
	float out12 = 0, out13 = 0, out14 = 0, out15 = 0, out16 = 0, out17 = 0;
	float f0 = 0, f1 = 0, f2 = 0, f3 = 0, f4 = 0, f5 = 0;

	for (j = 0; j != 3; j++) {
		i = j + off;
		//>>>>>>>>>>>> 12-point IMDCT
		//>>>>>> 6-point IDCT
		io[15 + i] += (io[12 + i] += io[9 + i]) + io[6 + i];
		io[9 + i] += (io[6 + i] += io[3 + i]) + io[i];
		io[3 + i] += io[i];

		//>>> 3-point IDCT on even
		out1 = (in1 = io[i]) - (in2 = io[12 + i]);
		in3 = in1 + in2 * 0.5f;
		in4 = io[6 + i] * 0.8660254f;
		out0 = in3 + in4;
		out2 = in3 - in4;
		//<<< End 3-point IDCT on even

		//>>> 3-point IDCT on odd (for 6-point IDCT)
		out4 = ((in1 = io[3 + i]) - (in2 = io[15 + i])) * 0.7071068f;
		in3 = in1 + in2 * 0.5f;
		in4 = io[9 + i] * 0.8660254f;
		out5 = (in3 + in4) * 0.5176381f;
		out3 = (in3 - in4) * 1.9318516f;
		//<<< End 3-point IDCT on odd

		// Output: butterflies on 2,3-point IDCT's (for 6-point IDCT)
		tmp = out0; out0 += out5; out5 = tmp - out5;
		tmp = out1; out1 += out4; out4 = tmp - out4;
		tmp = out2; out2 += out3; out3 = tmp - out3;
		//<<<<<< End 6-point IDCT
		//<<<<<<<<<<<< End 12-point IDCT

		tmp = out3 * 0.1072064f;
		switch (j) {
		case 0:
			out6 = tmp;
			out7 = out4 * 0.5f;
			out8 = out5 * 2.3319512f;
			out9 = -out5 * 3.0390580f;
			out10 = -out4 * 1.2071068f;
			out11 = -tmp * 7.5957541f;

			f0 = out2 * 0.6248445f;
			f1 = out1 * 0.5f;
			f2 = out0 * 0.4000996f;
			f3 = out0 * 0.3070072f;
			f4 = out1 * 0.2071068f;
			f5 = out2 * 0.0822623f;
			break;
		case 1:
			out12 = tmp - f0;
			out13 = out4 * 0.5f - f1;
			out14 = out5 * 2.3319512f - f2;
			out15 = -out5 * 3.0390580f - f3;
			out16 = -out4 * 1.2071068f - f4;
			out17 = -tmp * 7.5957541f - f5;

			f0 = out2 * 0.6248445f;
			f1 = out1 * 0.5f;
			f2 = out0 * 0.4000996f;
			f3 = out0 * 0.3070072f;
			f4 = out1 * 0.2071068f;
			f5 = out2 * 0.0822623f;
			break;
		case 2:
			// output
			i = off;
			io[i + 0] = pre[i + 0];
			io[i + 1] = pre[i + 1];
			io[i + 2] = pre[i + 2];
			io[i + 3] = pre[i + 3];
			io[i + 4] = pre[i + 4];
			io[i + 5] = pre[i + 5];
			io[i + 6] = pre[i + 6] + out6;
			io[i + 7] = pre[i + 7] + out7;
			io[i + 8] = pre[i + 8] + out8;
			io[i + 9] = pre[i + 9] + out9;
			io[i + 10] = pre[i + 10] + out10;
			io[i + 11] = pre[i + 11] + out11;
			io[i + 12] = pre[i + 12] + out12;
			io[i + 13] = pre[i + 13] + out13;
			io[i + 14] = pre[i + 14] + out14;
			io[i + 15] = pre[i + 15] + out15;
			io[i + 16] = pre[i + 16] + out16;
			io[i + 17] = pre[i + 17] + out17;

			pre[i + 0] = tmp - f0;
			pre[i + 1] = out4 * 0.5f - f1;
			pre[i + 2] = out5 * 2.3319512f - f2;
			pre[i + 3] = -out5 * 3.0390580f - f3;
			pre[i + 4] = -out4 * 1.2071068f - f4;
			pre[i + 5] = -tmp * 7.5957541f - f5;
			pre[i + 6] = -out2 * 0.6248445f;
			pre[i + 7] = -out1 * 0.5f;
			pre[i + 8] = -out0 * 0.4000996f;
			pre[i + 9] = -out0 * 0.3070072f;
			pre[i + 10] = -out1 * 0.2071068f;
			pre[i + 11] = -out2 * 0.0822623f;
			pre[i + 12] = pre[i + 13] = pre[i + 14] = 0;
			pre[i + 15] = pre[i + 16] = pre[i + 17] = 0;
		}
	}
}

static const float _imdctWIN[4][36] =
{
	{
		0.0322824, 0.1072064, 0.2014143, 0.3256164, 0.5, 0.7677747,
		1.2412229, 2.3319514, 7.7441506, -8.4512568, -3.0390580, -1.9483297,
		-1.4748814, -1.2071068, -1.0327232, -0.9085211, -0.8143131, -0.7393892,
		-0.6775254, -0.6248445, -0.5787917, -0.5376016, -0.5, -0.4650284,
		-0.4319343, -0.4000996, -0.3689899, -0.3381170, -0.3070072, -0.2751725,
		-0.2420785, -0.2071068, -0.1695052, -0.1283151, -0.0822624, -0.0295815
	},
	{
		0.0322824, 0.1072064, 0.2014143, 0.3256164, 0.5, 0.7677747,
		1.2412229, 2.3319514, 7.7441506, -8.4512568, -3.0390580, -1.9483297,
		-1.4748814, -1.2071068, -1.0327232, -0.9085211, -0.8143131, -0.7393892,
		-0.6781709, -0.6302362, -0.5928445, -0.5636910, -0.5411961, -0.5242646,
		-0.5077583, -0.4659258, -0.3970546, -0.3046707, -0.1929928, -0.0668476,
		-0.0, -0.0, -0.0, -0.0, -0.0, -0.0
	},
	{/* block_type = 2 */ 0.0 },
	{
		0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
		0.3015303, 1.4659259, 6.9781060, -9.0940447, -3.5390582, -2.2903500,
		-1.6627548, -1.3065630, -1.0828403, -0.9305795, -0.8213398, -0.7400936,
		-0.6775254, -0.6248445, -0.5787917, -0.5376016, -0.5, -0.4650284,
		-0.4319343, -0.4000996, -0.3689899, -0.3381170, -0.3070072, -0.2751725,
		-0.2420785, -0.2071068, -0.1695052, -0.1283151, -0.0822624, -0.0295815
	}
};

static void imdct36(float pre_block[SBLIMIT * SSLIMIT], float recv[SBLIMIT * SSLIMIT], int off, int block_type)
{
	float* io = recv;
	float* pre = pre_block;
	int i = off;
	float in0, in1, in2, in3, in4, in5, in6, in7, in8, in9, in10, in11;
	float in12, in13, in14, in15, in16, in17;
	float out0, out1, out2, out3, out4, out5, out6, out7, out8, out9;
	float out10, out11, out12, out13, out14, out15, out16, out17, tmp;

	// 采用 Byeong Gi Lee 的算法

	//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 36-point IDCT
	//>>>>>>>>>>>>>>>>>> 18-point IDCT for odd
	io[i + 17] += (io[i + 16] += io[i + 15]) + io[i + 14];
	io[i + 15] += (io[i + 14] += io[i + 13]) + io[i + 12];
	io[i + 13] += (io[i + 12] += io[i + 11]) + io[i + 10];
	io[i + 11] += (io[i + 10] += io[i + 9]) + io[i + 8];
	io[i + 9] += (io[i + 8] += io[i + 7]) + io[i + 6];
	io[i + 7] += (io[i + 6] += io[i + 5]) + io[i + 4];
	io[i + 5] += (io[i + 4] += io[i + 3]) + io[i + 2];
	io[i + 3] += (io[i + 2] += io[i + 1]) + io[i + 0];
	io[i + 1] += io[i + 0];

	//>>>>>>>>> 9-point IDCT on even
	in0 = io[i + 0] + io[i + 12] * 0.5f;
	in1 = io[i + 0] - io[i + 12];
	in2 = io[i + 8] + io[i + 16] - io[i + 4];

	out4 = in1 + in2;

	in3 = in1 - in2 * 0.5f;
	in4 = (io[i + 10] + io[i + 14] - io[i + 2]) * 0.8660254f; // cos(PI/6)

	out1 = in3 - in4;
	out7 = in3 + in4;

	in5 = (io[i + 4] + io[i + 8]) * 0.9396926f;		//cos( PI/9)
	in6 = (io[i + 16] - io[i + 8]) * 0.1736482f;	//cos(4PI/9)
	in7 = -(io[i + 4] + io[i + 16]) * 0.7660444f;	//cos(2PI/9)

	in17 = in0 - in5 - in7;
	in8 = in5 + in0 + in6;
	in9 = in0 + in7 - in6;

	in12 = io[i + 6] * 0.8660254f;				//cos(PI/6)
	in10 = (io[i + 2] + io[i + 10]) * 0.9848078f;	//cos(PI/18)
	in11 = (io[i + 14] - io[i + 10]) * 0.3420201f;	//cos(7PI/18)

	in13 = in10 + in11 + in12;

	out0 = in8 + in13;
	out8 = in8 - in13;

	in14 = -(io[i + 2] + io[i + 14]) * 0.6427876f;	//cos(5PI/18)
	in15 = in10 + in14 - in12;
	in16 = in11 - in14 - in12;

	out3 = in9 + in15;
	out5 = in9 - in15;

	out2 = in17 + in16;
	out6 = in17 - in16;
	//<<<<<<<<< End 9-point IDCT on even

	//>>>>>>>>> 9-point IDCT on odd
	in0 = io[i + 1] + io[i + 13] * 0.5f;	//cos(PI/3)
	in1 = io[i + 1] - io[i + 13];
	in2 = io[i + 9] + io[i + 17] - io[i + 5];

	out13 = (in1 + in2) * 0.7071068f;	//cos(PI/4)

	in3 = in1 - in2 * 0.5f;
	in4 = (io[i + 11] + io[i + 15] - io[i + 3]) * 0.8660254f;	//cos(PI/6)

	out16 = (in3 - in4) * 0.5176381f;	// 0.5/cos( PI/12)
	out10 = (in3 + in4) * 1.9318517f;	// 0.5/cos(5PI/12)

	in5 = (io[i + 5] + io[i + 9]) * 0.9396926f;	// cos( PI/9)
	in6 = (io[i + 17] - io[i + 9]) * 0.1736482f;	// cos(4PI/9)
	in7 = -(io[i + 5] + io[i + 17]) * 0.7660444f;	// cos(2PI/9)

	in17 = in0 - in5 - in7;
	in8 = in5 + in0 + in6;
	in9 = in0 + in7 - in6;

	in12 = io[i + 7] * 0.8660254f;				// cos(PI/6)
	in10 = (io[i + 3] + io[i + 11]) * 0.9848078f;	// cos(PI/18)
	in11 = (io[i + 15] - io[i + 11]) * 0.3420201f;	// cos(7PI/18)

	in13 = in10 + in11 + in12;

	out17 = (in8 + in13) * 0.5019099f;		// 0.5/cos(PI/36)
	out9 = (in8 - in13) * 5.7368566f;		// 0.5/cos(17PI/36)

	in14 = -(io[i + 3] + io[i + 15]) * 0.6427876f;	// cos(5PI/18)
	in15 = in10 + in14 - in12;
	in16 = in11 - in14 - in12;

	out14 = (in9 + in15) * 0.6103873f;		// 0.5/cos(7PI/36)
	out12 = (in9 - in15) * 0.8717234f;		// 0.5/cos(11PI/36)

	out15 = (in17 + in16) * 0.5516890f;		// 0.5/cos(5PI/36)
	out11 = (in17 - in16) * 1.1831008f;		// 0.5/cos(13PI/36)
	//<<<<<<<<< End. 9-point IDCT on odd

	// Butterflies on 9-point IDCT's
	tmp = out0; out0 += out17; out17 = tmp - out17;
	tmp = out1; out1 += out16; out16 = tmp - out16;
	tmp = out2; out2 += out15; out15 = tmp - out15;
	tmp = out3; out3 += out14; out14 = tmp - out14;
	tmp = out4; out4 += out13; out13 = tmp - out13;
	tmp = out5; out5 += out12; out12 = tmp - out12;
	tmp = out6; out6 += out11; out11 = tmp - out11;
	tmp = out7; out7 += out10; out10 = tmp - out10;
	tmp = out8; out8 += out9;  out9 = tmp - out9;
	//<<<<<<<<<<<<<<<<<< End of 18-point IDCT
	//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< End of 36-point IDCT

	// output
	const float* win = _imdctWIN[block_type];

	io[i + 0] = pre[i + 0] + out9 * win[0];
	io[i + 1] = pre[i + 1] + out10 * win[1];
	io[i + 2] = pre[i + 2] + out11 * win[2];
	io[i + 3] = pre[i + 3] + out12 * win[3];
	io[i + 4] = pre[i + 4] + out13 * win[4];
	io[i + 5] = pre[i + 5] + out14 * win[5];
	io[i + 6] = pre[i + 6] + out15 * win[6];
	io[i + 7] = pre[i + 7] + out16 * win[7];
	io[i + 8] = pre[i + 8] + out17 * win[8];
	io[i + 9] = pre[i + 9] + out17 * win[9];
	io[i + 10] = pre[i + 10] + out16 * win[10];
	io[i + 11] = pre[i + 11] + out15 * win[11];
	io[i + 12] = pre[i + 12] + out14 * win[12];
	io[i + 13] = pre[i + 13] + out13 * win[13];
	io[i + 14] = pre[i + 14] + out12 * win[14];
	io[i + 15] = pre[i + 15] + out11 * win[15];
	io[i + 16] = pre[i + 16] + out10 * win[16];
	io[i + 17] = pre[i + 17] + out9 * win[17];

	pre[i + 0] = out8 * win[18];
	pre[i + 1] = out7 * win[19];
	pre[i + 2] = out6 * win[20];
	pre[i + 3] = out5 * win[21];
	pre[i + 4] = out4 * win[22];
	pre[i + 5] = out3 * win[23];
	pre[i + 6] = out2 * win[24];
	pre[i + 7] = out1 * win[25];
	pre[i + 8] = out0 * win[26];
	pre[i + 9] = out0 * win[27];
	pre[i + 10] = out1 * win[28];
	pre[i + 11] = out2 * win[29];
	pre[i + 12] = out3 * win[30];
	pre[i + 13] = out4 * win[31];
	pre[i + 14] = out5 * win[32];
	pre[i + 15] = out6 * win[33];
	pre[i + 16] = out7 * win[34];
	pre[i + 17] = out8 * win[35];
}

//static void imdct36(const float samples_sb[SSLIMIT], float output[36])
//{
//	for (int i = 0; i < 36; ++i) {
//		output[i] = 0.0;
//		for (int k = 0; k < SSLIMIT; k += 3) {
//			output[i] += samples_sb[k + 0] * imdct_l[i][k + 0];
//			output[i] += samples_sb[k + 1] * imdct_l[i][k + 1];
//			output[i] += samples_sb[k + 2] * imdct_l[i][k + 2];
//		}
//	}
//}
//
//static void imdct12(const float samples_sb[SSLIMIT], float samples_pre[SBLIMIT * SSLIMIT], unsigned short off)
//{
//	for (int i = 0; i < 36; ++i) {
//		samples_pre[off + i] = 0.0;
//		for (int k = 0; k < SSLIMIT; k += 3) {
//			samples_pre[off + i] += samples_sb[k + 0] * imdct_l[i][k + 0];
//			samples_pre[off + i] += samples_sb[k + 1] * imdct_l[i][k + 1];
//			samples_pre[off + i] += samples_sb[k + 2] * imdct_l[i][k + 2];
//		}
//	}
//}
//
//static void l3_imdct_s(const float samples_sb[SSLIMIT], float output[36])
//{
//	window_s[];
//	imdct_s[];
//}
//
//static void l3_imdct_l(const float samples_sb[SSLIMIT], float output[36], unsigned char block_type)
//{
//	int i;
//
//	imdct36(samples_sb, output);
//
//	switch (block_type) {
//	case 0:	// normal window
//		for (i = 0; i < 36; i += 4) {
//			output[i + 0] *= window_l[i + 0];
//			output[i + 1] *= window_l[i + 1];
//			output[i + 2] *= window_l[i + 2];
//			output[i + 3] *= window_l[i + 3];
//		}
//		break;
//	case 1:	// start block
//		for (i = 0; i < 18; i += 3) {
//			output[i + 0] *= window_l[i + 0];
//			output[i + 1] *= window_l[i + 1];
//			output[i + 2] *= window_l[i + 2];
//		}
//		/*  (i = 18; i < 24; ++i) z[i] unchanged */
//		for (i = 24; i < 30; ++i)
//			output[i] *= window_s[i - 18];
//		for (i = 30; i < 36; ++i)
//			output[i] = 0;
//		break;
//	case 3:	// stop block
//		for (i = 0; i < 6; ++i)
//			output[i] = 0;
//		for (i = 6; i < 12; ++i)
//			output[i] *= window_s[i - 6];
//		/*  (i = 12; i < 18; ++i) z[i] unchanged */
//		for (i = 18; i < 36; i += 3) {
//			output[i + 0] *= window_l[i + 0];
//			output[i + 1] *= window_l[i + 1];
//			output[i + 2] *= window_l[i + 2];
//		}
//		break;
//	}
//}

// static float _samples_pre[2][SBLIMIT * SSLIMIT];
static void l3_hybrid(struct ch_info* cur_ch, const int ch)
{
	int off = 0, block_type;

	for (off = 0; off < cur_ch->nonzero_len; off += 18) {
		block_type = (cur_ch->blocksplit_flag && cur_ch->mixed_block_flag && off < 36) ? 0 : cur_ch->block_type;

		if (block_type == 2)
			imdct12(pre_block[ch], cur_ch->buf, off);
		else
			imdct36(pre_block[ch], cur_ch->buf, off, block_type);
	}

	// 0值区
	for (; off < 576; ++off) {
		cur_ch->buf[off] = pre_block[ch][off];
		pre_block[ch][off] = 0;
	}
}

void l3_init(const struct mpeg_header* const header)
{
	cur_sfb_table.index_long = __sfb_index_long[header->sampling_frequency];
	cur_sfb_table.index_short = __sfb_index_short[header->sampling_frequency];
	cur_sfb_table.width_long = __sfb_width_long[header->sampling_frequency];
	cur_sfb_table.width_short = __sfb_width_short[header->sampling_frequency];

	int i/*, k*/;
	for (i = 0; i < 8207; ++i)
		gain_powreq[i] = (float)pow((double)i, 4.0 / 3.0);

	for (i = -256; i < 118 + 4; ++i)
		gain_pow2_is[i + 256] = (float)pow(2.0, (i + 210.0) * -1.0 / 4.0);

#if 0
	float div_c;
	for (i = 0; i < 8; ++i) {
		div_c = sqrt(1.0 + pow(__c[i], 2.0));
		cs[i] = 1.0 / div_c;
		ca[i] = __c[i] / cs[i];
	}

	for (i = 0; i < 12; ++i) {
		for (k = 0; k < 6; ++k) {
			imdct_s[i][k] = cos(PI * (2.0 * i + 7.0) * (2.0 * k + 1.0) / 24.0);
		}
		window_s[i] = sin(PI * (2.0 * i + 1.0) / 24.0);
	}

	for (i = 0; i < 36; ++i) {
		for (k = 0; k < 18; ++k) {
			imdct_l[i][k] = cos(PI * (2.0 * i + 19.0) * (2.0 * k + 1.0) / 72.0);
		}
		window_l[i] = sin(PI * (2.0 * i + 1.0) / 72.0);
	}
#endif
	// static float is_coef[] = { 0.0, 0.211324865, 0.366025404, 0.5, 0.633974596, 0.788675135, 1.0 };
	double is_ratio;
	for (i = 0; i < 7; ++i) {
		is_ratio = tan(i * PI / 12.0);
		is_table[i] = (float)(is_ratio / (1.0 + is_ratio));
	}

	//float q = sqrt(2);	// # define M_SQRT2	1.41421356237309504880
	//for (i = 0; i < 16; ++i) {
	//	float t = tan(i * M_PI / 12.0);
	//	_intesity_tabs[0][0][i] = t / (1.0 + t);
	//	_intesity_tabs[0][1][i] = 1.0 / (1.0 + t);
	//	_intesity_tabs[1][0][i] = q * t / (1.0 + t);
	//	_intesity_tabs[1][1][i] = q / (1.0 + t);
	//}

	//init_synthesis_tabs();
}

static float is[2][2][576];
int l3_decode_samples(struct decoder_handle* handle, unsigned frame_count)
{
	// unsigned char l3_scf[2][39]; // short[13][3], mixed[38], long[22]
	// static float xr[2][2][SBLIMIT * SSLIMIT];

	const struct mpeg_frame* const cur_frame = &handle->cur_frame;
	struct bs* const file_stream = handle->file_stream;
	struct bs* const sideinfo_stream = handle->sideinfo_stream;
	struct bs* const maindata_stream = handle->maindata_stream;
	struct l3_sideinfo sideinfo = { 0 };
	char log_msg_buf[64];
	
	sideinfo_stream->byte_ptr = file_stream->byte_ptr;
	sideinfo_stream->bit_pos = 0;

	if (l3_decode_sideinfo(sideinfo_stream, &sideinfo, cur_frame->nch) == -1) {
		sprintf(log_msg_buf, "frame#%u skipped(decode sideinfo failed)!", frame_count);
		LOG_E("l3_decode_sideinfo", log_msg_buf);
		return 1;
	}

	memcpy(maindata_stream->bit_buf, maindata_stream->end_ptr - sideinfo.main_data_begin, sideinfo.main_data_begin);
	maindata_stream->byte_ptr = maindata_stream->bit_buf;
	maindata_stream->bit_pos = 0;
	maindata_stream->end_ptr -= sideinfo.main_data_begin;

	if (bs_Append(maindata_stream, sideinfo_stream->byte_ptr, sideinfo.main_data_begin, cur_frame->maindata_size) != cur_frame->maindata_size) {
		sprintf(log_msg_buf, "frame#%u maindata_stream overflow!", frame_count);
		LOG_E("bs_Append(maindata_stream)", log_msg_buf);
		return -1;
	}

	for (int gr = 0; gr < 2; ++gr) {
		for (int ch = 0; ch < cur_frame->nch; ++ch) {
			struct ch_info* const cur_ch = &sideinfo.info_ch[ch][gr];
			int part2_start = (maindata_stream->byte_ptr - maindata_stream->bit_buf) * 8 + maindata_stream->bit_pos, bit_pos_end;

			l3_decode_scalefactors(maindata_stream, &sideinfo, gr, ch);
			l3_huffman_decode(maindata_stream, cur_ch, gr, ch, part2_start + cur_ch->part2_3_len - 1, is[ch][gr]);

			l3_requantize_samples(cur_ch, gr, ch);
		}
	}





	//int maindata_len = maindata_stream->end_ptr - maindata_stream->bit_buf;
	//if (maindata_len < sideinfo.main_data_begin) {
	//	fprintf(stderr, "[W] frame#%u skipped(need more maindata)!\n", cur_frame_id);
	//	if (bs_append(maindata_stream, decode_stream->byte_ptr, 0, frame->maindata_size) != frame->maindata_size) {
	//		fprintf(stderr, "[E] maindata stream overflow!\n");
	//		return -1;
	//	}
	//	return 1;
	//}

	//int skip_size = maindata_len - (maindata_stream->byte_ptr - maindata_stream->bit_reservoir) - sideinfo.main_data_begin, ret;
	//// printf("%u <-> %d <-> %d <-> %d <-> %d\n", frame->maindata_size, maindata_len, maindata_stream->byte_ptr - maindata_stream->bit_reservoir, sideinfo->main_data_end, skip_size);
	//if ((ret = bs_skipBytes(maindata_stream, skip_size)) != skip_size) {
	//	fprintf(stderr, "[E] frame #%u (bs_skipBytes(%d) returned %d)!\n", cur_frame_id, skip_size, ret);
	//	return -1;
	//}

	//if (bs_append(maindata_stream, decode_stream->byte_ptr, 0, frame->maindata_size) != frame->maindata_size) {
	//	fprintf(stderr, "[E] maindata stream overflow!\n");
	//	return -1;
	//}

	for (int gr = 0; gr < 2; ++gr) {
		for (int ch = 0; ch < frame->nch; ++ch) {
			//struct ch_info* cur_ch = &sideinfo.info_ch;

			l3_decode_scalefactors(maindata_stream, &sideinfo, gr, ch);
			if (!l3_huffman_decode(maindata_stream, &sideinfo, gr, ch)) {
				fprintf(stderr, "[W] nonzero_len == 0\n");
			}

			l3_requantize_samples(frame, &sideinfo, gr, ch);
		}

		if (frame->is_MS || frame->is_Intensity) {
			l3_do_stereo(frame, &sideinfo, gr);
		}


	}

	static float samples_tmp[2][SBLIMIT];
	for (int ch = 0; ch < frame->nch; ++ch) {
		for (int gr = 0; gr < 2; ++gr) {
			struct ch_info* cur_ch = &sideinfo.info_ch[ch][gr];
			l3_antialias(cur_ch);
			l3_hybrid(cur_ch, ch);

			for (int ss = 0, i, sb; ss < SSLIMIT; ss += 2) {
				for (i = ss, sb = 0; sb < 32; ++sb, i += 18)
					samples_tmp[ch][sb] = cur_ch->buf[i];
				synthesis_subband_filter(samples_tmp[ch], pcm_out, write_ptr, ch, frame->nch);

				for (i = ss + 1, sb = 0; sb < 32; sb += 2, i += 36) {
					samples_tmp[ch][sb] = cur_ch->buf[i];

					samples_tmp[ch][sb + 1] = -cur_ch->buf[i + 18];	//多相频率倒置(INVERSE QUANTIZE SAMPLES)
				}
				synthesis_subband_filter(samples_tmp[ch], pcm_out, write_ptr, ch, frame->nch);
			}
		}
	}

	//if (frame->is_MS || frame->is_Intensity) {
	//	if (sideinfo->ch[0].gr[gr].nonzero_len <= sideinfo->ch[1].gr[gr].nonzero_len)
	//		sideinfo->ch[0].gr[gr].nonzero_len = sideinfo->ch[1].gr[gr].nonzero_len;
	//	else sideinfo->ch[1].gr[gr].nonzero_len = sideinfo->ch[0].gr[gr].nonzero_len;

	//	float* in[2] = { (float*)l3_samples_in[0], (float*)l3_samples_in[1] };
	//	for (int i = SSLIMIT * sideinfo->ch[0].gr[gr].nonzero_len; i; --i) {
	//		*in[0] += *in[1];
	//		++in[0];
	//		++in[1];
	//	}
	//}

	return 0;
}
