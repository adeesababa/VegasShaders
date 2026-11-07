// (2026-11-07) Scene `denoisepeople`: RtDenoise's RGB temporal pass (RtDenoiseTemporalRgb.spv, the path tracer's denoiser) with
// Settings.PeopleHistory: people's pixels (TESR_CapturedNormalsBuffer alpha > 1.5) keep at most PeopleHistory frames of history.
//
// A tester's video (PTRealtime, dialogue): the NPC's mouth and the hat's shadow trailed behind as he talked; a face changes shape with no
// motion the reprojection knows of, and the path tracer's denoiser kept 32 frames there. Here a still camera over a floor, a history of
// 32 frames of black, a new frame of white, the captured normals marking the left half as a person's: with PeopleHistory 4 the left
// half must blend by 1 / 5 (history capped to 4, + this frame) and the right half by 1 / 32 (MaxHistory); with 0 both by 1 / 32.
static bool sceneDenoisePeople(Ctx& c) {
	Host& h = *c.host;
	// the pass with its two targets (as RtDenoise.cpp Make("RtDenoiseTemporalRgb", 7, 192, 2))
	FragPass pass;
	{
		FILE* f = nullptr; const std::string file = gM.compute + "\\RtDenoiseTemporalRgb.spv"; fopen_s(&f, file.c_str(), "rb");
		if (!f) { say("  error: no %s\n", file.c_str()); return false; }
		std::vector<char> spirv; char buf[65536]; size_t n;
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0) spirv.insert(spirv.end(), buf, buf + n);
		fclose(f);
		NVR_EXT_BINDING_DESC bindings[8] = {};
		for (UINT i = 0; i < 7; i++) bindings[i] = { 1, i, NVR_EXT_BIND_SAMPLED_IMAGE, i, NVR_EXT_VIEW_2D, FALSE, 0 };
		bindings[7] = { 0, 0, NVR_EXT_BIND_SAMPLER, 0, NVR_EXT_VIEW_2D, FALSE, 192 - 16 };
		NVR_EXT_GRAPHICS_PIPELINE_DESC desc = {};
		desc.pFragmentSpirv = spirv.data(); desc.FragmentSpirvSize = UINT(spirv.size());
		desc.BindingCount = 8; desc.pBindings = bindings; desc.PushConstantSize = 192;
		desc.UsesSamplerHeap = TRUE; desc.SamplerHeapSet = 0; desc.SamplerHeapBinding = 0;
		desc.ColorTargetCount = 2; desc.Blend = NVR_EXT_BLEND_NONE; desc.ColorWriteMask = 0xF; desc.pDebugName = "RtDenoiseTemporalRgb";
		CHECKHR(h.ext->CreateGraphicsPipeline(&desc, &pass.pipe), "CreateGraphicsPipeline");
		pass.images = 7; pass.push = 192;
		if (!gPointClamp) { NVR_EXT_SAMPLER_DESC sd = { NVR_EXT_FILTER_NEAREST, NVR_EXT_ADDRESS_CLAMP }; CHECKHR(h.ext->CreateSampler(&sd, &gPointClamp), "CreateSampler"); }
	}
	World w;
	Mesh floor; floor.quad({ -8000, -8000, 0 }, { 8000, -8000, 0 }, { 8000, 8000, 0 }, { -8000, 8000, 0 });
	w.meshes = { floor };
	Object f; f.mesh = 0; w.objects = { f };
	const Camera cam = Camera::lookDir({ 0, 0, 3000 }, { 0, 0, -1 }, { 0, 1, 0 }, 1.5f, 1.5f);
	std::vector<GPixel> px; std::vector<float> dep, nor;
	gbuffer(w, cam, px, dep, nor);
	Tex noisy, depth, histA, histB, captured, outA, outB;
	if (!makeTex(h, S, S, D3DFMT_A16B16G16R16F, false, noisy) || !makeTex(h, S, S, D3DFMT_R32F, false, depth) ||
		!makeTex(h, S, S, D3DFMT_A16B16G16R16F, false, histA) || !makeTex(h, S, S, D3DFMT_A16B16G16R16F, false, histB) ||
		!makeTex(h, S, S, D3DFMT_A16B16G16R16F, false, captured) ||
		!makeTex(h, S, S, D3DFMT_A16B16G16R16F, true, outA) || !makeTex(h, S, S, D3DFMT_A16B16G16R16F, true, outB)) return false;
	std::vector<float> white(size_t(S) * S * 4, 1.0f), black32(size_t(S) * S * 4, 0.0f), hb(size_t(S) * S * 4, 0.0f), cap(size_t(S) * S * 4, 0.0f);
	for (UINT i = 0; i < S * S; i++) {
		black32[i * 4 + 3] = 32.0f;                          // 32 frames of history, black
		hb[i * 4 + 1] = dep[i] * cam.farZ;                   // the history's view depth (the same surface)
		cap[i * 4 + 3] = (i % S) < S / 2 ? 2.0f : 1.0f;      // the left half: a person's pixel (alpha 2); the right: a lit surface (1)
	}
	if (!fillTex(noisy, white, 4) || !fillTex(depth, dep, 1) || !fillTex(histA, black32, 4) || !fillTex(histB, hb, 4) || !fillTex(captured, cap, 4)) return false;
	struct Push { float prevViewProj[16], forward[4], right[4], up[4], move[4], params[4], backbone[4], rect[4]; uint32_t samplerIndex; float clampK, peopleCap; uint32_t pad; };
	static_assert(sizeof(Push) == 192, "RtDenoise.cpp's push block");
	bool ok = true;
	for (float people : { 4.0f, 0.0f }) {
		Push t = {};
		const V3 ax = cam.r * cam.p00, ay = cam.u * cam.p11, aw = cam.f;   // RtDenoise's camera-relative view-projection (clip = M * p)
		for (int col = 0; col < 3; col++) { t.prevViewProj[col * 4 + 0] = (&ax.x)[col]; t.prevViewProj[col * 4 + 1] = (&ay.x)[col]; t.prevViewProj[col * 4 + 3] = (&aw.x)[col]; }
		put4(t.forward, cam.f, cam.farZ); put4(t.right, cam.r * (1.0f / cam.p00), 0); put4(t.up, cam.u * (1.0f / cam.p11), 0);
		put4(t.move, V3(0, 0, 0), 0.0f);
		put4(t.params, 32.0f, 0.03f, 1.0f / S, 1.0f / S);
		put4(t.rect, 1, 1, 1, 1);
		t.peopleCap = people;
		IUnknown* images[7] = { noisy.t, histA.t, depth.t, histB.t, depth.t, depth.t, captured.t };
		NVR_EXT_RESOURCE_BINDING b[8] = {};
		for (UINT i = 0; i < 7; i++) { b[i].Slot = i; b[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; b[i].pResource = images[i]; }
		b[7].Slot = 0; b[7].Type = NVR_EXT_BIND_SAMPLER; b[7].pResource = gPointClamp;
		NVR_EXT_DRAW_DESC d = {};
		d.pPipeline = pass.pipe; d.TargetCount = 2; d.Targets[0].pResource = outA.s; d.Targets[1].pResource = outB.s;
		d.BindingCount = 8; d.pBindings = b; d.pPushData = &t; d.PushDataSize = 192;
		CHECKHR(h.ext->Draw(&d), "Draw");
		std::vector<float> out;
		if (!readTex(h, outA, out, 4)) return false;
		int checked = 0, bad = 0, shown = 0;
		for (UINT j = 2; j < S - 2; j++) for (UINT i = 2; i < S - 2; i++) {
			if (!px[j * S + i].valid || i == S / 2 - 1 || i == S / 2) continue;
			const bool person = i < S / 2;
			const float want = person && people > 0.0f ? 1.0f / 5.0f : 1.0f / 32.0f, wantLen = person && people > 0.0f ? 5.0f : 32.0f;
			const float* o = &out[(j * S + i) * 4];
			checked++;
			if (std::fabs(o[0] - want) > 2e-3f || std::fabs(o[3] - wantLen) > 1e-2f) { bad++; if (shown++ < 4) say("    pixel (%u, %u) %s: %.4f (length %.2f), want %.4f (%.0f)\n", i, j, person ? "person" : "surface", o[0], o[3], want, wantLen); }
		}
		const bool pass1 = bad == 0 && checked > int(S * S) / 2;
		say("  RGB temporal, PeopleHistory %.0f: person pixels blend %s, others 1/32; %d pixels checked, %d wrong -> %s\n", people,
			people > 0.0f ? "1/5 (history capped to 4)" : "1/32 (no cap)", checked, bad, pass1 ? "ok" : "BAD");
		if (!pass1) ok = false;
	}
	return ok;
}
