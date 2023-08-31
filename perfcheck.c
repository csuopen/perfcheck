/** ============================================================================
** Author:             TangCheng
** Email:              tangcheng@csudata.com
** Current maintainer: TangCheng - AT CSUDATA.COM
** Email:              tangcheng@csudata.com
** ----------------------------------------------------------------------------
** Copyright (C) 2023 CSUDATA.COM Limited
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 2, or (at your option) any
** later version.
**
** This program is distributed in the hope that it will be useful, but
** WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
** See the GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
** ----------------------------------------------------------------------------*/

// gcc -static perfcheck.c -o -lpthread perfcheck 

#undef   _FILE_OFFSET_BITS
#define  _FILE_OFFSET_BITS  64

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdlib.h>
#include <malloc.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <limits.h>
#if defined(SysV)
#include <sys/times.h>
#else
#include <sys/resource.h>
#endif
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>


#define DEFAULT_TEST_FILE_MB_SIZE 20*1024

#define IO_CNT_PER_TIME 500
#define MAX_INT_VAL_STEP(t) \
        ((t) 1 << (8 * sizeof(t) - 1 - ((t) -1 < 1)))

#define MAX_INT_VAL(t) \
        ((MAX_INT_VAL_STEP(t) - 1) + MAX_INT_VAL_STEP(t))

#define OFF_T_MAX MAX_INT_VAL(off_t)        

typedef struct
{
	int  seq;
	int  totalthread;
	int  testmode;
	double  runtimes;
	int  iocnt;
	double  cputimes;
	int testtimes;
    off_t filesize;
    char * testfile;
    int blocksize;
    pthread_attr_t threadattr;
	pthread_t threadid;
#if defined(TCHDEBUG)	
	unsigned int * tj;
#endif
}IOTHREADPARA;


typedef struct
{
	int threadseq;
	pthread_attr_t threadattr;
	pthread_t threadid;
	double runtimes;
    uint64_t cnt;
}CPUTHREADPARA;



int testpos(int fd,off_t pos);
static double cpu_so_far();
static double time_so_far();
off_t getfilesize(char * filename);

void * IoThreadProc(void * pPara);
IOTHREADPARA * g_pIoThreadPara;

void * CpuThreadProc(void * pPara);
CPUTHREADPARA * g_pCpuThreadPara;


/****************************** for rand function *******************************/
#define	TYPE_0		0
#define	BREAK_0		8
#define	DEG_0		0
#define	SEP_0		0

/* x**7 + x**3 + 1.  */
#define	TYPE_1		1
#define	BREAK_1		32
#define	DEG_1		7
#define	SEP_1		3

/* x**15 + x + 1.  */
#define	TYPE_2		2
#define	BREAK_2		64
#define	DEG_2		15
#define	SEP_2		1

/* x**31 + x**3 + 1.  */
#define	TYPE_3		3
#define	BREAK_3		128
#define	DEG_3		31
#define	SEP_3		3

/* x**63 + x + 1.  */
#define	TYPE_4		4
#define	BREAK_4		256
#define	DEG_4		63
#define	SEP_4		1
#define	MAX_TYPES	5

struct myrandom_poly_info
{
  int seps[MAX_TYPES];
  int degrees[MAX_TYPES];
};

static const struct myrandom_poly_info myrandom_poly_info =
{
  { SEP_0, SEP_1, SEP_2, SEP_3, SEP_4 },
  { DEG_0, DEG_1, DEG_2, DEG_3, DEG_4 }
};

struct myrandom_data
{
    int32_t *fptr;		/* Front pointer.  */
    int32_t *rptr;		/* Rear pointer.  */
    int32_t *state;		/* Array of state values.  */
    int rand_type;		/* Type of random number generator.  */
    int rand_deg;		/* Degree of random number generator.  */
    int rand_sep;		/* Distance between front and rear.  */
    int32_t *end_ptr;		/* Pointer behind state table.  */
};

int myinitstate_r (unsigned int seed,char *arg_state,size_t n,struct myrandom_data *buf);
int mysrandom_r ( unsigned int seed,struct myrandom_data *buf);
int mysetstate_r (char *arg_state,struct myrandom_data *buf);
int mysetstate_r (char *arg_state,struct myrandom_data *buf);
/****************************************************************************************/

