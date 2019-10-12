#include<stdio.h>
#include<unistd.h>
#include "timer.h"

void cbfunc(){

	printf("cb function\n");

}


int main(){

	timer_init();

	int nTimerId = timer_start(-1, 1000, "my_timer",
	                                 (timer_cb)cbfunc, NULL);
	while(1){
		usleep(100);
	}
}
