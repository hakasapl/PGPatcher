// Inputs
Texture2D<float4> Input : register(t0);

cbuffer params : register(b0)
{
    float fAlbedoSatPower;
    float fAlbedoNorm;
};

// Outputs
RWTexture2D<float4> Output : register(u0);

// Define the thread group size
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float4 origPixel = Input.Load(uint3(id.xy, 0));

    // Read input color
    float3 color = origPixel.rgb;

    float3 albedo = pow(max(0, color), fAlbedoSatPower * length(color));
    albedo = saturate(lerp(albedo, normalize(albedo), fAlbedoNorm));

    // Write final output
    Output[id.xy] = float4(albedo, origPixel.a);
}
