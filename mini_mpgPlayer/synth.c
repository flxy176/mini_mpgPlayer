#include "synth.h"
#include <stdbool.h>
#include <math.h>
#include <immintrin.h>

#define M_PI       3.14159265358979323846

/*
coefficients Di for the synthesis window
*/
static const float _D[512] =
{
	0.000000000f, -0.000015259f, -0.000015259f, -0.000015259f,
	-0.000015259f, -0.000015259f, -0.000015259f, -0.000030518f,
	-0.000030518f, -0.000030518f, -0.000030518f, -0.000045776f,
	-0.000045776f, -0.000061035f, -0.000061035f, -0.000076294f,
	-0.000076294f, -0.000091553f, -0.000106812f, -0.000106812f,
	-0.000122070f, -0.000137329f, -0.000152588f, -0.000167847f,
	-0.000198364f, -0.000213623f, -0.000244141f, -0.000259399f,
	-0.000289917f, -0.000320435f, -0.000366211f, -0.000396729f,
	-0.000442505f, -0.000473022f, -0.000534058f, -0.000579834f,
	-0.000625610f, -0.000686646f, -0.000747681f, -0.000808716f,
	-0.000885010f, -0.000961304f, -0.001037598f, -0.001113892f,
	-0.001205444f, -0.001296997f, -0.001388550f, -0.001480103f,
	-0.001586914f, -0.001693726f, -0.001785278f, -0.001907349f,
	-0.002014160f, -0.002120972f, -0.002243042f, -0.002349854f,
	-0.002456665f, -0.002578735f, -0.002685547f, -0.002792358f,
	-0.002899170f, -0.002990723f, -0.003082275f, -0.003173828f,
	0.003250122f, 0.003326416f, 0.003387451f, 0.003433228f,
	0.003463745f, 0.003479004f, 0.003479004f, 0.003463745f,
	0.003417969f, 0.003372192f, 0.003280640f, 0.003173828f,
	0.003051758f, 0.002883911f, 0.002700806f, 0.002487183f,
	0.002227783f, 0.001937866f, 0.001617432f, 0.001266479f,
	0.000869751f, 0.000442505f, -0.000030518f, -0.000549316f,
	-0.001098633f, -0.001693726f, -0.002334595f, -0.003005981f,
	-0.003723145f, -0.004486084f, -0.005294800f, -0.006118774f,
	-0.007003784f, -0.007919312f, -0.008865356f, -0.009841919f,
	-0.010848999f, -0.011886597f, -0.012939453f, -0.014022827f,
	-0.015121460f, -0.016235352f, -0.017349243f, -0.018463135f,
	-0.019577026f, -0.020690918f, -0.021789551f, -0.022857666f,
	-0.023910522f, -0.024932861f, -0.025909424f, -0.026840210f,
	-0.027725220f, -0.028533936f, -0.029281616f, -0.029937744f,
	-0.030532837f, -0.031005859f, -0.031387329f, -0.031661987f,
	-0.031814575f, -0.031845093f, -0.031738281f, -0.031478882f,
	0.031082153f, 0.030517578f, 0.029785156f, 0.028884888f,
	0.027801514f, 0.026535034f, 0.025085449f, 0.023422241f,
	0.021575928f, 0.019531250f, 0.017257690f, 0.014801025f,
	0.012115479f, 0.009231567f, 0.006134033f, 0.002822876f,
	-0.000686646f, -0.004394531f, -0.008316040f, -0.012420654f,
	-0.016708374f, -0.021179199f, -0.025817871f, -0.030609131f,
	-0.035552979f, -0.040634155f, -0.045837402f, -0.051132202f,
	-0.056533813f, -0.061996460f, -0.067520142f, -0.073059082f,
	-0.078628540f, -0.084182739f, -0.089706421f, -0.095169067f,
	-0.100540161f, -0.105819702f, -0.110946655f, -0.115921021f,
	-0.120697021f, -0.125259399f, -0.129562378f, -0.133590698f,
	-0.137298584f, -0.140670776f, -0.143676758f, -0.146255493f,
	-0.148422241f, -0.150115967f, -0.151306152f, -0.151962280f,
	-0.152069092f, -0.151596069f, -0.150497437f, -0.148773193f,
	-0.146362305f, -0.143264771f, -0.139450073f, -0.134887695f,
	-0.129577637f, -0.123474121f, -0.116577148f, -0.108856201f,
	0.100311279f, 0.090927124f, 0.080688477f, 0.069595337f,
	0.057617187f, 0.044784546f, 0.031082153f, 0.016510010f,
	0.001068115f, -0.015228271f, -0.032379150f, -0.050354004f,
	-0.069168091f, -0.088775635f, -0.109161377f, -0.130310059f,
	-0.152206421f, -0.174789429f, -0.198059082f, -0.221984863f,
	-0.246505737f, -0.271591187f, -0.297210693f, -0.323318481f,
	-0.349868774f, -0.376800537f, -0.404083252f, -0.431655884f,
	-0.459472656f, -0.487472534f, -0.515609741f, -0.543823242f,
	-0.572036743f, -0.600219727f, -0.628295898f, -0.656219482f,
	-0.683914185f, -0.711318970f, -0.738372803f, -0.765029907f,
	-0.791213989f, -0.816864014f, -0.841949463f, -0.866363525f,
	-0.890090942f, -0.913055420f, -0.935195923f, -0.956481934f,
	-0.976852417f, -0.996246338f, -1.014617920f, -1.031936646f,
	-1.048156738f, -1.063217163f, -1.077117920f, -1.089782715f,
	-1.101211548f, -1.111373901f, -1.120223999f, -1.127746582f,
	-1.133926392f, -1.138763428f, -1.142211914f, -1.144287109f,
	1.144989014f, 1.144287109f, 1.142211914f, 1.138763428f,
	1.133926392f, 1.127746582f, 1.120223999f, 1.111373901f,
	1.101211548f, 1.089782715f, 1.077117920f, 1.063217163f,
	1.048156738f, 1.031936646f, 1.014617920f, 0.996246338f,
	0.976852417f, 0.956481934f, 0.935195923f, 0.913055420f,
	0.890090942f, 0.866363525f, 0.841949463f, 0.816864014f,
	0.791213989f, 0.765029907f, 0.738372803f, 0.711318970f,
	0.683914185f, 0.656219482f, 0.628295898f, 0.600219727f,
	0.572036743f, 0.543823242f, 0.515609741f, 0.487472534f,
	0.459472656f, 0.431655884f, 0.404083252f, 0.376800537f,
	0.349868774f, 0.323318481f, 0.297210693f, 0.271591187f,
	0.246505737f, 0.221984863f, 0.198059082f, 0.174789429f,
	0.152206421f, 0.130310059f, 0.109161377f, 0.088775635f,
	0.069168091f, 0.050354004f, 0.032379150f, 0.015228271f,
	-0.001068115f, -0.016510010f, -0.031082153f, -0.044784546f,
	-0.057617187f, -0.069595337f, -0.080688477f, -0.090927124f,
	0.100311279f, 0.108856201f, 0.116577148f, 0.123474121f,
	0.129577637f, 0.134887695f, 0.139450073f, 0.143264771f,
	0.146362305f, 0.148773193f, 0.150497437f, 0.151596069f,
	0.152069092f, 0.151962280f, 0.151306152f, 0.150115967f,
	0.148422241f, 0.146255493f, 0.143676758f, 0.140670776f,
	0.137298584f, 0.133590698f, 0.129562378f, 0.125259399f,
	0.120697021f, 0.115921021f, 0.110946655f, 0.105819702f,
	0.100540161f, 0.095169067f, 0.089706421f, 0.084182739f,
	0.078628540f, 0.073059082f, 0.067520142f, 0.061996460f,
	0.056533813f, 0.051132202f, 0.045837402f, 0.040634155f,
	0.035552979f, 0.030609131f, 0.025817871f, 0.021179199f,
	0.016708374f, 0.012420654f, 0.008316040f, 0.004394531f,
	0.000686646f, -0.002822876f, -0.006134033f, -0.009231567f,
	-0.012115479f, -0.014801025f, -0.017257690f, -0.019531250f,
	-0.021575928f, -0.023422241f, -0.025085449f, -0.026535034f,
	-0.027801514f, -0.028884888f, -0.029785156f, -0.030517578f,
	0.031082153f, 0.031478882f, 0.031738281f, 0.031845093f,
	0.031814575f, 0.031661987f, 0.031387329f, 0.031005859f,
	0.030532837f, 0.029937744f, 0.029281616f, 0.028533936f,
	0.027725220f, 0.026840210f, 0.025909424f, 0.024932861f,
	0.023910522f, 0.022857666f, 0.021789551f, 0.020690918f,
	0.019577026f, 0.018463135f, 0.017349243f, 0.016235352f,
	0.015121460f, 0.014022827f, 0.012939453f, 0.011886597f,
	0.010848999f, 0.009841919f, 0.008865356f, 0.007919312f,
	0.007003784f, 0.006118774f, 0.005294800f, 0.004486084f,
	0.003723145f, 0.003005981f, 0.002334595f, 0.001693726f,
	0.001098633f, 0.000549316f, 0.000030518f, -0.000442505f,
	-0.000869751f, -0.001266479f, -0.001617432f, -0.001937866f,
	-0.002227783f, -0.002487183f, -0.002700806f, -0.002883911f,
	-0.003051758f, -0.003173828f, -0.003280640f, -0.003372192f,
	-0.003417969f, -0.003463745f, -0.003479004f, -0.003479004f,
	-0.003463745f, -0.003433228f, -0.003387451f, -0.003326416f,
	0.003250122f, 0.003173828f, 0.003082275f, 0.002990723f,
	0.002899170f, 0.002792358f, 0.002685547f, 0.002578735f,
	0.002456665f, 0.002349854f, 0.002243042f, 0.002120972f,
	0.002014160f, 0.001907349f, 0.001785278f, 0.001693726f,
	0.001586914f, 0.001480103f, 0.001388550f, 0.001296997f,
	0.001205444f, 0.001113892f, 0.001037598f, 0.000961304f,
	0.000885010f, 0.000808716f, 0.000747681f, 0.000686646f,
	0.000625610f, 0.000579834f, 0.000534058f, 0.000473022f,
	0.000442505f, 0.000396729f, 0.000366211f, 0.000320435f,
	0.000289917f, 0.000259399f, 0.000244141f, 0.000213623f,
	0.000198364f, 0.000167847f, 0.000152588f, 0.000137329f,
	0.000122070f, 0.000106812f, 0.000106812f, 0.000091553f,
	0.000076294f, 0.000076294f, 0.000061035f, 0.000061035f,
	0.000045776f, 0.000045776f, 0.000030518f, 0.000030518f,
	0.000030518f, 0.000030518f, 0.000015259f, 0.000015259f,
	0.000015259f, 0.000015259f, 0.000015259f, 0.000015259f
};