int GetCpuNums();
int g_totalthread;
int g_fileflags;

volatile unsigned char g_bIsExit=0;
void signal_func(int no);

void set_signal()
{
    signal(SIGHUP, signal_func);
    signal(SIGQUIT, signal_func);
    signal(SIGBUS, SIG_DFL);

    signal(SIGURG,signal_func); 

   
    signal(SIGPIPE,SIG_IGN); 

    signal(SIGABRT,SIG_IGN); 
    signal(SIGTRAP,SIG_IGN);
    signal(SIGILL,signal_func);
    signal(SIGSEGV,signal_func); 
    //signal(SIGCHLD,SIG_IGN);
    signal(SIGTERM,signal_func);
    signal(SIGINT, signal_func); //Ctrl+C	
}

static double cpu_so_far()
{
#if defined(SysV)
    struct tms tms;

    if (times(&tms) == -1)
        io_error("times");
    return ((double) tms.tms_utime) / ((double) sysconf(_SC_CLK_TCK)) +
           ((double) tms.tms_stime) / ((double) sysconf(_SC_CLK_TCK));

#else

    struct rusage rusage;

    getrusage(RUSAGE_SELF, &rusage);
    return
        ((double) rusage.ru_utime.tv_sec) +
        (((double) rusage.ru_utime.tv_usec) / 1000000.0) +
        ((double) rusage.ru_stime.tv_sec) +
        (((double) rusage.ru_stime.tv_usec) / 1000000.0);
#endif
}


static double time_so_far()
{
#if defined(SysV)
    int        val;
    struct tms tms;

    if ((val = times(&tms)) == -1)
    {
        printf("Call times() error\n");
    }
    return ((double) val) / ((double) sysconf(_SC_CLK_TCK));

#else

    struct timeval tp;

    if (gettimeofday(&tp, (struct timezone *) NULL) == -1)
    {
        printf("Call gettyimeofday error\n");
    }
    return ((double) (tp.tv_sec)) +
           (((double) tp.tv_usec) / 1000000.0);
#endif
}


off_t getfilesize(char * filename)
{
	int fd;
	off_t filesize;
	off_t step;
	
	fd=open(filename,O_RDONLY);
    if(fd==-1)
    {
        printf("Can not open file:%s\n",filename);
        return -1;
    }


	filesize=lseek(fd,0,SEEK_END);
	if(filesize>0)
	{
		close(fd);
		return filesize;
	}
	
	step=OFF_T_MAX -2;

	filesize=step;
	while(step>0)
	{
	    if(testpos(fd,filesize))
	    {
		    if(step<=4)
		    {
		    	filesize++;
		    	while(testpos(fd,filesize))
		    	{
		    		filesize++;
		    	}
		    	step=0;
		    	break;
		    }
		    step=(off_t)(step/2);
		    filesize=filesize+step;
	    }
	    else
	    {
		    if(step<=4)
		    {
		    	filesize--;
		    	while( (! testpos(fd,filesize)) && filesize>-1 )
		    	{
		    		filesize--;
		    	}
		    	filesize++;
		    	step=0;
		    	break;
		    }

		    step=(off_t)(step/2);
		    filesize=filesize - step;
	    }
    }
    
    close(fd);
    return filesize;
}

int testpos(int fd,off_t pos)
{
	char buf[1];
	ssize_t ret;
	off_t retpos;
	retpos=lseek(fd,pos,SEEK_SET);
	if(retpos==-1 || pos==0)
	{
		return 0;
	}

	ret=read(fd, buf, 1);
	if(ret<=0)
    {
    	return 0;
    }
    else
    {
    	return 1;
    }
}



int GetCpuNums()
{
	FILE * fp;
	char buf[20];
    char * cmd="cat /proc/cpuinfo | grep processor | wc -l"; 
	fp=popen(cmd, "r");
	if(fp==NULL)
	{
		printf("Can not run cmd:%s\n",cmd);
		return 0;
	}
	
	fgets(buf,18,fp);
	pclose(fp);
	return atoi(buf);
}


