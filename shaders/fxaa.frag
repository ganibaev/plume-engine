#version 460

layout (location = 0) in vec2 inTexCoords;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D frameTexture;

layout (push_constant) uniform constants
{
	bool USE_FXAA;
};

vec3 applyFxaa(vec2 uvInterp, sampler2D frameTex)
{
	const float FXAA_SPAN_MAX = 8.0;
	const float FXAA_REDUCE_MUL = 1.0 / 8.0;
	const float FXAA_REDUCE_MIN = 1.0 / 128.0;

	vec2 pixelSize = 1.0 / textureSize(frameTexture, 0);

	vec3 rgbNW = textureLod(frameTex, uvInterp + vec2(-1.0, -1.0) * pixelSize, 0.0).xyz;
	vec3 rgbNE = textureLod(frameTex, uvInterp + vec2(1.0, -1.0) * pixelSize, 0.0).xyz;
	vec3 rgbSW = textureLod(frameTex, uvInterp + vec2(-1.0, 1.0) * pixelSize, 0.0).xyz;
	vec3 rgbSE = textureLod(frameTex, uvInterp + vec2(1.0, 1.0) * pixelSize, 0.0).xyz;

	vec3 rgbM = textureLod(frameTex, uvInterp, 0.0).xyz;
	vec3 lumaConversion = vec3(0.299, 0.587, 0.114);
	float lumaNW = dot(rgbNW, lumaConversion);
	float lumaNE = dot(rgbNE, lumaConversion);
	float lumaSW = dot(rgbSW, lumaConversion);
	float lumaSE = dot(rgbSE, lumaConversion);
	float lumaM = dot(rgbM, lumaConversion);
	float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

	vec2 dir;
	dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
	dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

	float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);

	float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
	dir = min(vec2(FXAA_SPAN_MAX, FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX), dir * rcpDirMin)) * pixelSize;

	vec3 rgbA = 0.5 * (textureLod(frameTex, uvInterp + dir * (1.0 / 3.0 - 0.5), 0.0).xyz +
		textureLod(frameTex, uvInterp + dir * (2.0 / 3.0 - 0.5), 0.0).xyz);
	vec3 rgbB = rgbA * 0.5 + 0.25 * (textureLod(frameTex, uvInterp + dir * -0.5, 0.0).xyz + textureLod(frameTex, uvInterp + dir * 0.5, 0.0).xyz);

	float lumaB = dot(rgbB, lumaConversion);
	if ((lumaB < lumaMin) || (lumaB > lumaMax))
	{
		return rgbA;
	}

	return rgbB;
}

void main()
{
	if (USE_FXAA)
	{
		outColor = vec4(applyFxaa(inTexCoords, frameTexture), 1.0);
	}
	else
	{
		outColor = textureLod(frameTexture, inTexCoords, 0.0);
	}
}