/*
coefficients Nik for the synthesis window
*/
// static float _N[64][32]; // need init ( _N[i][k] = cos((16+i)*(2*k+1)*PI/64) )
static float _N[32][32]; // need init ( _N[i][k] = cos(i*(2*k+1)*PI/64) )

static float _U[512];
static float _V[2][1024];

static void memcpy_dword(void* dst, const void* src, uint32_t cnt, bool down)
{
	if (down)
		__asm std;

	__asm {
		mov edi, dword ptr[dst]
		mov esi, dword ptr[src]
		mov ecx, dword ptr[cnt]
		rep movsd
		cld
	}
}

static void dct32to64(const float s[32], const uint8_t ch)
{
	float f_out[32], f_tmp[4];
	__m128 f4_S0 = _mm_loadu_ps(s), f4_S1 = _mm_loadu_ps(s + 4), f4_S2 = _mm_loadu_ps(s + 8), f4_S3 = _mm_loadu_ps(s + 12), f4_S4 = _mm_loadu_ps(s + 16);
	__m128 f4_S5 = _mm_loadu_ps(s + 20), f4_S6 = _mm_loadu_ps(s + 24), f4_S7 = _mm_loadu_ps(s + 28);
	int i;

	for (i = 0; i < 32; ++i) {
		__m128 f4_sum = _mm_mul_ps(_mm_loadu_ps(_N[i]), f4_S0);
		f4_sum = _mm_add_ps(f4_sum, _mm_mul_ps(_mm_loadu_ps(_N[i] + 4), f4_S1));
		f4_sum = _mm_add_ps(f4_sum, _mm_mul_ps(_mm_loadu_ps(_N[i] + 8), f4_S2));
		f4_sum = _mm_add_ps(f4_sum, _mm_mul_ps(_mm_loadu_ps(_N[i] + 12), f4_S3));
		f4_sum = _mm_add_ps(f4_sum, _mm_mul_ps(_mm_loadu_ps(_N[i] + 16), f4_S4));
		f4_sum = _mm_add_ps(f4_sum, _mm_mul_ps(_mm_loadu_ps(_N[i] + 20), f4_S5));
		f4_sum = _mm_add_ps(f4_sum, _mm_mul_ps(_mm_loadu_ps(_N[i] + 24), f4_S6));
		f4_sum = _mm_add_ps(f4_sum, _mm_mul_ps(_mm_loadu_ps(_N[i] + 28), f4_S7));
		_mm_storeu_ps(f_tmp, f4_sum);
		f_out[i] = f_tmp[0] + f_tmp[1] + f_tmp[2] + f_tmp[3];
	}

	memcpy_dword(_V[ch], f_out + 16, 16, 0);
	_V[ch][16] = 0;
	for (i = 17; i < 48; ++i)
		_V[ch][i] = -f_out[48 - i];
	for (; i < 64; ++i)
		_V[ch][i] = -f_out[i - 48];
}