int mysrandom_r ( unsigned int seed,struct myrandom_data *buf)
{
    int type;
    int32_t *state;
    long int i;
    long int word;
    int32_t *dst;
    int kc;

    if (buf == NULL)
        goto fail;
    type = buf->rand_type;
    if ((unsigned int) type >= MAX_TYPES)
        goto fail;

    state = buf->state;
    /* We must make sure the seed is not 0.  Take arbitrarily 1 in this case.  */
    if (seed == 0)
        seed = 1;
    state[0] = seed;
    if (type == TYPE_0)
        goto done;

    dst = state;
    word = seed;
    kc = buf->rand_deg;
    for (i = 1; i < kc; ++i)
    {
        /* This does:
        state[i] = (16807 * state[i - 1]) % 2147483647;
        but avoids overflowing 31 bits.  */
        long int hi = word / 127773;
        long int lo = word % 127773;
        word = 16807 * lo - 2836 * hi;
        if (word < 0)
            word += 2147483647;
        *++dst = word;
    }

    buf->fptr = &state[buf->rand_sep];
    buf->rptr = &state[0];
    kc *= 10;
    while (--kc >= 0)
    {
        int32_t discard;
        (void) myrandom_r (buf, &discard);
    }

done:
    return 0;

fail:
    return -1;
}

int myinitstate_r (unsigned int seed,char *arg_state,size_t n,struct myrandom_data *buf)
{
    if (buf == NULL)
        goto fail;

    int32_t *old_state = buf->state;
    if (old_state != NULL)
    {
        int old_type = buf->rand_type;
        if (old_type == TYPE_0)
            old_state[-1] = TYPE_0;
        else
            old_state[-1] = (MAX_TYPES * (buf->rptr - old_state)) + old_type;
    }

    int type;
    if (n >= BREAK_3)
        type = n < BREAK_4 ? TYPE_3 : TYPE_4;
    else if (n < BREAK_1)
    {
        if (n < BREAK_0)
        {
            goto fail;
        }
        type = TYPE_0;
    }
    else
        type = n < BREAK_2 ? TYPE_1 : TYPE_2;

    int degree = myrandom_poly_info.degrees[type];
    int separation = myrandom_poly_info.seps[type];

    buf->rand_type = type;
    buf->rand_sep = separation;
    buf->rand_deg = degree;
    int32_t *state = &((int32_t *) arg_state)[1];	/* First location.  */
    /* Must set END_PTR before srandom.  */
    buf->end_ptr = &state[degree];

    buf->state = state;

    mysrandom_r (seed, buf);

    state[-1] = TYPE_0;
    if (type != TYPE_0)
        state[-1] = (buf->rptr - state) * MAX_TYPES + type;

    return 0;

fail:
    errno = EINVAL;
    return -1;
}

int mysetstate_r (char *arg_state,struct myrandom_data *buf)
{
    int32_t *new_state = 1 + (int32_t *) arg_state;
    int type;
    int old_type;
    int32_t *old_state;
    int degree;
    int separation;

    if (arg_state == NULL || buf == NULL)
        goto fail;

    old_type = buf->rand_type;
    old_state = buf->state;
    if (old_type == TYPE_0)
        old_state[-1] = TYPE_0;
    else
        old_state[-1] = (MAX_TYPES * (buf->rptr - old_state)) + old_type;

    type = new_state[-1] % MAX_TYPES;
    if (type < TYPE_0 || type > TYPE_4)
        goto fail;

    buf->rand_deg = degree = myrandom_poly_info.degrees[type];
    buf->rand_sep = separation = myrandom_poly_info.seps[type];
    buf->rand_type = type;

    if (type != TYPE_0)
    {
        int rear = new_state[-1] / MAX_TYPES;
        buf->rptr = &new_state[rear];
        buf->fptr = &new_state[(rear + separation) % degree];
    }
    buf->state = new_state;
    /* Set end_ptr too.  */
    buf->end_ptr = &new_state[degree];

    return 0;

fail:
    errno = EINVAL;
    return -1;
}

