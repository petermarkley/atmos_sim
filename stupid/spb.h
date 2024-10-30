//SPB (Stupid Progress Bar)
//Copyright 2013 Peter Markley <peter@petermarkley.com>

#ifndef __SPB_HEADER
#define __SPB_HEADER

#include <stdio.h>
#include <math.h>
#include <time.h>

#define SPB_STRMAX 256

#ifdef __cplusplus
// "__cplusplus" is defined whenever it's a C++ compiler,
// not a C compiler, that is doing the compiling.
extern "C" {
#endif

//must initialize 'real_goal' & 'bar_goal' before calling any SPB
//functions, and must maintain 'real_progress' throughout; 'bar_progress'
//is for internal SPB use only
struct spb_instance
	{
	int real_goal;     //total number of items that need processed
	int real_progress; //current number of items processed
	int bar_goal;      //character width of the bar
	int bar_progress;  //characters printed so far
	const char *p;     //prefix to line of progress bar text (e.g. tab indentations - NO NEWLINES)
	const char *n;     //plural noun for the type of item counted by 'real_goal' & 'real_progress' (if NULL will print "items")
	int largest;       //real_goal for the bar with the largest item count, for better alignment
	
	int phase;         //used internally for animating the filled portion of the bar
	time_t start;      //used internally for estimating time
	char s[SPB_STRMAX];//used internally for formatting string
	};

//call before the process begins, after setting 'i->bar_goal' and 'i->real_goal'
void spb_init(struct spb_instance *i, const char *p, const char *n)
	{
	i->real_progress = 0;
	i->bar_progress = 0;
	i->phase = 0;
	i->start = time(NULL);
	i->p = p;
	i->n = n;
	snprintf(i->s,SPB_STRMAX,"%%s%%%1$dd/%%%1$dd %%s [",(int)ceil(log10((i->largest>i->real_goal?i->largest:i->real_goal))));
	return;
	}

//call between every processed item, while 'i->progress' is being incremented/maintained
void spb_update(struct spb_instance *i)
	{
	int j, mins, hrs;
	time_t t = time(NULL);
	double rate, secs;
	
	//estimate time
	if (t>i->start)
		{
		if (i->real_progress < i->real_goal)
			{
			rate = ((double)i->real_progress)/((double)(t-i->start));
			secs = ((double)(i->real_goal-i->real_progress))/rate;
			}
		else
			secs = (double)(t-i->start);
		}
	else
		secs = 0.0;
	//convert to hh:mm:ss.s format
	mins = (int)floor(secs/60.0);
	secs -= ((double)mins)*60.0;
	hrs = mins/60;
	mins -= hrs*60;
	
	//convert real progress to bar progress
	i->bar_progress = (int)round(((double)(i->real_progress))*i->bar_goal/((double)(i->real_goal)));
	
	//print 
	fprintf(stderr,i->s,(i->p!=NULL?i->p:""),i->real_progress,i->real_goal,(i->n!=NULL?i->n:"items"));
	j=0;
	for (; j<i->bar_progress; j++)
		switch ((i->phase+j)%4)
			{
			case 0:
				fprintf(stderr,"/");
				break;
			case 1:
				fprintf(stderr,".");
				break;
			case 2:
				fprintf(stderr,".");
				break;
			case 3:
				fprintf(stderr,".");
				break;
			default:
				fprintf(stderr,"=");
				break;
			}
	if (j<i->bar_goal)
		{
		fprintf(stderr,"|");
		j++;
		}
	for (; j<i->bar_goal; j++)
		fprintf(stderr," ");
	if (i->real_progress < i->real_goal)
		fprintf(stderr,"] %5.1lf%% | %02dh %02d'%04.1lf\" remaining\r",(((double)i->real_progress)/((double)i->real_goal))*100.0,(hrs>99?99:hrs),(hrs>99?99:mins),(hrs>99?99.9:secs));
	else
		fprintf(stderr,"] 100.0%% | in %02dh %02d'%02.lf\"         \n",(hrs>99?99:hrs),(hrs>99?99:mins),(hrs>99?99.9:secs));
	
	//animate filled portion of bar
	i->phase++;
	return;
	}

#ifdef __cplusplus
}
#endif
#endif
