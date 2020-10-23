#define SAMPLE_MEAN_BITS 15
#define SAMPLE_STD_BITS 13
#define GH_ORDER_BITS 4

#define SAMPLE_MEAN_MAX_UINT ((1 << SAMPLE_MEAN_BITS) - 1)
#define SAMPLE_STD_MAX_UINT ((1 << SAMPLE_STD_BITS) - 1)
#define GH_ORDER_MAX_UINT ((1 << GH_ORDER_BITS) - 1)

float offsetCalc(in uint x, in uint level)
{	
	uint den = (1 << level); // den = 1,2,4 when level = 0,1,2
	return (x == 0 ? 0.5f  / den : 1.0f - 0.5f / den); 
}
/*
uint packComponent(in float m, in float v, in uint o)
{
	o &= GH_ORDER_MAX_UINT;
	o <<= (32 - GH_ORDER_BITS);

	uint t = uint(v * SAMPLE_STD_MAX_UINT);
	t &= SAMPLE_STD_MAX_UINT;
	t <<= (32 - SAMPLE_STD_BITS - GH_ORDER_BITS);
	o |= t;

	t = uint(m * SAMPLE_MEAN_MAX_UINT);
	t &= SAMPLE_MEAN_MAX_UINT;
	o |= t;

	return o;
}*/

uvec2 packSampleStat(in vec2 m, in vec2 v, in uvec2 o)
{
	o &= GH_ORDER_MAX_UINT;
	o <<= (32 - GH_ORDER_BITS);

	uvec2 t = uvec2(v * SAMPLE_STD_MAX_UINT);
	t &= SAMPLE_STD_MAX_UINT;
	t <<= (32 - SAMPLE_STD_BITS - GH_ORDER_BITS);
	o |= t;

	t = uvec2(m * SAMPLE_MEAN_MAX_UINT);
	t &= SAMPLE_MEAN_MAX_UINT;
	o |= t;

	return o;
}

void unpackSampleStat(in uvec2 c, out vec2 m, out vec2 v, out uvec2 o) 
{
	m = vec2(c & SAMPLE_MEAN_MAX_UINT) / SAMPLE_MEAN_MAX_UINT;
	v = vec2((c >> SAMPLE_MEAN_BITS) & SAMPLE_STD_MAX_UINT) / SAMPLE_STD_MAX_UINT;
	o = (c >> (SAMPLE_STD_BITS + SAMPLE_MEAN_BITS));
}

/*
uvec2 packSampleStat(in vec2 mean, in vec2 var, in uvec2 order)
{	
	//return uvec2(packComponent(mean.x, var.x, order_x), packComponent(mean.y, var.y, order_y));
}
*/
/*
void unpackSampleStat(in uvec2 d, out vec2 mean, out vec2 var, out uint order_x, out uint order_y)
{
	unpackComponent(d.x, mean.x, var.x, order_x);
	unpackComponent(d.y, mean.y, var.y, order_y);
}*/