int myrandom_r (struct myrandom_data *buf,int32_t *result)
{
    int32_t *state;

    if (buf == NULL || result == NULL)
        goto fail;

    state = buf->state;

    if (buf->rand_type == 0)
    {
        int32_t val = state[0];
        val = ((state[0] * 1103515245) + 12345) & 0x7fffffff;
        state[0] = val;
        *result = val;
    }
    else
    {
        int32_t *fptr = buf->fptr;
        int32_t *rptr = buf->rptr;
        int32_t *end_ptr = buf->end_ptr;
        int32_t val;

        val = *fptr += *rptr;
        /* Chucking least random bit.  */
        *result = (val >> 1) & 0x7fffffff;
        ++fptr;
        if (fptr >= end_ptr)
        {
            fptr = state;
            ++rptr;
        }
        else
        {
            ++rptr;
            if (rptr >= end_ptr)
                rptr = state;
        }
        buf->fptr = fptr;
        buf->rptr = rptr;
    }
    return 0;

fail:
    errno= EINVAL;
    return -1;
}

void signal_func(int no)
{
    switch (no)
    {
    case 1:
        printf("Get signal SIGHUP.\n");
        break;
    case SIGINT:
        printf("Get Ctrl+C or signal SIGINT, iopress is stoping....\n");
        g_bIsExit=1;
        break;
    case SIGTERM:
        printf("Get kill signal,iopress is stoping...\n");
        g_bIsExit=1;
        break;
    case SIGQUIT:
        printf("Get SIGQUIT signal.\n");
        break;

    case SIGABRT:
        printf("Get SIGABRT signal.\n");
        break;

    case SIGILL:
        printf("Get SIGILL signal.\n");
        break;

    case SIGSEGV:
        printf("Get SIGSEGV signal.\n");
        g_bIsExit=1;
        break;

    case SIGPIPE:
        printf("Get SIGPIPE signal.\n");
        break;

    default:
        printf("GET %d sigial!\n",no);
        break;
    break;
    }
}


int create_test_file(char * testfile, int mb_size)
{
    int fd;
    char * buf;
    int i;
    ssize_t iosz;
    int err_code;

    fd = open(testfile, O_RDWR | O_CREAT, 0700);
    if (fd < 0)
    {
        err_code = errno;
        printf("Can not open %s, Errno=%d: %s", testfile, strerror(err_code));
        perror("\n\t");
        return -1;
    }

    buf = malloc(1024*1024);
    
    for (i=0; i<mb_size; i++)
    {
        iosz = write(fd, buf, 1024*1024);
        if (iosz < 0)
        {
            err_code = errno;
            printf("Can not write %s, iosz=%d, Errno=%d: %s\n", testfile, iosz, err_code, strerror(err_code));
            return -1;
        }
    }
    close(fd);
    return 0;
}




