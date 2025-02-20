// Inputs
Texture2D<float4> Input : register(t0);

// Outputs
RWTexture2D<float4> Output : register(u0);

// Define the thread group size
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // Read input color
    float heightVal = Input.Load(uint3(id.xy, 0)).r;
    // Write final output
    Output[id.xy] = float4(0, 0, 0, heightVal);
}
