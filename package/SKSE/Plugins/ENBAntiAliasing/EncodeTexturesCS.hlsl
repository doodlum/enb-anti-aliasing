Texture2D<float> Depth : register(t0);
Texture2D<float2> MotionVectorsInput : register(t1);
Texture2D<float2> TAAMask : register(t2);

RWTexture2D<float2> MotionVectorsOutput : register(u0);
RWTexture2D<float> AlphaMask : register(u1);

cbuffer PerFrame : register(b12)
{
	row_major float4x4 CameraView[1] : packoffset(c0);
	row_major float4x4 CameraProj[1] : packoffset(c4);
	row_major float4x4 CameraViewProj[1] : packoffset(c8);
	row_major float4x4 CameraViewProjUnjittered[1] : packoffset(c12);
	row_major float4x4 CameraPreviousViewProjUnjittered[1] : packoffset(c16);
	row_major float4x4 CameraProjUnjittered[1] : packoffset(c20);
	row_major float4x4 CameraProjUnjitteredInverse[1] : packoffset(c24);
	row_major float4x4 CameraViewInverse[1] : packoffset(c28);
	row_major float4x4 CameraViewProjInverse[1] : packoffset(c32);
	row_major float4x4 CameraProjInverse[1] : packoffset(c36);
	float4 CameraPosAdjust[1] : packoffset(c40);
	float4 CameraPreviousPosAdjust[1] : packoffset(c41);  // fDRClampOffset in w
	float4 FrameParams : packoffset(c42);                 // inverse fGamma in x, some flags in yzw
	float4 DynamicResolutionParams1 : packoffset(c43);    // fDynamicResolutionWidthRatio in x,
														  // fDynamicResolutionHeightRatio in y,
														  // fDynamicResolutionPreviousWidthRatio in z,
														  // fDynamicResolutionPreviousHeightRatio in w
	float4 DynamicResolutionParams2 : packoffset(c44);    // inverse fDynamicResolutionWidthRatio in x, inverse
														  // fDynamicResolutionHeightRatio in y,
														  // fDynamicResolutionWidthRatio - fDRClampOffset in z,
														  // fDynamicResolutionPreviousWidthRatio - fDRClampOffset in w
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	float2 motionVectors;
	if (Depth[dispatchID.xy] == 1.0)
	{
		float2 uv = float2(dispatchID.xy + 0.5) * rcp(float2(3440, 1440));

		float4 positionCS = float4(2 * float2(uv.x, -uv.y + 1) - 1, 1, 1);
		positionCS = mul(CameraViewProjInverse[0], positionCS);
		positionCS.xyz = positionCS.xyz / positionCS.w;

		float4 screenPosition = mul(CameraViewProjUnjittered[0], positionCS);
		float4 previousScreenPosition = mul(CameraPreviousViewProjUnjittered[0], positionCS);
		screenPosition.xy = screenPosition.xy / screenPosition.ww;
		previousScreenPosition.xy = previousScreenPosition.xy / previousScreenPosition.ww;
		motionVectors = float2(-0.5, 0.5) * (screenPosition.xy - previousScreenPosition.xy);
	} else {
		motionVectors = MotionVectorsInput[dispatchID.xy];
	}

	MotionVectorsOutput[dispatchID.xy] = motionVectors;

	float2 taaMask = TAAMask[dispatchID.xy];	

	float alphaMask = taaMask.x * 0.5;
	alphaMask = lerp(alphaMask, 1.0, taaMask.y);

	AlphaMask[dispatchID.xy] = alphaMask;
}
