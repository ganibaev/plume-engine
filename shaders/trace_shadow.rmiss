#version 460
#extension GL_EXT_ray_tracing : require

layout (location = 1) rayPayloadInEXT bool shadowRayHit;

void main()
{
	shadowRayHit = false;
}