#if 0
static void write_f4_s2_samples(const float f_4[4], const unsigned nch, short pcm_out[8], unsigned* off)
{
	if (f_4[0] >= 32766.5f) {
		pcm_out[0] = 32767;
	} else if (f_4[0] <= -32767.5f) {
		pcm_out[0] = -32768;
	} else {
		if ((pcm_out[0] = (short)(f_4[0] + 0.5f)) < 0)
			pcm_out[0] -= 1;
	}
	///////////
	if (f_4[1] >= 32766.5f) {
		pcm_out[2] = 32767;
	} else if (f_4[1] <= -32767.5f) {
		pcm_out[2] = -32768;
	} else {
		if ((pcm_out[2] = (short)(f_4[1] + 0.5f)) < 0)
			pcm_out[2] -= 1;
	}
	///////////
	if (f_4[2] >= 32766.5f) {
		pcm_out[4] = 32767;
	} else if (f_4[2] <= -32767.5f) {
		pcm_out[4] = -32768;
	} else {
		if ((pcm_out[4] = (short)(f_4[2] + 0.5f)) < 0)
			pcm_out[4] -= 1;
	}
	///////////
	if (f_4[3] >= 32766.5f) {
		pcm_out[6] = 32767;
	} else if (f_4[3] <= -32767.5f) {
		pcm_out[6] = -32768;
	} else {
		if ((pcm_out[6] = (short)(f_4[3] + 0.5f)) < 0)
			pcm_out[6] -= 1;
	}
	////////

	if (nch == 1) {
		pcm_out[1] = pcm_out[0];
		pcm_out[3] = pcm_out[2];
		pcm_out[5] = pcm_out[4];
		pcm_out[7] = pcm_out[6];
	}

	*off += 16;
}
#endif