void * IoThreadProc(void * pPara)
{
	IOTHREADPARA * pThreadPara;
	int fd;
    off_t blocknum;
    off_t currpos;
    off_t blocktotals;
    off_t seekret;
    char * buf;

    double starttime,endtime;
    double startcpu,endcpu;
    double currtime;
    int stepcnt;
    int cnt;
    int randpos=0;
    unsigned int randcnt=0;
    unsigned int randfactor=0;
    unsigned int seed;
    int32_t randnum;
    int ret;
    int i;
    
	pThreadPara=(IOTHREADPARA *)pPara;
    
    struct myrandom_data randombuf;
    unsigned int state[32] = {
                                    3,
                                    0x9a319039, 0x32d9c024, 0x9b663182, 0x5da1f342,
                                    0x7449e56b, 0xbeb1dbb0, 0xab5c5918, 0x946554fd,
                                    0x8c2e680f, 0xeb3d799f, 0xb11ee0b7, 0x2d436b86,
                                    0xda672e2a, 0x1588ca88, 0xe369735d, 0x904f35f7,
                                    0xd7158fd6, 0x6fa6f051, 0x616e6b96, 0xac94efdc,
                                    0xde3b81e0, 0xdf0a6fb5, 0xf103bc02, 0x48f340fb,
                                    0x36413f93, 0xc622c298, 0xf5a42ab8, 0x8a88d77b,
                                    0xf5ad9d0e, 0x8999220b, 0x27fb47b9
                                };


    buf=(char *)memalign(4096, pThreadPara->blocksize);
    if(buf==NULL)
    {
        printf("Thread %d can not allocate memory size:%d\n",pThreadPara->seq,pThreadPara->blocksize);
        return NULL;
    }
    memset(buf, 0, pThreadPara->blocksize);
    for (i=0; i++; i<pThreadPara->blocksize)
    {
        buf[i] = (char)i;
    }
    
    if(pThreadPara->testmode==0)
    {
        fd=open(pThreadPara->testfile,O_RDONLY|g_fileflags);
    }
    else
    {
        fd=open(pThreadPara->testfile,O_RDWR|g_fileflags);
    }
    if(fd==-1)
    {
        printf("Thread %d can not open file:%s\n",pThreadPara->seq,pThreadPara->testfile);
        free(buf);
        return NULL;
    }
	else
	{
	    //printf("Thread %d open file :%s successfully.\n",pThreadPara->seq,pThreadPara->testfile);
	}

    
    blocktotals=pThreadPara->filesize/pThreadPara->blocksize;
    //printf("total blocks=%ld\n",(long)blocktotals);

    starttime=time_so_far();
    startcpu=cpu_so_far();

    stepcnt=0;
    cnt=0;
    
    seed= (long)(time_so_far()*1000000)%1000000+ 100*pThreadPara->seq;
    
    myinitstate_r(seed,(char *)state,128,&randombuf);
    mysrandom_r(seed, &randombuf);
    
    while(1)
    {
        
        //blocknum=myrand((unsigned int )(blocktotals -1),&seed,&randcnt,&randfactor);
        myrandom_r(&randombuf,&randnum);
        blocknum=randnum%blocktotals;
        // if(pThreadPara->testmode==0)
        // {
        //     printf("%d\t", blocknum);
        // }
#if defined(TCHDEBUG)        
        pThreadPara->tj[blocknum]++;
#endif        
        currpos=blocknum*pThreadPara->blocksize;
        //printf("curr blocknum is %ld\n",(long) blocknum);
        seekret=lseek(fd, currpos, SEEK_SET);
        if(seekret == (off_t)-1)
        {
        	printf("Thread %d call lseek error,curr blocknum is %ld,fd=%d\n!\n",pThreadPara->seq,(long) blocknum,fd);
        	printf("Errno=%d,",errno);
            perror("Err info");
        	break;
        }
        currpos=currpos+pThreadPara->blocksize;
        if(currpos>=pThreadPara->filesize - pThreadPara->blocksize)
        {
    	    currpos=0;
        }
        
        if(pThreadPara->testmode==0)
        {
            if(read(fd, buf, pThreadPara->blocksize)<=0)
            {                
                printf("Thread %d read data error,blocknum=%ld\n",pThreadPara->seq,(long)blocknum);
                printf("Errno=%d,",errno);
                perror("Err info");
                continue;
            }
        }
        else if(pThreadPara->testmode==1)
        {
            ret=write(fd,buf,pThreadPara->blocksize);
            if(ret!=pThreadPara->blocksize)
            {
                printf("Thread %d write data error,blocknum=%ld\n",pThreadPara->seq,(long)blocknum);
                printf("Errno=%d,",errno);
                perror("Err info");
                continue;
            }

        }
        else if(pThreadPara->testmode==2)
        {
            if(read(fd, buf, pThreadPara->blocksize)<=0)
            {
                printf("Thread %d read data error,blocknum=%ld\n",pThreadPara->seq,(long)blocknum);
                printf("Errno=%d,",errno);
                perror("Err info");                
                continue;
            }
            seekret=lseek(fd, seekret, SEEK_SET);
            if(seekret == (off_t)-1)
            {
        	    printf("Thread %d call lseek error!\n",pThreadPara->seq);
        	    printf("Errno=%d,",errno);
                perror("Err info");
        	    break;
            }
            
            if(write(fd,buf,pThreadPara->blocksize)!=pThreadPara->blocksize)
            {
                printf("Thread %d write data error,blocknum=%ld\n",pThreadPara->seq,(long)blocknum);
                printf("Errno=%d,",errno);
                perror("Err info");
                continue;
            }

        }
		
        if(pThreadPara->testmode==2)
        {
        	cnt+=2;
        }
        else
        {
            cnt++;
        }
		
		if(g_bIsExit)
		{
			break;
		}
		
		
        // stepcnt++;
        // if(stepcnt >=IO_CNT_PER_TIME)
        // {
            
		// 	currtime=time_so_far();
        //     stepcnt=0;
        //     if(currtime - starttime >pThreadPara->testtimes)
        //     {
        //         break;
        //     }
			
		// 	stepcnt=0;
		// 	printf(".");
        // }
		
    }

    endtime=time_so_far();
    endcpu=cpu_so_far();
    pThreadPara->cputimes=endcpu - startcpu;
    pThreadPara->runtimes=endtime - starttime;
    pThreadPara->iocnt=cnt;
    //printf("Thread %d, iocnt=%d\n", pThreadPara->seq, cnt);
    free(buf);
	//printf("Thread %d is exit.\n",pThreadPara->seq);
	close(fd);
	//printf("Close file is over\n");
    return NULL;
}


