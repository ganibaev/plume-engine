#if !defined(PATH_TRACING_UTILS_GLSL)
#define PATH_TRACING_UTILS_GLSL

#include "hit_properties.glsl"


vec2 HiresToLowres(ivec2 ipos, vec2 jitterOffset)
{
    vec2 inputSize = vec2(gl_LaunchSizeEXT);
    vec2 outputSize = vec2(gl_LaunchSizeEXT);

    return (vec2(ipos) + vec2(0.5)) * (inputSize / outputSize) - jitterOffset;
}


float GetSampleWeight(vec2 delta, float scale)
{
    return clamp(1 - scale * dot(delta, delta), 0, 1);
}


HitProperties TraceRay(vec3 origin, vec3 direction, float tMin, float tMax, uint rayFlags)
{
	HitProperties resHitProperties;
	InitHitProperties(resHitProperties);

	if (rayConstants.USE_SHADER_EXECUTION_REORDERING)
	{
		hitObjectNV hObj;
		hitObjectRecordEmptyNV(hObj);

		hitObjectTraceRayNV(hObj,
			TLAS,	           // TLAS
			rayFlags,		   // flags
			0xFF,			   // cull mask
			0,				   // SBT record offset
			0,				   // SBT record stride
			0,				   // miss shader index
			origin,	           // ray origin
			tMin,			   // ray min range
			direction,         // ray direction
			tMax,			   // ray max range
			0				   // ray payload location
		);

		reorderThreadNV(hObj);
		hitObjectGetAttributesNV(hObj, 0);

		if (hitObjectIsHitNV(hObj))
		{
			int instId = hitObjectGetInstanceCustomIndexNV(hObj);
			int primId = hitObjectGetPrimitiveIndexNV(hObj);
			mat4x3 objectToWorld = hitObjectGetObjectToWorldNV(hObj);
			mat4x3 worldToObject = hitObjectGetWorldToObjectNV(hObj);

			if (primId >= 0 && instId >= 0)
			{
				resHitProperties = GetHitProperties(instId, primId, objectToWorld, worldToObject, hitUV.xy);
			}
		}
		else
		{
			// vec3 rayMissDirection = normalize(hitObjectGetWorldRayDirectionNV(hObj));
			// vec3 missValue = texture(skyboxSampler, rayMissDirection).rgb;
			vec3 missValue = vec3(253.0f / 255.0f, 251.0f / 255.0f, 211.0f / 255.0f) * 5;
			OnRayMiss(rayPayload, missValue);
		}
	}
	else
	{
		traceRayEXT(TLAS,	   // TLAS
			rayFlags,		   // flags
			0xFF,			   // cull mask
			0,				   // SBT record offset
			0,				   // SBT record stride
			0,				   // miss shader index
			origin,	           // ray origin
			tMin,			   // ray min range
			direction,         // ray direction
			tMax,			   // ray max range
			0				   // ray payload location
		);

		resHitProperties.texCoord = rayPayload.texCoord;
		resHitProperties.worldPos = rayPayload.hitPosition;
		resHitProperties.tangent = rayPayload.tangent;
		resHitProperties.bitangent = rayPayload.bitangent;
		resHitProperties.normal = rayPayload.normal;
		resHitProperties.emittance = rayPayload.emittance;
		resHitProperties.matID = rayPayload.matID;
	}

	return resHitProperties;
}


bool ShadowRayHit(vec3 origin, vec3 direction, float distToLight)
{
	uint shadowFlags = gl_RayFlagsTerminateOnFirstHitEXT;

	TraceRay(origin, direction, 0.001, distToLight, shadowFlags);

	return rayPayload.hasMissed;
}


float CalculateCurrentFrameWeightAndMotion(vec3 primaryHitPos, vec2 frameUV, vec2 subpixelJitter, out vec2 motion)
{
	vec3 worldSpacePositionCurr = primaryHitPos;
	vec3 worldSpacePositionPrev = primaryHitPos;

	vec4 screenPosCurr = (CAM_DATA.viewproj * vec4(worldSpacePositionCurr, 1));
	screenPosCurr /= screenPosCurr.w;

	vec4 screenPosPrev = (CAM_DATA.prevViewProj * vec4(worldSpacePositionPrev, 1));
	screenPosPrev /= screenPosPrev.w;

	vec2 prevUV = screenPosPrev.xy * 0.5 + 0.5;
	vec2 currUV = screenPosCurr.xy * 0.5 + 0.5;
	motion = prevUV - currUV + vec2(0.5) / vec2(gl_LaunchSizeEXT.xy);

	float uvDiffLength = length(motion);

	vec2 nearestRenderPos = HiresToLowres(ivec2(gl_LaunchIDEXT.xy), subpixelJitter);
	ivec2 intRenderPos = ivec2(round(nearestRenderPos.x), round(nearestRenderPos.y));
	intRenderPos = clamp(intRenderPos, ivec2(0), ivec2(gl_LaunchSizeEXT.x - 1, gl_LaunchSizeEXT.y - 1));

	float sampleWeight = GetSampleWeight(nearestRenderPos - intRenderPos, 1.0);

	float resCurrentFrameWeight = clamp(max(smoothstep(0, 1.0, uvDiffLength), sampleWeight) * 0.1, 0.0, 1.0);

	vec3 oldPosition = texture(prevPositions, frameUV + motion).xyz;

	bool posRejected = length(oldPosition - primaryHitPos) > 2.5;

	if (clamp(prevUV, 0.0, 1.0) != prevUV || posRejected)
	{
		resCurrentFrameWeight = 1.0;
	}

	return resCurrentFrameWeight;
}

#endif // PATH_TRACING_UTILS_GLSL
