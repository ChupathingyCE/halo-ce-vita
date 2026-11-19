/*
VITA_PIXEL_KEY.H

The Vita's pixel shader key: everything a pixel program its Cg translator
writes depends on (platform/nv2a_psh_cg.c), and what its program cache is
keyed by. The game's own key (its port/linux/src/nv2a_pixel_key.h) is the
OpenGL renderer's, renamed out of the way by vita_xgpu.h.
*/

#ifndef __VITA_PIXEL_KEY_H
#define __VITA_PIXEL_KEY_H

struct nv2a_pixel_shader_key
{
	DWORD combiner_state[D3DRS_PS_MAX];
	/* D3DRS_PSTEXTUREMODES lies past D3DRS_PS_MAX */
	DWORD texture_modes;
	unsigned char sampler_type[4];
	unsigned char alpha_kill[4];
	/* D3DTSS_COLORSIGN: channels (bit 0 alpha ... bit 3 blue, as
	D3DTSIGN_*) that hold signed data in an unsigned texture format */
	unsigned char color_sign[4];
	/* D3DCMP_* function for the alpha test, or 0 when disabled */
	unsigned long alpha_test_function;
	unsigned char fog_enable;
	unsigned char fog_table_mode;
	/* inside a visibility test: count the samples that pass (Android) */
	unsigned char count_samples;
	unsigned char pad;
	/* (Vita) stages whose 2D fetch takes the varying itself: the vertex
	program leaves the coordinate's w at 1 and the texture is swizzled
	(scale 1), so the read does not depend on fragment arithmetic and the
	SGX prefetches it (a computed coordinate stalls the texture unit) */
	unsigned char raw_coordinates;
	/* (Vita) stages whose 2D fetch is a projective read of the varying
	(the program writes its w): tex2Dproj, the divide done by the iterator */
	unsigned char projective_coordinates;
	/* (Vita) the blend's constant factor folded into the program: its
	output becomes o * scale + offset, two rows past the fragment uniforms'
	second buffer (vita_xgpu.h VITA_FU_BLEND_SCALE, blend_constant.h) */
	unsigned char blend_fold;
	/* (Vita) of the raw_coordinates stages, those whose coordinates reach
	far (halo_d3d_texcoord_extent, d3d8_gxm.c), wrapped into [0, 1) in the
	program and fetched with the unwrapped coordinates' derivatives */
	unsigned char wrapped_coordinates;
	/* (Vita) a volume texture's stage: log2 of its slices and of a slice's
	width, laid side by side in one 2D texture (vita_textures.c); 0 else */
	unsigned char volume_slices_log2[4];
	unsigned char volume_width_log2[4];
	/* (Vita) stages addressed D3DTADDRESS_BORDER (GXM has no border
	mode: the program returns the border colour, D3DCOLOR, outside the
	texture) */
	unsigned char border_mask;
	/* (Vita) the alpha test without a discard: a fragment failing it
	returns a colour the draw's blend leaves the target unchanged with -
	1 zero (ONE or INVSRCALPHA or INVSRCCOLOR destination factor),
	2 one (a plain multiply) - d3d8_gxm.c alpha_test_neutral; 0 discard */
	unsigned char alpha_test_neutral;
	unsigned char border_pad[2];
	DWORD border_color[4];
};

#endif