void * CpuThreadProc(void * pPara)
{
	CPUTHREADPARA *pThreadPara;
	int errcode;
	int i;
	char * psrc,*pdst;
	int m,n;
	double starttime;
	double endtime;
	double d1,d2;
    uint64_t cnt = 0;
	
	pThreadPara=(CPUTHREADPARA *)pPara;

	psrc=malloc(1024);
	pdst=malloc(1024);
	starttime=time_so_far();
    while(1)
    {
	    m=10242;
		n=124323;
		m=m*n;
		m=m/n;
		m=m+n;
		d1=1232323.22323;
		d2=9485757.22323;
		d1=d1*d2;
		d1=d1/d2;
		d1=d1+d2;
		memcpy(pdst,psrc,1024);
        cnt++;
        if(g_bIsExit)
		{
			break;
		}

	}
	endtime=time_so_far();
	pThreadPara->runtimes = endtime - starttime;
    pThreadPara->cnt = cnt;
	free(psrc);
	free(pdst);
	return NULL;
}


int io_test(int testtimes, char * testfile, int test_file_mb_size)
{
    int i;
    int totalio=0;
    double totalruntimes=0;
    double totalcputimes=0;
    int blocksize;
    int fd;
    off_t filesize=0;
    int err_code;
    int wiops;
    int riops;

	g_fileflags= O_SYNC|O_DIRECT;  //O_DIRECT, O_NOATIME
    blocksize = 8192;
    testtimes=10;
    g_totalthread=16;

    //20GB的文件做测试
    create_test_file(testfile, test_file_mb_size);
    filesize=getfilesize(testfile);

    if(filesize!= test_file_mb_size*1024*1024)
    {
    	printf("Can not create test file.\n");
    	return 1;
    }

    if(sizeof(off_t)==8)
    {
        printf("Large file is supported,file %s size is %lu Mbytes,blocksize is %d.\n",
               testfile,
               (unsigned long)(filesize/1024/1024),
               blocksize
               );
    }
    else
    {
        printf("File %s size is %lu Mbytes,blocksize is %d.\n",
               testfile,
               (unsigned long)(filesize/1024/1024),
               blocksize
               );
    }

    g_pIoThreadPara=(IOTHREADPARA *)malloc(sizeof(IOTHREADPARA)*g_totalthread);
    printf("begin write test ...\n");
    for(i=0; i<g_totalthread; i++)
    {
    	g_pIoThreadPara[i].seq=i;
        g_pIoThreadPara[i].testmode=1;
        g_pIoThreadPara[i].filesize=filesize;
        g_pIoThreadPara[i].iocnt=0;
        g_pIoThreadPara[i].runtimes=0;
        g_pIoThreadPara[i].cputimes=0;
    	g_pIoThreadPara[i].testfile=testfile;
    	g_pIoThreadPara[i].blocksize=blocksize;
    	g_pIoThreadPara[i].testtimes=testtimes;
    	pthread_attr_init(&g_pIoThreadPara[i].threadattr);
#if defined(TCHDEBUG)   	
    	g_pIoThreadPara[i].tj=malloc(sizeof(int)*(filesize/blocksize)); 	
    	memset(g_pIoThreadPara[i].tj,0,sizeof(int)*(filesize/blocksize));
#endif

		pthread_create (&g_pIoThreadPara[i].threadid, &g_pIoThreadPara[i].threadattr,IoThreadProc,(void *)&g_pIoThreadPara[i]);
    }

	sleep(testtimes);
	g_bIsExit=1;
	
    for(i=0;i<g_totalthread;i++)
    {
		pthread_join(g_pIoThreadPara[i].threadid, NULL);
    }
    g_bIsExit=0;
	

#if defined(TCHDEBUG) 
    for(i=1;i<g_totalthread;i++)
    {
       for(j=0;j<filesize/blocksize;j++)
       {
           g_pIoThreadPara[0].tj[j]+=g_pIoThreadPara[i].tj[j];
       }
    }	
	
    for(j=0;j<filesize/blocksize;j++)
    {
        printf("%d,%d\n",j,g_pIoThreadPara[0].tj[j]);
    }
    
    for(i=0;i<g_totalthread;i++)
    {
    	free(g_pIoThreadPara[i].tj);
    }
#endif

    for(i=0;i<g_totalthread;i++)
    {
    	totalio+=g_pIoThreadPara[i].iocnt;
    	totalruntimes+=g_pIoThreadPara[i].runtimes;
    	totalcputimes+=g_pIoThreadPara[i].cputimes;
    }

    wiops = (int) ((double)totalio*g_totalthread/totalruntimes);


    printf("begin read test ...\n");
    for(i=0;i<g_totalthread;i++)
    {
        memset(&g_pIoThreadPara[i], 0, sizeof(IOTHREADPARA));
    	g_pIoThreadPara[i].seq=i;
        g_pIoThreadPara[i].testmode=0;
        g_pIoThreadPara[i].filesize=filesize;
        g_pIoThreadPara[i].iocnt=0;
        g_pIoThreadPara[i].runtimes=0;
        g_pIoThreadPara[i].cputimes=0;
    	g_pIoThreadPara[i].testfile=testfile;
    	g_pIoThreadPara[i].blocksize=blocksize;
    	g_pIoThreadPara[i].testtimes=testtimes;
        pthread_attr_destroy(&g_pIoThreadPara[i].threadattr);
    	pthread_attr_init(&g_pIoThreadPara[i].threadattr);
#if defined(TCHDEBUG)   	
    	g_pIoThreadPara[i].tj=malloc(sizeof(int)*(filesize/blocksize)); 	
    	memset(g_pIoThreadPara[i].tj,0,sizeof(int)*(filesize/blocksize));
#endif

		err_code = pthread_create (&g_pIoThreadPara[i].threadid, &g_pIoThreadPara[i].threadattr,IoThreadProc,(void *)&g_pIoThreadPara[i]);
        if (err_code != 0)
        {
            printf("create thread %d error: %s", i, strerror(errno));
        }
    }

	sleep(testtimes);
	g_bIsExit=1;
	
    for(i=0;i<g_totalthread;i++)
    {
		pthread_join(g_pIoThreadPara[i].threadid, NULL);
    }
    g_bIsExit=0;

    for(i=0;i<g_totalthread;i++)
    {
    	totalio+=g_pIoThreadPara[i].iocnt;
    	totalruntimes+=g_pIoThreadPara[i].runtimes;
    	totalcputimes+=g_pIoThreadPara[i].cputimes;
    }

    riops = (int) ((double)totalio*g_totalthread/totalruntimes);
    free(g_pIoThreadPara);

    //     printf("IO count is %d,Times is %.2fseconds,IOPS is %d,mean response time is %.2fms,Cpu is %.2f%%\n",
    //         totalio,
    //         totalruntimes/g_totalthread,
    //         (int) ((double)totalio*g_totalthread/totalruntimes),
    //         1000*totalruntimes/(double)totalio,
    //         100*(totalcputimes)/totalruntimes
    //       );


    printf("写IOPS为: %d\n", wiops);
    printf("读IOPS为: %d\n", riops);
    printf("IO分数为: %d\n", (riops+wiops)/2);
}

