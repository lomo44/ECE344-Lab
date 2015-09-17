#include <assert.h>
#include "common.h"
#include "point.h"
#include "math.h"

void
point_translate(struct point *p, double x, double y)
{
	p->x = p->x + x;
	p->y = p->y + y;
}

double
point_distance(const struct point *p1, const struct point *p2)
{
	return sqrt((p1->x - p2 ->x) * (p1 ->x - p2 -> x) + (p1 -> y - p2 -> y) * (p1 ->y - p2->y ));
}

int
point_compare(const struct point *p1, const struct point *p2)
{
	struct point orgin;
	orgin.x = 0;
	orgin.y = 0;
	float distance1 = point_distance(&orgin,p1);
	float distance2 = point_distance(&orgin,p2);
	if(distance1 == distance2)
		return 0;
	else if ( distance1 < distance2)
		return -1;
	else
		return 1;
}