void init_synthesis_tabs(void)
{
	int i, j, k;

	for (i = 0; i < 32; ++i) {
		for (j = 0; j < 32; ++j) {
			k = i * (2 * j + 1);
			_N[i][j] = (float)cos(k * M_PI / 64.0);
		}
	}

	//for (i = 0; i < 512; ++i) {
	//	_D[i] *= 32767.0;
	//}
}

void synthesis_subband_filter(const float s[32], const uint8_t ch, const uint8_t nch, uint8_t* pcm_buf, uint32_t* off)
{
	static __m128 f4_32768 = { 32768.0f, 32768.0f, 32768.0f, 32768.0f };
	int i;
	__m128 f4_sum[8] = { 0 };
	float f_tmp[8 * 4];

	// Shifting
	memcpy_dword(_V[ch] + 1023, _V[ch] + 1023 - 64, 960, 1);	// using loop queue?

	// Matrixing (DCT(32 -> 64))
	dct32to64(s, ch);

	/*
	* Build a 512 values vector U
	* Window by 512 coefficients
	*/
	for (i = 0; i < 512; i += 64) {
#if 1
		__m128 f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2]), _mm_loadu_ps(&_D[i]));
		_mm_storeu_ps(&_U[i], f4_U);
		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 96]), _mm_loadu_ps(&_D[i + 32]));
		_mm_storeu_ps(&_U[i + 32], f4_U);

		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 4]), _mm_loadu_ps(&_D[i + 4]));
		_mm_storeu_ps(&_U[i + 4], f4_U);
		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 96 + 4]), _mm_loadu_ps(&_D[i + 32 + 4]));
		_mm_storeu_ps(&_U[i + 32 + 4], f4_U);

		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 8]), _mm_loadu_ps(&_D[i + 8]));
		_mm_storeu_ps(&_U[i + 8], f4_U);
		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 96 + 8]), _mm_loadu_ps(&_D[i + 32 + 8]));
		_mm_storeu_ps(&_U[i + 32 + 8], f4_U);

		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 12]), _mm_loadu_ps(&_D[i + 12]));
		_mm_storeu_ps(&_U[i + 12], f4_U);
		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 96 + 12]), _mm_loadu_ps(&_D[i + 32 + 12]));
		_mm_storeu_ps(&_U[i + 32 + 12], f4_U);

		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 16]), _mm_loadu_ps(&_D[i + 16]));
		_mm_storeu_ps(&_U[i + 16], f4_U);
		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 96 + 16]), _mm_loadu_ps(&_D[i + 32 + 16]));
		_mm_storeu_ps(&_U[i + 32 + 16], f4_U);

		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 20]), _mm_loadu_ps(&_D[i + 20]));
		_mm_storeu_ps(&_U[i + 20], f4_U);
		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 96 + 20]), _mm_loadu_ps(&_D[i + 32 + 20]));
		_mm_storeu_ps(&_U[i + 32 + 20], f4_U);

		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 24]), _mm_loadu_ps(&_D[i + 24]));
		_mm_storeu_ps(&_U[i + 24], f4_U);
		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 96 + 24]), _mm_loadu_ps(&_D[i + 32 + 24]));
		_mm_storeu_ps(&_U[i + 32 + 24], f4_U);

		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 28]), _mm_loadu_ps(&_D[i + 28]));
		_mm_storeu_ps(&_U[i + 28], f4_U);
		f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 96 + 28]), _mm_loadu_ps(&_D[i + 32 + 28]));
		_mm_storeu_ps(&_U[i + 32 + 28], f4_U);
