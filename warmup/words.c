#include "common.h"

int
main(int argc, char* arvg[])
{
  int i = 1;
  for(; i < argc; i++)
    {
    printf(arvg[i]);
    printf("\n");
    }
	return 0;
}