int cpu_test(int testtimes)
{    
    int i;
    int iCpuNums;
    int err_code;
    double totalruntimes=0;

    uint64_t totalcnt = 0;


    printf("begin cpu test ...\n");
    iCpuNums = GetCpuNums();
    if (iCpuNums == 0)
    {
        printf("can not get nubmers of cpu cores!");
        exit(1);
    }

	g_pCpuThreadPara=(CPUTHREADPARA *)malloc(sizeof(CPUTHREADPARA)*iCpuNums);
	
    for(i=0; i<iCpuNums; i++)
    {
		g_pCpuThreadPara[i].threadseq=i;
        g_pCpuThreadPara[i].cnt = i;
    	pthread_attr_init(&g_pCpuThreadPara[i].threadattr);
		pthread_create (&g_pCpuThreadPara[i].threadid,
 		                &g_pCpuThreadPara[i].threadattr,
						CpuThreadProc,(void *)&g_pCpuThreadPara[i]);
    }
    
    sleep(testtimes);
    g_bIsExit = 1;

	for(i=0;i<iCpuNums;i++)
    {
        pthread_join(g_pCpuThreadPara[i].threadid, NULL);
    }

	for(i=0;i<iCpuNums;i++)
    {
        totalcnt += g_pCpuThreadPara[i].cnt;
    	totalruntimes += g_pCpuThreadPara[i].runtimes;
    }
    uint64_t cpumark =  (totalcnt*iCpuNums/(uint64_t)totalruntimes/10000);
    printf("CPU分数为: %llu\n", cpumark);
}

