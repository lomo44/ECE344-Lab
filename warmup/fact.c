#include "common.h"
#include "string.h"
int factorial(int i);


int
main(int argc, char* argv[])
{
  if(argc == 1)
    {
      printf("Huh?\n");
    }
  else if (argc >= 3)
    {
      printf("Huh?\n");
    }
  else
    {
      int temp_num = atoi(argv[1]);
      if(temp_num == 0)
	printf("Huh?\n");
      else 
      if(temp_num > 12)
	printf("Overflow\n");
      if(strlen(argv[1]) > 2)
	 printf("Huh?\n");
      else
	{
	int i = factorial(temp_num);
	char output[33];
	sprintf(output,"%d",i);
	printf(output);
	printf("\n");
	}
    }
	return 0;
}

int factorial(int i)
{
  if(i == 1)
    return i;
  return i * factorial(i-1);
}

