#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "point.h"
#include "sorted_points.h"

struct sorted_points {
	struct linker* m_pStartLinker;
	struct linker* m_pEndLinker;
	int m_iSize;
};

struct linker{
	struct point* m_pPoint;
	struct linker* m_pNextLinker;
	struct linker* m_pPriorLinker;
};

int detail_point_compare(const struct point* p1, const struct point* p2){
	int result = point_compare(p1,p2);
	if(result != 0)
		return result;
	else
	{
		if(p1 -> x < p2 -> x)
			return -1;
		else if(p1 -> x > p2 -> x)
			return 1;
		else if(p1 -> y < p2 -> y)
			return -1;
		else if(p1 -> y > p2 -> y)
			return 1;
		else
			return 0;
	}
}

struct sorted_points *
sp_init()
{
	struct sorted_points *sp;

	sp = (struct sorted_points *)malloc(sizeof(struct sorted_points));

	sp->m_pStartLinker = NULL;
	sp->m_pEndLinker = NULL;
	sp->m_iSize = 0;

	assert(sp);
	return sp;
}

void
sp_destroy(struct sorted_points *sp)
{
	if(sp->m_iSize == 0)
	{
		free(sp);	
		return;
	}
	else
	{
		struct linker* current_linker = sp->m_pStartLinker;
		struct linker* next_linker = current_linker->m_pNextLinker;
		for(;next_linker!=NULL;)
		{
			free(current_linker->m_pPoint);
			free(current_linker);
			current_linker = next_linker;
			next_linker = current_linker->m_pNextLinker;
		}
		free(current_linker->m_pPoint);
		free(current_linker);
	}
	free(sp);
}

int
sp_add_point(struct sorted_points *sp, double x, double y)
{
	struct point* np = (struct point*)malloc(sizeof(struct point));
	np->x = x;
	np->y = y;
	struct linker* newLinker = (struct linker*)malloc(sizeof(struct linker));
	if(np == NULL || newLinker == NULL)
		return 0;
	// empty link list
	if(sp->m_pStartLinker == NULL)
	{
		//printf("Insert front\n");
		newLinker->m_pNextLinker = NULL;
		newLinker->m_pPriorLinker = NULL;
		newLinker->m_pPoint = np;
		sp->m_pStartLinker = newLinker;
		sp->m_pEndLinker = newLinker;
		sp->m_iSize++;
		return 1;
	}
	else
	{
		// need to traverse through linked list
		struct linker* current_linker = sp->m_pStartLinker;
		struct linker* prior_linker = sp->m_pStartLinker->m_pPriorLinker;
		for(;current_linker!=NULL;)
		{
			struct point* cp = current_linker->m_pPoint;
			int status = detail_point_compare(cp,np);
			if(status == 1 || status == 0)
			{
				// insertion at front;
				if(current_linker == sp->m_pStartLinker)
				{
					//printf("Insert at Front\n");
					newLinker->m_pNextLinker = sp->m_pStartLinker;
					newLinker->m_pPriorLinker = NULL;
					newLinker->m_pPoint = np;
					sp->m_pStartLinker->m_pPriorLinker = newLinker;
					sp->m_pStartLinker = newLinker;
					sp->m_iSize = sp->m_iSize + 1;
					return 1;
				}
				else
				{
					//printf("Insert between\n");
					prior_linker->m_pNextLinker = newLinker;
					newLinker->m_pNextLinker = current_linker;
					current_linker->m_pPriorLinker = newLinker;
					newLinker->m_pPriorLinker = prior_linker;
					newLinker->m_pPoint = np;
					sp->m_iSize = sp->m_iSize + 1;
					return 1;	
				}
				
			}
			prior_linker = current_linker;
			current_linker = current_linker->m_pNextLinker;
		}
		//printf("Insert end\n");
		newLinker->m_pNextLinker = NULL;
		prior_linker->m_pNextLinker = newLinker;
		newLinker->m_pPriorLinker = prior_linker;
		newLinker->m_pPoint = np;
		sp->m_pEndLinker = newLinker;
		sp->m_iSize = sp->m_iSize + 1;
		return 1;	
	}
  }