int main(int argc,char * argv[])
{
    int blocksize;
    int i,j;
    int testtimes = 10;
    char * buf;
    char * testdir = NULL;
    char * testfile;
    int mode;
    int iCpuNums;
	int iRet;
    
    int err_code;
    uint64_t totalcnt = 0;
    int test_io = 0;
    int test_cpu = 0;
    int opt;
    int test_cpu_seconds = 60;
    int test_io_seconds = 60;
    int test_file_mb_size = DEFAULT_TEST_FILE_MB_SIZE;



    if(argc<2)
    {
        printf("perfcheck v1.0\n");
        printf("Copyright (C) 2023 CSUDATA.COM\n");
        printf("perfcheck -d <dir> \n");
		
        printf("Example: perfcheck -d /data\n");
        printf("Author: TangCheng\n");
        return 0;
    }

    const char *optstring = "icd:C:I:S:"; // 其中d选项后有冒号，所以后面必须有参数
    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch (opt) {
            case 'i':
                test_io = 1;
                break;

            case 'I':
                test_io_seconds = atoi(optarg);
                test_io = 1;
                break;

            case 'c':
                test_cpu = 1;
                break;

            case 'C':
                test_cpu_seconds = atoi(optarg);
                test_cpu = 1;
                break;
            case 'S':
                test_file_mb_size = atoi(optarg);
                break;

            case 'd':
                testdir = optarg;
        }
    }

    set_signal();

    if (test_io == 0 && test_cpu == 0)
    {
        test_io = 1;
        test_cpu = 1;
    }

	printf("checkperf v1.0\n");
	printf("Author: TangCheng\n");
    printf("Copyright (C) 2023 CSUDATA.COM\n");
    
    
    if (test_io)
    {
        if (testdir == NULL)
        {
            printf("You must specify a directory by -d !\n");
            return 1;
        }
        testfile = malloc(PATH_MAX);
        sprintf(testfile, "%s/perftest.dat", testdir);
        io_test(test_io_seconds, testfile, test_file_mb_size);
    }

    if (test_cpu)
    {
        cpu_test(test_cpu_seconds);
    }
	
    return 0;
}