#else
		for (j = 0; j < 32; j += 4) {
			__m128 f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + j]), _mm_loadu_ps(&_D[i + j]));
			_mm_storeu_ps(&_U[i + j], f4_U);
			f4_U = _mm_mul_ps(_mm_loadu_ps(&_V[ch][i * 2 + 96 + j]), _mm_loadu_ps(&_D[i + 32 + j]));
			_mm_storeu_ps(&_U[i + 32 + j], f4_U);
		}
#endif
	}

	/*
	* Calculate 32 Samples
	* Output 32 reconstructed PCM Samples
	*/
	for (i = 0; i < 512; i += 32) {
		f4_sum[0] = _mm_add_ps(f4_sum[0], _mm_loadu_ps(_U + i));
		f4_sum[1] = _mm_add_ps(f4_sum[1], _mm_loadu_ps(_U + i + 4));
		f4_sum[2] = _mm_add_ps(f4_sum[2], _mm_loadu_ps(_U + i + 8));
		f4_sum[3] = _mm_add_ps(f4_sum[3], _mm_loadu_ps(_U + i + 12));
		f4_sum[4] = _mm_add_ps(f4_sum[4], _mm_loadu_ps(_U + i + 16));
		f4_sum[5] = _mm_add_ps(f4_sum[5], _mm_loadu_ps(_U + i + 20));
		f4_sum[6] = _mm_add_ps(f4_sum[6], _mm_loadu_ps(_U + i + 24));
		f4_sum[7] = _mm_add_ps(f4_sum[7], _mm_loadu_ps(_U + i + 28));
	}

	f4_sum[0] = _mm_mul_ps(f4_sum[0], f4_32768);
	_mm_storeu_ps(f_tmp, f4_sum[0]);
	f4_sum[1] = _mm_mul_ps(f4_sum[1], f4_32768);
	_mm_storeu_ps(f_tmp + 4, f4_sum[1]);
	f4_sum[2] = _mm_mul_ps(f4_sum[2], f4_32768);
	_mm_storeu_ps(f_tmp + 8, f4_sum[2]);
	f4_sum[3] = _mm_mul_ps(f4_sum[3], f4_32768);
	_mm_storeu_ps(f_tmp + 12, f4_sum[3]);
	f4_sum[4] = _mm_mul_ps(f4_sum[4], f4_32768);
	_mm_storeu_ps(f_tmp + 16, f4_sum[4]);
	f4_sum[5] = _mm_mul_ps(f4_sum[5], f4_32768);
	_mm_storeu_ps(f_tmp + 20, f4_sum[5]);
	f4_sum[6] = _mm_mul_ps(f4_sum[6], f4_32768);
	_mm_storeu_ps(f_tmp + 24, f4_sum[6]);
	f4_sum[7] = _mm_mul_ps(f4_sum[7], f4_32768);
	_mm_storeu_ps(f_tmp + 28, f4_sum[7]);

