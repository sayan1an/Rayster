float offsetCalc(in uint x, in uint level)
{	
	uint den = (1 << level); // den = 1,2,4 when level = 0,1,2
	return (x == 0 ? 0.5f  / den : 1.0f - 0.5f / den); 
}