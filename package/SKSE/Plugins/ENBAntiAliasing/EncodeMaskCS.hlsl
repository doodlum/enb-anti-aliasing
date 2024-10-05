Texture2D<float2> InputTexture : register(t0);
RWTexture2D<float> OutputTexture : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {

	float2 taaMask = InputTexture[DTid.xy];								
	OutputTexture[DTid.xy] = (0.5 * taaMask.x) + taaMask.y;
}