#if 1
	// Output reconstructed PCM Sample
	for (i = 0; i < 32; i += 4) {
		int16_t* pcm_out = (int16_t*)(pcm_buf + *off);
		if (f_tmp[i] >= 32766.5f) {
			pcm_out[0] = 32767;
		} else if (f_tmp[i] <= -32767.5f) {
			pcm_out[0] = -32768;
		} else if ((pcm_out[0] = (int16_t)(f_tmp[i] + 0.5f)) < 0)
			pcm_out[0] -= 1;
		///////////
		if (f_tmp[i + 1] >= 32766.5f) {
			pcm_out[2] = 32767;
		} else if (f_tmp[i + 1] <= -32767.5f) {
			pcm_out[2] = -32768;
		} else if ((pcm_out[2] = (int16_t)(f_tmp[i + 1] + 0.5f)) < 0)
			pcm_out[2] -= 1;

		///////////
		if (f_tmp[i + 2] >= 32766.5f) {
			pcm_out[4] = 32767;
		} else if (f_tmp[i + 2] <= -32767.5f) {
			pcm_out[4] = -32768;
		} else if ((pcm_out[4] = (int16_t)(f_tmp[i + 2] + 0.5f)) < 0)
			pcm_out[4] -= 1;
		///////////
		if (f_tmp[i + 3] >= 32766.5f) {
			pcm_out[6] = 32767;
		} else if (f_tmp[i + 3] <= -32767.5f) {
			pcm_out[6] = -32768;
		} else if ((pcm_out[6] = (int16_t)(f_tmp[i + 3] + 0.5f)) < 0)
			pcm_out[6] -= 1;
		////////

		if (nch == 1) {
			pcm_out[1] = pcm_out[0];
			pcm_out[3] = pcm_out[2];
			pcm_out[5] = pcm_out[4];
			pcm_out[7] = pcm_out[6];
		}
		*off += 16;
	}
#else
	write_f4_s2_samples(f_tmp, nch, (short*)(pcm_buf + *off), off);
	write_f4_s2_samples(f_tmp + 4, nch, (short*)(pcm_buf + *off), off);
	write_f4_s2_samples(f_tmp + 8, nch, (short*)(pcm_buf + *off), off);
	write_f4_s2_samples(f_tmp + 12, nch, (short*)(pcm_buf + *off), off);
	write_f4_s2_samples(f_tmp + 16, nch, (short*)(pcm_buf + *off), off);
	write_f4_s2_samples(f_tmp + 20, nch, (short*)(pcm_buf + *off), off);
	write_f4_s2_samples(f_tmp + 24, nch, (short*)(pcm_buf + *off), off);
	write_f4_s2_samples(f_tmp + 28, nch, (short*)(pcm_buf + *off), off);
#endif
}
