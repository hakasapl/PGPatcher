// Test shader of PGD3DTest: halves every channel of the input texture.
Texture2D<float4> Input : register(t0);
RWTexture2D<float4> Output : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Output[id.xy] = Input.Load(uint3(id.xy, 0)) * 0.5;
}
