#version 460

layout (location = 0) in vec2 inTexCoords;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D frameTexture;

layout (push_constant) uniform constants
{
	bool USE_DENOISER;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//  Copyright (c) 2018-2019 Michele Morrone
//  All rights reserved.
//
//  https://michelemorrone.eu - https://BrutPitt.com
//
//  me@michelemorrone.eu - brutpitt@gmail.com
//  twitter: @BrutPitt - github: BrutPitt
//  
//  https://github.com/BrutPitt/glslSmartDeNoise/
//
//  This software is distributed under the terms of the BSD 2-Clause license
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#define INV_SQRT_OF_2PI 0.39894228040143267793994605993439  // 1.0/SQRT_OF_2PI
#define INV_PI          0.31830988618379067153776752674503

//  Parameters:
//      sampler2D tex     - sampler image / texture
//      vec2 uv           - actual fragment coord
//      float sigma  >  0 - sigma Standard Deviation
//      float kSigma >= 0 - sigma coefficient
//          kSigma * sigma  -->  radius of the circular kernel
//      float threshold   - edge sharpening threshold
vec4 morroneDenoiser(sampler2D tex, vec2 uv, float sigma, float kSigma, float threshold)
{
    float radius = round(kSigma * sigma);
    float radQ = radius * radius;

    float invSigmaQx2 = .5 / (sigma * sigma);      // 1.0 / (sigma^2 * 2.0)
    float invSigmaQx2PI = INV_PI * invSigmaQx2;    // // 1/(2 * PI * sigma^2)

    float invThresholdSqx2 = .5 / (threshold * threshold);     // 1.0 / (sigma^2 * 2.0)
    float invThresholdSqrt2PI = INV_SQRT_OF_2PI / threshold;   // 1.0 / (sqrt(2*PI) * sigma)

    vec4 centrPx = texture(tex, uv);

    float zBuff = 0.0;
    vec4 aBuff = vec4(0.0);
    vec2 size = vec2(textureSize(tex, 0));

    vec2 d;
    for (d.x = -radius; d.x <= radius; ++d.x) {
        float pt = sqrt(radQ - d.x * d.x);       // pt = yRadius: have circular trend
        for (d.y=-pt; d.y <= pt; d.y++) {
            float blurFactor = exp(-dot(d , d) * invSigmaQx2) * invSigmaQx2PI;

            vec4 walkPx = texture(tex, uv + d / size);
            
            vec4 dC = walkPx - centrPx;
            float deltaFactor = exp(-dot(dC.rgb, dC.rgb) * invThresholdSqx2) * invThresholdSqrt2PI * blurFactor;

            zBuff += deltaFactor;
            aBuff += deltaFactor * walkPx;
        }
    }
    return aBuff / zBuff;
}

// from http://filmicworlds.com/blog/filmic-tonemapping-operators/
vec3 unchartedTonemapImpl(vec3 color)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

vec3 unchartedTonemap(vec3 color)
{
    const float W = 11.2;
    const float exposureBias = 2.0;
    color = unchartedTonemapImpl(color * exposureBias);
    vec3 whiteScale = 1.0 / unchartedTonemapImpl(vec3(W));
    return color * whiteScale;
}


void main()
{
    float sigma = 1.0;
    float kSigma = 1.0;
    float threshold = 0.075;


    if (USE_DENOISER)
    {
        outColor = morroneDenoiser(frameTexture, inTexCoords, sigma, kSigma, threshold);
    }
    else
    {
        outColor = vec4(texture(frameTexture, inTexCoords).rgb, 1.0);
    }

	outColor = vec4(unchartedTonemap(outColor.rgb), 1.0);
}