int
sp_remove_first(struct sorted_points *sp, struct point *ret)
{
	//printf("remove by first\n");
	if(sp->m_iSize == 0)
		return 0;
	else if (sp->m_iSize == 1)
	{
		ret->x = sp->m_pStartLinker->m_pPoint->x;
		ret->y = sp->m_pStartLinker->m_pPoint->y;
		free(sp->m_pStartLinker->m_pPoint);
		free(sp->m_pStartLinker);
		sp->m_pStartLinker = NULL;
		sp->m_pEndLinker = NULL;
		--sp->m_iSize;
		return 1;
	}
	else
	{
		struct linker* temp = sp->m_pStartLinker;
		sp->m_pStartLinker = temp->m_pNextLinker;
		temp->m_pNextLinker->m_pPriorLinker = NULL;
		ret->x = temp->m_pPoint->x;
		ret->y = temp->m_pPoint->y;
		--sp->m_iSize;
		free(temp->m_pPoint);
		free(temp);
		return 1;
	}
}

int
sp_remove_last(struct sorted_points *sp, struct point *ret)
{
	//printf("remove by last\n");
	if(sp->m_iSize == 0)
		return 0;
	else if (sp->m_iSize == 1)
	{
		ret->x = sp->m_pStartLinker->m_pPoint->x;
		ret->y = sp->m_pStartLinker->m_pPoint->y;
		free(sp->m_pStartLinker->m_pPoint);
		free(sp->m_pStartLinker);
		--sp->m_iSize;
		sp->m_pStartLinker = NULL;
		sp->m_pEndLinker = NULL;
		return 1;
	}
	else
	{
		struct linker* temp = sp->m_pEndLinker;
		sp->m_pEndLinker = temp->m_pPriorLinker;
		temp->m_pPriorLinker->m_pNextLinker = NULL;
		ret-> x = temp->m_pPoint->x;
		ret-> y = temp->m_pPoint->y;
		--sp->m_iSize;
		free(temp->m_pPoint);
		free(temp);
		return 1;
	}
}

int
sp_remove_by_index(struct sorted_points *sp, int index, struct point *ret)
{
	//printf("remove by index\n");
	if(index > sp->m_iSize-1 || index < 0)
		return 0;
	else if(index == 0)
		return sp_remove_first(sp,ret);
	else if(index == sp->m_iSize-1)
		return sp_remove_last(sp,ret);
	else
	{
		int counter = 0;
		struct linker* temp = sp->m_pStartLinker;	
		for(;counter < index; ++counter)
		{
			temp = temp->m_pNextLinker;
		}
		temp->m_pPriorLinker->m_pNextLinker = temp->m_pNextLinker;
		temp->m_pNextLinker->m_pPriorLinker = temp->m_pPriorLinker;
		ret->x = temp->m_pPoint->x;
		ret->y = temp->m_pPoint->y;
		--sp->m_iSize;
		free(temp->m_pPoint);		
		free(temp);
		return 1;
	}
}

int
sp_delete_duplicates(struct sorted_points *sp)
{
	//printf("Delete duplicates\n");
	int num_of_deletion = 0;
	if(sp->m_iSize <= 1)
		return num_of_deletion;
	else
	{
		//printf("size: %d",sp->m_iSize);
		struct linker* current_linker = sp->m_pStartLinker;
		double x = current_linker->m_pPoint->x;
		double y = current_linker->m_pPoint->y;
		current_linker = current_linker->m_pNextLinker;
		for(;sp->m_iSize > 1 && current_linker!=NULL;)
		{
			struct point* tp = current_linker->m_pPoint;
			if(tp->x == x && tp->y == y)
			{
				if(current_linker != sp->m_pEndLinker)
				{
					current_linker->m_pNextLinker->m_pPriorLinker = current_linker->m_pPriorLinker;
					current_linker->m_pPriorLinker->m_pNextLinker = current_linker->m_pNextLinker;
					struct linker* temp = current_linker;
					current_linker = current_linker->m_pNextLinker;
					free(temp->m_pPoint);
					free(temp);
					num_of_deletion++;
					sp->m_iSize--;
				}
				else
				{
					struct point p1;
					sp_remove_last(sp,&p1);
					num_of_deletion++;
					return num_of_deletion;
				}
			}
			else
			{
				x = current_linker->m_pPoint->x;
				y = current_linker->m_pPoint->y;
				current_linker = current_linker -> m_pNextLinker;
			}
		}
		//printf("deleted: %d",num_of_deletion);
		return num_of_deletion;
	}

}



